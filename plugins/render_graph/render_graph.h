#pragma once

/**
 * @file render_graph.h
 * @brief Render graph module API (skeleton).
 *
 * Implemented by the sk-render-graph plugin (SHARED, statically linked
 * sk-core). The plugin registers a static sk_render_graph_api_t on the app
 * context; hosts obtain it **only** via the app registry:
 *
 *   const sk_render_graph_api_t* graph =
 *       (const sk_render_graph_api_t*)app_api->get_api(
 *           ctx, SK_RENDER_GRAPH_API_TYPE_ID);
 *
 * This is a scaffold only: the API surface and full graph logic land in later
 * tasks. init is a stub that currently always succeeds.
 */

#include "app.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_render_graph_api_t in the app registry. */
#define SK_RENDER_GRAPH_API_TYPE_ID SK_TYPE_ID("sk.render_graph_api", 0xc77df474a355fb80ULL, 0x11b457b7c17d0220ULL)

/**
 * Global render-graph module API (one table per process after plugin load).
 * Skeleton surface; entries are added as the graph logic lands.
 */
typedef struct sk_render_graph_api_t {
	/**
	 * Initialize the render graph module.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*init)(void);

	/**
	 * Shutdown the render graph module. Safe to call multiple times;
	 * no-op when init never succeeded.
	 */
	void (*shutdown)(void);
} sk_render_graph_api_t;

/**
 * Register the static sk_render_graph_api_t on the app context.
 * Called from sk_plugin_entry_point.
 * @param context App context (must not be NULL).
 * @param app_api App module table (must not be NULL).
 */
void sk_render_graph_init(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
