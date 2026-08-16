/**
 * @file editor_layout.c
 * @brief Workspace layout store: dock JSON + per-window save/load (APX-368).
 */

#include "editor_layout.h"

#include "allocator.h"
#include "array.h"
#include "filesystem.h"
#include "logger.h"
#include "path.h"
#include "serialization.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LAYOUT_WINDOW_CAP 32u
#define LAYOUT_DOCK_JSON_CAP 65536u
#define LAYOUT_STATE_CAP 4096u
#define LAYOUT_ID_CAP 64u

typedef struct layout_window_t {
	char dock_id[LAYOUT_ID_CAP];
	char* state;
	u32 state_len;
	u8 _pad0[4];
} layout_window_t;

typedef struct layout_workspace_t {
	u32 type_id;
	u8 _pad0[4];
	char* dock_json;
	u32 dock_json_len;
	u8 _pad1[4];
	SK_ARRAY(layout_window_t) windows;
} layout_workspace_t;

typedef SK_ARRAY(layout_workspace_t) layout_workspace_array_t;

typedef struct sk_editor_layout_store_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	char path[SK_FS_PATH_MAX];
	i32 path_set;
	i32 dirty;
	u32 active_type;
	u8 _pad0[4];
	layout_workspace_array_t workspaces;
} sk_editor_layout_store_t;

static const sk_editor_workspace_preset_t layout_presets[] = {
	{SK_EDITOR_WORKSPACE_SCENE, "Scene"},
	{SK_EDITOR_WORKSPACE_GRAPH, "Graph"},
	{SK_EDITOR_WORKSPACE_ANIMATOR, "Animator"},
	{SK_EDITOR_WORKSPACE_MATERIAL, "Material"},
};

u32 sk_editor_workspace_preset_count(void) {
	return (u32)(sizeof(layout_presets) / sizeof(layout_presets[0]));
}

const sk_editor_workspace_preset_t* sk_editor_workspace_preset_at(u32 index) {
	if (index >= sk_editor_workspace_preset_count()) {
		return NULL;
	}
	return &layout_presets[index];
}

const sk_editor_workspace_preset_t* sk_editor_workspace_preset_by_type(u32 workspace_type_id) {
	u32 i;
	for (i = 0u; i < sk_editor_workspace_preset_count(); ++i) {
		if (layout_presets[i].type_id == workspace_type_id) {
			return &layout_presets[i];
		}
	}
	return NULL;
}

static void layout_warn(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t fmt, ...) {
	const sk_logger_api_t* log_api = app_api->logger_api(app_context);
	sk_logger_context_t* log_ctx = app_api->logger_context(app_context);
	sk_logger_t* log;
	va_list args;
	if (log_api == NULL || log_ctx == NULL) {
		return;
	}
	log = log_api->create_logger(log_ctx, "editor.layout");
	if (log == NULL) {
		return;
	}
	va_start(args, fmt);
	sk_log_messagev(log_api, SK_LOGGER_TYPE_WARN, log, fmt, args);
	va_end(args);
	log_api->destroy_logger(log_ctx, log);
}

static char* layout_dup(const sk_allocator_t* alloc, const_chr_t src, u32 len) {
	char* dst;
	if (src == NULL || len == 0u) {
		return NULL;
	}
	dst = (char*)alloc->alloc(alloc->instance, (size_t)len + 1u);
	if (dst == NULL) {
		return NULL;
	}
	memcpy(dst, src, (size_t)len);
	dst[len] = '\0';
	return dst;
}

static void layout_window_free(const sk_allocator_t* alloc, layout_window_t* window) {
	if (window->state != NULL) {
		alloc->free(alloc->instance, window->state);
		window->state = NULL;
	}
	window->state_len = 0u;
	window->dock_id[0] = '\0';
}

static void layout_workspace_clear(const sk_allocator_t* alloc, layout_workspace_t* wl) {
	u32 i;
	if (wl->dock_json != NULL) {
		alloc->free(alloc->instance, wl->dock_json);
		wl->dock_json = NULL;
	}
	wl->dock_json_len = 0u;
	for (i = 0u; i < wl->windows.count; ++i) {
		layout_window_free(alloc, &wl->windows.items[i]);
	}
	sk_array_clear(&wl->windows);
}

static void layout_workspace_free(const sk_allocator_t* alloc, layout_workspace_t* wl) {
	layout_workspace_clear(alloc, wl);
	sk_array_free(&wl->windows);
}

static sk_editor_layout_store_t* layout_store_get(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (sk_editor_layout_store_t*)app_api->get_api(app_context, SK_EDITOR_LAYOUT_STORE_TYPE_ID);
}

static i32 layout_default_path(sk_app_context_t* app_context, const sk_app_api_t* app_api, char* out, u32 cap) {
	const sk_filesystem_api_t* fs = app_api->filesystem_api(app_context);
	char base[SK_FS_PATH_MAX];
	char mid[SK_FS_PATH_MAX];
	if (fs == NULL) {
		return -1;
	}
	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("Skore"), mid, (u32)sizeof(mid)) < 0) {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(mid), sk_str_view_cstr("EditorLayout.json"), out, cap) < 0) {
		return -1;
	}
	return 0;
}

