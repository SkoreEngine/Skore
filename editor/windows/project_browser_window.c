/**
 * @file project_browser_window.c
 * @brief Reference window (APX-365): Project Browser through a struct table.
 *
 * Public methods are static and only reachable via
 * `sk_editor_project_browser_ops(ctx, api)->...`. Selection / activate / drop
 * publish notify.h observers. Asset tree is mocked.
 */

#include "project_browser_window.h"

#include "allocator.h"
#include "notify.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID SK_TYPE_ID("sk.editor.project_browser.state", 0xfb0f9ae989d9c601ULL, 0x9eeb7516b535a5b9ULL)

#define PB_MENU_CAP 16u
#define PB_HIDDEN_EXT_CAP 16u
#define PB_ITEM_CAP 8u
#define PB_SELECTED_CAP 16u
#define PB_LISTING_CAP 256u
#define PB_DROP_PATH_CAP 512u
#define PB_NEW_ASSET_RID_BASE 100ull

typedef struct pb_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	sk_editor_menu_item_desc_t menus[PB_MENU_CAP];
	u32 menu_count;
	u8 _pad0[4];
	const_chr_t hidden_ext[PB_HIDDEN_EXT_CAP];
	u32 hidden_count;
} pb_class_state_t;

typedef struct pb_item_t {
	sk_rid_t rid;
	const_chr_t name;
	i32 is_directory;
	u8 _pad0[4];
} pb_item_t;

typedef struct pb_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	sk_rid_t open_directory;
	sk_rid_t last_selected;
	sk_rid_t rename_item;
	sk_rid_t selected[PB_SELECTED_CAP];
	u32 selected_count;
	u32 workspace_id;
	u32 created_count;
	u8 _pad0[4];
	pb_item_t items[PB_ITEM_CAP];
	u32 item_count;
	u8 _pad1[4];
	sk_editor_on_drop_file_t drop_observer;
	char last_drop_path[PB_DROP_PATH_CAP];
	char listing[PB_LISTING_CAP];
} pb_state_t;

static void pb_init(sk_editor_window_t* window);
static void pb_draw(sk_editor_window_t* window, i32* open);
static void pb_destroy(sk_editor_window_t* window);

static sk_editor_window_t pb_window = {
	.title = "Project Browser",
	.dock_id = "sk.editor_window.project_browser",
	.dock_position = SK_EDITOR_DOCK_BOTTOM_LEFT,
	.order = 0,
	.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	.init = pb_init,
	.draw = pb_draw,
	.render = NULL,
	.destroy = pb_destroy,
};

static pb_class_state_t* pb_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (pb_class_state_t*)app_api->get_api(app_context, SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID);
}

static sk_editor_window_t* pb_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
}

static void pb_add_menu_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item) {
	pb_class_state_t* cls = pb_class(app_context, app_api);
	if (cls->menu_count >= PB_MENU_CAP) {
		return;
	}
	cls->menus[cls->menu_count] = *item;
	cls->menu_count++;
}

static i32 pb_can_create_asset(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event) {
	(void)app_context;
	(void)app_api;
	(void)event;
	return 1;
}

static void pb_clear_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)scope;
	state->selected_count = 0u;
	state->last_selected = SK_RID_ZERO;
	sk_editor_notify_selection_changed(app_context, app_api);
}

static void pb_select_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	u32 i;
	(void)scope;
	for (i = 0u; i < state->selected_count; ++i) {
		if (SK_RID_EQ(state->selected[i], rid)) {
			state->last_selected = rid;
			sk_editor_notify_selection_changed(app_context, app_api);
			sk_editor_notify_asset_selection(app_context, app_api, state->workspace_id, rid);
			return;
		}
	}
	if (state->selected_count < PB_SELECTED_CAP) {
		state->selected[state->selected_count] = rid;
		state->selected_count++;
	}
	state->last_selected = rid;
	sk_editor_notify_selection_changed(app_context, app_api);
	sk_editor_notify_asset_selection(app_context, app_api, state->workspace_id, rid);
}

static void pb_set_rename_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	pb_clear_selection(app_context, app_api, window, scope);
	state->rename_item = rid;
	pb_select_item(app_context, app_api, window, rid, scope);
}

static sk_rid_t pb_get_open_directory(const sk_editor_window_t* window) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	return state->open_directory;
}

