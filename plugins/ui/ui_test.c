/**
 * @file ui_test.c
 * @brief Ergonomic UI test authoring API implementation (APX-261).
 *
 * Session lifecycle, high-level actions, assertions with automatic soft-render
 * frame capture on failure, and sample tests written purely against this API.
 */

#include "ui_test.h"

#include "app.h"
#include "allocator.h"
#include "filesystem.h"
#include "logger.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
/*
 * ui_test.h pulls in test.h/Unity, which may define `noreturn` as `_Noreturn`.
 * Windows UCRT <stdlib.h> uses `__declspec(noreturn)`; under clang-tidy that
 * expands to an invalid attribute. Same fix as core/test.c.
 */
#ifdef noreturn
#undef noreturn
#endif
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS

/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static const sk_logger_api_t* ut_logger_api(void) {
	static const sk_logger_api_t* api = NULL;
	if (api == NULL) {
		sk_app_boot_t boot = sk_app_create();
		if (boot.api != NULL && boot.context != NULL) {
			api = boot.api->logger_api(boot.context);
			sk_app_shutdown(boot.context);
		}
	}
	return api;
}

static sk_logger_t* ut_logger(void) {
	static sk_logger_t* log = NULL;
	static sk_logger_context_t* log_ctx = NULL;
	const sk_logger_api_t* api = ut_logger_api();
	if (log == NULL && api != NULL) {
		log_ctx = sk_logger_context_create(sk_allocator_default());
		if (log_ctx != NULL) {
			log = api->create_logger(log_ctx, "ui-test-author");
		}
	}
	return log;
}

/* -------------------------------------------------------------------------- */
/* Minimal filesystem mock (plugin TU cannot call sk_filesystem_api)          */
/* -------------------------------------------------------------------------- */

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <errno.h>
#include <sys/stat.h>
#endif