static sk_editor_layout_store_t* layout_store_ensure(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_layout_store_t* store = layout_store_get(app_context, app_api);
	if (store != NULL) {
		return store;
	}
	store = (sk_editor_layout_store_t*)alloc->alloc(alloc->instance, sizeof(*store));
	if (store == NULL) {
		return NULL;
	}
	memset(store, 0, sizeof(*store));
	store->app_context = app_context;
	store->app_api = app_api;
	sk_array_init(&store->workspaces, alloc);
	(void)layout_default_path(app_context, app_api, store->path, (u32)sizeof(store->path));
	app_api->set_api(app_context, SK_EDITOR_LAYOUT_STORE_TYPE_ID, store);
	return store;
}

static layout_workspace_t* layout_find(sk_editor_layout_store_t* store, u32 type_id) {
	u32 i;
	for (i = 0u; i < store->workspaces.count; ++i) {
		if (store->workspaces.items[i].type_id == type_id) {
			return &store->workspaces.items[i];
		}
	}
	return NULL;
}

static layout_workspace_t* layout_get_or_create(sk_editor_layout_store_t* store, u32 type_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	layout_workspace_t* found = layout_find(store, type_id);
	layout_workspace_t fresh;
	if (found != NULL) {
		return found;
	}
	memset(&fresh, 0, sizeof(fresh));
	fresh.type_id = type_id;
	sk_array_init(&fresh.windows, alloc);
	if (sk_array_push(&store->workspaces, fresh) != 0) {
		sk_array_free(&fresh.windows);
		return NULL;
	}
	return &store->workspaces.items[store->workspaces.count - 1u];
}

static void layout_drop(sk_editor_layout_store_t* store, u32 type_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	u32 i;
	for (i = 0u; i < store->workspaces.count; ++i) {
		if (store->workspaces.items[i].type_id == type_id) {
			layout_workspace_free(alloc, &store->workspaces.items[i]);
			sk_array_swap_remove(&store->workspaces, i);
			store->dirty = 1;
			return;
		}
	}
}

u32 sk_editor_workspace_preset_window_ids(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id, const_chr_t* out, u32 out_cap) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const_ptr_t* impls;
	u32 impl_count = app_api->impl_count(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID);
	u32 count = 0u;
	u32 i;
	if (impl_count == 0u) {
		return 0u;
	}
	impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)impl_count * sizeof(const_ptr_t));
	if (impls == NULL) {
		return 0u;
	}
	(void)app_api->get_all_impls(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, impl_count);
	for (i = 0u; i < impl_count; ++i) {
		const sk_editor_window_t* impl = (const sk_editor_window_t*)impls[i];
		if (!sk_editor_workspace_mask_contains(impl->workspace_mask, workspace_type_id) || impl->dock_id == NULL) {
			continue;
		}
		if (out != NULL && count < out_cap) {
			out[count] = impl->dock_id;
		}
		count++;
	}
	alloc->free(alloc->instance, impls);
	return count;
}

static i32 layout_copy_id(char* dst, u32 cap, const_chr_t src) {
	u32 n = 0u;
	if (src == NULL) {
		return -1;
	}
	while (src[n] != '\0') {
		n++;
	}
	if (n + 1u > cap) {
		return -1;
	}
	memcpy(dst, src, (size_t)n + 1u);
	return 0;
}

i32 sk_editor_workspace_capture(sk_editor_workspace_t* workspace) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_app_context_t* app_context = sk_editor_workspace_app_context(workspace);
	const sk_app_api_t* app_api = sk_editor_workspace_app_api(workspace);
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	layout_workspace_t* wl;
	sk_editor_window_t* open[LAYOUT_WINDOW_CAP];
	char dock_buf[LAYOUT_DOCK_JSON_CAP];
	u32 dock_len = 0u;
	u32 n;
	u32 i;
	if (store == NULL) {
		return -1;
	}
	wl = layout_get_or_create(store, sk_editor_workspace_type_id(workspace));
	if (wl == NULL) {
		return -1;
	}
	layout_workspace_clear(alloc, wl);

	if (sk_editor_workspace_save_dock_json(workspace, dock_buf, (u32)sizeof(dock_buf), &dock_len) == 0 && dock_len > 0u) {
		wl->dock_json = layout_dup(alloc, dock_buf, dock_len);
		wl->dock_json_len = wl->dock_json != NULL ? dock_len : 0u;
	}

	n = sk_editor_window_iterate(app_context, app_api, open, LAYOUT_WINDOW_CAP);
	if (n > LAYOUT_WINDOW_CAP) {
		n = LAYOUT_WINDOW_CAP;
	}
	for (i = 0u; i < n; ++i) {
		layout_window_t rec;
		char state_buf[LAYOUT_STATE_CAP];
		u32 state_len = 0u;
		sk_editor_window_t* window = open[i];
		memset(&rec, 0, sizeof(rec));
		if (layout_copy_id(rec.dock_id, LAYOUT_ID_CAP, window->dock_id) != 0) {
			continue;
		}
		if (window->save != NULL && window->save(window, state_buf, (u32)sizeof(state_buf), &state_len) == 0 && state_len > 0u) {
			rec.state = layout_dup(alloc, state_buf, state_len);
			rec.state_len = rec.state != NULL ? state_len : 0u;
		}
		if (sk_array_push(&wl->windows, rec) != 0) {
			layout_window_free(alloc, &rec);
			return -1;
		}
	}
	store->active_type = sk_editor_workspace_type_id(workspace);
	store->dirty = 1;
	return 0;
}

