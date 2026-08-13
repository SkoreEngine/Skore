#include "app.h"
#include "common.h"
#include "jolt.h"

/* Defined in jolt.cpp; registers the static API table on the app. */
void sk_jolt_init(sk_app_context_t* context, const sk_app_api_t* app_api);

/**
 * Plugin load entry: resolved via host platform lib_symbol after app init.
 * extern "C" keeps the symbol unmangled for dlsym/GetProcAddress.
 *
 * Registers sk_jolt_api_t under SK_JOLT_API_TYPE_ID via sk_jolt_init. Hosts
 * look it up with app_api->get_api.
 *
 * @param context Process app context (API registry). Must not be NULL.
 * @param app_api App module table (set_api/get_api). Must not be NULL.
 * @return 0 on success.
 */
extern "C" SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api);

extern "C" SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api) {
	sk_jolt_init(context, app_api);
	return 0;
}
