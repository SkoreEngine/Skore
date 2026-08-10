/* Entry point for the exported game (player host).
 *
 * Port of C++ main `Player/Source/Skore/Main.cpp` render-graph call sites:
 * acquire the render_graph plugin fn table via the app registry (no C++
 * RenderGraph types). Full swapchain + RHI frame execute lands when the
 * player device path is wired; the lookup and pipeline-context ownership
 * pattern matches main's RenderPipelineContext usage today.
 */

#include "app.h"
#include "platform_window.h"
#include "render_graph.h"
#include "render_pipeline.h"

#include <stddef.h>

int main(int argc, char* argv[]) {
	sk_app_context_t* ctx = sk_app_init(argc, argv);
	if (ctx == NULL) {
		return 1;
	}

	/* Plugins auto-loaded from {app_folder}/plugins (window, render_graph, …). */
	const sk_app_api_t* app_api = sk_app_api();
	const sk_platform_window_api_t* win_api = app_api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	const sk_render_graph_api_t* rg_api = sk_render_graph_api_from_app(ctx, app_api);

	if (win_api->init() != 0) {
		sk_app_destroy(ctx);
		return 1;
	}

	/* Require the migrated render_graph plugin (replaces C++ RenderGraph). */
	if (rg_api == NULL || rg_api->create == NULL || rg_api->begin == NULL || rg_api->execute == NULL) {
		sk_app_destroy(ctx);
		return 1;
	}
	if (rg_api->init() != 0) {
		sk_app_destroy(ctx);
		return 1;
	}

	sk_window_t window = win_api->create_window("Skore", 1280u, 720u, (u32)(SK_WINDOW_FLAG_RESIZABLE | SK_WINDOW_FLAG_MAXIMIZED));
	if (window == NULL) {
		rg_api->shutdown();
		sk_app_destroy(ctx);
		return 1;
	}

	/*
	 * Pipeline context owns the graph for the process lifetime (main's
	 * RenderPipelineContext). Graph create needs a render_device handle;
	 * when the player RHI/swapchain path is online, call
	 * sk_render_pipeline_context_create + per-frame
	 * set_current_output_index / set_output_size / Execute as on main.
	 */
	(void)window;

	while (sk_app_tick(ctx)) {
		win_api->poll_events();

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(ctx);
		}
	}

	rg_api->shutdown();
	sk_app_destroy(ctx);
	return 0;
}
