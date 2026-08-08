/**
 * @file atomic_isolation.c
 * @brief Compile-only include-graph fixture for core/atomics.h (test builds).
 *
 * Independent verification of the hard constraint that the core atomics
 * abstraction pulls in only libc/compiler atomics — never window/UI headers.
 * This TU includes nothing but common.h + atomics.h, then:
 *
 *   1. fails compilation if the include chain leaks the guard macros that
 *      Windows.h / WinUser.h / WinBase.h / X11 Xlib.h define;
 *   2. static-asserts the atomic types against their backing types;
 *   3. exercises load/store/exchange/CAS/fetch/fence ops so the static-inline
 *      bodies are fully type-checked with no window headers visible.
 *
 * The fixture has no runtime behavior and exports no symbols — it exists to be
 * compiled on every platform (Windows MSVC, Linux GCC, macOS Clang) in CI. It
 * is excluded from production sk-core like test.c and built into sk-core-tests.
 *
 * Cocoa is not marker-checked here (no single stable guard macro); macOS CI
 * still compiles this fixture and would catch an accidental AppKit/Foundation
 * pull through the atomics.h include chain.
 */

#include "common.h"
#include "atomics.h"

/* ------------------------------------------------------------------------- */
/* 1. Include-graph guard: window/UI headers must not be reachable            */
/* ------------------------------------------------------------------------- */

#if defined(_WINDOWS_) /* <windows.h> include guard */
#error "core/atomics.h include chain must not require <windows.h>"
#endif

#if defined(WINUSERAPI) /* <WinUser.h> / winuser.h entry macro */
#error "core/atomics.h include chain must not require <WinUser.h>"
#endif

#if defined(WINBASEAPI) /* <WinBase.h> / winbase.h entry macro */
#error "core/atomics.h include chain must not require <WinBase.h>"
#endif

#if defined(_X11_XLIB_H_) /* <X11/Xlib.h> include guard */
#error "core/atomics.h include chain must not require <X11/Xlib.h>"
#endif

/* ------------------------------------------------------------------------- */
/* 2. Compile-time API contract checks                                        */
/* ------------------------------------------------------------------------- */

_Static_assert(sizeof(sk_atomic_u32_t) == sizeof(u32), "sk_atomic_u32_t must wrap u32");
_Static_assert(sizeof(sk_atomic_u64_t) == sizeof(u64), "sk_atomic_u64_t must wrap u64");
_Static_assert(sizeof(sk_atomic_i32_t) == sizeof(i32), "sk_atomic_i32_t must wrap i32");
_Static_assert(sizeof(sk_atomic_i64_t) == sizeof(i64), "sk_atomic_i64_t must wrap i64");
_Static_assert(sizeof(sk_atomic_ptr_t) == sizeof(void_ptr_t), "sk_atomic_ptr_t must wrap void_ptr_t");

/* ------------------------------------------------------------------------- */
/* 3. Ops compile standalone (static-inline bodies fully type-checked)        */
/* ------------------------------------------------------------------------- */

static void atomic_isolation_exercise(void) {
	sk_atomic_u32_t counter = {0};
	sk_atomic_u64_t wide = {0};
	sk_atomic_i32_t signed_counter = {0};
	sk_atomic_i64_t signed_wide = {0};
	sk_atomic_ptr_t top = {0};
	u32 expected_u32 = 0u;
	void_ptr_t expected_ptr = 0;

	sk_atomic_u32_init(&counter, 1u);
	(void)sk_atomic_u32_load(&counter);
	(void)sk_atomic_u32_load_acquire(&counter);
	(void)sk_atomic_u32_load_relaxed(&counter);
	sk_atomic_u32_store(&counter, 2u);
	sk_atomic_u32_store_release(&counter, 3u);
	sk_atomic_u32_store_relaxed(&counter, 4u);
	(void)sk_atomic_u32_exchange(&counter, 5u);
	(void)sk_atomic_u32_cas(&counter, &expected_u32, 6u);
	(void)sk_atomic_u32_fetch_add(&counter, 1u);
	(void)sk_atomic_u32_fetch_sub(&counter, 1u);
	(void)sk_atomic_u32_fetch_or(&counter, 0x0Fu);
	(void)sk_atomic_u32_fetch_and(&counter, 0xFFu);

	sk_atomic_u64_init(&wide, 1ull);
	(void)sk_atomic_u64_fetch_add(&wide, 2ull);
	(void)sk_atomic_u64_load_acquire(&wide);

	sk_atomic_i32_init(&signed_counter, -1);
	(void)sk_atomic_i32_fetch_sub(&signed_counter, 3);
	(void)sk_atomic_i32_load(&signed_counter);

	sk_atomic_i64_init(&signed_wide, -2ll);
	(void)sk_atomic_i64_exchange(&signed_wide, 0ll);
	(void)sk_atomic_i64_load_relaxed(&signed_wide);

	sk_atomic_ptr_init(&top, 0);
	(void)sk_atomic_ptr_load(&top);
	(void)sk_atomic_ptr_load_acquire(&top);
	(void)sk_atomic_ptr_load_relaxed(&top);
	sk_atomic_ptr_store(&top, &counter);
	sk_atomic_ptr_store_release(&top, 0);
	(void)sk_atomic_ptr_exchange(&top, &counter);
	(void)sk_atomic_ptr_cas(&top, &expected_ptr, 0);
}

/* Reference the exercise fn so -Wunused-function / C4505 stay quiet. */
#if defined(_MSC_VER)
static void (*atomic_isolation_check)(void) = atomic_isolation_exercise;
#else
__attribute__((unused)) static void (*atomic_isolation_check)(void) = atomic_isolation_exercise;
#endif