i32 sk_editor_workspace_restore(sk_editor_workspace_t* workspace) {
	sk_app_context_t* app_context = sk_editor_workspace_app_context(workspace);
	const sk_app_api_t* app_api = sk_editor_workspace_app_api(workspace);
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	layout_workspace_t* wl = store != NULL ? layout_find(store, sk_editor_workspace_type_id(workspace)) : NULL;
	const_chr_t ids[LAYOUT_WINDOW_CAP];
	u32 count = 0u;
	u32 i;

	if (wl == NULL || wl->windows.count == 0u) {
		sk_editor_workspace_reset_to_preset(workspace);
		return 0;
	}

	count = wl->windows.count < LAYOUT_WINDOW_CAP ? wl->windows.count : LAYOUT_WINDOW_CAP;
	for (i = 0u; i < count; ++i) {
		ids[i] = wl->windows.items[i].dock_id;
		if (sk_editor_window_impl_by_dock_id(app_context, app_api, ids[i]) == NULL) {
			layout_warn(app_context, app_api, "skipping unknown window id '%s' in saved layout", ids[i]);
		}
	}
	(void)sk_editor_workspace_apply_layout(workspace, ids, count, wl->dock_json, wl->dock_json_len);

	for (i = 0u; i < wl->windows.count; ++i) {
		layout_window_t* rec = &wl->windows.items[i];
		sk_editor_window_t* window = sk_editor_window_by_dock_id(app_context, app_api, rec->dock_id);
		if (window == NULL || window->load == NULL || rec->state == NULL || rec->state_len == 0u) {
			continue;
		}
		(void)window->load(window, rec->state, rec->state_len);
	}
	return 0;
}

void sk_editor_workspace_reset_to_preset(sk_editor_workspace_t* workspace) {
	sk_editor_layout_store_t* store = layout_store_ensure(sk_editor_workspace_app_context(workspace), sk_editor_workspace_app_api(workspace));
	if (store != NULL) {
		layout_drop(store, sk_editor_workspace_type_id(workspace));
	}
	sk_editor_dockspace_reset(workspace);
}

i32 sk_editor_layout_has_saved(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id) {
	sk_editor_layout_store_t* store = layout_store_get(app_context, app_api);
	layout_workspace_t* wl;
	if (store == NULL) {
		return 0;
	}
	wl = layout_find(store, workspace_type_id);
	return (wl != NULL && wl->windows.count > 0u) ? 1 : 0;
}

void sk_editor_layout_set_path(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path) {
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	if (store == NULL) {
		return;
	}
	if (path == NULL || path[0] == '\0') {
		store->path[0] = '\0';
		store->path_set = 0;
		(void)layout_default_path(app_context, app_api, store->path, (u32)sizeof(store->path));
		return;
	}
	(void)layout_copy_id(store->path, (u32)sizeof(store->path), path);
	store->path_set = 1;
}

const_chr_t sk_editor_layout_path(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	return store != NULL ? store->path : "";
}

static i32 layout_ensure_parent_dir(const sk_filesystem_api_t* fs, const_chr_t path) {
	char parent[SK_FS_PATH_MAX];
	u32 n = 0u;
	u32 last_sep = 0u;
	if (path == NULL) {
		return -1;
	}
	while (path[n] != '\0' && n + 1u < (u32)sizeof(parent)) {
		if (sk_path_is_sep(path[n])) {
			last_sep = n;
		}
		parent[n] = path[n];
		n++;
	}
	if (last_sep == 0u) {
		return 0;
	}
	parent[last_sep] = '\0';
	if (fs->get_file_status(parent) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
	return fs->create_directory(parent);
}

static i32 layout_write_file(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path, const_chr_t data, u32 len) {
	const sk_filesystem_api_t* fs = app_api->filesystem_api(app_context);
	sk_file_handle_t file;
	u64 wrote;
	if (fs == NULL || path == NULL || path[0] == '\0') {
		return -1;
	}
	if (layout_ensure_parent_dir(fs, path) != 0) {
		return -1;
	}
	file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	if (file == NULL) {
		return -1;
	}
	wrote = fs->write_file(file, data, (size_t)len);
	fs->close_file(file);
	return wrote == (u64)len ? 0 : -1;
}

static i32 layout_read_file(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path, char** out, u32* out_len) {
	const sk_filesystem_api_t* fs = app_api->filesystem_api(app_context);
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_file_handle_t file;
	u64 size_u64;
	u32 size;
	char* buf;
	u64 got;
	if (fs == NULL || path == NULL || path[0] == '\0') {
		return -1;
	}
	if (fs->get_file_status(path) != SK_FILE_STATUS_FILE) {
		return 1; /* missing */
	}
	file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		return -1;
	}
	size_u64 = fs->get_file_size(file);
	if (size_u64 == 0u || size_u64 > 4u * 1024u * 1024u) {
		fs->close_file(file);
		return -1;
	}
	size = (u32)size_u64;
	buf = (char*)alloc->alloc(alloc->instance, (size_t)size + 1u);
	if (buf == NULL) {
		fs->close_file(file);
		return -1;
	}
	got = fs->read_file(file, buf, (size_t)size);
	fs->close_file(file);
	if (got != (u64)size) {
		alloc->free(alloc->instance, buf);
		return -1;
	}
	buf[size] = '\0';
	*out = buf;
	*out_len = size;
	return 0;
}