static sk_file_status_t ut_mock_status(const_chr_t path) {
	struct stat st;
	if (path == NULL || path[0] == '\0') {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (stat(path, &st) != 0) {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (S_ISDIR(st.st_mode)) {
		return SK_FILE_STATUS_DIRECTORY;
	}
	return SK_FILE_STATUS_FILE;
}

static i32 ut_mock_mkdir(const_chr_t path) {
#if defined(_WIN32)
	if (_mkdir(path) == 0) {
		return 0;
	}
#else
	if (mkdir(path, 0755) == 0) {
		return 0;
	}
	if (errno == EEXIST && ut_mock_status(path) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
#endif
	return ut_mock_status(path) == SK_FILE_STATUS_DIRECTORY ? 0 : -1;
}

static i32 ut_mock_temp(char* out, u32 out_cap) {
	const char* t = getenv("TMPDIR");
	if (t == NULL || t[0] == '\0') {
		t = getenv("TEMP");
	}
	if (t == NULL || t[0] == '\0') {
#if defined(_WIN32)
		t = ".";
#else
		t = "/tmp";
#endif
	}
	if ((u32)strlen(t) + 1u > out_cap) {
		return -1;
	}
	memcpy(out, t, strlen(t) + 1u);
	return 0;
}

static void ut_fill_mock_fs(sk_filesystem_api_t* fs) {
	memset(fs, 0, sizeof(*fs));
	fs->get_file_status = ut_mock_status;
	fs->create_directory = ut_mock_mkdir;
	fs->temp_folder = ut_mock_temp;
}

/* -------------------------------------------------------------------------- */
/* Error / frame capture                                                      */
/* -------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void ut_set_error(sk_ui_test_t* t, const_chr_t fmt, ...) {
	va_list args;
	if (t == NULL) {
		return;
	}
	va_start(args, fmt);
	(void)vsnprintf(t->last_error, sizeof(t->last_error), fmt, args);
	va_end(args);
}

/**
 * Soft-render capture into test-artifact root. Best-effort: never aborts the
 * test solely because capture failed; clears fail_frame_path on failure.
 */
i32 sk_ui_test_capture_frame(sk_ui_test_t* t, const_chr_t tag) {
	const sk_ui_api_t* ui;
	sk_ui_harness_t* h;
	const u8* px;
	u32 pw = 0u;
	u32 ph = 0u;
	sk_ui_cpu_image_t img;
	sk_filesystem_api_t fs;
	char name[192];
	char path[512];

	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return -1;
	}
	ui = t->ui;
	t->fail_frame_path[0] = '\0';

	/* Ensure a soft-rendered frame is current. */
	if (ui->test_engine_step(t->engine, 1.0f / 60.0f) != SK_UI_TEST_OK) {
		ut_set_error(t, "capture_frame: step failed before write");
		return -1;
	}

	h = ui->test_engine_harness(t->engine);
	px = ui->harness_pixels(h);
	ui->harness_pixel_size(h, &pw, &ph);
	if (px == NULL || pw == 0u || ph == 0u) {
		ut_set_error(t, "capture_frame: no soft-render pixels (soft_render disabled?)");
		return -1;
	}

	{
		const_chr_t base = (t->name != NULL && t->name[0] != '\0') ? t->name : "ui_test";
		const_chr_t suf = (tag != NULL && tag[0] != '\0') ? tag : "fail";
		(void)snprintf(name, sizeof(name), "ui_author_%s_%s", base, suf);
	}

	ut_fill_mock_fs(&fs);
	if (ui->test_artifact_png_path(&fs, name, path, (u32)sizeof(path)) != 0) {
		ut_set_error(t, "capture_frame: test_artifact_png_path failed");
		return -1;
	}

	memset(&img, 0, sizeof(img));
	img.width = pw;
	img.height = ph;
	img.channels = 4u;
	img.pixels = (u8*)SK_CONST_CAST(void*, px);

	if (ui->cpu_image_write_png(&img, &fs, path) != 0) {
		ut_set_error(t, "capture_frame: cpu_image_write_png failed for '%s'", path);
		return -1;
	}

	(void)snprintf(t->fail_frame_path, sizeof(t->fail_frame_path), "%s", path);
	if (ut_logger_api() != NULL && ut_logger() != NULL) {
		sk_log_message(ut_logger_api(), SK_LOGGER_TYPE_INFO, ut_logger(), "ui test frame captured: %s", path);
	}
	return 0;
}

/** Capture frame and append path to last_error for a readable failure. */
static void ut_fail_with_frame(sk_ui_test_t* t, const_chr_t detail) {
	size_t n;
	const_chr_t path;
	const_chr_t d;

	if (t == NULL) {
		return;
	}
	(void)sk_ui_test_capture_frame(t, "fail");
	path = t->fail_frame_path;
	d = (detail != NULL && detail[0] != '\0') ? detail : "assertion failed";

	/* Copy detail, then append fail_frame=… without a second full snprintf. */
	n = 0u;
	while (d[n] != '\0' && n + 1u < sizeof(t->last_error)) {
		t->last_error[n] = d[n];
		n += 1u;
	}
	t->last_error[n] = '\0';

	if (n + 1u < sizeof(t->last_error)) {
		const_chr_t suffix_label = " | fail_frame=";
		const_chr_t suffix_path = (path[0] != '\0') ? path : "(capture failed)";
		size_t i;
		for (i = 0u; suffix_label[i] != '\0' && n + 1u < sizeof(t->last_error); ++i) {
			t->last_error[n++] = suffix_label[i];
		}
		for (i = 0u; suffix_path[i] != '\0' && n + 1u < sizeof(t->last_error); ++i) {
			t->last_error[n++] = suffix_path[i];
		}
		t->last_error[n] = '\0';
	}
}

static void ut_assert_fail(sk_ui_test_t* t) {
	const_chr_t msg = (t != NULL && t->last_error[0] != '\0') ? t->last_error : "sk_ui_test assertion failed";
	TEST_FAIL_MESSAGE(msg);
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

i32 sk_ui_test_begin(sk_ui_test_t* t, const sk_ui_api_t* ui, const_chr_t name, const sk_ui_test_engine_desc_t* desc) {
	sk_ui_test_engine_desc_t d;

	if (t == NULL || ui == NULL) {
		return -1;
	}
	memset(t, 0, sizeof(*t));
	t->ui = ui;
	t->name = name != NULL ? name : "ui_test";

	if (desc != NULL) {
		d = *desc;
	} else {
		memset(&d, 0, sizeof(d));
		d.width = 400.0f;
		d.height = 300.0f;
		d.content_scale = 1.0f;
	}
	/* Always soft-render so failure capture has pixels. */
	d.soft_render = 1;

	t->engine = ui->test_engine_create(&d);
	if (t->engine == NULL) {
		ut_set_error(t, "sk_ui_test_begin: test_engine_create failed");
		return -1;
	}
	t->ctx = ui->test_engine_context(t->engine);
	return 0;
}

void sk_ui_test_end(sk_ui_test_t* t) {
	if (t == NULL) {
		return;
	}
	if (t->ui != NULL && t->engine != NULL) {
		t->ui->test_engine_destroy(t->engine);
	}
	t->engine = NULL;
	t->ctx = NULL;
}

void sk_ui_test_run(const sk_ui_api_t* ui, const_chr_t name, const sk_ui_test_engine_desc_t* desc, sk_ui_test_hook_fn setup, sk_ui_test_hook_fn teardown, sk_ui_test_body_fn body) {
	sk_ui_test_t t;

	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "sk_ui_test_run: ui API is NULL");
	TEST_ASSERT_NOT_NULL_MESSAGE(body, "sk_ui_test_run: body is NULL");
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, sk_ui_test_begin(&t, ui, name, desc), "sk_ui_test_run: begin failed");

	if (setup != NULL) {
		setup(&t);
	}
	body(&t);
	if (teardown != NULL) {
		teardown(&t);
	}
	sk_ui_test_end(&t);
}

const sk_ui_api_t* sk_ui_test_api(const sk_ui_test_t* t) {
	return t != NULL ? t->ui : NULL;
}

sk_ui_context_t* sk_ui_test_context(const sk_ui_test_t* t) {
	return t != NULL ? t->ctx : NULL;
}

sk_ui_test_engine_t* sk_ui_test_engine(const sk_ui_test_t* t) {
	return t != NULL ? t->engine : NULL;
}

const_chr_t sk_ui_test_fail_frame_path(const sk_ui_test_t* t) {
	return t != NULL ? t->fail_frame_path : "";
}

const_chr_t sk_ui_test_last_error(const sk_ui_test_t* t) {
	return t != NULL ? t->last_error : "";
}

i32 sk_ui_test_step(sk_ui_test_t* t) {
	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return SK_UI_TEST_ERR_STEP;
	}
	return t->ui->test_engine_step(t->engine, 1.0f / 60.0f);
}

