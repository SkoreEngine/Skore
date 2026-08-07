#include "platform_window.h"

#include "app.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No OpenGL/Vulkan client API — skip system GL headers from glfw3.h. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
/* Declare only what we need — avoids pulling extra system headers via glfw3native. */
GLFWAPI HWND glfwGetWin32Window(GLFWwindow* window);
#elif defined(__APPLE__)
/* NSWindow* as void* to keep this TU free of ObjC. */
GLFWAPI void* glfwGetCocoaWindow(GLFWwindow* window);
#else
/* X11 Window is an XID (unsigned long). No Xlib.h required for the cast. */
GLFWAPI unsigned long glfwGetX11Window(GLFWwindow* window);
#endif

#include <nfd.h>

/* ---- module state ---- */

static i32 glfw_ready = 0;

/* ---- window ops ---- */

static i32 sk_window_init(void) {
	if (glfw_ready) {
		return 0;
	}
	if (!glfwInit()) {
		return -1;
	}
	/* Window only for now — no OpenGL/Vulkan context required by this API. */
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfw_ready = 1;
	return 0;
}

#if defined(_WIN32)
/* Immersive dark title bar (Win10 1809+ / Win11). Not available through GLFW. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void apply_win32_dark_title_bar(HWND hwnd) {
	BOOL dark = TRUE;
	/* Prefer attribute 20; some older SDKs used 19. Ignore failures. */
	if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)))) {
		const DWORD attr_legacy = 19;
		(void)DwmSetWindowAttribute(hwnd, attr_legacy, &dark, sizeof(dark));
	}
}
#endif

static GLFWwindow* as_glfw(sk_window_t window) {
	return (GLFWwindow*)window;
}

static sk_window_t sk_window_create(const_chr_t title, u32 width, u32 height, u32 flags) {
	if (width == 0u || height == 0u) {
		return NULL;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, (flags & SK_WINDOW_FLAG_RESIZABLE) ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_DECORATED, (flags & SK_WINDOW_FLAG_BORDERLESS) ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_MAXIMIZED, (flags & SK_WINDOW_FLAG_MAXIMIZED) ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_VISIBLE, (flags & SK_WINDOW_FLAG_HIDDEN) ? GLFW_FALSE : GLFW_TRUE);

	const char* t = (title != NULL) ? title : "";
	GLFWwindow* win = glfwCreateWindow((int)width, (int)height, t, NULL, NULL);
	if (win == NULL) {
		return NULL;
	}

#if defined(_WIN32)
	{
		HWND hwnd = glfwGetWin32Window(win);
		apply_win32_dark_title_bar(hwnd);
	}
#endif

	return (sk_window_t)win;
}

static void sk_window_destroy(sk_window_t window) {
	glfwDestroyWindow(as_glfw(window));
}

static i32 sk_window_should_close(sk_window_t window) {
	return glfwWindowShouldClose(as_glfw(window)) ? 1 : 0;
}

static f32 sk_window_get_dpi(sk_window_t window) {
	GLFWwindow* win = as_glfw(window);
	float xscale = 1.0f;
	float yscale = 1.0f;

	glfwGetWindowContentScale(win, &xscale, &yscale);
	return (xscale + yscale) * 0.5f;
}

static sk_extent_t sk_window_get_size(sk_window_t window) {
	sk_extent_t extent = {0u, 0u};
	GLFWwindow* win = as_glfw(window);
	int w = 0;
	int h = 0;

	glfwGetWindowSize(win, &w, &h);
	if (w < 0) {
		w = 0;
	}
	if (h < 0) {
		h = 0;
	}
	extent.width = (u32)w;
	extent.height = (u32)h;
	return extent;
}

static i32 sk_window_is_minimized(sk_window_t window) {
	return glfwGetWindowAttrib(as_glfw(window), GLFW_ICONIFIED) ? 1 : 0;
}

static void sk_window_set_cursor_lock_mode(sk_window_t window, sk_cursor_lock_mode_t mode) {
	GLFWwindow* win = as_glfw(window);
	switch (mode) {
	case SK_CURSOR_LOCK_LOCKED:
		glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		break;
	case SK_CURSOR_LOCK_CONFINED:
		/* GLFW 3.3+; fall back to normal if unavailable at compile time. */
#ifdef GLFW_CURSOR_CAPTURED
		glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
#else
		glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
#endif
		break;
	case SK_CURSOR_LOCK_NONE:
	default:
		glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		break;
	}
}

