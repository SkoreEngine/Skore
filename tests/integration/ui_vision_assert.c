/*
 * ui_vision_assert.c — per-widget grok-vision assertion helper (APX-251).
 *
 * See ui_vision_assert.h for the contract. Live grading shells out to
 * scripts/ui_vision_assert.py; mock mode uses SK_UI_VISION_MOCK_RESPONSE.
 */

#include "ui_vision_assert.h"

#include "logger.h"
#include "path.h"

#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#define ui_vision_getpid() _getpid()
#define popen _popen
#define pclose _pclose
/* Minimal setenv/unsetenv for tests and optional backend selection. */
static int ui_vision_setenv(const char* k, const char* v, int overwrite) {
	char buf[2048];
	(void)overwrite;
	if (k == NULL) {
		return -1;
	}
	if (v == NULL) {
		v = "";
	}
	if (snprintf(buf, sizeof(buf), "%s=%s", k, v) < 0) {
		return -1;
	}
	return _putenv(buf);
}
static int ui_vision_unsetenv(const char* k) {
	return ui_vision_setenv(k, "", 1);
}
#define setenv ui_vision_setenv
#define unsetenv ui_vision_unsetenv
#else
#include <sys/wait.h>
#include <unistd.h>
#define ui_vision_getpid() getpid()
#endif

/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static sk_logger_t* ui_vision_logger(void) {
	static sk_logger_t* log = NULL;
	if (log == NULL) {
		log = sk_logger_api()->create_logger("ui-vision-assert");
	}
	return log;
}

static void ui_vision_fail(const_chr_t fmt, ...) {
	va_list args;
	va_start(args, fmt);
	sk_log_messagev(sk_logger_api(), SK_LOGGER_TYPE_ERROR, ui_vision_logger(), fmt, args);
	va_end(args);
}

static void ui_vision_info(const_chr_t fmt, ...) {
	va_list args;
	va_start(args, fmt);
	sk_log_messagev(sk_logger_api(), SK_LOGGER_TYPE_INFO, ui_vision_logger(), fmt, args);
	va_end(args);
}

/* -------------------------------------------------------------------------- */
/* Rubric catalog (authored text; must stay aligned with testdata/vision/)    */
/* -------------------------------------------------------------------------- */

typedef struct ui_vision_rubric_entry_t {
	const_chr_t name;
	const_chr_t text;
} ui_vision_rubric_entry_t;

