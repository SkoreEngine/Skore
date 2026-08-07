#pragma once

/**
 * @file filesystem.h
 * @brief Host filesystem module API (paths, files, memory maps).
 *
 * Types and free-function declarations live in sk-core. The OS backends
 * (Win32 / Unix) that fill sk_filesystem_api_t are implemented in sk-app.
 * Hosts and tests that call sk_filesystem_api() must link sk-app.
 *
 * Paths are UTF-8 C strings (const_chr_t). Path getters write into a
 * caller-provided buffer (null-terminated on success).
 */

#include "common.h"

#include <stddef.h> /* size_t */
#include <string.h> /* strlen, strcmp, strstr */

#ifdef __cplusplus
extern "C" {
#endif

/** Recommended capacity for path out-buffers (bytes, including NUL). */
enum { SK_FS_PATH_MAX = 4096 };

/**
 * Opaque OS file or file-mapping handle.
 * NULL means invalid / not open.
 */
typedef void_ptr_t sk_file_handle_t;

/**
 * Opaque directory iterator handle from open_directory.
 * NULL means invalid / not open.
 */
typedef void_ptr_t sk_directory_iterator_t;

/**
 * Result of probing a path on disk.
 */
typedef enum sk_file_status_t { SK_FILE_STATUS_NOT_FOUND = 0, SK_FILE_STATUS_FILE = 1, SK_FILE_STATUS_DIRECTORY = 2, SK_FILE_STATUS_OTHER = 3 } sk_file_status_t;

/**
 * Access mode for open_file and create_file_mapping.
 * Values may be combined as READ | WRITE for read-write.
 */
typedef enum sk_file_access_t { SK_FILE_ACCESS_READ = 1, SK_FILE_ACCESS_WRITE = 2, SK_FILE_ACCESS_READ_WRITE = 3 } sk_file_access_t;

/**
 * Global filesystem module API (one process-wide table).
 *
 * Obtain via sk_filesystem_api() or sk_filesystem_get_api().
 * Implemented in sk-app.
 */
typedef struct sk_filesystem_api_t {
	/**
     * Override the temp folder returned by temp_folder.
     * Pass NULL or empty to clear the override (use OS default again).
     *
     * @param temp_folder UTF-8 path, or NULL/empty to reset.
     */
	void (*setup_temp_folder)(const_chr_t temp_folder);

	/**
     * Current working directory.
     *
     * @param out     Destination buffer (UTF-8).
     * @param out_cap Capacity of @p out in bytes (including NUL).
     * @return 0 on success, non-zero on failure (null args, too small, OS error).
     */
	i32 (*current_dir)(char_ptr_t out, u32 out_cap);

	/**
     * User documents directory (best-effort; may fall back to home or cwd).
     *
     * @param out     Destination buffer (UTF-8).
     * @param out_cap Capacity of @p out in bytes (including NUL).
     * @return 0 on success, non-zero on failure.
     */
	i32 (*documents_dir)(char_ptr_t out, u32 out_cap);

	/**
     * Directory containing the running executable (best-effort).
     *
     * @param out     Destination buffer (UTF-8).
     * @param out_cap Capacity of @p out in bytes (including NUL).
     * @return 0 on success, non-zero on failure.
     */
	i32 (*app_folder)(char_ptr_t out, u32 out_cap);

	/**
     * Process temp folder (setup_temp_folder override, else OS temp).
     *
     * @param out     Destination buffer (UTF-8).
     * @param out_cap Capacity of @p out in bytes (including NUL).
     * @return 0 on success, non-zero on failure.
     */
	i32 (*temp_folder)(char_ptr_t out, u32 out_cap);

	/**
     * Classify a path (missing / file / directory / other).
     *
     * @param path UTF-8 path. NULL/empty → NOT_FOUND.
     * @return Status enum.
     */
	sk_file_status_t (*get_file_status)(const_chr_t path);

	/**
     * Size in bytes of a regular file at @p path.
     *
     * @param path UTF-8 path.
     * @return Size, or 0 if missing / not a file / error.
     */
	u64 (*get_path_size)(const_chr_t path);

	/**
     * Stable-ish file identity for change detection (inode / file index).
     * Not guaranteed unique across volumes or after reuse.
     *
     * @param path UTF-8 path.
     * @return Non-zero id on success, 0 on failure.
     */
	u64 (*get_file_id)(const_chr_t path);

	/**
     * Create a single directory (parent must already exist).
     *
     * @param path UTF-8 path.
     * @return 0 on success (or already exists as a directory), non-zero on failure.
     */
	i32 (*create_directory)(const_chr_t path);

	/**
     * Remove a file or empty directory.
     *
     * @param path UTF-8 path.
     * @return 0 on success, non-zero on failure.
     */
	i32 (*remove)(const_chr_t path);

	/**
     * Rename / move a path.
     *
     * @param old_name Existing UTF-8 path.
     * @param new_name Destination UTF-8 path.
     * @return 0 on success, non-zero on failure.
     */
	i32 (*rename)(const_chr_t old_name, const_chr_t new_name);

	/**
     * Copy a file (overwrites @p to if it exists).
     *
     * @param from Source UTF-8 path.
     * @param to   Destination UTF-8 path.
     * @return 0 on success, non-zero on failure.
     */
	i32 (*copy_file)(const_chr_t from, const_chr_t to);

	/**
     * Open a file.
     * READ: existing file, read-only.
     * WRITE: create/truncate, write-only.
     * READ_WRITE: create if missing, read-write (does not truncate).
     *
     * @param path        UTF-8 path.
     * @param access_mode sk_file_access_t flags.
     * @return Handle, or NULL on failure.
     */
	sk_file_handle_t (*open_file)(const_chr_t path, sk_file_access_t access_mode);

	/**
     * Size in bytes of an open file.
     *
     * @param file Handle from open_file.
     * @return Size, or 0 if invalid / error.
     */
	u64 (*get_file_size)(sk_file_handle_t file);

	/**
     * Write bytes at the current file position.
     *
     * @param file Handle opened with write access.
     * @param data Source bytes (may be NULL only if size is 0).
     * @param size Number of bytes to write.
     * @return Bytes written (0 on failure or size 0).
     */
	u64 (*write_file)(sk_file_handle_t file, const_ptr_t data, size_t size);

	/**
     * Read bytes from the current file position.
     *
     * @param file Handle opened with read access.
     * @param data Destination buffer.
     * @param size Max bytes to read.
     * @return Bytes read (0 on EOF, failure, or size 0).
     */
	u64 (*read_file)(sk_file_handle_t file, void_ptr_t data, size_t size);

	/**
     * Read bytes at an absolute file offset (does not change position on success
     * paths that use pread; may reposition on some backends).
     *
     * @param file   Handle opened with read access.
     * @param data   Destination buffer.
     * @param size   Max bytes to read.
     * @param offset Byte offset from start of file.
     * @return Bytes read (0 on EOF, failure, or size 0).
     */
	u64 (*read_file_at)(sk_file_handle_t file, void_ptr_t data, size_t size, u64 offset);

	/**
     * Close a file opened with open_file.
     * Passing NULL is a no-op. Do not close mapping handles here.
     *
     * @param file Handle to close.
     */
	void (*close_file)(sk_file_handle_t file);

	/**
     * Create a file-mapping object for @p file (does not map a view yet).
     * On Unix the mapping is bound at map_view; size 0 means whole file.
     *
     * @param file        Open file handle.
     * @param access_mode Desired view access (read and/or write).
     * @param size        Mapping size in bytes; 0 = entire file.
     * @return Mapping handle, or NULL on failure.
     */
	sk_file_handle_t (*create_file_mapping)(sk_file_handle_t file, sk_file_access_t access_mode, u64 size);

	/**
     * Map a view of a mapping created with create_file_mapping.
     *
     * @param mapping Handle from create_file_mapping.
     * @return Mapped address, or NULL on failure.
     */
	void_ptr_t (*map_view_of_file)(sk_file_handle_t mapping);

	/**
     * Unmap a view returned by map_view_of_file.
     *
     * @param map Pointer from map_view_of_file.
     * @return 0 on success, non-zero on failure.
     */
	i32 (*unmap_view_of_file)(void_ptr_t map);

	/**
     * Close a mapping from create_file_mapping (after unmapping views).
     * Passing NULL is a no-op.
     *
     * @param mapping Mapping handle to close.
     */
	void (*close_file_mapping)(sk_file_handle_t mapping);

	/**
     * Open a directory for sequential iteration of entry names.
     *
     * @param path UTF-8 directory path.
     * @return Iterator handle, or NULL if missing / not a directory / OS error.
     */
	sk_directory_iterator_t (*open_directory)(const_chr_t path);

	/**
     * Advance the iterator and write the next entry basename into @p name_out
     * (null-terminated; not a full path). Includes "." / ".." and subdirs —
     * filter as needed.
     *
     * @param it       Handle from open_directory.
     * @param name_out Destination buffer for the entry name.
     * @param name_cap Capacity of @p name_out in bytes (including NUL).
     * @return 0 if an entry was written, non-zero when iteration is finished
     *         or on error (name too long, OS error).
     */
	i32 (*next_directory)(sk_directory_iterator_t it, char_ptr_t name_out, u32 name_cap);

	/**
     * Close a directory iterator from open_directory.
     *
     * @param it Handle to close.
     */
	void (*close_directory)(sk_directory_iterator_t it);
} sk_filesystem_api_t;

/**
 * Non-zero if @p name looks like a host shared library (.dll / .so / .dylib).
 * Skips empty and hidden names (leading '.'). Host extension rules only.
 *
 * @param name Basename (not a path). Caller must pass a non-null C string.
 * @return 1 if shared-library-like, 0 otherwise.
 */
SK_FINLINE i32 sk_is_shared_library_filename(const_chr_t name) {
	size_t len;

	if (name[0] == '\0' || name[0] == '.') {
		return 0;
	}

	len = strlen(name);
#if defined(_WIN32)
	/* Case-insensitive .dll (Windows filesystem is case-insensitive). */
	if (len > 4u) {
		const_chr_t e = name + (len - 4u);
		if (e[0] == '.' && (e[1] == 'd' || e[1] == 'D') && (e[2] == 'l' || e[2] == 'L') && (e[3] == 'l' || e[3] == 'L')) {
			return 1;
		}
	}
	return 0;
#elif defined(__APPLE__)
	return (len > 6u && strcmp(name + (len - 6u), ".dylib") == 0) ? 1 : 0;
#else
	if (len > 3u && strcmp(name + (len - 3u), ".so") == 0) {
		return 1;
	}
	/* Versioned ELF: libfoo.so.1 */
	return (strstr(name, ".so.") != NULL) ? 1 : 0;
#endif
}

/**
 * Fill @p out with the default host filesystem API table.
 * Implemented in sk-app.
 *
 * @param out Destination table; NULL is a no-op.
 */
void sk_filesystem_get_api(sk_filesystem_api_t* out);

/**
 * Return a process-lifetime pointer to the default filesystem API table.
 * Same backend as sk_filesystem_get_api. Implemented in sk-app.
 *
 * @return Non-NULL pointer to a static sk_filesystem_api_t.
 */
const sk_filesystem_api_t* sk_filesystem_api(void);

#ifdef __cplusplus
}
#endif
