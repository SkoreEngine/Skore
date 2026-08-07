/* Entry point for the exported game (player host). */

#include "app.h"
#include "platform_window.h"

#include <stddef.h>

int main(int argc, char* argv[]) {
	sk_app_context_t* ctx = sk_app_init(argc, argv);
	if (ctx == NULL) {
		return 1;
	}

	/* Platform window plugin is auto-loaded from {app_folder}/plugins. */
	const sk_app_api_t* app_api = sk_app_api();
	const sk_platform_window_api_t* win_api = app_api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);

	if (win_api->init() != 0) {
		sk_app_destroy(ctx);
		return 1;
	}

	sk_window_t window = win_api->create_window("Skore", 1280u, 720u, (u32)(SK_WINDOW_FLAG_RESIZABLE | SK_WINDOW_FLAG_MAXIMIZED));
	if (window == NULL) {
		sk_app_destroy(ctx);
		return 1;
	}

	while (sk_app_tick(ctx)) {
		win_api->poll_events();

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(ctx);
		}
	}

	sk_app_destroy(ctx);
	return 0;
}
