#pragma once

/**
 * @file atomics.h
 * @brief Lock-free atomic primitives for core (load / store / exchange / CAS / fetch-op).
 *
 * Header-only C11 atomics built directly on <stdatomic.h> — the C-standard
 * equivalent of std::atomic. No OS headers, no Win32 API, no windows.h: the
 * same calls compile unchanged on Windows (MSVC 17.5+), Linux, and macOS with
 * any C11-capable compiler.
 *
 * Each type is a POD wrapper around a single `_Atomic` member so atomics can be
 * embedded in components, resources, and lock-free queues. Zero-initialize
 * storage with `= {0}` and call *_init() (or *_store()) before publishing the
 * object to other threads. Types and helpers are `static inline`, so this
 * module costs nothing to link and adds no platform UI dependencies to
 * consumers.
 *
 * Memory order defaults to `memory_order_seq_cst`. *_acquire / *_release /
 * *_relaxed variants cover producer-consumer publication and relaxed counter
 * patterns:
 *
 *   static sk_atomic_u32_t ref_count;                 // zero-init
 *   sk_atomic_u32_init(&ref_count, 1u);               // before sharing
 *   u32 old = sk_atomic_u32_fetch_sub(&ref_count, 1u);
 *   sk_atomic_u32_store_release(&state, SK_STATE_READY);
 *   if (sk_atomic_u32_load_acquire(&state) == SK_STATE_READY) { ... }
 *
 * This header is C11-only. C++ consumers should use std::atomic directly.
 */

#include "common.h"

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__cplusplus)

/* ------------------------------------------------------------------------- */
/* Integer atomics (u32 / u64 / i32 / i64)                                   */
/* ------------------------------------------------------------------------- */

/**
 * Generate one atomic integer type plus its inline ops.
 *
 * @param sk_name Public type name (e.g. sk_atomic_u32_t).
 * @param prefix  Function-name prefix without the `_t` suffix (e.g. sk_atomic_u32).
 * @param ctype   Fixed-width backing type (e.g. u32).
 *
 * Generated ops: *_init, *_load, *_load_acquire, *_load_relaxed, *_store,
 * *_store_release, *_store_relaxed, *_exchange, *_cas (strong, seq_cst),
 * *_fetch_add, *_fetch_sub, *_fetch_or, *_fetch_and.
 */
#define SK_DEFINE_ATOMIC_INT(sk_name, prefix, ctype)                                                                                   \
	typedef struct sk_name {                                                                                                           \
		_Atomic(ctype) value;                                                                                                          \
	} sk_name;                                                                                                                         \
                                                                                                                                       \
	/** Non-atomic init (before sharing). @param a target @param v initial value. */                                                   \
	SK_FINLINE void prefix##_init(sk_name* a, ctype v) {                                                                               \
		atomic_init(&a->value, v);                                                                                                     \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst load. @param a target @return current value. */                                                                        \
	SK_FINLINE ctype prefix##_load(const sk_name* a) {                                                                                 \
		return atomic_load_explicit(&a->value, memory_order_seq_cst);                                                                  \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Acquire load (pairs with a *_store_release). @param a target @return value. */                                                 \
	SK_FINLINE ctype prefix##_load_acquire(const sk_name* a) {                                                                         \
		return atomic_load_explicit(&a->value, memory_order_acquire);                                                                  \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Relaxed load (plain counters, no ordering). @param a target @return value. */                                                  \
	SK_FINLINE ctype prefix##_load_relaxed(const sk_name* a) {                                                                         \
		return atomic_load_explicit(&a->value, memory_order_relaxed);                                                                  \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst store. @param a target @param v value to store. */                                                                     \
	SK_FINLINE void prefix##_store(sk_name* a, ctype v) {                                                                              \
		atomic_store_explicit(&a->value, v, memory_order_seq_cst);                                                                     \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Release store (pairs with a *_load_acquire). @param a target @param v value. */                                                \
	SK_FINLINE void prefix##_store_release(sk_name* a, ctype v) {                                                                      \
		atomic_store_explicit(&a->value, v, memory_order_release);                                                                     \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Relaxed store (plain counters, no ordering). @param a target @param v value. */                                                \
	SK_FINLINE void prefix##_store_relaxed(sk_name* a, ctype v) {                                                                      \
		atomic_store_explicit(&a->value, v, memory_order_relaxed);                                                                     \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst exchange. @param a target @param v value to swap in @return old value. */                                              \
	SK_FINLINE ctype prefix##_exchange(sk_name* a, ctype v) {                                                                          \
		return atomic_exchange_explicit(&a->value, v, memory_order_seq_cst);                                                           \
	}                                                                                                                                  \
                                                                                                                                       \
	/**                                                                                 \
     * Strong seq_cst compare-and-swap.                                               \
     * On success stores @p desired into @p a and returns 1. On failure writes the  \
     * current value into @p *expected and returns 0.                                 \
     * @param a        target                                                          \
     * @param expected in/out: expected old value, updated on failure                 \
     * @param desired  value to store on success                                      \
     * @return 1 if swapped, 0 otherwise                                              \
     */                                              \
	SK_FINLINE int prefix##_cas(sk_name* a, ctype* expected, ctype desired) {                                                          \
		return (int)atomic_compare_exchange_strong_explicit(&a->value, expected, desired, memory_order_seq_cst, memory_order_seq_cst); \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst fetch-and-add. @param a target @param n addend @return old value. */                                                   \
	SK_FINLINE ctype prefix##_fetch_add(sk_name* a, ctype n) {                                                                         \
		return atomic_fetch_add_explicit(&a->value, n, memory_order_seq_cst);                                                          \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst fetch-and-sub. @param a target @param n subtrahend @return old value. */                                               \
	SK_FINLINE ctype prefix##_fetch_sub(sk_name* a, ctype n) {                                                                         \
		return atomic_fetch_sub_explicit(&a->value, n, memory_order_seq_cst);                                                          \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst fetch-and-or (flag set). @param a target @param mask bits to set @return old value. */                                 \
	SK_FINLINE ctype prefix##_fetch_or(sk_name* a, ctype mask) {                                                                       \
		return atomic_fetch_or_explicit(&a->value, mask, memory_order_seq_cst);                                                        \
	}                                                                                                                                  \
                                                                                                                                       \
	/** Seq_cst fetch-and-and (flag clear). @param a target @param mask bits to keep @return old value. */                             \
	SK_FINLINE ctype prefix##_fetch_and(sk_name* a, ctype mask) {                                                                      \
		return atomic_fetch_and_explicit(&a->value, mask, memory_order_seq_cst);                                                       \
	}