static i32 layout_emit_json(sk_editor_layout_store_t* store, char* out, u32 cap, u32* out_len) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_archive_writer_t writer;
	sk_str_view_t json;
	u32 i;
	u32 j;
	if (sk_json_archive_writer_init(&writer, alloc) != 0) {
		return -1;
	}
	writer.write_int(writer.instance, sk_str_view_cstr("version"), SK_EDITOR_LAYOUT_VERSION);
	writer.write_uint(writer.instance, sk_str_view_cstr("active"), store->active_type);
	writer.begin_seq_named(writer.instance, sk_str_view_cstr("workspaces"));
	for (i = 0u; i < store->workspaces.count; ++i) {
		layout_workspace_t* wl = &store->workspaces.items[i];
		writer.begin_map(writer.instance);
		writer.write_uint(writer.instance, sk_str_view_cstr("type"), wl->type_id);
		if (wl->dock_json != NULL && wl->dock_json_len > 0u) {
			writer.write_string(writer.instance, sk_str_view_cstr("dock"), sk_str_view_make(wl->dock_json, wl->dock_json_len));
		}
		writer.begin_seq_named(writer.instance, sk_str_view_cstr("windows"));
		for (j = 0u; j < wl->windows.count; ++j) {
			layout_window_t* rec = &wl->windows.items[j];
			writer.begin_map(writer.instance);
			writer.write_string(writer.instance, sk_str_view_cstr("id"), sk_str_view_cstr(rec->dock_id));
			if (rec->state != NULL && rec->state_len > 0u) {
				writer.write_string(writer.instance, sk_str_view_cstr("state"), sk_str_view_make(rec->state, rec->state_len));
			}
			writer.end_map(writer.instance);
		}
		writer.end_seq(writer.instance);
		writer.end_map(writer.instance);
	}
	writer.end_seq(writer.instance);
	json = sk_json_archive_writer_emit_as_string(&writer);
	if (json.data == NULL || json.size + 1u > cap) {
		sk_archive_writer_destroy(&writer);
		return -1;
	}
	memcpy(out, json.data, (size_t)json.size);
	out[json.size] = '\0';
	if (out_len != NULL) {
		*out_len = json.size;
	}
	sk_archive_writer_destroy(&writer);
	return 0;
}

static i32 layout_parse_json(sk_editor_layout_store_t* store, const_chr_t json, u32 len) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_archive_reader_t reader;
	layout_workspace_array_t parsed;
	i64 version;
	u32 i;
	if (sk_json_archive_reader_init(&reader, sk_str_view_make(json, len), alloc) != 0) {
		return -1;
	}
	version = reader.read_int(reader.instance, sk_str_view_cstr("version"));
	if (version != (i64)SK_EDITOR_LAYOUT_VERSION) {
		layout_warn(store->app_context, store->app_api, "layout version %d unsupported (want %d); keep previous store", (i32)version, SK_EDITOR_LAYOUT_VERSION);
		sk_archive_reader_destroy(&reader);
		return -1;
	}
	sk_array_init(&parsed, alloc);
	store->active_type = (u32)reader.read_uint(reader.instance, sk_str_view_cstr("active"));
	if (reader.begin_seq_named(reader.instance, sk_str_view_cstr("workspaces")) != 0) {
		while (reader.next_seq_entry(reader.instance) != 0) {
			layout_workspace_t wl;
			memset(&wl, 0, sizeof(wl));
			sk_array_init(&wl.windows, alloc);
			reader.begin_map(reader.instance);
			wl.type_id = (u32)reader.read_uint(reader.instance, sk_str_view_cstr("type"));
			{
				sk_str_view_t dock = reader.read_string(reader.instance, sk_str_view_cstr("dock"));
				if (dock.size > 0u) {
					wl.dock_json = layout_dup(alloc, dock.data, dock.size);
					wl.dock_json_len = wl.dock_json != NULL ? dock.size : 0u;
				}
			}
			if (reader.begin_seq_named(reader.instance, sk_str_view_cstr("windows")) != 0) {
				while (reader.next_seq_entry(reader.instance) != 0) {
					layout_window_t rec;
					sk_str_view_t id;
					sk_str_view_t state;
					memset(&rec, 0, sizeof(rec));
					reader.begin_map(reader.instance);
					id = reader.read_string(reader.instance, sk_str_view_cstr("id"));
					state = reader.read_string(reader.instance, sk_str_view_cstr("state"));
					if (id.data != NULL && id.size > 0u && id.size + 1u <= LAYOUT_ID_CAP) {
						memcpy(rec.dock_id, id.data, (size_t)id.size);
						rec.dock_id[id.size] = '\0';
						if (state.size > 0u) {
							rec.state = layout_dup(alloc, state.data, state.size);
							rec.state_len = rec.state != NULL ? state.size : 0u;
						}
						if (sk_array_push(&wl.windows, rec) != 0) {
							layout_window_free(alloc, &rec);
						}
					}
					reader.end_map(reader.instance);
				}
				reader.end_seq(reader.instance);
			}
			reader.end_map(reader.instance);
			if (sk_array_push(&parsed, wl) != 0) {
				layout_workspace_free(alloc, &wl);
			}
		}
		reader.end_seq(reader.instance);
	}
	sk_archive_reader_destroy(&reader);

	for (i = 0u; i < store->workspaces.count; ++i) {
		layout_workspace_free(alloc, &store->workspaces.items[i]);
	}
	sk_array_free(&store->workspaces);
	store->workspaces = parsed;
	store->dirty = 0;
	return 0;
}

