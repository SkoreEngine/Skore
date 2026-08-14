#pragma once

/**
 * Named widget scenes for the lavapipe PNG review loop.
 *
 * Hosts call sandbox_widget_run after creating an offscreen capture at the
 * catalog size. This is not a test: no SK_TEST, no CTest, no vision CLI.
 */

#include "common.h"
#include "filesystem.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Live host pieces shared with the dock sandbox boot path. */
typedef struct sandbox_widget_host_t {
	const sk_ui_api_t* ui;
	const sk_filesystem_api_t* fs;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
	sk_ui_capture_t* capture;
	u32 width;
	u32 height;
} sandbox_widget_host_t;

/**
 * Print the manifest-backed widget catalog (names, states, scene ready?).
 * Does not touch the GPU.
 */
void sandbox_widget_list(void);

/**
 * Resolve a --widget name (aliases allowed). On success writes canvas size
 * and whether a builder exists (1) or the row is still a stub (0).
 * @return 0 if the name is in the catalog, non-zero if unknown.
 */
i32 sandbox_widget_lookup(const_chr_t name, u32* out_width, u32* out_height, i32* out_ready);

/**
 * Build the named widget, apply each requested state, and write
 * `{widget}_{state}.png` under @p out_dir via cpu_image_write_png.
 * @p state_filter NULL or "all" writes every applicable state.
 * @return 0 on success, non-zero if the widget has no scene or capture fails.
 */
i32 sandbox_widget_run(const sandbox_widget_host_t* host, const_chr_t name, const_chr_t state_filter, const_chr_t out_dir);

#ifdef __cplusplus
}
#endif
