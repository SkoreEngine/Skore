#include "filesystem.h"

#include "internal/app_context.h"
#include "internal/tables.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/syslimits.h>
#endif

enum { SK_FS_TEMP_CAP = SK_FS_PATH_MAX };

/* Process-wide temp override (plain name: no sk_ variable prefix). */
static char temp_override[SK_FS_TEMP_CAP];
static int temp_override_set;

/* ---- internal helpers -------------------------------------------------- */

typedef struct sk_fs_file_t {
	int fd;
} sk_fs_file_t;

typedef struct sk_fs_dir_t {
	DIR* dir;
} sk_fs_dir_t;

typedef struct sk_fs_mapping_t {
	int fd; /* dup of file fd; closed on close_file_mapping */
	int prot;
	int flags;
	u64 size;	/* 0 until map_view resolves whole-file size */
	void* view; /* last mapped view (for unmap size tracking) */
	size_t view_size;
	int view_active;
} sk_fs_mapping_t;

/* Track mapped views so unmap knows the length (munmap requires it). */
typedef struct sk_fs_view_entry_t {
	void* addr;
	size_t size;
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
	if (getcwd(buf, sizeof(buf)) == NULL) {
		return -1;
	}
	return copy_to_out(out, out_cap, buf);
}

static i32 documents_dir(char_ptr_t out, u32 out_cap) {
	char buf[SK_FS_PATH_MAX];

	if (out_cap == 0u) {
		return -1;
	}

	const char* home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return current_dir(out, out_cap);
	}

	/* Apple: ~/Documents. Other Unix: same path is a common default (XDG not required). */
	int n = snprintf(buf, sizeof(buf), "%s/Documents", home);
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		return -1;
	}
	return copy_to_out(out, out_cap, buf);
}

static i32 app_folder(char_ptr_t out, u32 out_cap) {
	char buf[SK_FS_PATH_MAX];

	if (out_cap == 0u) {
		return -1;
	}

#if defined(__APPLE__)
	{
		uint32_t size = (uint32_t)sizeof(buf);
		if (_NSGetExecutablePath(buf, &size) != 0) {
			return -1;
		}
	}
#elif defined(__linux__)
	{
		ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1u);
		if (n < 0) {
			return -1;
		}
		buf[n] = '\0';
	}
#else
	return current_dir(out, out_cap);
#endif

	char* slash = strrchr(buf, '/');
	if (slash != NULL) {
		if (slash == buf) {
			slash[1] = '\0';
		} else {
			*slash = '\0';
		}
	}
	return copy_to_out(out, out_cap, buf);
}

static i32 temp_folder(char_ptr_t out, u32 out_cap) {
	if (out_cap == 0u) {
		return -1;
	}
	if (temp_override_set) {
		return copy_to_out(out, out_cap, temp_override);
	}

	const char* t = getenv("TMPDIR");
	if (t == NULL || t[0] == '\0') {
		t = getenv("TMP");
	}
	if (t == NULL || t[0] == '\0') {
		t = getenv("TEMP");
	}
	if (t == NULL || t[0] == '\0') {
		t = "/tmp";
	}
	return copy_to_out(out, out_cap, t);
}

static sk_file_status_t get_file_status(const_chr_t path) {
	struct stat st;
	if (stat(path, &st) != 0) {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (S_ISREG(st.st_mode)) {
		return SK_FILE_STATUS_FILE;
	}
	if (S_ISDIR(st.st_mode)) {
		return SK_FILE_STATUS_DIRECTORY;
	}
	return SK_FILE_STATUS_OTHER;
}

static u64 get_path_size(const_chr_t path) {
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
		return 0ull;
	}
	if (st.st_size < 0) {
		return 0ull;
	}
	return (u64)st.st_size;
}

static u64 get_file_id(const_chr_t path) {
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0ull;
	}
	/* Combine device + inode into 64 bits (best-effort). */
	return ((u64)st.st_dev << 32) ^ st.st_ino;
}

static i32 create_directory(const_chr_t path) {
	struct stat st;
	if (mkdir(path, 0755) == 0) {
		return 0;
	}
	if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
		return 0;
	}
	return -1;
}

