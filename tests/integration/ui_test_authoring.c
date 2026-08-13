/*
 * ui_test_authoring.c — bridge: authoring API under sk-integration-tests
 * alongside the vision suite (APX-261).
 *
 * Loads sk-ui via the app registry (same path as vision / capture tests), then
 * runs a sample written purely against the ergonomic authoring API
 * (sk_ui_test_run + clickItem / itemExists / valueEquals / frame capture).
 *
 * Implementation of the authoring helpers lives in plugins/ui/ui_test.c and is
 * also compiled into this binary so free functions resolve without exporting
 * them from the plugin shared library.
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"
#include "ui.h"
#include "ui_test.h"

#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS

typedef struct uta_env_t {
	sk_app_context_t* app;
	const sk_ui_api_t* ui;
} uta_env_t;

static i32 uta_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];
	i32 n;

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static i32 uta_boot(uta_env_t* env) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	memset(env, 0, sizeof(*env));
	sk_app_boot_t boot = sk_app_init(0, NULL);
	env->app = boot.context;
	if (env->app == NULL) {
		return -1;
	}
	if (uta_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		boot.api->load_plugin(env->app, path);
	}
	env->ui = (const sk_ui_api_t*)boot.api->get_api(env->app, SK_UI_API_TYPE_ID);
	if (env->ui == NULL) {
		sk_app_shutdown(env->app);
		env->app = NULL;
		return -1;
	}
	return 0;
}

static void uta_shutdown(uta_env_t* env) {
	if (env != NULL && env->app != NULL) {
		sk_app_shutdown(env->app);
		env->app = NULL;
		env->ui = NULL;
	}
}

static void uta_set_size(sk_ui_test_t* t, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, t->ui->node_merge_inline_style(t->ctx, node, &p));
}

static i32 g_uta_clicks;

static void uta_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	(void)user;
	g_uta_clicks += 1;
}

static void uta_sample_body(sk_ui_test_t* t) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_t ti;
	sk_ui_node_callbacks_t cbs;

	btn = ui->widget_button(ctx, root, "Bridge", "btn-bridge");
	uta_set_size(t, btn, 96.0f, 28.0f);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = uta_on_click;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	ti = ui->widget_text_input(ctx, root, "", "ti-bridge");
	uta_set_size(t, ti, 140.0f, 26.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "btn-bridge");

	g_uta_clicks = 0;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "btn-bridge"));
	TEST_ASSERT_EQUAL_INT(1, g_uta_clicks);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_type_into(t, "ti-bridge", "ok"));
	sk_ui_value_equals(t, "ti-bridge", "ok");
}

/**
 * Sample authoring-API test under the integration runner (vision suite peer).
 */
SK_TEST(ui_author_integration_bridge_sample) {
	uta_env_t env;
	if (uta_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip authoring bridge)");
		return;
	}
	sk_ui_test_run(env.ui, "integration_bridge_sample", NULL, NULL, NULL, uta_sample_body);
	uta_shutdown(&env);
}

/**
 * Broken assertion under the integration runner: check_* returns non-zero and
 * last_error embeds fail_frame= path to a written PNG.
 */
SK_TEST(ui_author_integration_broken_assert_frame_path) {
	uta_env_t env;
	sk_ui_test_t t;
	sk_ui_node_t root;
	sk_ui_node_t btn;
	i32 rc;
	const_chr_t err;
	const_chr_t path;

	if (uta_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip authoring bridge)");
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, sk_ui_test_begin(&t, env.ui, "integration_broken", NULL));
	root = env.ui->context_root(t.ctx);
	btn = env.ui->widget_button(t.ctx, root, "Only", "btn-only");
	uta_set_size(&t, btn, 64.0f, 24.0f);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(&t));

	rc = sk_ui_check_item_exists(&t, "definitely-missing");
	TEST_ASSERT_TRUE(rc != 0);
	err = sk_ui_test_last_error(&t);
	path = sk_ui_test_fail_frame_path(&t);
	TEST_ASSERT_TRUE(strstr(err, "fail_frame=") != NULL);
	TEST_ASSERT_TRUE(path[0] != '\0');
	TEST_ASSERT_TRUE(strstr(path, ".png") != NULL);
	TEST_ASSERT_TRUE(strstr(err, path) != NULL);

	sk_ui_test_end(&t);
	uta_shutdown(&env);
}

#endif /* SK_TESTS */