i32 sk_ui_test_yield(sk_ui_test_t* t, u32 frame_count) {
	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return SK_UI_TEST_ERR_STEP;
	}
	return t->ui->test_engine_yield_frames(t->engine, frame_count, 1.0f / 60.0f);
}

/* -------------------------------------------------------------------------- */
/* Actions                                                                    */
/* -------------------------------------------------------------------------- */

i32 sk_ui_click_item(sk_ui_test_t* t, const_chr_t test_id) {
	i32 rc;
	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	rc = t->ui->test_engine_click(t->engine, test_id);
	if (rc != SK_UI_TEST_OK) {
		ut_set_error(t, "clickItem(%s): %s", test_id != NULL ? test_id : "(null)", t->ui->test_engine_last_error(t->engine));
	}
	return rc;
}

i32 sk_ui_hover_item(sk_ui_test_t* t, const_chr_t test_id) {
	i32 rc;
	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	rc = t->ui->test_engine_hover(t->engine, test_id);
	if (rc != SK_UI_TEST_OK) {
		ut_set_error(t, "hoverItem(%s): %s", test_id != NULL ? test_id : "(null)", t->ui->test_engine_last_error(t->engine));
	}
	return rc;
}

i32 sk_ui_drag_item_to(sk_ui_test_t* t, const_chr_t from_id, const_chr_t to_id) {
	const sk_ui_api_t* ui;
	const sk_ui_test_item_t* a;
	const sk_ui_test_item_t* b;
	f32 x0, y0, x1, y1;
	i32 rc;

	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	ui = t->ui;
	a = ui->test_engine_find_by_id(t->engine, from_id);
	b = ui->test_engine_find_by_id(t->engine, to_id);
	if (a == NULL) {
		ut_set_error(t, "dragItemTo: from_id not found: %s", from_id != NULL ? from_id : "(null)");
		return SK_UI_TEST_ERR_NOT_FOUND;
	}
	if (b == NULL) {
		ut_set_error(t, "dragItemTo: to_id not found: %s", to_id != NULL ? to_id : "(null)");
		return SK_UI_TEST_ERR_NOT_FOUND;
	}
	x0 = a->rect.x + a->rect.width * 0.5f;
	y0 = a->rect.y + a->rect.height * 0.5f;
	x1 = b->rect.x + b->rect.width * 0.5f;
	y1 = b->rect.y + b->rect.height * 0.5f;
	rc = ui->test_engine_drag(t->engine, x0, y0, x1, y1, 4u, 1.0f / 60.0f);
	if (rc != SK_UI_TEST_OK) {
		ut_set_error(t, "dragItemTo(%s→%s): %s", from_id, to_id, ui->test_engine_last_error(t->engine));
	}
	return rc;
}

