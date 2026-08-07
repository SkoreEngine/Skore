#include "filesystem.h"

/* Windows-only backend (app/CMakeLists.txt only compiles this TU on WIN32).
 * Guard so Linux/macOS clang-tidy can parse the file without a Windows SDK. */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SK_FS_TEMP_CAP = SK_FS_PATH_MAX };

/* Process-wide temp override (plain name: no sk_ variable prefix). */
static char temp_override[SK_FS_TEMP_CAP];
static int temp_override_set;

typedef struct sk_fs_file_t {
	HANDLE handle;
} sk_fs_file_t;

typedef struct sk_fs_dir_t {
	HANDLE find;
	WIN32_FIND_DATAA fd;
	i32 first_pending; /* 1 until first FindFirstFile entry is consumed */
	i32 exhausted;
} sk_fs_dir_t;

typedef struct sk_fs_mapping_t {
	HANDLE mapping;
	DWORD map_access; /* MapViewOfFile desired access */
	u64 size;
} sk_fs_mapping_t;

/* Track mapped views for UnmapViewOfFile (Win32 only needs the address). */
typedef struct sk_fs_view_entry_t {
	void* addr;
	struct sk_fs_view_entry_t* next;
} sk_fs_view_entry_t;

static sk_fs_view_entry_t* view_list;

static i32 copy_to_out(char_ptr_t out, u32 out_cap, const_chr_t src) {
	if (out_cap == 0u) {
		return -1;
	}
	size_t len = strlen(src);
	if (len + 1u > (size_t)out_cap) {
		return -1;
	}
	memcpy(out, src, len + 1u);
	return 0;
}

static void setup_temp_folder(const_chr_t temp_folder) {
	if (temp_folder == NULL || temp_folder[0] == '\0') {
		temp_override[0] = '\0';
		temp_override_set = 0;
		return;
	}
	if (copy_to_out(temp_override, (u32)sizeof(temp_override), temp_folder) != 0) {
		temp_override[0] = '\0';
		temp_override_set = 0;
		return;
	}
	temp_override_set = 1;
}

static i32 current_dir(char_ptr_t out, u32 out_cap) {
	char buf[SK_FS_PATH_MAX];
	if (out_cap == 0u) {
		return -1;
	}
	DWORD n = GetCurrentDirectoryA((DWORD)sizeof(buf), buf);
	if (n == 0u || n >= (DWORD)sizeof(buf)) {
		return -1;
	}
	return copy_to_out(out, out_cap, buf);
}

static i32 documents_dir(char_ptr_t out, u32 out_cap) {
	char buf[MAX_PATH];
	if (out_cap == 0u) {
		return -1;
	}
	if (FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, buf))) {
		return current_dir(out, out_cap);
	}
	return copy_to_out(out, out_cap, buf);
}

static i32 app_folder(char_ptr_t out, u32 out_cap) {
	char buf[SK_FS_PATH_MAX];

	if (out_cap == 0u) {
		return -1;
	}

	DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
	if (n == 0u || n >= (DWORD)sizeof(buf)) {
		return -1;
	}

	char* slash = strrchr(buf, '\\');
	if (slash == NULL) {
		slash = strrchr(buf, '/');
	}
	if (slash != NULL) {
		*slash = '\0';
	}
	return copy_to_out(out, out_cap, buf);
}

static i32 temp_folder(char_ptr_t out, u32 out_cap) {
	char buf[SK_FS_PATH_MAX];

	if (out_cap == 0u) {
		return -1;
	}
	if (temp_override_set) {
		return copy_to_out(out, out_cap, temp_override);
	}

	DWORD n = GetTempPathA((DWORD)sizeof(buf), buf);
	if (n == 0u || n >= (DWORD)sizeof(buf)) {
		return -1;
	}
	/* Strip trailing slash for consistency with other path getters. */
	if (n > 1u && (buf[n - 1u] == '\\' || buf[n - 1u] == '/')) {
		buf[n - 1u] = '\0';
	}
	return copy_to_out(out, out_cap, buf);
}