i32 sk_editor_layout_save(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	sk_editor_workspace_t* active;
	char json[LAYOUT_DOCK_JSON_CAP];
	u32 len = 0u;
	if (store == NULL) {
		return -1;
	}
	active = sk_editor_workspace_active(app_context, app_api);
	if (active != NULL) {
		(void)sk_editor_workspace_capture(active);
	}
	if (layout_emit_json(store, json, (u32)sizeof(json), &len) != 0) {
		return -1;
	}
	if (layout_write_file(app_context, app_api, store->path, json, len) != 0) {
		return -1;
	}
	store->dirty = 0;
	return 0;
}

i32 sk_editor_layout_load(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	char* json = NULL;
	u32 len = 0u;
	i32 rc;
	if (store == NULL) {
		return -1;
	}
	rc = layout_read_file(app_context, app_api, store->path, &json, &len);
	if (rc == 1) {
		return 0; /* missing file: empty store, not an error */
	}
	if (rc != 0) {
		layout_warn(app_context, app_api, "failed to read layout file");
		return -1;
	}
	rc = layout_parse_json(store, json, len);
	sk_allocator_default()->free(sk_allocator_default()->instance, json);
	if (rc != 0) {
		layout_warn(app_context, app_api, "stale or corrupt layout file; default preset will be used");
	}
	return rc;
}

void sk_editor_layout_init(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_layout_store_t* store = layout_store_ensure(app_context, app_api);
	if (store == NULL) {
		return;
	}
	(void)sk_editor_layout_load(app_context, app_api);
}

void sk_editor_layout_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_layout_store_t* store = layout_store_get(app_context, app_api);
	u32 i;
	if (store == NULL) {
		return;
	}
	for (i = 0u; i < store->workspaces.count; ++i) {
		layout_workspace_free(alloc, &store->workspaces.items[i]);
	}
	sk_array_free(&store->workspaces);
	app_api->set_api(app_context, SK_EDITOR_LAYOUT_STORE_TYPE_ID, NULL);
	alloc->free(alloc->instance, store);
}

#ifdef SK_TESTS

#include "editor_api.h"
#include "main_windows.h"
#include "platform.h"
#include "test.h"
#include "ui.h"
#include "windows/entity_tree_window.h"
#include "windows/project_browser_window.h"

typedef struct layout_ui_fixture_t {
	sk_app_boot_t boot;
	const sk_ui_api_t* ui;
	sk_shared_lib_t lib;
} layout_ui_fixture_t;

static i32 layout_ui_fixture_start(layout_ui_fixture_t* fx) {
	const sk_platform_api_t* plat;
	const sk_filesystem_api_t* fs;
	sk_directory_iterator_t it;
	char base[SK_FS_PATH_MAX];
	char plugins_dir[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	char name[SK_FS_PATH_MAX];
	i32 found = 0;
	typedef int (*layout_plugin_entry_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

	memset(fx, 0, sizeof(*fx));
	fx->boot = sk_app_startup();
	if (fx->boot.context == NULL) {
		return -1;
	}
	plat = fx->boot.api->platform_api(fx->boot.context);
	fs = fx->boot.api->filesystem_api(fx->boot.context);
	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		return -1;
	}
	it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		return -1;
	}
	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		if (strncmp(name, "sk-ui.", 6u) != 0 || sk_is_shared_library_filename(name) == 0) {
			continue;
		}
		if (sk_path_join(sk_str_view_cstr(plugins_dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path)) < 0) {
			continue;
		}
		fx->lib = plat->lib_open(full_path);
		if (fx->lib == NULL) {
			continue;
		}
		found = 1;
		break;
	}
	fs->close_directory(it);
	if (found == 0) {
		return -1;
	}
	{
		void_ptr_t raw = plat->lib_symbol(fx->lib, "sk_plugin_entry_point");
		layout_plugin_entry_fn entry;
		if (raw == NULL) {
			plat->lib_close(fx->lib);
			fx->lib = NULL;
			return -1;
		}
		entry = SK_PTR_TO_FN(layout_plugin_entry_fn, raw);
		(void)entry(fx->boot.context, fx->boot.api);
	}
	fx->ui = (const sk_ui_api_t*)fx->boot.api->get_api(fx->boot.context, SK_UI_API_TYPE_ID);
	return fx->ui != NULL ? 0 : -1;
}

static void layout_ui_fixture_stop(layout_ui_fixture_t* fx) {
	const sk_platform_api_t* plat = NULL;
	if (fx->boot.context != NULL) {
		plat = fx->boot.api->platform_api(fx->boot.context);
		sk_app_shutdown(fx->boot.context);
	}
	if (fx->lib != NULL && plat != NULL) {
		plat->lib_close(fx->lib);
	}
	memset(fx, 0, sizeof(*fx));
}