i32 sk_ui_type_into(sk_ui_test_t* t, const_chr_t test_id, const_chr_t text) {
	i32 rc;
	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return SK_UI_TEST_ERR_INPUT;
	}
	rc = t->ui->test_engine_type(t->engine, test_id, text);
	if (rc != SK_UI_TEST_OK) {
		ut_set_error(t, "typeInto(%s): %s", test_id != NULL ? test_id : "(null)", t->ui->test_engine_last_error(t->engine));
	}
	return rc;
}

i32 sk_ui_open_menu_path(sk_ui_test_t* t, const_chr_t id_path) {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	char buf[256];
	char* tok;
	char* segs[16];
	u32 nseg = 0u;
	u32 i;
	i32 rc;

	if (t == NULL || t->ui == NULL || t->engine == NULL || id_path == NULL || id_path[0] == '\0') {
		if (t != NULL) {
			ut_set_error(t, "openMenuPath: invalid path");
		}
		return SK_UI_TEST_ERR_NOT_FOUND;
	}
	ui = t->ui;
	ctx = t->ctx;
	if (strlen(id_path) >= sizeof(buf)) {
		ut_set_error(t, "openMenuPath: path too long");
		return SK_UI_TEST_ERR_INPUT;
	}
	memcpy(buf, id_path, strlen(id_path) + 1u);

	/* Split once so we know which segment is the leaf (click) vs intermediate. */
	for (tok = strtok(buf, "/"); tok != NULL && nseg < 16u; tok = strtok(NULL, "/")) {
		if (tok[0] != '\0') {
			segs[nseg++] = tok;
		}
	}
	if (nseg == 0u) {
		ut_set_error(t, "openMenuPath: empty path after split: %s", id_path);
		return SK_UI_TEST_ERR_NOT_FOUND;
	}

	for (i = 0u; i < nseg; ++i) {
		const_chr_t seg = segs[i];
		sk_ui_node_t node;
		sk_ui_prop_value_t pv;
		const_chr_t widget = NULL;
		i32 is_menu_like = 0;

		node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, seg);
		if (!sk_ui_node_is_valid(node) || !ui->node_alive(ctx, node)) {
			ut_set_error(t, "openMenuPath: segment not found: %s (path=%s)", seg, id_path);
			return SK_UI_TEST_ERR_NOT_FOUND;
		}
		if (ui->node_get_prop(ctx, node, "widget", &pv) == 0 && pv.type == SK_UI_PROP_STR) {
			widget = pv.data.str_value;
		}
		if (widget != NULL && (strcmp(widget, "menu") == 0 || strcmp(widget, "dropdown") == 0 || strcmp(widget, "submenu") == 0 || strcmp(widget, "menu_popup") == 0 ||
							   strcmp(widget, "context_menu") == 0)) {
			is_menu_like = 1;
		}

		if (is_menu_like) {
			/* Force open (not toggle) so paths are deterministic across runs. */
			if (ui->menu_set_open(ctx, node, 1) != 0) {
				ut_set_error(t, "openMenuPath: menu_set_open failed at '%s'", seg);
				return SK_UI_TEST_ERR_INPUT;
			}
		} else {
			rc = sk_ui_click_item(t, seg);
			if (rc != SK_UI_TEST_OK) {
				ut_set_error(t, "openMenuPath: click failed at '%s' (path=%s): %s", seg, id_path, t->last_error[0] != '\0' ? t->last_error : ui->test_engine_last_error(t->engine));
				return rc;
			}
		}

		rc = sk_ui_test_step(t);
		if (rc != SK_UI_TEST_OK) {
			ut_set_error(t, "openMenuPath: step failed after '%s'", seg);
			return rc;
		}
	}
	return SK_UI_TEST_OK;
}

