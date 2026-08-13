#pragma once

/**
 * @file test.h
 * @brief In-source unit test registry (SK_TESTS builds only).
 *
 * Production / Release: SK_TESTS is undefined. SK_TEST expands to nothing.
 * Debug (and other non-Release configs when BUILD_TESTING): define SK_TESTS,
 * embed SK_TEST bodies next to production code, auto-register via constructor.
 *
 * Plugins export sk_plugin_run_tests() only when SK_TESTS is set (plugin-local
 * Unity). Host sk-tests runs core/app in-process, then loads each plugin DLL
 * and calls that export if present (skips when missing — e.g. Release).
 * Never ship SK_TESTS into Release plugin/app artifacts.
 *
 * Default `ctest` in skore-test-suite runs unit and integration.
 * GPU / device / UI-capture binaries (`sk-integration-tests`,
 * `sk-text-screenshot`) are registered with label `integration` and skip
 * only when `SK_RUN_INTEGRATION=0` under CTest (`SK_CTEST_GATE`). Direct
 * invocation always runs. Filter a registry with `--filter=` /
 * `SK_TEST_FILTER` (comma-separated exact names or `prefix*` tokens).
 * `--list` / `SK_TEST_LIST=1` prints names and exits.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Aggregate result of one sk_test_run_all / sk_plugin_run_tests invocation. */
typedef struct sk_test_report_t {
	i32 ran;
	i32 failed;
} sk_test_report_t;

/** Symbol name for plugin test entry (GetProcAddress / dlsym). */
#define SK_PLUGIN_RUN_TESTS_NAME "sk_plugin_run_tests"

/**
 * Plugin export (SK_TESTS builds only): run tests registered in this DLL.
 * Not present in Release — host treats a missing symbol as “no plugin tests”.
 * @param out Optional report; may be NULL.
 * @return 0 if failed==0, non-zero if any test failed.
 */
typedef i32 (*sk_plugin_run_tests_fn)(sk_test_report_t* out);

#ifdef SK_TESTS

#include "unity.h"

/**
 * Register a test function (called from SK_TEST constructors).
 * @param name Display name (usually stringified test id).
 * @param fn   Test body.
 */
void sk_test_register(const_chr_t name, void (*fn)(void));

/**
 * Run all tests registered in this module/binary (plugin-local or host).
 * Uses a process-local Unity instance linked with this registry.
 * @param out Optional report; may be NULL.
 */
void sk_test_run_all(sk_test_report_t* out);

/**
 * Same as sk_test_run_all; returns non-zero if any failure.
 * @param out Optional report; may be NULL.
 * @return 0 on success, non-zero if failures.
 */
i32 sk_test_run_all_status(sk_test_report_t* out);

/** CTest SKIP_RETURN_CODE for gated integration binaries. */
enum { SK_TEST_SKIP_CODE = 77 };

/**
 * Parsed argv for sk-tests / sk-integration-tests.
 * @p unknown_opt is set when parse fails on a flag.
 */
typedef struct sk_test_cli_t {
	const_chr_t filter;
	const_chr_t plugins_dir;
	const_chr_t unknown_opt;
	i32 list_only;
	i32 help;
} sk_test_cli_t;

/**
 * Parse host/integration runner flags.
 * @param argc Argument count (including argv[0]).
 * @param argv Argument vector.
 * @param out Filled on return; must not be NULL.
 * @return 0 on success, non-zero if an unknown flag was seen.
 */
i32 sk_test_parse_cli(int argc, char** argv, sk_test_cli_t* out);

/**
 * Print runner usage to stdout.
 * @param prog argv[0] (may be NULL).
 * @param has_plugins_dir Non-zero to document the optional plugins_dir operand.
 */
void sk_test_print_runner_help(const_chr_t prog, i32 has_plugins_dir);

/**
 * Apply --filter / --list to the process environment so this registry and
 * subsequently loaded plugins see the same SK_TEST_FILTER / SK_TEST_LIST.
 * @param cli Parsed CLI; must not be NULL.
 */
void sk_test_apply_cli(const sk_test_cli_t* cli);

/**
 * Set or clear a process environment variable (test-harness helper).
 * @param key Variable name. Must not be NULL.
 * @param value New value, or NULL / empty to unset.
 * @return 0 on success, non-zero on failure.
 */
i32 sk_test_set_env(const_chr_t key, const_chr_t value);

/**
 * Match a test name against a SK_TEST_FILTER string.
 * NULL or empty @p filter matches every name. Tokens are comma-separated
 * exact names or trailing-'*' prefixes (bare "*" matches all).
 * @param name Test name.
 * @param filter Filter string (may be NULL).
 * @return 1 if the name should run, 0 if it should be skipped.
 */
i32 sk_test_name_matches_filter(const_chr_t name, const_chr_t filter);

/**
 * Whether a CTest-gated integration binary should skip.
 * True only when SK_CTEST_GATE is set and SK_RUN_INTEGRATION is explicitly 0.
 * Unset or any other value runs. Direct invocation (no SK_CTEST_GATE) always runs.
 * @return 1 to skip (print a message first), 0 to run.
 */
i32 sk_test_should_skip_integration(void);

/**
 * Map a process exit code for CTest: convert the standalone "no ICD" code 2
 * to SK_TEST_SKIP_CODE when SK_CTEST_GATE is set.
 * @param process_rc Exit code from the real work.
 * @return Code the process should return to CTest.
 */
i32 sk_test_ctest_map_skip(i32 process_rc);

/* ---- constructor registration (MSVC CRT$XCU / GCC constructor) ---- */

#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
#define SK_TEST_CONSTRUCTOR(fn)                                \
	static void fn(void);                                      \
	__declspec(allocate(".CRT$XCU")) void (*fn##_)(void) = fn; \
	static void fn(void)
#else
#define SK_TEST_CONSTRUCTOR(fn)                        \
	static void fn(void) __attribute__((constructor)); \
	static void fn(void)
#endif

/**
 * Define a unit test in the same translation unit as production code.
 * Only compiled when SK_TESTS is defined.
 *
 * Example:
 *   SK_TEST(vec3_dot)
 *   {
 *       TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, sk_vec3_dot(...));
 *   }
 */
#define SK_TEST(name)                               \
	static void sk_test_fn_##name(void);            \
	SK_TEST_CONSTRUCTOR(sk_test_reg_##name) {       \
		sk_test_register(#name, sk_test_fn_##name); \
	}                                               \
	static void sk_test_fn_##name(void)

#else /* !SK_TESTS */

/*
 * SK_TEST is only valid inside `#ifdef SK_TESTS` … `#endif`.
 * Wrap every test section so the body is preprocessor-stripped in Release.
 */
#define SK_TEST(name) static void sk_test_fn_##name##_no_sk_tests(void)

#endif /* SK_TESTS */

#ifdef __cplusplus
}
#endif