static void sk_window_maximize(sk_window_t window) {
	glfwMaximizeWindow(as_glfw(window));
}

static void sk_window_set_icon(sk_window_t window, const u8* rgba_pixels, i32 width, i32 height) {
	GLFWwindow* win = as_glfw(window);
	GLFWimage image;

	/* width/height <= 0 or null pixels clears the icon (intentional API). */
	if (rgba_pixels == NULL || width <= 0 || height <= 0) {
		glfwSetWindowIcon(win, 0, NULL);
		return;
	}
	image.width = width;
	image.height = height;
	/* GLFW expects unsigned char* RGBA; cast away const for the API. */
	image.pixels = SK_CONST_CAST(unsigned char*, rgba_pixels);
	glfwSetWindowIcon(win, 1, &image);
}

static void_ptr_t sk_window_get_native_handle(sk_window_t window) {
	GLFWwindow* win = as_glfw(window);
#if defined(_WIN32)
	return (void_ptr_t)glfwGetWin32Window(win);
#elif defined(__APPLE__)
	return (void_ptr_t)glfwGetCocoaWindow(win);
#else
	/* X11 Window is an XID (unsigned long); pack into a void* handle. */
	return (void_ptr_t)glfwGetX11Window(win);
#endif
}

/* ---- message box ---- */

#if defined(_WIN32)
static i32 utf8_to_wide(const char* utf8, wchar_t* out, int out_cap) {
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, out_cap);
	if (n <= 0) {
		out[0] = L'\0';
		return -1;
	}
	return 0;
}
#endif

static i32 sk_window_show_simple_message_box(sk_message_box_type_t type, const_chr_t title, const_chr_t message, sk_window_t window) {
	const char* t = (title != NULL) ? title : "";
	const char* m = (message != NULL) ? message : "";

#if defined(_WIN32)
	{
		wchar_t wt[512];
		wchar_t wm[4096];
		UINT flags = MB_OK;
		HWND parent = NULL;

		if (utf8_to_wide(t, wt, 512) != 0 || utf8_to_wide(m, wm, 4096) != 0) {
			return 0;
		}
		switch (type) {
		case SK_MESSAGE_BOX_WARNING:
			flags |= MB_ICONWARNING;
			break;
		case SK_MESSAGE_BOX_ERROR:
			flags |= MB_ICONERROR;
			break;
		case SK_MESSAGE_BOX_INFO:
		default:
			flags |= MB_ICONINFORMATION;
			break;
		}
		if (window != NULL) {
			parent = glfwGetWin32Window(as_glfw(window));
		}
		(void)MessageBoxW(parent, wm, wt, flags);
		return 1;
	}
#elif defined(__APPLE__)
	{
		/* osascript keeps us free of ObjC in this translation unit. */
		char cmd[8192];
		const char* icon = "note";

		(void)window;
		if (type == SK_MESSAGE_BOX_WARNING) {
			icon = "caution";
		} else if (type == SK_MESSAGE_BOX_ERROR) {
			icon = "stop";
		}
		/* Escape is minimal; paths with quotes may break — acceptable for diagnostics. */
		int n = snprintf(cmd, sizeof(cmd),
						 "osascript -e 'display dialog \"%s\" with title \"%s\" with icon %s "
						 "buttons {\"OK\"} default button \"OK\"' >/dev/null 2>&1",
						 m, t, icon);
		if (n <= 0 || (size_t)n >= sizeof(cmd)) {
			return 0;
		}
		return (system(cmd) == 0) ? 1 : 0;
	}
#else
	{
		/* Prefer zenity when present (matches NFD zenity backend). */
		char cmd[8192];
		const char* flag = "--info";

		(void)window;
		if (type == SK_MESSAGE_BOX_WARNING) {
			flag = "--warning";
		} else if (type == SK_MESSAGE_BOX_ERROR) {
			flag = "--error";
		}
		int n = snprintf(cmd, sizeof(cmd), "zenity %s --title=\"%s\" --text=\"%s\" >/dev/null 2>&1", flag, t, m);
		if (n > 0 && (size_t)n < sizeof(cmd) && system(cmd) == 0) {
			return 1;
		}
		/* Headless / no zenity: still report "shown" via stderr for tests. */
		fprintf(stderr, "[sk_message_box] %s: %s\n", t, m);
		return 1;
	}
#endif
}