/* -------------------------------------------------------------------------- */
/* Value helpers                                                              */
/* -------------------------------------------------------------------------- */

static i32 ut_read_value_string(sk_ui_test_t* t, const_chr_t test_id, char* out, u32 out_cap) {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_node_t node;
	const_chr_t widget;
	const_chr_t text;

	if (t == NULL || t->ui == NULL || t->ctx == NULL || test_id == NULL || out == NULL || out_cap == 0u) {
		return -1;
	}
	ui = t->ui;
	ctx = t->ctx;
	node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, test_id);
	if (!sk_ui_node_is_valid(node) || !ui->node_alive(ctx, node)) {
		ut_set_error(t, "valueEquals: item not found: %s", test_id);
		return -1;
	}

	widget = NULL;
	{
		sk_ui_prop_value_t pv;
		if (ui->node_get_prop(ctx, node, "widget", &pv) == 0 && pv.type == SK_UI_PROP_STR) {
			widget = pv.data.str_value;
		}
	}

	if (widget != NULL && strcmp(widget, "checkbox") == 0) {
		(void)snprintf(out, out_cap, "%d", ui->checkbox_get_checked(ctx, node) != 0 ? 1 : 0);
		return 0;
	}
	if (widget != NULL && strcmp(widget, "radio") == 0) {
		(void)snprintf(out, out_cap, "%d", ui->radio_get_checked(ctx, node) != 0 ? 1 : 0);
		return 0;
	}
	if (widget != NULL && strcmp(widget, "toggle") == 0) {
		(void)snprintf(out, out_cap, "%d", ui->toggle_get_on(ctx, node) != 0 ? 1 : 0);
		return 0;
	}
	if (widget != NULL && strcmp(widget, "slider") == 0) {
		(void)snprintf(out, out_cap, "%.4g", (double)ui->slider_get_value(ctx, node));
		return 0;
	}
	if (widget != NULL && strcmp(widget, "progress") == 0) {
		(void)snprintf(out, out_cap, "%.4g", (double)ui->progress_get_value(ctx, node));
		return 0;
	}
	if (widget != NULL && strcmp(widget, "text_input") == 0) {
		text = ui->text_input_get_text(ctx, node);
		(void)snprintf(out, out_cap, "%s", text != NULL ? text : "");
		return 0;
	}

	text = ui->node_get_visible_text(ctx, node);
	(void)snprintf(out, out_cap, "%s", text != NULL ? text : "");
	return 0;
}

/* -------------------------------------------------------------------------- */
/* check_*                                                                    */
/* -------------------------------------------------------------------------- */