static void layout_test_bind(layout_ui_fixture_t* fx, char* path, u32 path_cap) {
	const sk_filesystem_api_t* fs = fx->boot.api->filesystem_api(fx->boot.context);
	char tmp[SK_FS_PATH_MAX];
	sk_editor_bind_tables(fx->boot.context, fx->boot.api);
	sk_editor_workspace_register_impls(fx->boot.context, fx->boot.api);
	sk_editor_project_browser_register(fx->boot.context, fx->boot.api);
	sk_editor_entity_tree_register(fx->boot.context, fx->boot.api);
	sk_editor_windows_register_impls(fx->boot.context, fx->boot.api);
	if (fs != NULL && fs->temp_folder(tmp, (u32)sizeof(tmp)) == 0 && sk_path_join(sk_str_view_cstr(tmp), sk_str_view_cstr("skore-apx368-layout-test.json"), path, path_cap) >= 0) {
		if (fs->get_file_status(path) == SK_FILE_STATUS_FILE) {
			(void)fs->remove(path);
		}
		sk_editor_layout_set_path(fx->boot.context, fx->boot.api, path);
	}
	sk_editor_layout_init(fx->boot.context, fx->boot.api);
}

static i32 layout_test_window_open(sk_app_context_t* app, const sk_app_api_t* api, sk_type_id_t type) {
	return sk_editor_window_by_type(app, api, type) != NULL ? 1 : 0;
}

SK_TEST(editor_workspace_presets_match_manifest) {
	sk_app_boot_t boot = sk_app_create();
	const_chr_t ids[16];
	u32 n;
	u32 i;
	i32 have_console = 0;
	i32 have_scene = 0;
	i32 have_graph = 0;
	i32 have_packages = 0;

	TEST_ASSERT_NOT_NULL(boot.context);
	TEST_ASSERT_EQUAL_UINT(4u, sk_editor_workspace_preset_count());
	TEST_ASSERT_EQUAL_STRING("Scene", sk_editor_workspace_preset_at(0u)->name);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_WORKSPACE_SCENE, sk_editor_workspace_preset_at(0u)->type_id);
	TEST_ASSERT_EQUAL_STRING("Graph", sk_editor_workspace_preset_by_type(SK_EDITOR_WORKSPACE_GRAPH)->name);
	TEST_ASSERT_EQUAL_STRING("Animator", sk_editor_workspace_preset_by_type(SK_EDITOR_WORKSPACE_ANIMATOR)->name);
	TEST_ASSERT_EQUAL_STRING("Material", sk_editor_workspace_preset_by_type(SK_EDITOR_WORKSPACE_MATERIAL)->name);
	TEST_ASSERT_NULL(sk_editor_workspace_preset_by_type(99u));

	sk_editor_windows_register_impls(boot.context, boot.api);
	n = sk_editor_workspace_preset_window_ids(boot.context, boot.api, SK_EDITOR_WORKSPACE_SCENE, ids, 16u);
	TEST_ASSERT_TRUE(n >= 7u);
	for (i = 0u; i < n; ++i) {
		if (strcmp(ids[i], "sk.editor_window.console") == 0) {
			have_console = 1;
		}
		if (strcmp(ids[i], "sk.editor_window.scene_view") == 0) {
			have_scene = 1;
		}
		if (strcmp(ids[i], "sk.editor_window.graph_editor") == 0) {
			have_graph = 1;
		}
		if (strcmp(ids[i], "sk.editor_window.packages") == 0) {
			have_packages = 1;
		}
	}
	TEST_ASSERT_EQUAL_INT(1, have_console);
	TEST_ASSERT_EQUAL_INT(1, have_scene);
	TEST_ASSERT_EQUAL_INT(0, have_graph);
	TEST_ASSERT_EQUAL_INT(0, have_packages);

	n = sk_editor_workspace_preset_window_ids(boot.context, boot.api, SK_EDITOR_WORKSPACE_GRAPH, ids, 16u);
	have_graph = 0;
	have_scene = 0;
	for (i = 0u; i < n; ++i) {
		if (strcmp(ids[i], "sk.editor_window.graph_editor") == 0) {
			have_graph = 1;
		}
		if (strcmp(ids[i], "sk.editor_window.scene_view") == 0) {
			have_scene = 1;
		}
	}
	TEST_ASSERT_EQUAL_INT(1, have_graph);
	TEST_ASSERT_EQUAL_INT(0, have_scene);
	sk_app_shutdown(boot.context);
}

