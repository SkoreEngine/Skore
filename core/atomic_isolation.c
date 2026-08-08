/**
 * @file atomic_isolation.c
 * @brief Compile-only isolation check for atomics.h (test builds only).
 *
 * This TU is wired into the test build (sk-core-tests) and deliberately
 * excluded from production sk-core. It proves, entirely at compile time,
 * that atomics.h stays a pure C11 atomics wrapper:
 *
 *   - The TU includes nothing but common.h and atomics.h. If any windowing /
 *     UI header guard macro shows up here, one of those two headers leaked an
 *     OS/UI include and the build fails (see the #error block below).
 *   - Every atomic wrapper type keeps the plain scalar size and alignment of
 *     its value type (_Static_assert).
 *   - Every public operation exists with the documented signature: each op
 *     is invoked with its exact engine argument types and the result is
 *     assigned to a variable of the documented result type. The functions
 *     below are never called at runtime; they exist only so the compiler
 *     type-checks every public op under the project's -Werror warning set.
 *
 * The check functions are non-static (with file-scope prototypes) so the TU
 * compiles cleanly inside a static library under GCC/Clang (-Wunused-function,
 * -Wmissing-prototypes) and MSVC (/W4 /WX).
 *
 * Deliberately NOT wrapped in #ifdef SK_TESTS: the checks must not be silently
 * stripped if the file is ever compiled outside the test build. CMake is what
 * keeps this file out of production sk-core.
 */

#include "common.h"
#include "atomics.h"

/* ---------------------------------------------------------------------------
 * Window/UI header-leak guard.
 *
 * The macros below are guard/API macros that only exist after a windowing or
 * UI header has been processed (windows.h's user/base/gdi sections, Xlib).
 * This TU includes only common.h and atomics.h, so any of them being defined
 * here means atomics.h started pulling in an OS/UI header — hard build error.
 * ------------------------------------------------------------------------- */
#if defined(_WINDOWS_) || defined(WINUSERAPI) || defined(WINBASEAPI) || defined(WINGDIAPI) || defined(_X11_XLIB_H_)
#error "atomics.h leaks a windowing/UI header guard macro; atomics.h must include only common.h and <stdatomic.h>"
#endif

/* ---- type-size / alignment static asserts (must match the value types) ---- */

_Static_assert(sizeof(sk_atomic_u32_t) == sizeof(u32), "sk_atomic_u32_t must be 4 bytes");
_Static_assert(_Alignof(sk_atomic_u32_t) == _Alignof(u32), "sk_atomic_u32_t must keep u32 alignment");
_Static_assert(sizeof(sk_atomic_i32_t) == sizeof(i32), "sk_atomic_i32_t must be 4 bytes");
_Static_assert(_Alignof(sk_atomic_i32_t) == _Alignof(i32), "sk_atomic_i32_t must keep i32 alignment");
_Static_assert(sizeof(sk_atomic_u64_t) == sizeof(u64), "sk_atomic_u64_t must be 8 bytes");
_Static_assert(_Alignof(sk_atomic_u64_t) == _Alignof(u64), "sk_atomic_u64_t must keep u64 alignment");
_Static_assert(sizeof(sk_atomic_i64_t) == sizeof(i64), "sk_atomic_i64_t must be 8 bytes");
_Static_assert(_Alignof(sk_atomic_i64_t) == _Alignof(i64), "sk_atomic_i64_t must keep i64 alignment");
_Static_assert(sizeof(sk_atomic_ptr_t) == sizeof(void_ptr_t), "sk_atomic_ptr_t must be pointer-sized");
_Static_assert(_Alignof(sk_atomic_ptr_t) == _Alignof(void_ptr_t), "sk_atomic_ptr_t must keep pointer alignment");

/* ---- prototypes so -Wmissing-prototypes / -Wstrict-prototypes stay quiet ---- */

void sk_atomic_isolation_u32_ops(void);
void sk_atomic_isolation_i32_ops(void);
void sk_atomic_isolation_u64_ops(void);
void sk_atomic_isolation_i64_ops(void);
void sk_atomic_isolation_ptr_ops(void);
void sk_atomic_isolation_fences(void);

/* ---- u32: every op in every memory order (seq_cst, acquire, release, acq_rel, relaxed) ---- */