SK_DEFINE_ATOMIC_INT(sk_atomic_u32_t, sk_atomic_u32, u32)
SK_DEFINE_ATOMIC_INT(sk_atomic_u64_t, sk_atomic_u64, u64)
SK_DEFINE_ATOMIC_INT(sk_atomic_i32_t, sk_atomic_i32, i32)
SK_DEFINE_ATOMIC_INT(sk_atomic_i64_t, sk_atomic_i64, i64)

/* ------------------------------------------------------------------------- */
/* Pointer atomics                                                           */
/* ------------------------------------------------------------------------- */

/** Atomic pointer (lock-free linked lists / stack top). */
typedef struct sk_atomic_ptr_t {
	_Atomic(void_ptr_t) value;
} sk_atomic_ptr_t;

/** Non-atomic init (before sharing). @param a target @param v initial value. */
SK_FINLINE void sk_atomic_ptr_init(sk_atomic_ptr_t* a, void_ptr_t v) {
	atomic_init(&a->value, v);
}

/** Seq_cst load. @param a target @return current pointer. */
SK_FINLINE void_ptr_t sk_atomic_ptr_load(const sk_atomic_ptr_t* a) {
	return atomic_load_explicit(&a->value, memory_order_seq_cst);
}

/** Acquire load (pairs with sk_atomic_ptr_store_release). @param a target @return pointer. */
SK_FINLINE void_ptr_t sk_atomic_ptr_load_acquire(const sk_atomic_ptr_t* a) {
	return atomic_load_explicit(&a->value, memory_order_acquire);
}

/** Relaxed load. @param a target @return pointer. */
SK_FINLINE void_ptr_t sk_atomic_ptr_load_relaxed(const sk_atomic_ptr_t* a) {
	return atomic_load_explicit(&a->value, memory_order_relaxed);
}

/** Seq_cst store. @param a target @param v pointer to store. */
SK_FINLINE void sk_atomic_ptr_store(sk_atomic_ptr_t* a, void_ptr_t v) {
	atomic_store_explicit(&a->value, v, memory_order_seq_cst);
}

/** Release store (pairs with sk_atomic_ptr_load_acquire). @param a target @param v pointer. */
SK_FINLINE void sk_atomic_ptr_store_release(sk_atomic_ptr_t* a, void_ptr_t v) {
	atomic_store_explicit(&a->value, v, memory_order_release);
}

/** Seq_cst exchange. @param a target @param v pointer to swap in @return old pointer. */
SK_FINLINE void_ptr_t sk_atomic_ptr_exchange(sk_atomic_ptr_t* a, void_ptr_t v) {
	return atomic_exchange_explicit(&a->value, v, memory_order_seq_cst);
}

/**
 * Strong seq_cst compare-and-swap.
 * On success stores @p desired into @p a and returns 1. On failure writes the
 * current pointer into @p *expected and returns 0.
 * @param a        target
 * @param expected in/out: expected old pointer, updated on failure
 * @param desired  pointer to store on success
 * @return 1 if swapped, 0 otherwise
 */
SK_FINLINE int sk_atomic_ptr_cas(sk_atomic_ptr_t* a, void_ptr_t* expected, void_ptr_t desired) {
	return (int)atomic_compare_exchange_strong_explicit(&a->value, expected, desired, memory_order_seq_cst, memory_order_seq_cst);
}

#endif /* !__cplusplus */

#ifdef __cplusplus
}
#endif