static sk_file_status_t get_file_status(const_chr_t path) {
	DWORD attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
		return SK_FILE_STATUS_DIRECTORY;
	}
	if ((attrs & FILE_ATTRIBUTE_DEVICE) != 0u || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
		/* Treat reparse points as other unless we want to resolve them. */
		if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
			return SK_FILE_STATUS_DIRECTORY;
		}
	}
	return SK_FILE_STATUS_FILE;
}

static u64 get_path_size(const_chr_t path) {
	WIN32_FILE_ATTRIBUTE_DATA data;
	ULARGE_INTEGER uli;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
		return 0ull;
	}
	if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
		return 0ull;
	}
	uli.LowPart = data.nFileSizeLow;
	uli.HighPart = data.nFileSizeHigh;
	return uli.QuadPart;
}

static u64 get_file_id(const_chr_t path) {
	BY_HANDLE_FILE_INFORMATION info;

	HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return 0ull;
	}
	if (!GetFileInformationByHandle(h, &info)) {
		CloseHandle(h);
		return 0ull;
	}
	CloseHandle(h);
	u64 id = ((u64)info.nFileIndexHigh << 32) | (u64)info.nFileIndexLow;
	return id;
}

static i32 create_directory(const_chr_t path) {
	if (CreateDirectoryA(path, NULL)) {
		return 0;
	}
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		DWORD attrs = GetFileAttributesA(path);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
			return 0;
		}
	}
	return -1;
}

static i32 remove_path(const_chr_t path) {
	DWORD attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return -1;
	}
	if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
		return RemoveDirectoryA(path) ? 0 : -1;
	}
	return DeleteFileA(path) ? 0 : -1;
}

static i32 rename_path(const_chr_t old_name, const_chr_t new_name) {
	return MoveFileExA(old_name, new_name, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

static i32 copy_file(const_chr_t from, const_chr_t to) {
	return CopyFileA(from, to, FALSE) ? 0 : -1;
}

static sk_file_handle_t open_file(const_chr_t path, sk_file_access_t access_mode) {
	DWORD access = 0;
	DWORD share = FILE_SHARE_READ;
	DWORD disp = 0;

	switch ((int)access_mode) {
	case SK_FILE_ACCESS_READ:
		access = GENERIC_READ;
		share = FILE_SHARE_READ;
		disp = OPEN_EXISTING;
		break;
	case SK_FILE_ACCESS_WRITE:
		access = GENERIC_WRITE;
		share = 0;
		disp = CREATE_ALWAYS;
		break;
	case SK_FILE_ACCESS_READ_WRITE:
		access = GENERIC_READ | GENERIC_WRITE;
		share = FILE_SHARE_READ;
		disp = OPEN_ALWAYS;
		break;
	default:
		return NULL;
	}

	HANDLE h = CreateFileA(path, access, share, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	sk_fs_file_t* file = (sk_fs_file_t*)malloc(sizeof(sk_fs_file_t));
	if (file == NULL) {
		CloseHandle(h);
		return NULL;
	}
	file->handle = h;
	return (sk_file_handle_t)file;
}

static u64 get_file_size(sk_file_handle_t file) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	LARGE_INTEGER li;
	if (f->handle == INVALID_HANDLE_VALUE) {
		return 0ull;
	}
	if (!GetFileSizeEx(f->handle, &li)) {
		return 0ull;
	}
	return (u64)li.QuadPart;
}

static u64 write_file(sk_file_handle_t file, const_ptr_t data, size_t size) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	u64 total = 0ull;

	if (size == 0u) {
		return 0ull;
	}

	const u8* p = (const u8*)data;
	size_t left = size;
	while (left > 0u) {
		DWORD chunk = left > 0x7fffffffu ? 0x7fffffffu : (DWORD)left;
		DWORD written = 0;
		if (!WriteFile(f->handle, p, chunk, &written, NULL)) {
			break;
		}
		if (written == 0u) {
			break;
		}
		p += written;
		left -= (size_t)written;
		total += (u64)written;
	}
	return total;
}

static u64 read_file(sk_file_handle_t file, void_ptr_t data, size_t size) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	u64 total = 0ull;

	if (size == 0u) {
		return 0ull;
	}

	u8* p = (u8*)data;
	size_t left = size;
	while (left > 0u) {
		DWORD chunk = left > 0x7fffffffu ? 0x7fffffffu : (DWORD)left;
		DWORD nread = 0;
		if (!ReadFile(f->handle, p, chunk, &nread, NULL)) {
			break;
		}
		if (nread == 0u) {
			break;
		}
		p += nread;
		left -= (size_t)nread;
		total += (u64)nread;
	}
	return total;
}