static sk_rid_t pb_get_last_selected_item(const sk_editor_window_t* window) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	return state->last_selected;
}

static void pb_activate_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	pb_select_item(app_context, app_api, window, rid, NULL);
	sk_editor_notify_asset_opened(app_context, app_api, state->workspace_id, rid);
	sk_editor_notify_asset_activated(app_context, app_api, state->workspace_id, rid);
}

static void pb_asset_new(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event) {
	sk_editor_window_t* window = event->window;
	pb_state_t* state;
	sk_rid_t rid;
	if (window == NULL) {
		window = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	}
	if (window == NULL) {
		return;
	}
	state = (pb_state_t*)window->user_data;
	rid.id = PB_NEW_ASSET_RID_BASE + (u64)state->created_count;
	state->created_count++;
	if (state->item_count < PB_ITEM_CAP) {
		state->items[state->item_count].rid = rid;
		state->items[state->item_count].name = "New Asset";
		state->items[state->item_count].is_directory = 0;
		state->item_count++;
	}
	pb_set_rename_item(app_context, app_api, window, rid, NULL);
}

static void pb_hide_extension(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension) {
	pb_class_state_t* cls = pb_class(app_context, app_api);
	u32 i;
	if (extension[0] == '\0' || cls->hidden_count >= PB_HIDDEN_EXT_CAP) {
		return;
	}
	for (i = 0u; i < cls->hidden_count; ++i) {
		if (strcmp(cls->hidden_ext[i], extension) == 0) {
			return;
		}
	}
	cls->hidden_ext[cls->hidden_count] = extension;
	cls->hidden_count++;
}

static i32 pb_extension_is_hidden(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension) {
	pb_class_state_t* cls = pb_class(app_context, app_api);
	u32 i;
	for (i = 0u; i < cls->hidden_count; ++i) {
		if (strcmp(cls->hidden_ext[i], extension) == 0) {
			return 1;
		}
	}
	return 0;
}

static const sk_editor_project_browser_ops_t pb_ops = {
	pb_open,
	pb_add_menu_item,
	pb_can_create_asset,
	pb_asset_new,
	pb_hide_extension,
	pb_extension_is_hidden,
	pb_clear_selection,
	pb_select_item,
	pb_set_rename_item,
	pb_get_open_directory,
	pb_get_last_selected_item,
	pb_activate_item,
};

static void pb_on_drop_file(void* user, const_chr_t path) {
	pb_state_t* state = (pb_state_t*)user;
	size_t n = strlen(path);
	if (n >= PB_DROP_PATH_CAP) {
		n = PB_DROP_PATH_CAP - 1u;
	}
	memcpy(state->last_drop_path, path, n);
	state->last_drop_path[n] = '\0';
}

static void pb_seed_items(pb_state_t* state) {
	state->items[0].rid.id = 1ull;
	state->items[0].name = "Assets";
	state->items[0].is_directory = 1;
	state->items[1].rid.id = 2ull;
	state->items[1].name = "Scenes";
	state->items[1].is_directory = 1;
	state->items[2].rid.id = 3ull;
	state->items[2].name = "Textures";
	state->items[2].is_directory = 1;
	state->items[3].rid.id = 4ull;
	state->items[3].name = "Main.scene";
	state->items[3].is_directory = 0;
	state->items[4].rid.id = 5ull;
	state->items[4].name = "Hero.png";
	state->items[4].is_directory = 0;
	state->item_count = 5u;
	state->open_directory.id = 1ull;
}

static void pb_rebuild_listing(pb_state_t* state) {
	u32 i;
	size_t used = 0u;
	state->listing[0] = '\0';
	for (i = 0u; i < state->item_count; ++i) {
		const_chr_t mark = SK_RID_EQ(state->items[i].rid, state->last_selected) ? "*" : " ";
		const_chr_t slash = state->items[i].is_directory != 0 ? "/" : "";
		int n = snprintf(state->listing + used, PB_LISTING_CAP - used, "%s%s%s\n", mark, state->items[i].name, slash);
		if (n < 0 || (size_t)n >= PB_LISTING_CAP - used) {
			break;
		}
		used += (size_t)n;
	}
}