i32 sk_ui_check_item_exists(sk_ui_test_t* t, const_chr_t test_id) {
	const sk_ui_test_item_t* item;
	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return -1;
	}
	item = t->ui->test_engine_find_by_id(t->engine, test_id);
	if (item == NULL) {
		/* Also try live query (before any step registry rebuild). */
		sk_ui_node_t n = t->ui->query_by_test_id(t->ctx, SK_UI_NODE_INVALID, test_id);
		if (!sk_ui_node_is_valid(n) || !t->ui->node_alive(t->ctx, n)) {
			char detail[256];
			(void)snprintf(detail, sizeof(detail), "itemExists: not found: %s", test_id != NULL ? test_id : "(null)");
			ut_fail_with_frame(t, detail);
			return -1;
		}
	}
	return 0;
}

i32 sk_ui_check_item_rect(sk_ui_test_t* t, const_chr_t test_id, f32 x, f32 y, f32 w, f32 h, f32 eps) {
	const sk_ui_test_item_t* item;
	sk_ui_rect_t r;
	f32 e;

	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return -1;
	}
	e = eps > 0.0f ? eps : 0.5f;
	item = t->ui->test_engine_find_by_id(t->engine, test_id);
	if (item != NULL) {
		r = item->rect;
	} else {
		sk_ui_node_t n = t->ui->query_by_test_id(t->ctx, SK_UI_NODE_INVALID, test_id);
		if (!sk_ui_node_is_valid(n) || t->ui->node_get_abs_rect(t->ctx, n, &r, NULL) != 0) {
			char detail[256];
			(void)snprintf(detail, sizeof(detail), "itemRect: not found: %s", test_id != NULL ? test_id : "(null)");
			ut_fail_with_frame(t, detail);
			return -1;
		}
	}

	if (fabsf(r.x - x) > e || fabsf(r.y - y) > e || fabsf(r.width - w) > e || fabsf(r.height - h) > e) {
		char detail[320];
		(void)snprintf(detail, sizeof(detail), "itemRect(%s): got (%.2f,%.2f,%.2f,%.2f) expected (%.2f,%.2f,%.2f,%.2f) eps=%.2f", test_id != NULL ? test_id : "(null)", (double)r.x,
					   (double)r.y, (double)r.width, (double)r.height, (double)x, (double)y, (double)w, (double)h, (double)e);
		ut_fail_with_frame(t, detail);
		return -1;
	}
	return 0;
}

i32 sk_ui_check_item_state(sk_ui_test_t* t, const_chr_t test_id, u32 must_have, u32 must_not) {
	const sk_ui_test_item_t* item;
	u32 st = 0u;
	sk_ui_node_t n;

	if (t == NULL || t->ui == NULL || t->engine == NULL) {
		return -1;
	}
	item = t->ui->test_engine_find_by_id(t->engine, test_id);
	if (item != NULL) {
		st = item->state_flags;
	} else {
		n = t->ui->query_by_test_id(t->ctx, SK_UI_NODE_INVALID, test_id);
		if (!sk_ui_node_is_valid(n) || !t->ui->node_alive(t->ctx, n)) {
			char detail[256];
			(void)snprintf(detail, sizeof(detail), "itemState: not found: %s", test_id != NULL ? test_id : "(null)");
			ut_fail_with_frame(t, detail);
			return -1;
		}
		st = t->ui->node_get_state(t->ctx, n);
	}

	if ((must_have != 0u && (st & must_have) != must_have) || (must_not != 0u && (st & must_not) != 0u)) {
		char detail[320];
		(void)snprintf(detail, sizeof(detail), "itemState(%s): flags=0x%x must_have=0x%x must_not=0x%x", test_id != NULL ? test_id : "(null)", st, must_have, must_not);
		ut_fail_with_frame(t, detail);
		return -1;
	}
	return 0;
}

