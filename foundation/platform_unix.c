#include "platform.h"

#include "app.h"
#include "internal/app_context.h"
#include "internal/tables.h"

#include <dlfcn.h>
#include <string.h>
#include <time.h>

enum { SK_PLATFORM_ERR_CAP = 512 };

#if defined(__APPLE__)
#define SK_PLATFORM_THREAD_LOCAL __thread
#elif defined(__GNUC__)
#define SK_PLATFORM_THREAD_LOCAL __thread
#else
#define SK_PLATFORM_THREAD_LOCAL
#endif

static SK_PLATFORM_THREAD_LOCAL char platform_err[SK_PLATFORM_ERR_CAP];

static void set_error(const_chr_t msg) {
	strncpy(platform_err, msg, SK_PLATFORM_ERR_CAP - 1);
	platform_err[SK_PLATFORM_ERR_CAP - 1] = '\0';
}

static void set_dl_error(const_chr_t prefix) {
	const char* dl = dlerror();
	if (dl == NULL || dl[0] == '\0') {
		set_error(prefix);
		return;
	}

	/* prefix + ": " + dl, truncated to capacity. */
	size_t i = 0;
	while (prefix[i] != '\0' && i + 1 < SK_PLATFORM_ERR_CAP) {
		platform_err[i] = prefix[i];
		i++;
	}
	if (i + 2 < SK_PLATFORM_ERR_CAP) {
		platform_err[i++] = ':';
		platform_err[i++] = ' ';
	}
	size_t j = 0;
	while (dl[j] != '\0' && i + 1 < SK_PLATFORM_ERR_CAP) {
		platform_err[i++] = dl[j++];
	}
	platform_err[i] = '\0';
}

static sk_shared_lib_t lib_open(const_chr_t path) {
	/* Clear pending dlerror before the call (required by POSIX). */
	(void)dlerror();

	void* handle = dlopen(path, RTLD_NOW);
	if (handle == NULL) {
		set_dl_error("dlopen failed");
		return NULL;
	}

	set_error("");
	return handle;
}

static void_ptr_t lib_symbol(sk_shared_lib_t lib, const_chr_t name) {
	(void)dlerror();
	void* sym = dlsym(lib, name);
	const char* err = dlerror();
	if (err != NULL) {
		set_dl_error("dlsym failed");
		return NULL;
	}
	if (sym == NULL) {
		/* Valid NULL symbol is rare; treat as not found for engine use. */
		set_error("dlsym returned NULL");
		return NULL;
	}

	set_error("");
	return sym;
}

static void lib_close(sk_shared_lib_t lib) {
	(void)dlclose(lib);
}

static const_chr_t lib_error(void) {
	return platform_err;
}

static f64 monotonic_seconds(void) {
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0.0;
	}
	return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1.0e-9;
}

static const sk_platform_api_t platform_api = {
	lib_open, lib_symbol, lib_close, lib_error, monotonic_seconds,
};

void sk_platform_get_api(sk_platform_api_t* out) {
	*out = platform_api;
}

const sk_platform_api_t* sk_platform_api(void) {
	return &platform_api;
}

void sk_platform_install(sk_app_context_t* ctx) {
	ctx->platform = &platform_api;
}