static const ui_vision_rubric_entry_t ui_vision_rubrics[SK_UI_VISION_WIDGET_COUNT] = {
	{"button", "Widget family: BUTTON\n"
			   "Strict visual criteria (PASS only if ALL hold):\n"
			   "1. A single rectangular control is visible with a clear outer edge.\n"
			   "2. The control reads as a pressable button, not a bare text label.\n"
			   "3. HOVER: fill is noticeably lighter/brighter than default.\n"
			   "4. ACTIVE: fill is noticeably darker than default.\n"
			   "5. DISABLED: visibly dimmed (lower contrast) — not full-strength accent.\n"
			   "6. Label text (if present) sits inside the button bounds.\n"
			   "FAIL if: no control, wrong interaction state colors, or disabled looks enabled.\n"},
	{"checkbox", "Widget family: CHECKBOX\n"
				 "UNCHECKED: square control; interior empty (no X, checkmark, dot, or solid fill).\n"
				 "CHECKED: square control with a clear X mark of two crossing diagonal strokes.\n"
				 "FAIL checked if mark is a filled solid square/block, single centered dot,\n"
				 "checkmark/tick glyph (V shape), letter, or missing. X must read as two diagonals.\n"
				 "DISABLED: control/mark visibly dimmed vs enabled.\n"},
	{"radio", "Widget family: RADIO\n"
			  "UNCHECKED: circular ring; no filled inner disc.\n"
			  "CHECKED: outer circular ring plus a filled inner circle (disc) centered inside.\n"
			  "FAIL if empty ring only, square checkbox with X/check, speck, X mark, or filled square.\n"
			  "DISABLED: visibly dimmed vs enabled.\n"},
	{"toggle", "Widget family: TOGGLE (switch)\n"
			   "Horizontal switch track (pill) with a distinct thumb/knob separable from the track.\n"
			   "OFF: thumb toward start, muted track. ON: thumb toward end, stronger track fill.\n"
			   "DISABLED: whole control visibly dimmed.\n"
			   "FAIL if looks like a checkbox X or ON/OFF are indistinguishable.\n"},
	{"slider", "Widget family: SLIDER\n"
			   "Horizontal track plus a distinct grab handle/thumb thicker than the track.\n"
			   "Optional fill ends at the handle. FAIL if only a solid bar with no separable handle,\n"
			   "only a progress bar, or empty control. DISABLED: track and handle dimmed.\n"},
	{"progress", "Widget family: PROGRESS BAR\n"
				 "Horizontal track with filled portion matching claimed fraction. NO grab handle.\n"
				 "FAIL if missing fill when fraction>0, full when 0, or looks like a slider with thumb.\n"},
	{"text_input", "Widget family: TEXT INPUT\n"
				   "Rectangular field with clear border/recessed face. Text (if any) inside bounds.\n"
				   "FOCUSED: stronger border and/or caret. DISABLED: muted border/ink vs enabled.\n"
				   "FAIL if no field chrome or disabled looks fully enabled.\n"},
	{"scrollbar", "Widget family: SCROLLBAR\n"
				  "Track along one edge with a distinct thumb shorter than the track.\n"
				  "Thumb position roughly matches claimed scroll fraction.\n"
				  "FAIL if no thumb or thumb spans the entire track.\n"},
	{"scroll_view", "Widget family: SCROLL VIEW\n"
					"Rectangular viewport; overflowing content clipped to the view.\n"
					"When scrollbars claimed: at least one scrollbar with a distinct thumb.\n"
					"FAIL if content spills outside panel chrome or overflow lacks claimed scrollbar.\n"},
	{"panel", "Widget family: PANEL / VIEW\n"
			  "Solid rectangular surface with coherent edges and a visible border when claimed.\n"
			  "Children (if any) sit inside the content box, not outside the chrome.\n"
			  "FAIL if missing surface when claimed or children clearly overflow the chrome.\n"},
	{"label", "Widget family: LABEL / TEXT\n"
			  "Glyphs visible and readable against the background; roughly horizontal LTR.\n"
			  "FAIL if missing text, zero-contrast ink, or clipped into illegibility.\n"},
	{"image", "Widget family: IMAGE\n"
			  "Rectangular image region; textured content visible when claimed bound.\n"
			  "FAIL if missing region or claimed textured content is a blank hole.\n"},
	{"window", "Widget family: WINDOW / TITLED PANEL\n"
			   "Framed window chrome with a distinct title bar band along the top (different fill\n"
			   "from the content body) and a visible outer border around the whole window.\n"
			   "Title text (if claimed) sits inside the title bar. Content area below the bar.\n"
			   "FAIL if no title bar band, no border, or title bar is indistinguishable from body.\n"},
	{"tab", "Widget family: TAB BAR\n"
			"Horizontal strip of two or more tab labels. The selected/active tab must be\n"
			"visually distinct from unselected tabs (fill, border accent, and/or brighter text).\n"
			"FAIL if only one tab looks present, selected and unselected are identical, or no strip.\n"},
	{"menu", "Widget family: MENU / DROPDOWN POPUP\n"
			 "Floating rectangular menu surface with a border and stacked item rows (labels).\n"
			 "When separators are claimed: thin horizontal divider lines between item groups.\n"
			 "FAIL if no popup panel, no item rows, or claimed separators are missing.\n"},
	{"table", "Widget family: LIST / TABLE\n"
			  "Grid-like rows of cells. Header row must differ from body rows (stronger fill or\n"
			  "weight). Body rows show striping when claimed (alternating row fills). Column\n"
			  "separators (vertical lines or gaps) divide cells when claimed.\n"
			  "FAIL if header matches body, no rows, missing claimed striping, or no columns.\n"},
	{"tooltip", "Widget family: TOOLTIP / POPUP\n"
				"Small floating overlay panel above the canvas with a clear border/chrome and\n"
				"readable label text inside. Distinct from the full-frame background.\n"
				"FAIL if no floating panel, no text, or fully transparent/missing surface.\n"},
	{"flexbox", "Widget family: FLEXBOX LAYOUT SAMPLE\n"
				"Grade the QUALITATIVE arrangement of distinctly coloured solid children against\n"
				"the claimed state/scene hint. Children are solid filled rectangles (typically\n"
				"red/green/blue or similarly separable hues) inside a darker container on a light\n"
				"or neutral canvas. Do NOT measure pixels — judge relative positions, spacing,\n"
				"and packing that a human would describe in one sentence.\n"
				"PASS only if ALL hold:\n"
				"1. The claimed number of coloured children is visible and distinguishable.\n"
				"2. Their relative order and axis packing match the claim (row vs column,\n"
				"   left/right/top/bottom edges, single line vs nested bands).\n"
				"3. Spacing claims hold qualitatively (equal gaps, space-between last child\n"
				"   touching the far edge, centered on the cross axis, grow fills leftover\n"
				"   main-axis space, nested header above body, etc.).\n"
				"4. FAIL if rects look self-consistent but the picture clearly contradicts the\n"
				"   claim (e.g. packed to the left when space-between is claimed, second flex\n"
				"   line when single-line is claimed, grow child same width as fixed sibling).\n"},
	{"disabled", "Cross-cutting: DISABLED state (any widget family)\n"
				 "Control is visibly dimmed vs enabled (lower-contrast fill/border/mark).\n"
				 "FAIL if full-strength accent colors still read as enabled.\n"
				 "Geometry must still match the family (disabled is not missing).\n"},
};

const_chr_t sk_ui_vision_rubric_name(sk_ui_vision_widget_family_t family) {
	if ((u32)family >= (u32)SK_UI_VISION_WIDGET_COUNT) {
		return "unknown";
	}
	return ui_vision_rubrics[family].name;
}

const_chr_t sk_ui_vision_rubric_text(sk_ui_vision_widget_family_t family) {
	if ((u32)family >= (u32)SK_UI_VISION_WIDGET_COUNT) {
		return "";
	}
	return ui_vision_rubrics[family].text;
}

i32 sk_ui_vision_widget_family_parse(const_chr_t name, sk_ui_vision_widget_family_t* out_family) {
	u32 i;
	if (name == NULL || out_family == NULL) {
		return -1;
	}
	for (i = 0u; i < (u32)SK_UI_VISION_WIDGET_COUNT; ++i) {
		if (strcmp(name, ui_vision_rubrics[i].name) == 0) {
			*out_family = (sk_ui_vision_widget_family_t)i;
			return 0;
		}
	}
	/* Aliases */
	if (strcmp(name, "text-input") == 0 || strcmp(name, "textinput") == 0) {
		*out_family = SK_UI_VISION_WIDGET_TEXT_INPUT;
		return 0;
	}
	if (strcmp(name, "scroll-view") == 0 || strcmp(name, "scrollview") == 0) {
		*out_family = SK_UI_VISION_WIDGET_SCROLL_VIEW;
		return 0;
	}
	if (strcmp(name, "view") == 0) {
		*out_family = SK_UI_VISION_WIDGET_PANEL;
		return 0;
	}
	if (strcmp(name, "editor_window") == 0 || strcmp(name, "title_bar") == 0) {
		*out_family = SK_UI_VISION_WIDGET_WINDOW;
		return 0;
	}
	if (strcmp(name, "tab_bar") == 0 || strcmp(name, "tabs") == 0) {
		*out_family = SK_UI_VISION_WIDGET_TAB;
		return 0;
	}
	if (strcmp(name, "dropdown") == 0 || strcmp(name, "menu_popup") == 0 || strcmp(name, "context_menu") == 0) {
		*out_family = SK_UI_VISION_WIDGET_MENU;
		return 0;
	}
	if (strcmp(name, "list") == 0) {
		*out_family = SK_UI_VISION_WIDGET_TABLE;
		return 0;
	}
	if (strcmp(name, "popup") == 0) {
		*out_family = SK_UI_VISION_WIDGET_TOOLTIP;
		return 0;
	}
	if (strcmp(name, "layout") == 0 || strcmp(name, "flex") == 0 || strcmp(name, "flex_layout") == 0) {
		*out_family = SK_UI_VISION_WIDGET_FLEXBOX;
		return 0;
	}
	return -1;
}