SK_TEST(editor_layout_switch_does_not_leak_or_double_register) {
	layout_ui_fixture_t fx;
	char path[SK_FS_PATH_MAX];
	const sk_editor_api_t* editor;
	sk_editor_workspace_t* scene_ws;
	sk_editor_workspace_t* graph_ws;
	sk_editor_window_t* console_a;
	sk_editor_window_t* console_b;
	sk_app_context_t* app;
	const sk_app_api_t* api;

	TEST_ASSERT_EQUAL_INT(0, layout_ui_fixture_start(&fx));
	layout_test_bind(&fx, path, (u32)sizeof(path));
	app = fx.boot.context;
	api = fx.boot.api;
	editor = (const sk_editor_api_t*)api->get_api(app, SK_EDITOR_API_TYPE_ID);

	scene_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_WORKSPACE_SCENE, editor->workspace_type_id(scene_ws));
	editor->dockspace_init(scene_ws);
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	console_a = editor->window_by_type(app, api, SK_EDITOR_WINDOW_CONSOLE);

	(void)sk_editor_workspace_capture(scene_ws);
	sk_editor_workspace_clear_dockspace(scene_ws);
	graph_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_GRAPH);
	editor->workspace_switch(graph_ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_restore(graph_ws));
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_ENTITY_TREE));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_CONSOLE));
	console_b = editor->window_by_type(app, api, SK_EDITOR_WINDOW_CONSOLE);
	TEST_ASSERT_EQUAL_PTR(console_a, console_b);

	(void)sk_editor_workspace_capture(graph_ws);
	sk_editor_workspace_clear_dockspace(graph_ws);
	editor->workspace_switch(scene_ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_restore(scene_ws));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	TEST_ASSERT_EQUAL_PTR(console_a, editor->window_by_type(app, api, SK_EDITOR_WINDOW_CONSOLE));

	editor->workspace_destroy(graph_ws);
	editor->workspace_destroy(scene_ws);
	sk_editor_layout_shutdown(app, api);
	layout_ui_fixture_stop(&fx);
}

SK_TEST(editor_layout_roundtrip_save_switch_restore_compare) {
	layout_ui_fixture_t fx;
	char path[SK_FS_PATH_MAX];
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_editor_workspace_t* graph_ws;
	sk_editor_window_t* pb;
	sk_ui_context_t* ctx;
	char dock_before[LAYOUT_DOCK_JSON_CAP];
	char dock_after[LAYOUT_DOCK_JSON_CAP];
	u32 before_len = 0u;
	u32 after_len = 0u;
	char state_json[] = "{\"treeOnlyView\":true,\"contentBrowserZoom\":2.25}";
	sk_app_context_t* app;
	const sk_app_api_t* api;

	TEST_ASSERT_EQUAL_INT(0, layout_ui_fixture_start(&fx));
	layout_test_bind(&fx, path, (u32)sizeof(path));
	app = fx.boot.context;
	api = fx.boot.api;
	editor = (const sk_editor_api_t*)api->get_api(app, SK_EDITOR_API_TYPE_ID);
	ui = fx.ui;

	scene_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	pb = editor->window_by_type(app, api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	TEST_ASSERT_NOT_NULL(pb);
	TEST_ASSERT_NOT_NULL(pb->load);
	TEST_ASSERT_EQUAL_INT(0, pb->load(pb, state_json, (u32)strlen(state_json)));

	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));

	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_save_dock_json(scene_ws, dock_before, (u32)sizeof(dock_before), &before_len));
	TEST_ASSERT_TRUE(before_len > 0u);
	TEST_ASSERT_EQUAL_INT(0, editor->workspace_capture(scene_ws));
	TEST_ASSERT_EQUAL_INT(1, editor->layout_has_saved(app, api, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_EQUAL_INT(0, editor->layout_save(app, api));

	graph_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_GRAPH);
	sk_editor_workspace_clear_dockspace(scene_ws);
	editor->workspace_switch(graph_ws);
	TEST_ASSERT_EQUAL_INT(0, editor->workspace_restore(graph_ws));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));

	(void)editor->workspace_capture(graph_ws);
	sk_editor_workspace_clear_dockspace(graph_ws);
	editor->workspace_switch(scene_ws);
	TEST_ASSERT_EQUAL_INT(0, editor->workspace_restore(scene_ws));

	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	pb = editor->window_by_type(app, api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	TEST_ASSERT_NOT_NULL(pb);
	{
		char restored[256];
		u32 restored_len = 0u;
		TEST_ASSERT_EQUAL_INT(0, pb->save(pb, restored, (u32)sizeof(restored), &restored_len));
		TEST_ASSERT_NOT_NULL(strstr(restored, "treeOnlyView"));
		TEST_ASSERT_NOT_NULL(strstr(restored, "2.25"));
	}
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.scene_view"));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_save_dock_json(scene_ws, dock_after, (u32)sizeof(dock_after), &after_len));
	TEST_ASSERT_EQUAL_UINT(before_len, after_len);
	TEST_ASSERT_EQUAL_INT(0, memcmp(dock_before, dock_after, (size_t)before_len));

	editor->workspace_destroy(graph_ws);
	editor->workspace_destroy(scene_ws);
	sk_editor_layout_shutdown(app, api);
	layout_ui_fixture_stop(&fx);
}