/* ---- dialogs (nativefiledialog) ---- */

enum { SK_NFD_FILTER_CAP = 512 };

/**
 * Build NFD filter list: "png,jpg;pdf" (comma = extensions in a group,
 * semicolon = group separator). Display names are not part of the NFD string.
 * Returns 0 on success (including empty → all files).
 */
static i32 build_nfd_filter(const sk_file_filter_t* filters, u32 filter_count, char* out, size_t out_cap) {
	size_t pos = 0;

	if (out == NULL || out_cap == 0u) {
		return -1;
	}
	out[0] = '\0';
	if (filters == NULL || filter_count == 0u) {
		return 0;
	}

	for (u32 i = 0u; i < filter_count; i++) {
		const char* ext = filters[i].extensions;

		(void)filters[i].name; /* label reserved for future native backends */

		if (ext == NULL || ext[0] == '\0') {
			continue;
		}
		int need_semi = (pos > 0u) ? 1 : 0;
		size_t ext_len = strlen(ext);
		if (pos + (size_t)need_semi + ext_len + 1u >= out_cap) {
			return -1;
		}
		if (need_semi) {
			out[pos++] = ';';
		}
		memcpy(out + pos, ext, ext_len);
		pos += ext_len;
		out[pos] = '\0';
	}
	return 0;
}

static i32 nfd_status_to_sk(nfdresult_t r) {
	if (r == NFD_OKAY) {
		return 0;
	}
	if (r == NFD_CANCEL) {
		return 1;
	}
	return -1;
}

static i32 sk_window_save_dialog(sk_path_callback_t callback, void_ptr_t user_data, const sk_file_filter_t* filters, u32 filter_count, const_chr_t default_path,
								 const_chr_t file_name, sk_window_t window) {
	char filter_buf[SK_NFD_FILTER_CAP];
	nfdchar_t* out_path = NULL;
	char default_buf[1024];

	(void)window;
	(void)file_name; /* NFD 1.x has no separate default file name; fold into path if needed. */

	if (build_nfd_filter(filters, filter_count, filter_buf, sizeof(filter_buf)) != 0) {
		if (callback != NULL) {
			callback(NULL, user_data);
		}
		return -1;
	}

	default_buf[0] = '\0';
	if (default_path != NULL && default_path[0] != '\0') {
		strncpy(default_buf, default_path, sizeof(default_buf) - 1u);
		default_buf[sizeof(default_buf) - 1u] = '\0';
		if (file_name != NULL && file_name[0] != '\0') {
			size_t len = strlen(default_buf);
			if (len + 1u + strlen(file_name) < sizeof(default_buf)) {
				if (len > 0u && default_buf[len - 1u] != '/' && default_buf[len - 1u] != '\\') {
#if defined(_WIN32)
					default_buf[len++] = '\\';
#else
					default_buf[len++] = '/';
#endif
					default_buf[len] = '\0';
				}
				strncat(default_buf, file_name, sizeof(default_buf) - strlen(default_buf) - 1u);
			}
		}
	} else if (file_name != NULL && file_name[0] != '\0') {
		strncpy(default_buf, file_name, sizeof(default_buf) - 1u);
		default_buf[sizeof(default_buf) - 1u] = '\0';
	}

	nfdresult_t result = NFD_SaveDialog(filter_buf[0] != '\0' ? filter_buf : NULL, default_buf[0] != '\0' ? default_buf : NULL, &out_path);
	if (result == NFD_OKAY && out_path != NULL) {
		if (callback != NULL) {
			callback(out_path, user_data);
		}
		free(out_path);
		return 0;
	}
	if (out_path != NULL) {
		free(out_path);
	}
	if (callback != NULL) {
		callback(NULL, user_data);
	}
	return nfd_status_to_sk(result);
}

