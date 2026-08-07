#include "platform.h"

#include "app.h"

/* Windows-only backend (app/CMakeLists.txt only compiles this TU on WIN32).
 * Guard so Linux/macOS clang-tidy can parse the file without a Windows SDK. */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <string.h>

enum { SK_PLATFORM_ERR_CAP = 512 };

static __declspec(thread) char platform_err[SK_PLATFORM_ERR_CAP];

static void set_error(const_chr_t msg) {
	strncpy(platform_err, msg, SK_PLATFORM_ERR_CAP - 1);
	platform_err[SK_PLATFORM_ERR_CAP - 1] = '\0';
}

static void set_win32_error(const_chr_t prefix) {
	DWORD err = GetLastError();
	char sys_buf[256];
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), sys_buf, (DWORD)sizeof(sys_buf),
							 NULL);

	if (n == 0) {
		snprintf(platform_err, sizeof(platform_err), "%s (GetLastError=%lu)", prefix, err);
		return;
	}

	/* Trim trailing CR/LF from FormatMessage. */
	while (n > 0 && (sys_buf[n - 1] == '\r' || sys_buf[n - 1] == '\n')) {
		sys_buf[--n] = '\0';
	}

	snprintf(platform_err, sizeof(platform_err), "%s: %s (GetLastError=%lu)", prefix, sys_buf, err);
}

static sk_shared_lib_t lib_open(const_chr_t path) {
	HMODULE mod = LoadLibraryA(path);
	if (mod == NULL) {
		set_win32_error("LoadLibraryA failed");
		return NULL;
	}

	set_error("");
	/* HMODULE is void* on Win32; sk_shared_lib_t is the same opaque handle type. */
	return mod;
}

static void_ptr_t lib_symbol(sk_shared_lib_t lib, const_chr_t name) {
	FARPROC sym = GetProcAddress((HMODULE)lib, name);
	if (sym == NULL) {
		set_win32_error("GetProcAddress failed");
		return NULL;
	}

	set_error("");
	/* FARPROC is a function pointer; round-trip via uintptr_t for void*. */
	return (void_ptr_t)(uintptr_t)sym;
}

static void lib_close(sk_shared_lib_t lib) {
	FreeLibrary((HMODULE)lib);
}

static const_chr_t lib_error(void) {
	return platform_err;
}

static f64 monotonic_seconds(void) {
	static LARGE_INTEGER freq = {0};
	LARGE_INTEGER now;

	if (freq.QuadPart == 0) {
		if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
			return 0.0;
		}
	}
	if (!QueryPerformanceCounter(&now)) {
		return 0.0;
	}
	return (f64)now.QuadPart / (f64)freq.QuadPart;
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

/**
 * Register the default platform API on the app context.
 * Not declared in core headers; called from sk-app initialization.
 */
void sk_platform_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_platform_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_PLATFORM_API_TYPE_ID, sk_platform_api());
}

#endif /* _WIN32 */
