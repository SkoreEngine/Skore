#include "app.h"
#include "common.h"
#include "profiler.h"

/**
 * Plugin load entry: resolved via host platform lib_symbol after app init.
 *
 * Registers sk_profiler_api_t under SK_PROFILER_API_TYPE_ID via
 * sk_profiler_init. Hosts look it up with app_api->get_api; the host's
 * plugin lifecycle (host) then calls init / begin_frame / end_frame /
 * shutdown on the table.
 *
 * @param context Process app context (API registry). Must not be NULL.
 * @param app_api App module table (set_api/get_api). Must not be NULL.
 * @return 0 on success.
 */
SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api);

SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api) {
	sk_profiler_init(context, app_api);
	return 0;
}

#ifdef SK_TESTS
#include "test.h"

/**
 * Plugin-local test entry. Only compiled when SK_TESTS is set (non-Release).
 * Host skips the symbol when missing (Release plugins).
 */
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out);

SK_API i32 sk_plugin_run_tests(sk_test_report_t* out) {
	return sk_test_run_all_status(out);
}
#endif /* SK_TESTS */