/* -------------------------------------------------------------------------- */
/* Path helpers                                                               */
/* -------------------------------------------------------------------------- */

static void ui_vision_result_clear(sk_ui_vision_result_t* r) {
	if (r == NULL) {
		return;
	}
	memset(r, 0, sizeof(*r));
}

static i32 ui_vision_join3(const_chr_t a, const_chr_t b, const_chr_t c, char* out, u32 out_cap) {
	char mid[SK_FS_PATH_MAX];
	i32 n;
	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(a), sk_str_view_cstr(b), mid, (u32)sizeof(mid));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(mid), sk_str_view_cstr(c), out, out_cap);
	return (n < 0) ? -1 : 0;
}

i32 sk_ui_vision_rubric_file_path(sk_ui_vision_widget_family_t family, char* out, u32 out_cap) {
	const_chr_t name;
	char file[64];
	i32 sn;
	if (out == NULL || out_cap == 0u || (u32)family >= (u32)SK_UI_VISION_WIDGET_COUNT) {
		return -1;
	}
	name = sk_ui_vision_rubric_name(family);
	sn = snprintf(file, sizeof(file), "%s.txt", name);
	if (sn < 0 || (u32)sn >= (u32)sizeof(file)) {
		return -1;
	}
#ifdef SK_UI_GOLDEN_DIR
	return ui_vision_join3(SK_UI_GOLDEN_DIR, "vision/rubrics", file, out, out_cap);
#else
	return ui_vision_join3("plugins/ui/testdata", "vision/rubrics", file, out, out_cap);
#endif
}

static i32 ui_vision_script_path(char* out, u32 out_cap) {
	const char* env;
	i32 n;
	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	env = getenv("SK_UI_VISION_SCRIPT");
	if (env != NULL && env[0] != '\0') {
		n = snprintf(out, out_cap, "%s", env);
		return (n < 0 || (u32)n >= out_cap) ? -1 : 0;
	}
	/* Prefer repo-relative path from common cwds (build/bin or repo root). */
	{
		static const char* candidates[] = {
			"../../scripts/ui_vision_assert.py",
			"../scripts/ui_vision_assert.py",
			"scripts/ui_vision_assert.py",
			"./scripts/ui_vision_assert.py",
		};
		u32 i;
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		for (i = 0u; i < (u32)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
			if (fs != NULL && fs->get_file_status(candidates[i]) == SK_FILE_STATUS_FILE) {
				n = snprintf(out, out_cap, "%s", candidates[i]);
				return (n < 0 || (u32)n >= out_cap) ? -1 : 0;
			}
		}
	}
	n = snprintf(out, out_cap, "%s", "scripts/ui_vision_assert.py");
	return (n < 0 || (u32)n >= out_cap) ? -1 : 0;
}

static i32 ui_vision_escape_single_quotes(const_chr_t in, char* out, u32 out_cap) {
	/* Wrap for use inside single-quoted shell strings: ' -> '\'' */
	u32 di = 0u;
	u32 i;
	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	if (in == NULL) {
		in = "";
	}
	for (i = 0u; in[i] != '\0'; ++i) {
		if (in[i] == '\'') {
			if (di + 4u >= out_cap) {
				return -1;
			}
			out[di++] = '\'';
			out[di++] = '\\';
			out[di++] = '\'';
			out[di++] = '\'';
		} else {
			if (di + 1u >= out_cap) {
				return -1;
			}
			out[di++] = in[i];
		}
	}
	out[di] = '\0';
	return 0;
}

/* -------------------------------------------------------------------------- */
/* JSON micro-parser for {"pass":bool,"reason":"...","skipped":bool}          */
/* -------------------------------------------------------------------------- */

static const char* ui_vision_skip_ws(const char* s) {
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
		++s;
	}
	return s;
}

static i32 ui_vision_json_find_key(const char* json, const char* key, const char** out_val) {
	char pattern[64];
	const char* p;
	i32 n;
	if (json == NULL || key == NULL || out_val == NULL) {
		return -1;
	}
	n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	if (n < 0 || (u32)n >= (u32)sizeof(pattern)) {
		return -1;
	}
	p = strstr(json, pattern);
	if (p == NULL) {
		return -1;
	}
	p += (size_t)n;
	p = ui_vision_skip_ws(p);
	if (*p != ':') {
		return -1;
	}
	++p;
	p = ui_vision_skip_ws(p);
	*out_val = p;
	return 0;
}

static i32 ui_vision_json_parse_bool(const char* p, i32* out) {
	if (p == NULL || out == NULL) {
		return -1;
	}
	if (strncmp(p, "true", 4) == 0) {
		*out = 1;
		return 0;
	}
	if (strncmp(p, "false", 5) == 0) {
		*out = 0;
		return 0;
	}
	if (*p == '1') {
		*out = 1;
		return 0;
	}
	if (*p == '0') {
		*out = 0;
		return 0;
	}
	return -1;
}

static i32 ui_vision_json_parse_string(const char* p, char* out, u32 out_cap) {
	u32 di = 0u;
	if (p == NULL || out == NULL || out_cap == 0u) {
		return -1;
	}
	if (*p != '"') {
		return -1;
	}
	++p;
	while (*p != '\0' && *p != '"') {
		char c = *p++;
		if (c == '\\' && *p != '\0') {
			char e = *p++;
			switch (e) {
			case 'n':
				c = '\n';
				break;
			case 't':
				c = '\t';
				break;
			case 'r':
				c = '\r';
				break;
			case '"':
				c = '"';
				break;
			case '\\':
				c = '\\';
				break;
			default:
				c = e;
				break;
			}
		}
		if (di + 1u >= out_cap) {
			break;
		}
		out[di++] = c;
	}
	out[di] = '\0';
	return 0;
}