i32 sk_ui_check_value_equals(sk_ui_test_t* t, const_chr_t test_id, const_chr_t expected) {
	char actual[256];

	if (expected == NULL) {
		expected = "";
	}
	if (ut_read_value_string(t, test_id, actual, (u32)sizeof(actual)) != 0) {
		/* last_error already set; still capture frame */
		ut_fail_with_frame(t, t->last_error[0] != '\0' ? t->last_error : "valueEquals: read failed");
		return -1;
	}
	if (strcmp(actual, expected) != 0) {
		char detail[384];
		(void)snprintf(detail, sizeof(detail), "valueEquals(%s): got \"%s\" expected \"%s\"", test_id != NULL ? test_id : "(null)", actual, expected);
		ut_fail_with_frame(t, detail);
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* assert_* (Unity)                                                           */
/* -------------------------------------------------------------------------- */

void sk_ui_item_exists(sk_ui_test_t* t, const_chr_t test_id) {
	if (sk_ui_check_item_exists(t, test_id) != 0) {
		ut_assert_fail(t);
	}
}

void sk_ui_item_rect(sk_ui_test_t* t, const_chr_t test_id, f32 x, f32 y, f32 w, f32 h, f32 eps) {
	if (sk_ui_check_item_rect(t, test_id, x, y, w, h, eps) != 0) {
		ut_assert_fail(t);
	}
}

void sk_ui_item_state(sk_ui_test_t* t, const_chr_t test_id, u32 must_have, u32 must_not) {
	if (sk_ui_check_item_state(t, test_id, must_have, must_not) != 0) {
		ut_assert_fail(t);
	}
}

void sk_ui_value_equals(sk_ui_test_t* t, const_chr_t test_id, const_chr_t expected) {
	if (sk_ui_check_value_equals(t, test_id, expected) != 0) {
		ut_assert_fail(t);
	}
}

/* -------------------------------------------------------------------------- */
/* Sample tests (pure authoring API) + failure-path verification              */
/*                                                                            */
/* Built only into the UI plugin (SK_UI_PLUGIN_BUILD): they use                */
/* ui_get_api_table(). Integration compiles this TU without that define and   */
/* supplies the API via sk_ui_test_run / SK_UI_TEST_WITH_API.                  */
/* -------------------------------------------------------------------------- */

#if defined(SK_UI_PLUGIN_BUILD)

static void ut_set_size(sk_ui_test_t* t, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	const sk_ui_api_t* ui = t->ui;
	memset(&p, 0, sizeof(p));
	/* style_props_clear may not be public; zero + mask is enough for merge. */
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(t->ctx, node, &p));
}

static i32 g_ut_clicks;

static void ut_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	(void)user;
	g_ut_clicks += 1;
}

/**
 * Sample test written purely against the authoring API: build, step, click,
 * assert existence / state / value.
 */
SK_UI_TEST(sample_click_and_type) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	sk_ui_node_t ti;
	sk_ui_node_callbacks_t cbs;

	TEST_ASSERT_NOT_NULL(ui);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	btn = ui->widget_button(ctx, root, "Go", "btn-go");
	ut_set_size(t, btn, 100.0f, 32.0f);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ut_on_click;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	ti = ui->widget_text_input(ctx, root, "", "ti-name");
	ut_set_size(t, ti, 160.0f, 28.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "btn-go");
	sk_ui_item_exists(t, "ti-name");

	g_ut_clicks = 0;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "btn-go"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "btn-go", (u32)SK_UI_STATE_HOVER, 0u);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "btn-go"));
	TEST_ASSERT_EQUAL_INT(1, g_ut_clicks);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_type_into(t, "ti-name", "hello"));
	sk_ui_value_equals(t, "ti-name", "hello");
}

static void ut_setup_menu_tree(sk_ui_test_t* t) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t menu;
	sk_ui_node_t popup;
	sk_ui_node_t item;

	bar = ui->widget_menu_bar(ctx, root, "menubar");
	ut_set_size(t, bar, 320.0f, 28.0f);
	menu = ui->widget_menu(ctx, bar, "File", "menu-file");
	ut_set_size(t, menu, 64.0f, 24.0f);
	popup = ui->menu_get_popup(ctx, menu);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(popup));
	item = ui->widget_menu_item(ctx, popup, "Open", "item-open");
	ut_set_size(t, item, 100.0f, 22.0f);
	(void)item;
}

