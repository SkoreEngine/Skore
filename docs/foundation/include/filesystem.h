#pragma once

/**
 * @file filesystem.h
 * @brief Contract delta for APX-278 (apply onto today's `core/filesystem.h`).
 *
 * UNCHANGED:
 *   - `SK_FS_PATH_MAX`, `sk_file_handle_t`, `sk_directory_iterator_t`,
 *     `sk_file_status_t`, `sk_file_access_t`
 *   - `sk_is_shared_library_filename`
 *   - signatures of **stateless** table entries (they take no filesystem
 *     context): `current_dir`, `documents_dir`, `app_folder`, `get_file_status`,
 *     `get_path_size`, `get_file_id`, `create_directory`, `remove`, `rename`,
 *     `copy_file`, `open_file`, `get_file_size`, `write_file`, `read_file`,
 *     `read_file_at`, `close_file`, `create_file_mapping`, `close_file_mapping`,
 *     `open_directory`, `next_directory`, `close_directory`
 *
 * CHANGED (stateful — first argument is the context that owns `temp_override`
 * and the mmap `view_list`):
 *   - `setup_temp_folder`
 *   - `temp_folder`
 *   - `map_view_of_file`
 *   - `unmap_view_of_file`
 *
 * DELETED:
 *   - `void sk_filesystem_get_api(sk_filesystem_api_t* out);`
 *   - `const sk_filesystem_api_t* sk_filesystem_api(void);`
 *
 * ADDED:
 *   - `sk_filesystem_context_t` + create/destroy in `foundation/filesystem.c`
 *     (shared TU; create/destroy **only**). Destroy walks `view_list`,
 *     `sk_filesystem_backend_unmap`, frees nodes, frees the context.
 *     `sk_fs_view_entry_t` lives in `foundation/internal/filesystem_context.h`.
 *   - `sk_filesystem_install` is **not** in `filesystem.c`; it lives in
 *     `filesystem_unix.c` / `filesystem_win32.c` next to the `static const` table.
 *   - `SK_FILESYSTEM_API_TYPE_ID`
 *
 * Obtain:
 *
 * @code
 * const sk_filesystem_api_t* fs = app_api->filesystem_api(app_ctx);
 * sk_filesystem_context_t* fs_ctx = app_api->filesystem_context(app_ctx);
 * fs->temp_folder(fs_ctx, out, cap);
 * fs->current_dir(out, cap); // still no context (OS cwd is process-wide)
 * @endcode
 */

#include "allocator.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque per-context filesystem state.
 * Owns `temp_override` / `temp_override_set` and the mmap view linked list
 * (replaces the file-scope statics in `filesystem_unix.c` / `filesystem_win32.c`).
 * Layout: `foundation/internal/filesystem_context.h`.
 */
typedef struct sk_filesystem_context_t sk_filesystem_context_t;

/** Type id for app-context registration of `sk_filesystem_api_t`. */
#define SK_FILESYSTEM_API_TYPE_ID SK_TYPE_ID("sk.filesystem_api", 0xb8556b0d8f2c7ef6ULL, 0x53aa55a880068ac1ULL)

/**
 * Allocate a filesystem context (empty override, empty view list).
 * Hosts normally do not call this — `sk_app_startup` / `sk_app_init` does.
 *
 * @param allocator Used for view-list nodes (must not be NULL).
 * @return New context, or NULL on allocation failure.
 */
sk_filesystem_context_t* sk_filesystem_context_create(const sk_allocator_t* allocator);

/**
 * Unmap any remaining views, free the view list, free the context.
 * Passing NULL is a no-op.
 *
 * @param fs Context from `sk_filesystem_context_create`.
 */
void sk_filesystem_context_destroy(sk_filesystem_context_t* fs);

/*
 * Stateful entries of `sk_filesystem_api_t` (replace the current first lines):
 *
 *   void (*setup_temp_folder)(sk_filesystem_context_t* fs, const_chr_t temp_folder);
 *   i32  (*temp_folder)(sk_filesystem_context_t* fs, char_ptr_t out, u32 out_cap);
 *   void_ptr_t (*map_view_of_file)(sk_filesystem_context_t* fs, sk_file_handle_t mapping);
 *   i32  (*unmap_view_of_file)(sk_filesystem_context_t* fs, void_ptr_t map);
 *
 * `setup_temp_folder`: Pass NULL or empty to clear the override (use OS default).
 * `temp_folder`: Writes the override if set, else the OS temp path.
 * `map_view_of_file` / `unmap_view_of_file`: record / drop view_list entries so
 * munmap/UnmapViewOfFile has a size and `sk_filesystem_context_destroy` can
 * drain leftovers (fixes the current shutdown leak).
 */

#ifdef __cplusplus
}
#endif