static u64 read_file_at(sk_file_handle_t file, void_ptr_t data, size_t size, u64 offset) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	OVERLAPPED ov;
	u64 total = 0ull;

	if (size == 0u) {
		return 0ull;
	}

	u8* p = (u8*)data;
	size_t left = size;
	while (left > 0u) {
		DWORD chunk = left > 0x7fffffffu ? 0x7fffffffu : (DWORD)left;
		DWORD nread = 0;
		u64 at = offset + total;
		memset(&ov, 0, sizeof(ov));
		ov.Offset = (DWORD)(at & 0xffffffffull);
		ov.OffsetHigh = (DWORD)(at >> 32);
		if (!ReadFile(f->handle, p, chunk, &nread, &ov)) {
			if (GetLastError() != ERROR_HANDLE_EOF) {
				break;
			}
			break;
		}
		if (nread == 0u) {
			break;
		}
		p += nread;
		left -= (size_t)nread;
		total += (u64)nread;
	}
	return total;
}

static void close_file(sk_file_handle_t file) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	if (f->handle != NULL && f->handle != INVALID_HANDLE_VALUE) {
		CloseHandle(f->handle);
	}
	free(f);
}

static sk_file_handle_t create_file_mapping(sk_file_handle_t file, sk_file_access_t access_mode, u64 size) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	DWORD protect = 0;
	DWORD map_access = 0;

	if (f->handle == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	if ((access_mode & SK_FILE_ACCESS_WRITE) != 0) {
		protect = PAGE_READWRITE;
		map_access = FILE_MAP_WRITE;
	} else if ((access_mode & SK_FILE_ACCESS_READ) != 0) {
		protect = PAGE_READONLY;
		map_access = FILE_MAP_READ;
	} else {
		return NULL;
	}

	DWORD size_high = 0;
	DWORD size_low = 0;
	if (size != 0ull) {
		size_high = (DWORD)(size >> 32);
		size_low = (DWORD)(size & 0xffffffffull);
	}

	HANDLE mapping = CreateFileMappingA(f->handle, NULL, protect, size_high, size_low, NULL);
	if (mapping == NULL) {
		return NULL;
	}

	sk_fs_mapping_t* m = (sk_fs_mapping_t*)malloc(sizeof(sk_fs_mapping_t));
	if (m == NULL) {
		CloseHandle(mapping);
		return NULL;
	}
	m->mapping = mapping;
	m->map_access = map_access;
	m->size = size;
	return (sk_file_handle_t)m;
}

static void_ptr_t map_view_of_file(sk_file_handle_t mapping) {
	sk_fs_mapping_t* m = (sk_fs_mapping_t*)mapping;

	if (m->mapping == NULL) {
		return NULL;
	}

	/* 0 maps the entire section created by CreateFileMapping. */
	void* addr = MapViewOfFile(m->mapping, m->map_access, 0, 0, m->size);
	if (addr == NULL) {
		return NULL;
	}

	sk_fs_view_entry_t* entry = (sk_fs_view_entry_t*)malloc(sizeof(sk_fs_view_entry_t));
	if (entry == NULL) {
		UnmapViewOfFile(addr);
		return NULL;
	}
	entry->addr = addr;
	entry->next = view_list;
	view_list = entry;
	return addr;
}