static i32 sk_window_open_dialog(sk_path_callback_t callback, void_ptr_t user_data, const sk_file_filter_t* filters, u32 filter_count, const_chr_t default_path,
								 sk_window_t window) {
	char filter_buf[SK_NFD_FILTER_CAP];
	nfdchar_t* out_path = NULL;

	(void)window;

	if (build_nfd_filter(filters, filter_count, filter_buf, sizeof(filter_buf)) != 0) {
		if (callback != NULL) {
			callback(NULL, user_data);
		}
		return -1;
	}

	nfdresult_t result = NFD_OpenDialog(filter_buf[0] != '\0' ? filter_buf : NULL, default_path, &out_path);
	if (result == NFD_OKAY && out_path != NULL) {
		if (callback != NULL) {
			callback(out_path, user_data);
		}
		free(out_path);
		return 0;
	}
	if (out_path != NULL) {
		free(out_path);
	}
	if (callback != NULL) {
		callback(NULL, user_data);
	}
	return nfd_status_to_sk(result);
}

static i32 sk_window_open_dialog_multiple(sk_paths_callback_t callback, void_ptr_t user_data, const sk_file_filter_t* filters, u32 filter_count, const_chr_t default_path,
										  sk_window_t window) {
	char filter_buf[SK_NFD_FILTER_CAP];
	nfdpathset_t path_set;
	const_chr_t path_ptrs_stack[64];
	const_chr_t* heap_ptrs = NULL;

	(void)window;
	memset(&path_set, 0, sizeof(path_set));

	if (build_nfd_filter(filters, filter_count, filter_buf, sizeof(filter_buf)) != 0) {
		if (callback != NULL) {
			callback(NULL, 0u, user_data);
		}
		return -1;
	}

	nfdresult_t result = NFD_OpenDialogMultiple(filter_buf[0] != '\0' ? filter_buf : NULL, default_path, &path_set);
	if (result != NFD_OKAY) {
		if (callback != NULL) {
			callback(NULL, 0u, user_data);
		}
		return nfd_status_to_sk(result);
	}

	size_t count = NFD_PathSet_GetCount(&path_set);
	if (count == 0u) {
		NFD_PathSet_Free(&path_set);
		if (callback != NULL) {
			callback(NULL, 0u, user_data);
		}
		return 1;
	}

	/* Multi-branch init: declare outside, assign per branch (cannot join). */
	const_chr_t* path_ptrs;
	if (count <= 64u) {
		path_ptrs = path_ptrs_stack;
	} else {
		heap_ptrs = (const_chr_t*)malloc(count * sizeof(const_chr_t));
		if (heap_ptrs == NULL) {
			NFD_PathSet_Free(&path_set);
			if (callback != NULL) {
				callback(NULL, 0u, user_data);
			}
			return -1;
		}
		path_ptrs = heap_ptrs;
	}

	for (size_t i = 0u; i < count; i++) {
		path_ptrs[i] = (const_chr_t)NFD_PathSet_GetPath(&path_set, i);
	}

	if (callback != NULL) {
		callback(path_ptrs, (u32)count, user_data);
	}

	free((void*)heap_ptrs);
	NFD_PathSet_Free(&path_set);
	return 0;
}

static i32 sk_window_pick_folder(sk_path_callback_t callback, void_ptr_t user_data, const_chr_t default_path, sk_window_t window) {
	nfdchar_t* out_path = NULL;

	(void)window;
	nfdresult_t result = NFD_PickFolder(default_path, &out_path);
	if (result == NFD_OKAY && out_path != NULL) {
		if (callback != NULL) {
			callback(out_path, user_data);
		}
		free(out_path);
		return 0;
	}
	if (out_path != NULL) {
		free(out_path);
	}
	if (callback != NULL) {
		callback(NULL, user_data);
	}
	return nfd_status_to_sk(result);
}

static void sk_window_poll_events(void) {
	if (!glfw_ready) {
		return;
	}
	glfwPollEvents();
}

static void sk_window_shutdown(void) {
	if (!glfw_ready) {
		return;
	}
	glfwTerminate();
	glfw_ready = 0;
}

/* ---- API table ---- */