SK_TEST(editor_layout_corrupt_and_unknown_window_do_not_crash) {
	layout_ui_fixture_t fx;
	char path[SK_FS_PATH_MAX];
	const sk_editor_api_t* editor;
	sk_editor_workspace_t* scene_ws;
	const sk_filesystem_api_t* fs;
	sk_file_handle_t file;
	sk_app_context_t* app;
	const sk_app_api_t* api;
	const char* garbage = "{ this is not json";
	const char* bad_ver = "{\n  \"version\": 99,\n  \"workspaces\": []\n}";
	const char*
		stale = "{\n  \"version\": 1,\n  \"active\": 1,\n  \"workspaces\": [\n    {\n      \"type\": 1,\n      \"windows\": [\n        { \"id\": \"sk.editor_window.console\" },\n "
				"       { \"id\": \"sk.editor_window.no_such_window\" },\n        { \"id\": \"sk.editor_window.scene_view\" }\n      ]\n    }\n  ]\n}";

	TEST_ASSERT_EQUAL_INT(0, layout_ui_fixture_start(&fx));
	layout_test_bind(&fx, path, (u32)sizeof(path));
	app = fx.boot.context;
	api = fx.boot.api;
	editor = (const sk_editor_api_t*)api->get_api(app, SK_EDITOR_API_TYPE_ID);
	fs = api->filesystem_api(app);
	TEST_ASSERT_NOT_NULL(fs);

	file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_EQUAL_UINT64((u64)strlen(garbage), fs->write_file(file, garbage, strlen(garbage)));
	fs->close_file(file);
	TEST_ASSERT_TRUE(editor->layout_load(app, api) != 0);

	file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_EQUAL_UINT64((u64)strlen(bad_ver), fs->write_file(file, bad_ver, strlen(bad_ver)));
	fs->close_file(file);
	TEST_ASSERT_TRUE(editor->layout_load(app, api) != 0);

	file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_EQUAL_UINT64((u64)strlen(stale), fs->write_file(file, stale, strlen(stale)));
	fs->close_file(file);
	TEST_ASSERT_EQUAL_INT(0, editor->layout_load(app, api));
	TEST_ASSERT_EQUAL_INT(1, editor->layout_has_saved(app, api, SK_EDITOR_WORKSPACE_SCENE));

	scene_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	TEST_ASSERT_EQUAL_INT(0, editor->workspace_restore(scene_ws));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_NULL(sk_editor_window_by_dock_id(app, api, "sk.editor_window.no_such_window"));
	/* Missing default windows (project browser, ...) stay closed — layout is source of truth. */
	TEST_ASSERT_EQUAL_INT(0, layout_test_window_open(app, api, SK_EDITOR_WINDOW_PROJECT_BROWSER));

	editor->workspace_reset_to_preset(scene_ws);
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_PROJECT_BROWSER));
	TEST_ASSERT_EQUAL_INT(1, layout_test_window_open(app, api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_EQUAL_INT(0, editor->layout_has_saved(app, api, SK_EDITOR_WORKSPACE_SCENE));

	editor->workspace_destroy(scene_ws);
	sk_editor_layout_shutdown(app, api);
	if (fs->get_file_status(path) == SK_FILE_STATUS_FILE) {
		(void)fs->remove(path);
	}
	layout_ui_fixture_stop(&fx);
}

SK_TEST(editor_layout_disk_roundtrip_restores_on_init) {
	layout_ui_fixture_t fx;
	char path[SK_FS_PATH_MAX];
	const sk_editor_api_t* editor;
	sk_editor_workspace_t* scene_ws;
	sk_editor_window_t* pb;
	char state_json[] = "{\"treeOnlyView\":true,\"contentBrowserZoom\":1.75}";
	sk_app_context_t* app;
	const sk_app_api_t* api;

	TEST_ASSERT_EQUAL_INT(0, layout_ui_fixture_start(&fx));
	layout_test_bind(&fx, path, (u32)sizeof(path));
	app = fx.boot.context;
	api = fx.boot.api;
	editor = (const sk_editor_api_t*)api->get_api(app, SK_EDITOR_API_TYPE_ID);

	scene_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_SCENE);
	editor->dockspace_init(scene_ws);
	pb = editor->window_by_type(app, api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	TEST_ASSERT_EQUAL_INT(0, pb->load(pb, state_json, (u32)strlen(state_json)));
	TEST_ASSERT_EQUAL_INT(0, editor->workspace_capture(scene_ws));
	TEST_ASSERT_EQUAL_INT(0, editor->layout_save(app, api));
	editor->workspace_destroy(scene_ws);
	sk_editor_layout_shutdown(app, api);

	sk_editor_layout_set_path(app, api, path);
	sk_editor_layout_init(app, api);
	TEST_ASSERT_EQUAL_INT(1, sk_editor_layout_has_saved(app, api, SK_EDITOR_WORKSPACE_SCENE));
	scene_ws = editor->workspace_create(app, api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_EQUAL_INT(0, editor->workspace_restore(scene_ws));
	pb = editor->window_by_type(app, api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	TEST_ASSERT_NOT_NULL(pb);
	{
		char restored[256];
		u32 restored_len = 0u;
		TEST_ASSERT_EQUAL_INT(0, pb->save(pb, restored, (u32)sizeof(restored), &restored_len));
		TEST_ASSERT_NOT_NULL(strstr(restored, "1.75"));
	}

	editor->workspace_destroy(scene_ws);
	sk_editor_layout_shutdown(app, api);
	{
		const sk_filesystem_api_t* fs = api->filesystem_api(app);
		if (fs != NULL && fs->get_file_status(path) == SK_FILE_STATUS_FILE) {
			(void)fs->remove(path);
		}
	}
	layout_ui_fixture_stop(&fx);
}

#endif /* SK_TESTS */