static i32 unmap_view_of_file(void_ptr_t map) {
	sk_fs_view_entry_t** pp = &view_list;
	while (*pp != NULL) {
		sk_fs_view_entry_t* e = *pp;
		if (e->addr == map) {
			i32 status = UnmapViewOfFile(e->addr) ? 0 : -1;
			*pp = e->next;
			free(e);
			return status;
		}
		pp = &e->next;
	}
	/* Not tracked — still try OS unmap. */
	return UnmapViewOfFile(map) ? 0 : -1;
}

static void close_file_mapping(sk_file_handle_t mapping) {
	sk_fs_mapping_t* m = (sk_fs_mapping_t*)mapping;
	if (m->mapping != NULL) {
		CloseHandle(m->mapping);
	}
	free(m);
}

static sk_directory_iterator_t open_directory(const_chr_t path) {
	char pattern[SK_FS_PATH_MAX];
	WIN32_FIND_DATAA fd;

	size_t len = strlen(path);
	if (len == 0u || len + 3u >= sizeof(pattern)) {
		return NULL;
	}
	memcpy(pattern, path, len);
	/* Append \* for FindFirstFile. */
	if (path[len - 1u] == '\\' || path[len - 1u] == '/') {
		pattern[len] = '*';
		pattern[len + 1u] = '\0';
	} else {
		pattern[len] = '\\';
		pattern[len + 1u] = '*';
		pattern[len + 2u] = '\0';
	}

	HANDLE find = FindFirstFileA(pattern, &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	sk_fs_dir_t* it = (sk_fs_dir_t*)malloc(sizeof(sk_fs_dir_t));
	if (it == NULL) {
		FindClose(find);
		return NULL;
	}
	it->find = find;
	it->fd = fd;
	it->first_pending = 1;
	it->exhausted = 0;
	return (sk_directory_iterator_t)it;
}

static i32 next_directory(sk_directory_iterator_t it, char_ptr_t name_out, u32 name_cap) {
	sk_fs_dir_t* d = (sk_fs_dir_t*)it;

	if (name_cap == 0u) {
		return -1;
	}
	if (d->exhausted) {
		return 1;
	}

	if (!d->first_pending) {
		if (!FindNextFileA(d->find, &d->fd)) {
			d->exhausted = 1;
			return 1;
		}
	}
	d->first_pending = 0;

	size_t len = strlen(d->fd.cFileName);
	if (len + 1u > (size_t)name_cap) {
		return -1;
	}
	memcpy(name_out, d->fd.cFileName, len + 1u);
	return 0;
}

static void close_directory(sk_directory_iterator_t it) {
	sk_fs_dir_t* d = (sk_fs_dir_t*)it;
	if (d->find != INVALID_HANDLE_VALUE) {
		FindClose(d->find);
	}
	free(d);
}

static const sk_filesystem_api_t filesystem_api = {
	setup_temp_folder,	 current_dir,	   documents_dir,	   app_folder,		   temp_folder,	   get_file_status, get_path_size,	 get_file_id,  create_directory,
	remove_path,		 rename_path,	   copy_file,		   open_file,		   get_file_size,  write_file,		read_file,		 read_file_at, close_file,
	create_file_mapping, map_view_of_file, unmap_view_of_file, close_file_mapping, open_directory, next_directory,	close_directory,
};

void sk_filesystem_get_api(sk_filesystem_api_t* out) {
	*out = filesystem_api;
}

const sk_filesystem_api_t* sk_filesystem_api(void) {
	return &filesystem_api;
}

#endif /* _WIN32 */