static void pb_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	pb_class_state_t* cls = (pb_class_state_t*)window->user_data;
	pb_state_t* state = (pb_state_t*)alloc->alloc(alloc->instance, sizeof(pb_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->workspace_id = SK_EDITOR_WORKSPACE_SCENE;
	pb_seed_items(state);
	state->drop_observer.order = 0;
	state->drop_observer.user = state;
	state->drop_observer.on_drop_file = pb_on_drop_file;
	cls->app_api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_DROP_FILE, &state->drop_observer);
	window->user_data = state;
}

static void pb_draw(sk_editor_window_t* window, i32* open) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	pb_rebuild_listing(state);
	/* Draw contract: the window may clear *open to request close. */
	*open = 1;
}

static void pb_destroy(sk_editor_window_t* window) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	if (state == NULL) {
		return;
	}
	state->app_api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_DROP_FILE, &state->drop_observer);
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

void sk_editor_project_browser_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	pb_class_state_t* cls = pb_class(app_context, app_api);
	if (cls != NULL) {
		pb_window.user_data = cls;
		return;
	}
	cls = (pb_class_state_t*)alloc->alloc(alloc->instance, sizeof(pb_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID, cls);

	pb_window.type_id = SK_EDITOR_WINDOW_PROJECT_BROWSER;
	pb_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &pb_window);
	app_api->add_impl(app_context, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID, &pb_ops);
}

void sk_editor_project_browser_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	pb_class_state_t* cls = pb_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &pb_window);
	app_api->remove_impl(app_context, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID, &pb_ops);
	app_api->set_api(app_context, SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (pb_window.user_data == cls) {
		pb_window.user_data = NULL;
	}
}

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

static const_chr_t pb_test_listing(const sk_editor_window_t* window) {
	return ((const pb_state_t*)window->user_data)->listing;
}

static const_chr_t pb_test_last_drop(const sk_editor_window_t* window) {
	return ((const pb_state_t*)window->user_data)->last_drop_path;
}

static sk_rid_t pb_test_rename_item(const sk_editor_window_t* window) {
	return ((const pb_state_t*)window->user_data)->rename_item;
}

typedef struct pb_obs_t {
	u32 selection_count;
	u32 asset_count;
	u32 opened_count;
	u32 activated_count;
	sk_rid_t last_asset;
	u32 last_workspace;
} pb_obs_t;

static void pb_obs_selection(void* user) {
	((pb_obs_t*)user)->selection_count++;
}

static void pb_obs_asset(void* user, u32 workspace_id, sk_rid_t rid) {
	pb_obs_t* o = (pb_obs_t*)user;
	o->asset_count++;
	o->last_workspace = workspace_id;
	o->last_asset = rid;
}

static void pb_obs_opened(void* user, u32 workspace_id, sk_rid_t rid) {
	pb_obs_t* o = (pb_obs_t*)user;
	o->opened_count++;
	o->last_workspace = workspace_id;
	o->last_asset = rid;
}

static void pb_obs_activated(void* user, u32 workspace_id, sk_rid_t rid) {
	pb_obs_t* o = (pb_obs_t*)user;
	o->activated_count++;
	o->last_workspace = workspace_id;
	o->last_asset = rid;
}

