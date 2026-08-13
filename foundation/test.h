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