static i32 remove_path(const_chr_t path) {
	struct stat st;
	if (lstat(path, &st) != 0) {
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		return rmdir(path) == 0 ? 0 : -1;
	}
	return unlink(path) == 0 ? 0 : -1;
}

static i32 rename_path(const_chr_t old_name, const_chr_t new_name) {
	return rename(old_name, new_name) == 0 ? 0 : -1;
}

static i32 copy_file(const_chr_t from, const_chr_t to) {
	int in_fd = -1;
	int out_fd = -1;
	char buf[64 * 1024];
	i32 status = -1;

	in_fd = open(from, O_RDONLY);
	if (in_fd < 0) {
		goto done;
	}

	out_fd = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd < 0) {
		goto done;
	}

	for (;;) {
		ssize_t nread = read(in_fd, buf, sizeof(buf));
		if (nread < 0) {
			goto done;
		}
		if (nread == 0) {
			break;
		}
		{
			const char* p = buf;
			ssize_t left = nread;
			while (left > 0) {
				ssize_t nwrite = write(out_fd, p, (size_t)left);
				if (nwrite < 0) {
					goto done;
				}
				p += nwrite;
				left -= nwrite;
			}
		}
	}
	status = 0;

done:
	if (in_fd >= 0) {
		close(in_fd);
	}
	if (out_fd >= 0) {
		close(out_fd);
	}
	return status;
}

static sk_file_handle_t open_file(const_chr_t path, sk_file_access_t access_mode) {
	int flags = 0;

	switch ((int)access_mode) {
	case SK_FILE_ACCESS_READ:
		flags = O_RDONLY;
		break;
	case SK_FILE_ACCESS_WRITE:
		flags = O_WRONLY | O_CREAT | O_TRUNC;
		break;
	case SK_FILE_ACCESS_READ_WRITE:
		flags = O_RDWR | O_CREAT;
		break;
	default:
		return NULL;
	}

	int fd = open(path, flags, 0644);
	if (fd < 0) {
		return NULL;
	}

	sk_fs_file_t* file = (sk_fs_file_t*)malloc(sizeof(sk_fs_file_t));
	if (file == NULL) {
		close(fd);
		return NULL;
	}
	file->fd = fd;
	return (sk_file_handle_t)file;
}

static u64 get_file_size(sk_file_handle_t file) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	struct stat st;
	if (fstat(f->fd, &st) != 0 || st.st_size < 0) {
		return 0ull;
	}
	return (u64)st.st_size;
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
		ssize_t n = write(f->fd, p, left);
		if (n < 0) {
			break;
		}
		if (n == 0) {
			break;
		}
		p += (size_t)n;
		left -= (size_t)n;
		total += (u64)n;
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
		ssize_t n = read(f->fd, p, left);
		if (n < 0) {
			break;
		}
		if (n == 0) {
			break;
		}
		p += (size_t)n;
		left -= (size_t)n;
		total += (u64)n;
	}
	return total;
}

static u64 read_file_at(sk_file_handle_t file, void_ptr_t data, size_t size, u64 offset) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	u64 total = 0ull;

	if (size == 0u) {
		return 0ull;
	}
	if (offset > (u64)LLONG_MAX) {
		return 0ull;
	}
	off_t off = (off_t)offset;

	u8* p = (u8*)data;
	size_t left = size;
	while (left > 0u) {
		ssize_t n = pread(f->fd, p, left, off + (off_t)total);
		if (n < 0) {
			break;
		}
		if (n == 0) {
			break;
		}
		p += (size_t)n;
		left -= (size_t)n;
		total += (u64)n;
	}
	return total;
}

static void close_file(sk_file_handle_t file) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	close(f->fd);
	free(f);
}