SK_TEST(editor_project_browser_ops_via_add_impl) {
	sk_app_boot_t boot = sk_app_create();
	const sk_editor_api_t* editor;
	const sk_editor_project_browser_ops_t* ops;
	sk_editor_window_t* window;
	sk_rid_t scene;
	i32 open_flag = 1;
	pb_obs_t obs;
	sk_editor_on_selection_changed_t on_sel;
	sk_editor_on_asset_selection_t on_asset;
	sk_editor_on_asset_opened_t on_open;
	sk_editor_on_asset_activated_t on_act;
	sk_editor_menu_item_desc_t create;
	sk_editor_menu_item_event_t event;

	TEST_ASSERT_NOT_NULL(boot.context);
	sk_editor_bind_tables(boot.context, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(boot.context, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_project_browser_ops(boot.context, boot.api));
	sk_editor_project_browser_register(boot.context, boot.api);
	sk_editor_project_browser_register(boot.context, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(boot.context, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID));

	ops = sk_editor_project_browser_ops(boot.context, boot.api);
	TEST_ASSERT_NOT_NULL(ops);
	TEST_ASSERT_EQUAL_PTR(&pb_ops, ops);
	TEST_ASSERT_NOT_NULL(ops->clear_selection);
	TEST_ASSERT_NOT_NULL(ops->select_item);

	window = ops->open(boot.context, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Project Browser", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(boot.context, boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER));
	TEST_ASSERT_EQUAL_UINT64(1ull, ops->get_open_directory(window).id);
	TEST_ASSERT_EQUAL_UINT64(0ull, ops->get_last_selected_item(window).id);

	memset(&obs, 0, sizeof(obs));
	memset(&on_sel, 0, sizeof(on_sel));
	memset(&on_asset, 0, sizeof(on_asset));
	memset(&on_open, 0, sizeof(on_open));
	memset(&on_act, 0, sizeof(on_act));
	on_sel.user = &obs;
	on_sel.on_selection_changed = pb_obs_selection;
	on_asset.user = &obs;
	on_asset.on_asset_selection = pb_obs_asset;
	on_open.user = &obs;
	on_open.on_asset_opened = pb_obs_opened;
	on_act.user = &obs;
	on_act.on_asset_activated = pb_obs_activated;
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &on_sel);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ASSET_SELECTION, &on_asset);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ASSET_OPENED, &on_open);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ASSET_ACTIVATED, &on_act);

	scene.id = 4ull;
	ops->select_item(boot.context, boot.api, window, scene, NULL);
	TEST_ASSERT_EQUAL_UINT64(4ull, ops->get_last_selected_item(window).id);
	TEST_ASSERT_EQUAL_UINT(1u, obs.selection_count);
	TEST_ASSERT_EQUAL_UINT(1u, obs.asset_count);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_WORKSPACE_SCENE, obs.last_workspace);

	ops->clear_selection(boot.context, boot.api, window, NULL);
	TEST_ASSERT_EQUAL_UINT64(0ull, ops->get_last_selected_item(window).id);
	TEST_ASSERT_EQUAL_UINT(2u, obs.selection_count);

	ops->set_rename_item(boot.context, boot.api, window, scene, NULL);
	TEST_ASSERT_EQUAL_UINT64(4ull, ops->get_last_selected_item(window).id);
	TEST_ASSERT_EQUAL_UINT64(4ull, pb_test_rename_item(window).id);

	ops->activate_item(boot.context, boot.api, window, scene);
	TEST_ASSERT_EQUAL_UINT(1u, obs.opened_count);
	TEST_ASSERT_EQUAL_UINT(1u, obs.activated_count);

	memset(&create, 0, sizeof(create));
	create.item_name = "Create/Scene";
	create.order = 0;
	ops->add_menu_item(boot.context, boot.api, &create);
	event.window = window;
	event.user = NULL;
	TEST_ASSERT_EQUAL_INT(1, ops->can_create_asset(boot.context, boot.api, &event));
	ops->asset_new(boot.context, boot.api, &event);
	TEST_ASSERT_EQUAL_UINT64(PB_NEW_ASSET_RID_BASE, ops->get_last_selected_item(window).id);
	TEST_ASSERT_EQUAL_UINT64(PB_NEW_ASSET_RID_BASE, pb_test_rename_item(window).id);

	ops->hide_extension(boot.context, boot.api, ".meta");
	ops->hide_extension(boot.context, boot.api, ".meta");
	TEST_ASSERT_EQUAL_INT(1, ops->extension_is_hidden(boot.context, boot.api, ".meta"));
	TEST_ASSERT_EQUAL_INT(0, ops->extension_is_hidden(boot.context, boot.api, ".png"));

	sk_editor_notify_drop_file(boot.context, boot.api, "/tmp/Hero.png");
	TEST_ASSERT_EQUAL_STRING("/tmp/Hero.png", pb_test_last_drop(window));

	window->draw(window, &open_flag);
	TEST_ASSERT_EQUAL_INT(1, open_flag);
	TEST_ASSERT_NOT_NULL(strstr(pb_test_listing(window), "Assets/"));
	TEST_ASSERT_NOT_NULL(strstr(pb_test_listing(window), "Main.scene"));

	editor->window_close(boot.context, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(0u, boot.api->impl_count(boot.context, SK_EDITOR_NOTIFY_DROP_FILE));
	TEST_ASSERT_NULL(editor->window_by_type(boot.context, boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER));

	sk_editor_project_browser_shutdown(boot.context, boot.api);
	TEST_ASSERT_NULL(sk_editor_project_browser_ops(boot.context, boot.api));
	sk_app_shutdown(boot.context);
}

#endif /* SK_TESTS */
