/**
 * @file atomic_isolation.c
 * @brief Compile-only include-graph fixture for core/atomic.h (test builds).
 *
 * Independent verification of the hard constraint that the core atomics
 * abstraction pulls in only libc/compiler atomics — never window/UI headers.
 * This TU includes nothing but common.h + atomic.h, then:
 *
 *   1. fails compilation if the include chain leaks the guard macros that
 *      Windows.h / WinUser.h / WinBase.h / X11 Xlib.h define;
 *   2. static-asserts the atomic types and memory-order mapping against the
 *      C11 <stdatomic.h> surfaces atomic.h wraps;
 *   3. exercises load/store/exchange/fetch/CAS/fence ops so the static-inline
 *      bodies are fully type-checked with no window headers visible.
 *
 * The fixture has no runtime behavior and exports no symbols — it exists to be
 * compiled on every platform (Windows MSVC, Linux GCC, macOS Clang) in CI. It
 * is excluded from production sk-core like test.c and built into sk-core-tests.
 *
 * Cocoa is not marker-checked here (no single stable guard macro); macOS CI
 * still compiles this fixture and would catch an accidental AppKit/Foundation
 * pull through the atomic.h include chain.
 */

#include "common.h"
#include "atomic.h"

/* ------------------------------------------------------------------------- */
/* 1. Include-graph guard: window/UI headers must not be reachable            */
/* ------------------------------------------------------------------------- */

#if defined(_WINDOWS_) /* <windows.h> include guard */
#error "core/atomic.h include chain must not require <windows.h>"
#endif

#if defined(WINUSERAPI) /* <WinUser.h> / winuser.h entry macro */
#error "core/atomic.h include chain must not require <WinUser.h>"
#endif

#if defined(WINBASEAPI) /* <WinBase.h> / winbase.h entry macro */
#error "core/atomic.h include chain must not require <WinBase.h>"
#endif

#if defined(_X11_XLIB_H_) /* <X11/Xlib.h> include guard */
#error "core/atomic.h include chain must not require <X11/Xlib.h>"
#endif

/* ------------------------------------------------------------------------- */
/* 2. Compile-time API contract checks                                        */
/* ------------------------------------------------------------------------- */

_Static_assert(sizeof(sk_atomic_u8_t) == sizeof(u8), "sk_atomic_u8_t must wrap u8");
_Static_assert(sizeof(sk_atomic_u16_t) == sizeof(u16), "sk_atomic_u16_t must wrap u16");
_Static_assert(sizeof(sk_atomic_u32_t) == sizeof(u32), "sk_atomic_u32_t must wrap u32");
_Static_assert(sizeof(sk_atomic_u64_t) == sizeof(u64), "sk_atomic_u64_t must wrap u64");
_Static_assert(sizeof(sk_atomic_i8_t) == sizeof(i8), "sk_atomic_i8_t must wrap i8");
_Static_assert(sizeof(sk_atomic_i16_t) == sizeof(i16), "sk_atomic_i16_t must wrap i16");
_Static_assert(sizeof(sk_atomic_i32_t) == sizeof(i32), "sk_atomic_i32_t must wrap i32");
_Static_assert(sizeof(sk_atomic_i64_t) == sizeof(i64), "sk_atomic_i64_t must wrap i64");

_Static_assert((int)SK_MEMORY_ORDER_RELAXED == (int)memory_order_relaxed, "relaxed must map to C11");
_Static_assert((int)SK_MEMORY_ORDER_CONSUME == (int)memory_order_consume, "consume must map to C11");
_Static_assert((int)SK_MEMORY_ORDER_ACQUIRE == (int)memory_order_acquire, "acquire must map to C11");
_Static_assert((int)SK_MEMORY_ORDER_RELEASE == (int)memory_order_release, "release must map to C11");
_Static_assert((int)SK_MEMORY_ORDER_ACQ_REL == (int)memory_order_acq_rel, "acq_rel must map to C11");
_Static_assert((int)SK_MEMORY_ORDER_SEQ_CST == (int)memory_order_seq_cst, "seq_cst must map to C11");

/* ------------------------------------------------------------------------- */
/* 3. Ops compile standalone (static-inline bodies fully type-checked)        */
/* ------------------------------------------------------------------------- */

static void atomic_isolation_exercise(void) {
	sk_atomic_u32_t counter = 0u;
	u32 expected = 0u;

	sk_atomic_store_u32(&counter, 1u, SK_MEMORY_ORDER_RELAXED);
	(void)sk_atomic_load_u32(&counter, SK_MEMORY_ORDER_ACQUIRE);
	(void)sk_atomic_exchange_u32(&counter, 5u, SK_MEMORY_ORDER_SEQ_CST);
	(void)sk_atomic_fetch_add_u32(&counter, 1u, SK_MEMORY_ORDER_RELAXED);
	(void)sk_atomic_fetch_sub_u32(&counter, 1u, SK_MEMORY_ORDER_RELAXED);
	(void)sk_atomic_fetch_and_u32(&counter, 0xFFu, SK_MEMORY_ORDER_RELAXED);
	(void)sk_atomic_fetch_or_u32(&counter, 0x0Fu, SK_MEMORY_ORDER_RELAXED);
	(void)sk_atomic_fetch_xor_u32(&counter, 0x55u, SK_MEMORY_ORDER_RELAXED);
	(void)sk_atomic_cas_strong_u32(&counter, &expected, 9u, SK_MEMORY_ORDER_ACQ_REL, SK_MEMORY_ORDER_ACQUIRE);
	(void)sk_atomic_cas_weak_u32(&counter, &expected, 9u, SK_MEMORY_ORDER_ACQ_REL, SK_MEMORY_ORDER_ACQUIRE);
	(void)sk_atomic_is_lock_free_u32(&counter);
	sk_atomic_thread_fence(SK_MEMORY_ORDER_SEQ_CST);
}

/* Reference the exercise fn so -Wunused-function / C4505 stay quiet. */
#if defined(_MSC_VER)
static void (*atomic_isolation_check)(void) = atomic_isolation_exercise;
#else
__attribute__((unused)) static void (*atomic_isolation_check)(void) = atomic_isolation_exercise;
#endif