static sk_file_handle_t create_file_mapping(sk_file_handle_t file, sk_file_access_t access_mode, u64 size) {
	sk_fs_file_t* f = (sk_fs_file_t*)file;
	int prot = 0;

	if ((access_mode & SK_FILE_ACCESS_READ) != 0) {
		prot |= PROT_READ;
	}
	if ((access_mode & SK_FILE_ACCESS_WRITE) != 0) {
		prot |= PROT_WRITE;
	}
	if (prot == 0) {
		return NULL;
	}

	int dup_fd = dup(f->fd);
	if (dup_fd < 0) {
		return NULL;
	}

	sk_fs_mapping_t* m = (sk_fs_mapping_t*)malloc(sizeof(sk_fs_mapping_t));
	if (m == NULL) {
		close(dup_fd);
		return NULL;
	}
	m->fd = dup_fd;
	m->prot = prot;
	m->flags = ((access_mode & SK_FILE_ACCESS_WRITE) != 0) ? MAP_SHARED : MAP_PRIVATE;
	m->size = size;
	m->view = NULL;
	m->view_size = 0u;
	m->view_active = 0;
	return (sk_file_handle_t)m;
}

static void_ptr_t map_view_of_file(sk_file_handle_t mapping) {
	sk_fs_mapping_t* m = (sk_fs_mapping_t*)mapping;

	u64 map_size = m->size;
	if (map_size == 0ull) {
		struct stat st;
		if (fstat(m->fd, &st) != 0 || st.st_size <= 0) {
			return NULL;
		}
		map_size = (u64)st.st_size;
	}
	if (map_size > (u64)SIZE_MAX) {
		return NULL;
	}

	void* addr = mmap(NULL, (size_t)map_size, m->prot, m->flags, m->fd, 0);
	if (addr == MAP_FAILED) {
		return NULL;
	}

	sk_fs_view_entry_t* entry = (sk_fs_view_entry_t*)malloc(sizeof(sk_fs_view_entry_t));
	if (entry == NULL) {
		munmap(addr, (size_t)map_size);
		return NULL;
	}
	entry->addr = addr;
	entry->size = (size_t)map_size;
	entry->next = view_list;
	view_list = entry;

	m->view = addr;
	m->view_size = (size_t)map_size;
	m->view_active = 1;
	return addr;
}

static i32 unmap_view_of_file(void_ptr_t map) {
	sk_fs_view_entry_t** pp = &view_list;
	while (*pp != NULL) {
		sk_fs_view_entry_t* e = *pp;
		if (e->addr == map) {
			i32 status = munmap(e->addr, e->size) == 0 ? 0 : -1;
			*pp = e->next;
			free(e);
			return status;
		}
		pp = &e->next;
	}
	return -1;
}

static void close_file_mapping(sk_file_handle_t mapping) {
	sk_fs_mapping_t* m = (sk_fs_mapping_t*)mapping;
	if (m->view_active && m->view != NULL) {
		(void)unmap_view_of_file(m->view);
		m->view_active = 0;
	}
	close(m->fd);
	free(m);
}

static sk_directory_iterator_t open_directory(const_chr_t path) {
	DIR* d = opendir(path);
	if (d == NULL) {
		return NULL;
	}

	sk_fs_dir_t* it = (sk_fs_dir_t*)malloc(sizeof(sk_fs_dir_t));
	if (it == NULL) {
		closedir(d);
		return NULL;
	}
	it->dir = d;
	return (sk_directory_iterator_t)it;
}

static i32 next_directory(sk_directory_iterator_t it, char_ptr_t name_out, u32 name_cap) {
	sk_fs_dir_t* d = (sk_fs_dir_t*)it;

	if (name_cap == 0u) {
		return -1;
	}

	struct dirent* ent = readdir(d->dir);
	if (ent == NULL) {
		return 1;
	}

	size_t len = strlen(ent->d_name);
	if (len + 1u > (size_t)name_cap) {
		return -1;
	}
	memcpy(name_out, ent->d_name, len + 1u);
	return 0;
}

static void close_directory(sk_directory_iterator_t it) {
	sk_fs_dir_t* d = (sk_fs_dir_t*)it;
	closedir(d->dir);
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

void sk_filesystem_install(sk_app_context_t* ctx) {
	ctx->filesystem_api = &filesystem_api;
}
