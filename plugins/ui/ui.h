#pragma once

/**
 * @file ui.h
 * @brief UI module API (scaffold).
 *
 * Implemented by the sk-ui plugin (SHARED, statically linked sk-core).
 * The plugin registers a static sk_ui_api_t on the app context; hosts
 * obtain it **only** via the app registry:
 *
 *   const sk_ui_api_t* ui =
 *       (const sk_ui_api_t*)app_api->get_api(ctx, SK_UI_API_TYPE_ID);
 *
 * This is a scaffold only: the retained element tree, flexbox layout, text
 * pipeline (FreeType + stb_rect_pack), and draw list land in later tasks.
 * init is a stub that currently always succeeds.
 */

#include "app.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_ui_api_t in the app registry. */
#define SK_UI_API_TYPE_ID SK_TYPE_ID("sk.ui_api", 0xc9c0d15c0efdbacbULL, 0x2376391989195a63ULL)

/**
 * Global UI module API (one table per process after plugin load).
 * Skeleton surface; entries are added as UI logic lands.
 */
typedef struct sk_ui_api_t {
	/**
	 * Initialize the UI module.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*init)(void);

	/**
	 * Shutdown the UI module. Safe to call multiple times;
	 * no-op when init never succeeded.
	 */
	void (*shutdown)(void);
} sk_ui_api_t;

/**
 * Register the static sk_ui_api_t on the app context.
 * Called from sk_plugin_entry_point.
 * @param context App context (must not be NULL).
 * @param app_api App module table (must not be NULL).
 */
void sk_ui_init(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