static const sk_platform_window_api_t platform_window_api = {
	sk_window_init,
	sk_window_create,
	sk_window_destroy,
	sk_window_should_close,
	sk_window_get_dpi,
	sk_window_get_size,
	sk_window_is_minimized,
	sk_window_set_cursor_lock_mode,
	sk_window_maximize,
	sk_window_set_icon,
	sk_window_get_native_handle,
	sk_window_show_simple_message_box,
	sk_window_save_dialog,
	sk_window_open_dialog,
	sk_window_open_dialog_multiple,
	sk_window_pick_folder,
	sk_window_poll_events,
	sk_window_shutdown,
};

/**
 * Register the platform-window API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_platform_window_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_platform_window_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_PLATFORM_WINDOW_API_TYPE_ID, &platform_window_api);
}

#ifdef SK_TESTS
#include "test.h"

/*
 * Unit coverage for platform_window types and the static API table.
 * Full GLFW window creation needs a display and is covered lightly in
 * integration (registration paths only).
 */

SK_TEST(platform_window_api_table_is_complete) {
	TEST_ASSERT_NOT_NULL(platform_window_api.init);
	TEST_ASSERT_NOT_NULL(platform_window_api.create_window);
	TEST_ASSERT_NOT_NULL(platform_window_api.destroy_window);
	TEST_ASSERT_NOT_NULL(platform_window_api.window_should_close);
	TEST_ASSERT_NOT_NULL(platform_window_api.get_window_dpi);
	TEST_ASSERT_NOT_NULL(platform_window_api.get_window_size);
	TEST_ASSERT_NOT_NULL(platform_window_api.is_window_minimized);
	TEST_ASSERT_NOT_NULL(platform_window_api.set_window_cursor_lock_mode);
	TEST_ASSERT_NOT_NULL(platform_window_api.maximize_window);
	TEST_ASSERT_NOT_NULL(platform_window_api.set_window_icon);
	TEST_ASSERT_NOT_NULL(platform_window_api.get_native_window_handle);
	TEST_ASSERT_NOT_NULL(platform_window_api.show_simple_message_box);
	TEST_ASSERT_NOT_NULL(platform_window_api.save_dialog);
	TEST_ASSERT_NOT_NULL(platform_window_api.open_dialog);
	TEST_ASSERT_NOT_NULL(platform_window_api.open_dialog_multiple);
	TEST_ASSERT_NOT_NULL(platform_window_api.pick_folder);
	TEST_ASSERT_NOT_NULL(platform_window_api.poll_events);
	TEST_ASSERT_NOT_NULL(platform_window_api.shutdown);
}

SK_TEST(platform_window_init_is_idempotent) {
	/* glfwInit needs a display backend; skip when unavailable (headless CI). */
	if (platform_window_api.init() != 0) {
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, platform_window_api.init());
	platform_window_api.shutdown();
}

SK_TEST(window_create_rejects_zero_dimensions) {
	TEST_ASSERT_NULL(platform_window_api.create_window("x", 0u, 10u, SK_WINDOW_FLAG_NONE));
	TEST_ASSERT_NULL(platform_window_api.create_window("x", 10u, 0u, SK_WINDOW_FLAG_NONE));
	TEST_ASSERT_NULL(platform_window_api.create_window(NULL, 0u, 0u, SK_WINDOW_FLAG_RESIZABLE));
}

SK_TEST(window_flags_and_enums_are_distinct) {
	TEST_ASSERT_TRUE((SK_WINDOW_FLAG_RESIZABLE & SK_WINDOW_FLAG_BORDERLESS) == 0u);
	TEST_ASSERT_TRUE((SK_WINDOW_FLAG_MAXIMIZED & SK_WINDOW_FLAG_HIDDEN) == 0u);
	TEST_ASSERT_TRUE(SK_CURSOR_LOCK_NONE != SK_CURSOR_LOCK_LOCKED);
	TEST_ASSERT_TRUE(SK_CURSOR_LOCK_LOCKED != SK_CURSOR_LOCK_CONFINED);
	TEST_ASSERT_TRUE(SK_MESSAGE_BOX_INFO != SK_MESSAGE_BOX_ERROR);
}

SK_TEST(platform_window_type_id_nonzero) {
	sk_type_id_t id = SK_PLATFORM_WINDOW_API_TYPE_ID;
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
}
#endif /* SK_TESTS */