void sk_atomic_isolation_u32_ops(void) {
	sk_atomic_u32_t a;
	u32 r;
	int ok;
	u32 expected = 7u;

	sk_atomic_u32_init(&a, 7u);
	r = sk_atomic_u32_load(&a);
	(void)r;
	r = sk_atomic_u32_load_acquire(&a);
	(void)r;
	r = sk_atomic_u32_load_relaxed(&a);
	(void)r;
	sk_atomic_u32_store(&a, 1u);
	sk_atomic_u32_store_release(&a, 1u);
	sk_atomic_u32_store_relaxed(&a, 1u);
	r = sk_atomic_u32_exchange(&a, 2u);
	(void)r;
	r = sk_atomic_u32_exchange_acq_rel(&a, 2u);
	(void)r;
	r = sk_atomic_u32_exchange_relaxed(&a, 2u);
	(void)r;
	ok = sk_atomic_u32_cas_strong(&a, &expected, 3u);
	(void)ok;
	ok = sk_atomic_u32_cas_strong_acquire(&a, &expected, 3u);
	(void)ok;
	ok = sk_atomic_u32_cas_strong_release(&a, &expected, 3u);
	(void)ok;
	ok = sk_atomic_u32_cas_strong_acq_rel(&a, &expected, 3u);
	(void)ok;
	ok = sk_atomic_u32_cas_strong_relaxed(&a, &expected, 3u);
	(void)ok;
	(void)expected;

	r = sk_atomic_u32_fetch_add(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_add_acquire(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_add_release(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_add_acq_rel(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_add_relaxed(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_sub(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_sub_acquire(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_sub_release(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_sub_acq_rel(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_sub_relaxed(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_or(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_or_acquire(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_or_release(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_or_acq_rel(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_or_relaxed(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_and(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_and_acquire(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_and_release(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_and_acq_rel(&a, 1u);
	(void)r;
	r = sk_atomic_u32_fetch_and_relaxed(&a, 1u);
	(void)r;
}

/* ---- i32: same full family (signed value type) ---- */

void sk_atomic_isolation_i32_ops(void) {
	sk_atomic_i32_t a;
	i32 r;
	int ok;
	i32 expected = -7;

	sk_atomic_i32_init(&a, -7);
	r = sk_atomic_i32_load(&a);
	(void)r;
	r = sk_atomic_i32_load_acquire(&a);
	(void)r;
	r = sk_atomic_i32_load_relaxed(&a);
	(void)r;
	sk_atomic_i32_store(&a, 1);
	sk_atomic_i32_store_release(&a, 1);
	sk_atomic_i32_store_relaxed(&a, 1);
	r = sk_atomic_i32_exchange(&a, 2);
	(void)r;
	r = sk_atomic_i32_exchange_acq_rel(&a, 2);
	(void)r;
	r = sk_atomic_i32_exchange_relaxed(&a, 2);
	(void)r;
	ok = sk_atomic_i32_cas_strong(&a, &expected, 3);
	(void)ok;
	ok = sk_atomic_i32_cas_strong_acquire(&a, &expected, 3);
	(void)ok;
	ok = sk_atomic_i32_cas_strong_release(&a, &expected, 3);
	(void)ok;
	ok = sk_atomic_i32_cas_strong_acq_rel(&a, &expected, 3);
	(void)ok;
	ok = sk_atomic_i32_cas_strong_relaxed(&a, &expected, 3);
	(void)ok;
	(void)expected;

	r = sk_atomic_i32_fetch_add(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_add_acquire(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_add_release(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_add_acq_rel(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_add_relaxed(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_sub(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_sub_acquire(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_sub_release(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_sub_acq_rel(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_sub_relaxed(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_or(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_or_acquire(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_or_release(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_or_acq_rel(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_or_relaxed(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_and(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_and_acquire(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_and_release(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_and_acq_rel(&a, 1);
	(void)r;
	r = sk_atomic_i32_fetch_and_relaxed(&a, 1);
	(void)r;
}

/* ---- u64: same full family (64-bit value type) ---- */

void sk_atomic_isolation_u64_ops(void) {
	sk_atomic_u64_t a;
	u64 r;
	int ok;
	u64 expected = 7ull;

	sk_atomic_u64_init(&a, 7ull);
	r = sk_atomic_u64_load(&a);
	(void)r;
	r = sk_atomic_u64_load_acquire(&a);
	(void)r;
	r = sk_atomic_u64_load_relaxed(&a);
	(void)r;
	sk_atomic_u64_store(&a, 1ull);
	sk_atomic_u64_store_release(&a, 1ull);
	sk_atomic_u64_store_relaxed(&a, 1ull);
	r = sk_atomic_u64_exchange(&a, 2ull);
	(void)r;
	r = sk_atomic_u64_exchange_acq_rel(&a, 2ull);
	(void)r;
	r = sk_atomic_u64_exchange_relaxed(&a, 2ull);
	(void)r;
	ok = sk_atomic_u64_cas_strong(&a, &expected, 3ull);
	(void)ok;
	ok = sk_atomic_u64_cas_strong_acquire(&a, &expected, 3ull);
	(void)ok;
	ok = sk_atomic_u64_cas_strong_release(&a, &expected, 3ull);
	(void)ok;
	ok = sk_atomic_u64_cas_strong_acq_rel(&a, &expected, 3ull);
	(void)ok;
	ok = sk_atomic_u64_cas_strong_relaxed(&a, &expected, 3ull);
	(void)ok;
	(void)expected;

	r = sk_atomic_u64_fetch_add(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_add_acquire(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_add_release(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_add_acq_rel(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_add_relaxed(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_sub(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_sub_acquire(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_sub_release(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_sub_acq_rel(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_sub_relaxed(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_or(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_or_acquire(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_or_release(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_or_acq_rel(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_or_relaxed(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_and(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_and_acquire(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_and_release(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_and_acq_rel(&a, 1ull);
	(void)r;
	r = sk_atomic_u64_fetch_and_relaxed(&a, 1ull);
	(void)r;
}

/* ---- i64: same full family (signed 64-bit value type) ---- */

void sk_atomic_isolation_i64_ops(void) {
	sk_atomic_i64_t a;
	i64 r;
	int ok;
	i64 expected = -7ll;

	sk_atomic_i64_init(&a, -7ll);
	r = sk_atomic_i64_load(&a);
	(void)r;
	r = sk_atomic_i64_load_acquire(&a);
	(void)r;
	r = sk_atomic_i64_load_relaxed(&a);
	(void)r;
	sk_atomic_i64_store(&a, 1ll);
	sk_atomic_i64_store_release(&a, 1ll);
	sk_atomic_i64_store_relaxed(&a, 1ll);
	r = sk_atomic_i64_exchange(&a, 2ll);
	(void)r;
	r = sk_atomic_i64_exchange_acq_rel(&a, 2ll);
	(void)r;
	r = sk_atomic_i64_exchange_relaxed(&a, 2ll);
	(void)r;
	ok = sk_atomic_i64_cas_strong(&a, &expected, 3ll);
	(void)ok;
	ok = sk_atomic_i64_cas_strong_acquire(&a, &expected, 3ll);
	(void)ok;
	ok = sk_atomic_i64_cas_strong_release(&a, &expected, 3ll);
	(void)ok;
	ok = sk_atomic_i64_cas_strong_acq_rel(&a, &expected, 3ll);
	(void)ok;
	ok = sk_atomic_i64_cas_strong_relaxed(&a, &expected, 3ll);
	(void)ok;
	(void)expected;

	r = sk_atomic_i64_fetch_add(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_add_acquire(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_add_release(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_add_acq_rel(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_add_relaxed(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_sub(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_sub_acquire(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_sub_release(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_sub_acq_rel(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_sub_relaxed(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_or(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_or_acquire(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_or_release(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_or_acq_rel(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_or_relaxed(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_and(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_and_acquire(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_and_release(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_and_acq_rel(&a, 1ll);
	(void)r;
	r = sk_atomic_i64_fetch_and_relaxed(&a, 1ll);
	(void)r;
}

/* ---- ptr (void_ptr_t): core family only — no fetch ops on void* ---- */

void sk_atomic_isolation_ptr_ops(void) {
	sk_atomic_ptr_t p;
	int obj_a = 0;
	void_ptr_t r;
	int ok;
	void_ptr_t expected = (void_ptr_t)0;

	sk_atomic_ptr_init(&p, (void_ptr_t)0);
	r = sk_atomic_ptr_load(&p);
	(void)r;
	r = sk_atomic_ptr_load_acquire(&p);
	(void)r;
	r = sk_atomic_ptr_load_relaxed(&p);
	(void)r;
	sk_atomic_ptr_store(&p, &obj_a);
	sk_atomic_ptr_store_release(&p, (void_ptr_t)0);
	sk_atomic_ptr_store_relaxed(&p, &obj_a);
	r = sk_atomic_ptr_exchange(&p, (void_ptr_t)0);
	(void)r;
	r = sk_atomic_ptr_exchange_acq_rel(&p, &obj_a);
	(void)r;
	r = sk_atomic_ptr_exchange_relaxed(&p, (void_ptr_t)0);
	(void)r;
	ok = sk_atomic_ptr_cas_strong(&p, &expected, &obj_a);
	(void)ok;
	ok = sk_atomic_ptr_cas_strong_acquire(&p, &expected, (void_ptr_t)0);
	(void)ok;
	ok = sk_atomic_ptr_cas_strong_release(&p, &expected, &obj_a);
	(void)ok;
	ok = sk_atomic_ptr_cas_strong_acq_rel(&p, &expected, (void_ptr_t)0);
	(void)ok;
	ok = sk_atomic_ptr_cas_strong_relaxed(&p, &expected, &obj_a);
	(void)ok;
	(void)expected;
}

/* ---- thread fences ---- */

void sk_atomic_isolation_fences(void) {
	sk_atomic_thread_fence_acquire();
	sk_atomic_thread_fence_release();
	sk_atomic_thread_fence_seq_cst();
}