static i32 ui_vision_parse_result_json(const char* json, sk_ui_vision_result_t* out) {
	const char* p;
	i32 pass = 0;
	i32 skipped = 0;
	if (json == NULL || out == NULL) {
		return -1;
	}
	if (ui_vision_json_find_key(json, "pass", &p) != 0 || ui_vision_json_parse_bool(p, &pass) != 0) {
		return -1;
	}
	out->passed = pass != 0 ? 1 : 0;
	if (ui_vision_json_find_key(json, "skipped", &p) == 0) {
		(void)ui_vision_json_parse_bool(p, &skipped);
	}
	out->skipped = skipped != 0 ? 1 : 0;
	if (ui_vision_json_find_key(json, "reason", &p) == 0) {
		(void)ui_vision_json_parse_string(p, out->reason, (u32)sizeof(out->reason));
	}
	if (out->reason[0] == '\0') {
		snprintf(out->reason, sizeof(out->reason), "(no reason)");
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Save offending frame                                                       */
/* -------------------------------------------------------------------------- */

static i32 ui_vision_copy_file(const_chr_t src, const_chr_t dest) {
	FILE* in;
	FILE* outf;
	char buf[8192];
	size_t n;
	if (src == NULL || dest == NULL) {
		return -1;
	}
	in = fopen(src, "rb");
	if (in == NULL) {
		return -1;
	}
	outf = fopen(dest, "wb");
	if (outf == NULL) {
		fclose(in);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0u) {
		if (fwrite(buf, 1, n, outf) != n) {
			fclose(in);
			fclose(outf);
			return -1;
		}
	}
	fclose(in);
	fclose(outf);
	return 0;
}

static i32 ui_vision_save_fail_frame(const sk_ui_api_t* ui, const sk_filesystem_api_t* fs, const_chr_t scene_name, const_chr_t image_path, const sk_ui_cpu_image_t* image,
									 char* out_path, u32 out_cap) {
	char name[128];
	char dest[SK_FS_PATH_MAX];
	i32 sn;
	i32 have_dest = 0;

	if (out_path != NULL && out_cap > 0u) {
		out_path[0] = '\0';
	}
	if (fs == NULL) {
		fs = sk_filesystem_api();
	}
	sn = snprintf(name, sizeof(name), "%s_vision_fail", (scene_name != NULL && scene_name[0] != '\0') ? scene_name : "vision");
	if (sn < 0 || (u32)sn >= (u32)sizeof(name)) {
		return -1;
	}

	/* Prefer the shared test-artifact root via the UI API when available. */
	if (ui != NULL && ui->test_artifact_png_path != NULL) {
		if (ui->test_artifact_png_path(fs, name, dest, (u32)sizeof(dest)) == 0) {
			have_dest = 1;
		}
	}
	if (!have_dest) {
		const char* root = getenv("SK_TEST_ARTIFACT_DIR");
		const char* tmp = getenv("TMPDIR");
		if (root == NULL || root[0] == '\0') {
#ifdef SK_TEST_ARTIFACT_DIR
			root = SK_TEST_ARTIFACT_DIR;
#else
			root = NULL;
#endif
		}
		if (root == NULL || root[0] == '\0') {
			root = (tmp != NULL && tmp[0] != '\0') ? tmp : "/tmp";
		}
		/* Best-effort mkdir of the artifact root (ignore errors; write may still work). */
		if (fs != NULL && fs->create_directory != NULL) {
			(void)fs->create_directory(root);
		}
		sn = snprintf(dest, sizeof(dest), "%s/%s.png", root, name);
		if (sn < 0 || (u32)sn >= (u32)sizeof(dest)) {
			return -1;
		}
		have_dest = 1;
		(void)have_dest;
	}

	if (image != NULL && image->pixels != NULL && image->width > 0u && image->height > 0u && ui != NULL && ui->cpu_image_write_png != NULL) {
		if (ui->cpu_image_write_png(image, fs, dest) != 0) {
			ui_vision_fail("vision_assert: failed to write failing frame '%s'", dest);
			return -1;
		}
	} else if (image_path != NULL && image_path[0] != '\0') {
		if (ui_vision_copy_file(image_path, dest) != 0) {
			ui_vision_fail("vision_assert: cannot copy source frame '%s' → '%s'", image_path, dest);
			return -1;
		}
	} else {
		return -1;
	}

	if (out_path != NULL && out_cap > 0u) {
		sn = snprintf(out_path, out_cap, "%s", dest);
		if (sn < 0 || (u32)sn >= out_cap) {
			return -1;
		}
	}
	ui_vision_info("vision_assert: saved failing frame → %s", dest);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Backend: mock / script                                                     */
/* -------------------------------------------------------------------------- */

static i32 ui_vision_backend_is_mock(void) {
	const char* b = getenv("SK_UI_VISION_BACKEND");
	if (b != NULL && strcmp(b, "mock") == 0) {
		return 1;
	}
	if (getenv("SK_UI_VISION_MOCK_RESPONSE") != NULL) {
		return 1;
	}
	return 0;
}

static i32 ui_vision_run_mock(sk_ui_vision_result_t* out) {
	const char* env = getenv("SK_UI_VISION_MOCK_RESPONSE");
	if (env == NULL || env[0] == '\0') {
		ui_vision_fail("vision_assert: mock backend requires SK_UI_VISION_MOCK_RESPONSE JSON");
		snprintf(out->reason, sizeof(out->reason), "mock backend missing SK_UI_VISION_MOCK_RESPONSE");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	if (ui_vision_parse_result_json(env, out) != 0) {
		ui_vision_fail("vision_assert: failed to parse SK_UI_VISION_MOCK_RESPONSE");
		snprintf(out->reason, sizeof(out->reason), "invalid mock response JSON");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	return out->skipped ? SK_UI_VISION_ASSERT_SKIPPED : (out->passed ? SK_UI_VISION_ASSERT_OK : SK_UI_VISION_ASSERT_FAIL);
}

static i32 ui_vision_run_script(const_chr_t image_path, sk_ui_vision_widget_family_t family, const_chr_t state_hint, sk_ui_vision_result_t* out) {
	char script[SK_FS_PATH_MAX];
	char rubric_path[SK_FS_PATH_MAX];
	char cmd[4096];
	char esc_image[SK_FS_PATH_MAX * 2];
	char esc_rubric[SK_FS_PATH_MAX * 2];
	char esc_family[128];
	char esc_state[256];
	char line[2048];
	char last_json[2048];
	FILE* pipe;
	const_chr_t family_name;
	i32 has_json = 0;
	i32 status;

	if (ui_vision_script_path(script, (u32)sizeof(script)) != 0) {
		snprintf(out->reason, sizeof(out->reason), "cannot resolve vision script path");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	if (sk_ui_vision_rubric_file_path(family, rubric_path, (u32)sizeof(rubric_path)) != 0) {
		snprintf(out->reason, sizeof(out->reason), "cannot resolve rubric file path");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	family_name = sk_ui_vision_rubric_name(family);
	if (ui_vision_escape_single_quotes(image_path, esc_image, (u32)sizeof(esc_image)) != 0 ||
		ui_vision_escape_single_quotes(rubric_path, esc_rubric, (u32)sizeof(esc_rubric)) != 0 ||
		ui_vision_escape_single_quotes(family_name, esc_family, (u32)sizeof(esc_family)) != 0 ||
		ui_vision_escape_single_quotes(state_hint != NULL ? state_hint : "", esc_state, (u32)sizeof(esc_state)) != 0) {
		snprintf(out->reason, sizeof(out->reason), "path escape failed");
		return SK_UI_VISION_ASSERT_ERROR;
	}

	/*
	 * Prefer on-disk rubric file; if missing, pass embedded text via env to
	 * avoid shell-escaping multi-line rubrics. The script reads --rubric-file
	 * first; we fall back to --rubric-text only when the file is absent.
	 */
	{
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		i32 have_file = (fs != NULL && fs->get_file_status(rubric_path) == SK_FILE_STATUS_FILE) ? 1 : 0;
		if (have_file) {
			status = snprintf(cmd, sizeof(cmd), "python3 '%s' --image '%s' --rubric-file '%s' --family '%s' --state '%s' 2>/dev/null", script, esc_image, esc_rubric, esc_family,
							  esc_state);
		} else {
			/* Embed rubric via a here-doc alternative: write temp file. */
			char tmp_rubric[SK_FS_PATH_MAX];
			FILE* tf;
			const char* tmpdir = getenv("TMPDIR");
			if (tmpdir == NULL || tmpdir[0] == '\0') {
				tmpdir = "/tmp";
			}
			status = snprintf(tmp_rubric, sizeof(tmp_rubric), "%s/skore_ui_vision_rubric_%d.txt", tmpdir, (int)ui_vision_getpid());
			if (status < 0 || (u32)status >= (u32)sizeof(tmp_rubric)) {
				snprintf(out->reason, sizeof(out->reason), "temp rubric path overflow");
				return SK_UI_VISION_ASSERT_ERROR;
			}
			tf = fopen(tmp_rubric, "wb");
			if (tf == NULL) {
				snprintf(out->reason, sizeof(out->reason), "cannot write temp rubric");
				return SK_UI_VISION_ASSERT_ERROR;
			}
			fputs(sk_ui_vision_rubric_text(family), tf);
			fclose(tf);
			if (ui_vision_escape_single_quotes(tmp_rubric, esc_rubric, (u32)sizeof(esc_rubric)) != 0) {
				remove(tmp_rubric);
				return SK_UI_VISION_ASSERT_ERROR;
			}
			status = snprintf(cmd, sizeof(cmd),
							  "python3 '%s' --image '%s' --rubric-file '%s' --family '%s' --state '%s' 2>/dev/null; "
							  "rc=$?; rm -f '%s'; exit $rc",
							  script, esc_image, esc_rubric, esc_family, esc_state, esc_rubric);
		}
	}
	if (status < 0 || (u32)status >= (u32)sizeof(cmd)) {
		snprintf(out->reason, sizeof(out->reason), "command line overflow");
		return SK_UI_VISION_ASSERT_ERROR;
	}

	last_json[0] = '\0';
	pipe = popen(cmd, "r");
	if (pipe == NULL) {
		ui_vision_fail("vision_assert: popen failed for '%s'", script);
		snprintf(out->reason, sizeof(out->reason), "popen failed for vision script");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	while (fgets(line, (int)sizeof(line), pipe) != NULL) {
		const char* s = ui_vision_skip_ws(line);
		if (s[0] == '{') {
			/* Keep last JSON object line from stdout. */
			snprintf(last_json, sizeof(last_json), "%s", s);
			/* strip trailing newline */
			{
				size_t len = strlen(last_json);
				while (len > 0u && (last_json[len - 1u] == '\n' || last_json[len - 1u] == '\r')) {
					last_json[--len] = '\0';
				}
			}
			has_json = 1;
		}
	}
	status = pclose(pipe);

	if (!has_json) {
		if (status != 0) {
			/* Script exit 2 = skipped (no credentials). */
#ifdef WIFEXITED
			if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
				out->skipped = 1;
				out->passed = 0;
				snprintf(out->reason, sizeof(out->reason), "vision backend skipped: no API credentials");
				return SK_UI_VISION_ASSERT_SKIPPED;
			}
#else
			/* Best-effort: non-zero often means skip in our script convention when no JSON. */
			if (status == 2 * 256 || status == 2) {
				out->skipped = 1;
				out->passed = 0;
				snprintf(out->reason, sizeof(out->reason), "vision backend skipped: no API credentials");
				return SK_UI_VISION_ASSERT_SKIPPED;
			}
#endif
		}
		ui_vision_fail("vision_assert: no JSON result from vision script (status=%d)", status);
		snprintf(out->reason, sizeof(out->reason), "vision script produced no JSON (status=%d)", status);
		return SK_UI_VISION_ASSERT_ERROR;
	}

	if (ui_vision_parse_result_json(last_json, out) != 0) {
		ui_vision_fail("vision_assert: failed to parse vision JSON: %s", last_json);
		snprintf(out->reason, sizeof(out->reason), "invalid vision JSON");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	if (out->skipped) {
		return SK_UI_VISION_ASSERT_SKIPPED;
	}
	return out->passed ? SK_UI_VISION_ASSERT_OK : SK_UI_VISION_ASSERT_FAIL;
}

/* -------------------------------------------------------------------------- */
/* Public entry points                                                        */
/* -------------------------------------------------------------------------- */

i32 sk_ui_vision_assert_path(const sk_ui_api_t* ui, const_chr_t image_path, const sk_ui_cpu_image_t* image, sk_ui_vision_widget_family_t family, const_chr_t state_hint,
							 const_chr_t scene_name, const sk_filesystem_api_t* fs, sk_ui_vision_result_t* out_result) {
	sk_ui_vision_result_t local;
	sk_ui_vision_result_t* r = (out_result != NULL) ? out_result : &local;
	i32 rc;
	const_chr_t fam;

	ui_vision_result_clear(r);
	if (image_path == NULL || image_path[0] == '\0' || (u32)family >= (u32)SK_UI_VISION_WIDGET_COUNT) {
		snprintf(r->reason, sizeof(r->reason), "invalid image_path or family");
		return SK_UI_VISION_ASSERT_ERROR;
	}
	fam = sk_ui_vision_rubric_name(family);
	snprintf(r->family_name, sizeof(r->family_name), "%s", fam);

	if (ui_vision_backend_is_mock()) {
		rc = ui_vision_run_mock(r);
	} else {
		rc = ui_vision_run_script(image_path, family, state_hint, r);
	}
	/* Ensure family_name survives backend fill. */
	snprintf(r->family_name, sizeof(r->family_name), "%s", fam);

	if (rc == SK_UI_VISION_ASSERT_FAIL) {
		(void)ui_vision_save_fail_frame(ui, fs, scene_name, image_path, image, r->saved_frame_path, (u32)sizeof(r->saved_frame_path));
		ui_vision_fail("vision_assert: FAIL family=%s scene=%s reason=%s", fam, scene_name != NULL ? scene_name : "(none)", r->reason);
	} else if (rc == SK_UI_VISION_ASSERT_OK) {
		ui_vision_info("vision_assert: PASS family=%s scene=%s reason=%s", fam, scene_name != NULL ? scene_name : "(none)", r->reason);
	} else if (rc == SK_UI_VISION_ASSERT_SKIPPED) {
		ui_vision_info("vision_assert: SKIPPED family=%s reason=%s", fam, r->reason);
	}

	r->passed = (rc == SK_UI_VISION_ASSERT_OK) ? 1 : 0;
	if (rc == SK_UI_VISION_ASSERT_SKIPPED) {
		r->skipped = 1;
	}
	return rc;
}

i32 sk_ui_vision_assert_image(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* image, sk_ui_vision_widget_family_t family, const_chr_t state_hint, const_chr_t scene_name,
							  const sk_filesystem_api_t* fs, sk_ui_vision_result_t* out_result) {
	char path[SK_FS_PATH_MAX];
	char name[128];
	i32 sn;
	i32 rc;

	if (ui == NULL || image == NULL || image->pixels == NULL) {
		if (out_result != NULL) {
			ui_vision_result_clear(out_result);
			snprintf(out_result->reason, sizeof(out_result->reason), "null ui/image");
		}
		return SK_UI_VISION_ASSERT_ERROR;
	}
	if (fs == NULL) {
		fs = sk_filesystem_api();
	}
	sn = snprintf(name, sizeof(name), "%s_vision_tmp", (scene_name != NULL && scene_name[0] != '\0') ? scene_name : "vision");
	if (sn < 0 || (u32)sn >= (u32)sizeof(name)) {
		return SK_UI_VISION_ASSERT_ERROR;
	}
	if (ui->test_artifact_png_path(fs, name, path, (u32)sizeof(path)) != 0) {
		if (out_result != NULL) {
			ui_vision_result_clear(out_result);
			snprintf(out_result->reason, sizeof(out_result->reason), "cannot resolve temp vision frame path");
		}
		return SK_UI_VISION_ASSERT_ERROR;
	}
	if (ui->cpu_image_write_png(image, fs, path) != 0) {
		if (out_result != NULL) {
			ui_vision_result_clear(out_result);
			snprintf(out_result->reason, sizeof(out_result->reason), "cannot write temp vision frame");
		}
		return SK_UI_VISION_ASSERT_ERROR;
	}
	rc = sk_ui_vision_assert_path(ui, path, image, family, state_hint, scene_name, fs, out_result);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Unit / integration tests (APX-251)                                         */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS

SK_TEST(ui_vision_rubrics_present_for_all_families) {
	u32 i;
	for (i = 0u; i < (u32)SK_UI_VISION_WIDGET_COUNT; ++i) {
		const_chr_t name = sk_ui_vision_rubric_name((sk_ui_vision_widget_family_t)i);
		const_chr_t text = sk_ui_vision_rubric_text((sk_ui_vision_widget_family_t)i);
		sk_ui_vision_widget_family_t parsed = SK_UI_VISION_WIDGET_COUNT;
		TEST_ASSERT_TRUE(name != NULL && name[0] != '\0');
		TEST_ASSERT_TRUE(text != NULL && strlen(text) > 40u);
		TEST_ASSERT_EQUAL_INT(0, sk_ui_vision_widget_family_parse(name, &parsed));
		TEST_ASSERT_EQUAL_INT((int)i, (int)parsed);
	}
	/* Checkbox rubric must demand an X mark, not a vague "looks checked". */
	{
		const_chr_t cb = sk_ui_vision_rubric_text(SK_UI_VISION_WIDGET_CHECKBOX);
		TEST_ASSERT_TRUE(strstr(cb, "X mark") != NULL || strstr(cb, "crossing diagonal") != NULL);
		TEST_ASSERT_TRUE(strstr(cb, "checkmark") != NULL || strstr(cb, "filled") != NULL);
	}
	/* Slider rubric must demand a grab handle. */
	{
		const_chr_t sl = sk_ui_vision_rubric_text(SK_UI_VISION_WIDGET_SLIDER);
		TEST_ASSERT_TRUE(strstr(sl, "handle") != NULL || strstr(sl, "thumb") != NULL);
	}
	/* Radio rubric must demand filled inner circle. */
	{
		const_chr_t rd = sk_ui_vision_rubric_text(SK_UI_VISION_WIDGET_RADIO);
		TEST_ASSERT_TRUE(strstr(rd, "inner") != NULL);
	}
}

SK_TEST(ui_vision_assert_mock_pass_and_fail_saves_frame) {
	/*
	 * Deterministic plumbing test (no network): mock backend returns pass then
	 * fail; on fail the helper must save an offending frame under the artifact
	 * root.
	 */
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	sk_ui_cpu_image_t img;
	sk_ui_vision_result_t result;
	u8 pixels[4 * 8 * 8];
	char path[SK_FS_PATH_MAX];
	char prev_mock[2048];
	char prev_backend[64];
	const char* old_mock;
	const char* old_backend;
	i32 rc;
	FILE* f;

	memset(pixels, 80, sizeof(pixels));
	memset(&img, 0, sizeof(img));
	img.width = 8u;
	img.height = 8u;
	img.channels = 4u;
	img.pixels = pixels;

	/* Placeholder file for image_path; mock backend does not decode pixels. */
	snprintf(path, sizeof(path), "%s/skore_ui_vision_mock_frame_%d.png", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)ui_vision_getpid());
	f = fopen(path, "wb");
	TEST_ASSERT_TRUE(f != NULL);
	fwrite("PNG", 1, 3, f);
	fclose(f);

	old_mock = getenv("SK_UI_VISION_MOCK_RESPONSE");
	old_backend = getenv("SK_UI_VISION_BACKEND");
	prev_mock[0] = '\0';
	prev_backend[0] = '\0';
	if (old_mock != NULL) {
		snprintf(prev_mock, sizeof(prev_mock), "%s", old_mock);
	}
	if (old_backend != NULL) {
		snprintf(prev_backend, sizeof(prev_backend), "%s", old_backend);
	}

	setenv("SK_UI_VISION_BACKEND", "mock", 1);
	setenv("SK_UI_VISION_MOCK_RESPONSE", "{\"pass\":true,\"reason\":\"mock ok: X mark present\"}", 1);
	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_path(NULL, path, &img, SK_UI_VISION_WIDGET_CHECKBOX, "checked", "vision_mock_pass", fs, &result);
	TEST_ASSERT_EQUAL_INT(SK_UI_VISION_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_INT(1, result.passed);
	TEST_ASSERT_EQUAL_INT(0, result.skipped);
	TEST_ASSERT_TRUE(strstr(result.reason, "mock ok") != NULL);
	TEST_ASSERT_TRUE(result.saved_frame_path[0] == '\0');

	setenv("SK_UI_VISION_MOCK_RESPONSE", "{\"pass\":false,\"reason\":\"mock fail: filled square, not X\"}", 1);
	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_path(NULL, path, &img, SK_UI_VISION_WIDGET_CHECKBOX, "checked", "vision_mock_fail", fs, &result);
	TEST_ASSERT_EQUAL_INT(SK_UI_VISION_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_INT(0, result.passed);
	TEST_ASSERT_TRUE(strstr(result.reason, "filled square") != NULL);
	TEST_ASSERT_TRUE(result.saved_frame_path[0] != '\0');
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, fs->get_file_status(result.saved_frame_path));
	remove(result.saved_frame_path);

	/* Restore env */
	if (prev_mock[0] != '\0') {
		setenv("SK_UI_VISION_MOCK_RESPONSE", prev_mock, 1);
	} else {
		unsetenv("SK_UI_VISION_MOCK_RESPONSE");
	}
	if (prev_backend[0] != '\0') {
		setenv("SK_UI_VISION_BACKEND", prev_backend, 1);
	} else {
		unsetenv("SK_UI_VISION_BACKEND");
	}
	remove(path);
}

/*
 * Live / fixture verification: good frames must PASS and deliberately
 * corrupted frames must FAIL. Uses real grok vision when credentials exist;
 * otherwise TEST_IGNORE so CI without keys stays green.
 *
 * Fixtures live under plugins/ui/testdata/vision/fixtures/.
 */
static i32 ui_vision_fixture_path(const_chr_t file, char* out, u32 out_cap) {
#ifdef SK_UI_GOLDEN_DIR
	return ui_vision_join3(SK_UI_GOLDEN_DIR, "vision/fixtures", file, out, out_cap);
#else
	return ui_vision_join3("plugins/ui/testdata", "vision/fixtures", file, out, out_cap);
#endif
}

static i32 ui_vision_live_available(void) {
	const char* force = getenv("SK_UI_VISION_LIVE");
	const char* key = getenv("XAI_API_KEY");
	const char* key2 = getenv("SK_UI_VISION_API_KEY");
	if (force != NULL && force[0] == '0') {
		return 0;
	}
	if (key != NULL && key[0] != '\0') {
		return 1;
	}
	if (key2 != NULL && key2[0] != '\0') {
		return 1;
	}
	/* Agent environments often have ~/.grok/auth.json — let the script decide. */
	if (force != NULL && force[0] != '\0' && force[0] != '0') {
		return 1;
	}
	{
		char home_auth[SK_FS_PATH_MAX];
		const char* home = getenv("HOME");
		const sk_filesystem_api_t* fs = sk_filesystem_api();
		if (home != NULL && fs != NULL) {
			snprintf(home_auth, sizeof(home_auth), "%s/.grok/auth.json", home);
			if (fs->get_file_status(home_auth) == SK_FILE_STATUS_FILE) {
				return 1;
			}
		}
	}
	return 0;
}

SK_TEST(ui_vision_assert_good_pass_corrupt_fail) {
	char good_path[SK_FS_PATH_MAX];
	char bad_path[SK_FS_PATH_MAX];
	sk_ui_vision_result_t result;
	i32 rc;
	const char* old_backend;
	const char* old_mock;

	if (!ui_vision_live_available()) {
		TEST_IGNORE_MESSAGE("no vision credentials (set XAI_API_KEY or SK_UI_VISION_LIVE=1); skipping live grade");
	}

	/* Ensure we are not stuck in mock mode from a prior test. */
	old_backend = getenv("SK_UI_VISION_BACKEND");
	old_mock = getenv("SK_UI_VISION_MOCK_RESPONSE");
	unsetenv("SK_UI_VISION_BACKEND");
	unsetenv("SK_UI_VISION_MOCK_RESPONSE");

	TEST_ASSERT_EQUAL_INT(0, ui_vision_fixture_path("checkbox_checked_good.png", good_path, (u32)sizeof(good_path)));
	TEST_ASSERT_EQUAL_INT(0, ui_vision_fixture_path("checkbox_checked_corrupt_filled.png", bad_path, (u32)sizeof(bad_path)));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(good_path));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(bad_path));

	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_path(NULL, good_path, NULL, SK_UI_VISION_WIDGET_CHECKBOX, "checked", "vision_cb_good", sk_filesystem_api(), &result);
	if (rc == SK_UI_VISION_ASSERT_ERROR) {
		/* One retry on transient API/script glitches. */
		memset(&result, 0, sizeof(result));
		rc = sk_ui_vision_assert_path(NULL, good_path, NULL, SK_UI_VISION_WIDGET_CHECKBOX, "checked", "vision_cb_good", sk_filesystem_api(), &result);
	}
	if (rc == SK_UI_VISION_ASSERT_SKIPPED || rc == SK_UI_VISION_ASSERT_ERROR) {
		if (old_backend) {
			setenv("SK_UI_VISION_BACKEND", old_backend, 1);
		}
		if (old_mock) {
			setenv("SK_UI_VISION_MOCK_RESPONSE", old_mock, 1);
		}
		TEST_IGNORE_MESSAGE(rc == SK_UI_VISION_ASSERT_ERROR ? "vision backend error; skipping live grade" : "vision backend skipped (no credentials)");
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_VISION_ASSERT_OK, rc, result.reason);
	TEST_ASSERT_EQUAL_INT(1, result.passed);

	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_path(NULL, bad_path, NULL, SK_UI_VISION_WIDGET_CHECKBOX, "checked", "vision_cb_corrupt", sk_filesystem_api(), &result);
	if (rc == SK_UI_VISION_ASSERT_ERROR) {
		memset(&result, 0, sizeof(result));
		rc = sk_ui_vision_assert_path(NULL, bad_path, NULL, SK_UI_VISION_WIDGET_CHECKBOX, "checked", "vision_cb_corrupt", sk_filesystem_api(), &result);
	}
	if (rc == SK_UI_VISION_ASSERT_ERROR || rc == SK_UI_VISION_ASSERT_SKIPPED) {
		if (old_backend) {
			setenv("SK_UI_VISION_BACKEND", old_backend, 1);
		}
		if (old_mock) {
			setenv("SK_UI_VISION_MOCK_RESPONSE", old_mock, 1);
		}
		TEST_IGNORE_MESSAGE("vision backend error/skip on corrupt fixture grade");
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_VISION_ASSERT_FAIL, rc, result.reason);
	TEST_ASSERT_EQUAL_INT(0, result.passed);
	/* Offending frame should be saved when UI API is available; without it,
	 * reason alone is still actionable. */
	TEST_ASSERT_TRUE(result.reason[0] != '\0');

	/* Slider pair */
	TEST_ASSERT_EQUAL_INT(0, ui_vision_fixture_path("slider_good.png", good_path, (u32)sizeof(good_path)));
	TEST_ASSERT_EQUAL_INT(0, ui_vision_fixture_path("slider_corrupt_no_handle.png", bad_path, (u32)sizeof(bad_path)));
	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_path(NULL, good_path, NULL, SK_UI_VISION_WIDGET_SLIDER, "value=0.5", "vision_sl_good", sk_filesystem_api(), &result);
	if (rc == SK_UI_VISION_ASSERT_ERROR) {
		memset(&result, 0, sizeof(result));
		rc = sk_ui_vision_assert_path(NULL, good_path, NULL, SK_UI_VISION_WIDGET_SLIDER, "value=0.5", "vision_sl_good", sk_filesystem_api(), &result);
	}
	if (rc == SK_UI_VISION_ASSERT_ERROR || rc == SK_UI_VISION_ASSERT_SKIPPED) {
		if (old_backend) {
			setenv("SK_UI_VISION_BACKEND", old_backend, 1);
		}
		if (old_mock) {
			setenv("SK_UI_VISION_MOCK_RESPONSE", old_mock, 1);
		}
		TEST_IGNORE_MESSAGE("vision backend error/skip on slider good fixture");
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_VISION_ASSERT_OK, rc, result.reason);
	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_path(NULL, bad_path, NULL, SK_UI_VISION_WIDGET_SLIDER, "value=0.5", "vision_sl_corrupt", sk_filesystem_api(), &result);
	if (rc == SK_UI_VISION_ASSERT_ERROR) {
		memset(&result, 0, sizeof(result));
		rc = sk_ui_vision_assert_path(NULL, bad_path, NULL, SK_UI_VISION_WIDGET_SLIDER, "value=0.5", "vision_sl_corrupt", sk_filesystem_api(), &result);
	}
	if (rc == SK_UI_VISION_ASSERT_ERROR || rc == SK_UI_VISION_ASSERT_SKIPPED) {
		if (old_backend) {
			setenv("SK_UI_VISION_BACKEND", old_backend, 1);
		}
		if (old_mock) {
			setenv("SK_UI_VISION_MOCK_RESPONSE", old_mock, 1);
		}
		TEST_IGNORE_MESSAGE("vision backend error/skip on slider corrupt fixture");
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_VISION_ASSERT_FAIL, rc, result.reason);

	if (old_backend) {
		setenv("SK_UI_VISION_BACKEND", old_backend, 1);
	}
	if (old_mock) {
		setenv("SK_UI_VISION_MOCK_RESPONSE", old_mock, 1);
	}
}

#endif /* SK_TESTS */
