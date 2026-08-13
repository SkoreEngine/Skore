#pragma once

/**
 * @file internal/filesystem_context.h
 * @brief Internal filesystem context + mmap view-list node (APX-278).
 *
 * NOT a public header. Implementing tasks copy this to
 * `foundation/internal/filesystem_context.h`.
 *
 * `sk_filesystem_context_create` / `_destroy` live in `foundation/filesystem.c`
 * (one shared TU). `sk_filesystem_install` and `sk_filesystem_backend_unmap`
 * live in `filesystem_unix.c` / `filesystem_win32.c`.
 */

#include "allocator.h"
#include "filesystem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_fs_view_entry_t {
	void* addr;
	size_t size;
	struct sk_fs_view_entry_t* next;
} sk_fs_view_entry_t;

struct sk_filesystem_context_t {
	char temp_override[SK_FS_PATH_MAX];
	i32 temp_override_set;
	u8 _pad0[4];
	sk_fs_view_entry_t* view_list;
	const sk_allocator_t* allocator;
};

/**
 * Unmap one view (munmap / UnmapViewOfFile). Defined in
 * `filesystem_unix.c` / `filesystem_win32.c`.
 */
void sk_filesystem_backend_unmap(void* addr, size_t size);

#ifdef __cplusplus
}
#endif