/**
 * openMenuPath + setup hook: click File then Open.
 */
SK_UI_TEST_EX(sample_open_menu_path, ut_setup_menu_tree, NULL) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_node_t menu;

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "menu-file");
	sk_ui_item_exists(t, "item-open");

	/* Single-segment: open the File menu. */
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK, sk_ui_open_menu_path(t, "menu-file"), t->last_error[0] != '\0' ? t->last_error : "openMenuPath(menu-file) failed");
	menu = ui->query_by_test_id(t->ctx, SK_UI_NODE_INVALID, "menu-file");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(menu));
	TEST_ASSERT_TRUE(ui->menu_get_open(t->ctx, menu) != 0);

	/* Multi-segment: open File then activate Open item. */
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK, sk_ui_open_menu_path(t, "menu-file/item-open"),
								  t->last_error[0] != '\0' ? t->last_error : "openMenuPath(menu-file/item-open) failed");
	sk_ui_item_exists(t, "item-open");
}

/**
 * Meta-test: an intentionally broken assertion produces a readable failure
 * string that includes the captured frame path (without failing the suite via
 * Unity assert — uses check_* return code).
 */
SK_TEST(ui_author_broken_assert_captures_frame_path) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_test_t t;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	i32 rc;
	const_chr_t err;
	const_chr_t path;
	sk_filesystem_api_t fs;

	TEST_ASSERT_EQUAL_INT(0, sk_ui_test_begin(&t, ui, "broken_assert", NULL));
	root = ui->context_root(t.ctx);
	btn = ui->widget_button(t.ctx, root, "X", "btn-only");
	ut_set_size(&t, btn, 80.0f, 24.0f);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(&t));

	/* Intentionally look up a missing id. */
	rc = sk_ui_check_item_exists(&t, "no-such-item");
	TEST_ASSERT_TRUE(rc != 0);

	err = sk_ui_test_last_error(&t);
	path = sk_ui_test_fail_frame_path(&t);
	TEST_ASSERT_NOT_NULL(err);
	TEST_ASSERT_TRUE(strstr(err, "itemExists") != NULL || strstr(err, "not found") != NULL);
	TEST_ASSERT_TRUE(strstr(err, "fail_frame=") != NULL);
	TEST_ASSERT_TRUE(path[0] != '\0');
	TEST_ASSERT_TRUE(strstr(path, ".png") != NULL);
	/* Message embeds the same path for readability. */
	TEST_ASSERT_TRUE(strstr(err, path) != NULL);

	ut_fill_mock_fs(&fs);
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs.get_file_status(path));

	sk_ui_test_end(&t);
}

/**
 * dragItemTo sample: drag slider thumb track from low to mid via item ids.
 * Uses a slider as source and a labeled target zone for destination geometry.
 */
SK_UI_TEST(sample_drag_item_to) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sl;
	sk_ui_node_t zone;
	f32 value;

	sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 0.0f, "sl-src");
	ut_set_size(t, sl, 200.0f, 24.0f);
	/* Place a destination panel to the right so drag has a clear end point. */
	zone = ui->widget_panel(ctx, root, "drop-zone");
	ut_set_size(t, zone, 40.0f, 40.0f);
	{
		sk_ui_layout_style_t ls;
		ui->node_get_layout_style(ctx, zone, &ls);
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(220.0f);
		ls.top = sk_ui_pt(0.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, zone, &ls));
	}

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "sl-src");
	sk_ui_item_exists(t, "drop-zone");

	/* Drag from slider (left) toward drop zone (right) updates slider value. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_drag_item_to(t, "sl-src", "drop-zone"));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_TRUE(value > 5.0f);
}

#endif /* SK_UI_PLUGIN_BUILD */

#endif /* SK_TESTS */
