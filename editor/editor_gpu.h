#pragma once

/**
 * @file editor_gpu.h
 * @brief Windowed sk-ui present for editor hosts (swapchain + UI renderer).
 *
 * Same pipeline as player/main.c: acquire swapchain image, renderer_prepare
 * outside the pass, renderer_encode inside, submit, present. Used by
 * --ui-migration and --shell so the OS window is not left blank.
 */

#include "app.h"
#include "logger.h"
#include "platform_window.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_editor_gpu_t sk_editor_gpu_t;

/**
 * Create a windowed UI present path (device, swapchain, UI renderer).
 * @param app        Live app context (render device / DXC come from the registry).
 * @param app_api    App API table.
 * @param ui         Live sk-ui table (renderer_create).
 * @param win_api    Platform window table (framebuffer size).
 * @param window     Window the swapchain is created against.
 * @param logger_api Optional logger table (NULL skips logs).
 * @param log        Optional logger instance.
 * @return GPU state, or NULL if the device / swapchain / renderer failed.
 */
sk_editor_gpu_t* sk_editor_gpu_create(sk_app_context_t* app, const sk_app_api_t* app_api, const sk_ui_api_t* ui, const sk_platform_window_api_t* win_api, sk_window_t window,
									  const sk_logger_api_t* logger_api, sk_logger_t* log);

/** Destroy GPU resources. Safe on NULL. */
void sk_editor_gpu_destroy(sk_editor_gpu_t* gpu);

/**
 * Encode the last sk-ui draw list onto the swapchain and present.
 * @param gpu     From sk_editor_gpu_create.
 * @param ctx     UI context whose get_draw_list is encoded.
 * @param fonts   Optional font system (MSDF / FONT cmds).
 * @param win_api Window table (resize / framebuffer size).
 * @param window  Same window passed to create.
 * @return 0 on success, non-zero if the frame was skipped or failed.
 */
i32 sk_editor_gpu_present(sk_editor_gpu_t* gpu, sk_ui_context_t* ctx, sk_ui_font_system_t* fonts, const sk_platform_window_api_t* win_api, sk_window_t window);

#ifdef __cplusplus
}
#endif
