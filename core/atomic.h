#pragma once

/**
 * @file atomic.h
 * @brief Core atomics abstraction for fixed-width integers.
 *
 * Header-only wrapper over C11 `<stdatomic.h>`. The include path pulls in only
 * libc/compiler atomics — no window/UI headers (no Windows.h, WinUser, Cocoa,
 * X11, …) — so core, app, plugin, and editor code can use it without dragging
 * in a platform UI stack.
 *
 * Operations are static-inline: they inline to the target's native atomic
 * instructions with no call overhead and no shared-library boundary. Memory
 * orders mirror the C11 names (see sk_memory_order_t); when unsure, pass
 * SK_MEMORY_ORDER_SEQ_CST.
 */

#include "common.h"

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Memory order                                                               */
/* ------------------------------------------------------------------------- */

/**
 * C11 memory ordering exposed with sk_ names.
 *
 * The enumerators keep the same values as the C11 `memory_order` constants,
 * so they can be passed straight to <stdatomic.h> behind the scenes.
 */
typedef enum sk_memory_order_t {
	SK_MEMORY_ORDER_RELAXED = memory_order_relaxed, /**< No ordering constraints. */
	SK_MEMORY_ORDER_CONSUME = memory_order_consume, /**< Consume (data-dependent load). */
	SK_MEMORY_ORDER_ACQUIRE = memory_order_acquire, /**< Acquire (loads/locks). */
	SK_MEMORY_ORDER_RELEASE = memory_order_release, /**< Release (stores/unlocks). */
	SK_MEMORY_ORDER_ACQ_REL = memory_order_acq_rel, /**< Acquire+release (read-modify-write). */
	SK_MEMORY_ORDER_SEQ_CST = memory_order_seq_cst	/**< Sequentially consistent (default). */
} sk_memory_order_t;

/* ------------------------------------------------------------------------- */
/* Atomic integer types                                                      */
/* ------------------------------------------------------------------------- */

typedef _Atomic(u8) sk_atomic_u8_t;
typedef _Atomic(u16) sk_atomic_u16_t;
typedef _Atomic(u32) sk_atomic_u32_t;
typedef _Atomic(u64) sk_atomic_u64_t;
typedef _Atomic(i8) sk_atomic_i8_t;
typedef _Atomic(i16) sk_atomic_i16_t;
typedef _Atomic(i32) sk_atomic_i32_t;
typedef _Atomic(i64) sk_atomic_i64_t;

/* ------------------------------------------------------------------------- */
/* Per-width operation family (macro-generated)                               */
/* ------------------------------------------------------------------------- */

/**
 * Generate the operation family for one atomic width.
 *
 * Each instantiation provides the sk_atomic_<w>_t type plus these operations
 * (all static-inline, all taking/returning the width's C type):
 *
 *   sk_atomic_load_<w>(a, order)            atomic load
 *   sk_atomic_store_<w>(a, value, order)    atomic store
 *   sk_atomic_exchange_<w>(a, value, order) atomic swap; returns previous value
 *   sk_atomic_fetch_add_<w>(a, value, order) atomic add; returns previous value
 *   sk_atomic_fetch_sub_<w>(a, value, order) atomic sub; returns previous value
 *   sk_atomic_fetch_and_<w>(a, value, order) atomic AND; returns previous value
 *   sk_atomic_fetch_or_<w>(a, value, order)  atomic OR; returns previous value
 *   sk_atomic_fetch_xor_<w>(a, value, order) atomic XOR; returns previous value
 *   sk_atomic_cas_strong_<w>(a, expected, desired, order_success, order_failure)
 *       compare-and-swap loop; returns 1 if swapped, 0 otherwise
 *   sk_atomic_cas_weak_<w>(a, expected, desired, order_success, order_failure)
 *       compare-and-swap (may spuriously fail); returns 1 if swapped
 *   sk_atomic_is_lock_free_<w>(a)           1 if the type is lock-free
 *
 * On success (both cas variants), *expected is unchanged. On failure it is
 * written with the observed value, matching the C11 contract.
 *
 * @param w Symbol suffix used in the generated names (u32, i64, …).
 * @param t C type backing the width (u32, i64, …).
 */
#define SK_ATOMIC_DEFINE(w, t)                                                                                                                               \
	SK_FINLINE t sk_atomic_load_##w(const sk_atomic_##w##_t* a, sk_memory_order_t order) {                                                                   \
		return atomic_load_explicit(a, (memory_order)order);                                                                                                 \
	}                                                                                                                                                        \
	SK_FINLINE void sk_atomic_store_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                            \
		atomic_store_explicit(a, value, (memory_order)order);                                                                                                \
	}                                                                                                                                                        \
	SK_FINLINE t sk_atomic_exchange_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                            \
		return atomic_exchange_explicit(a, value, (memory_order)order);                                                                                      \
	}                                                                                                                                                        \
	SK_FINLINE t sk_atomic_fetch_add_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                           \
		return atomic_fetch_add_explicit(a, value, (memory_order)order);                                                                                     \
	}                                                                                                                                                        \
	SK_FINLINE t sk_atomic_fetch_sub_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                           \
		return atomic_fetch_sub_explicit(a, value, (memory_order)order);                                                                                     \
	}                                                                                                                                                        \
	SK_FINLINE t sk_atomic_fetch_and_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                           \
		return atomic_fetch_and_explicit(a, value, (memory_order)order);                                                                                     \
	}                                                                                                                                                        \
	SK_FINLINE t sk_atomic_fetch_or_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                            \
		return atomic_fetch_or_explicit(a, value, (memory_order)order);                                                                                      \
	}                                                                                                                                                        \
	SK_FINLINE t sk_atomic_fetch_xor_##w(sk_atomic_##w##_t* a, t value, sk_memory_order_t order) {                                                           \
		return atomic_fetch_xor_explicit(a, value, (memory_order)order);                                                                                     \
	}                                                                                                                                                        \
	SK_FINLINE u8 sk_atomic_cas_strong_##w(sk_atomic_##w##_t* a, t* expected, t desired, sk_memory_order_t order_success, sk_memory_order_t order_failure) { \
		return (u8)atomic_compare_exchange_strong_explicit(a, expected, desired, (memory_order)order_success, (memory_order)order_failure);                  \
	}                                                                                                                                                        \
	SK_FINLINE u8 sk_atomic_cas_weak_##w(sk_atomic_##w##_t* a, t* expected, t desired, sk_memory_order_t order_success, sk_memory_order_t order_failure) {   \
		return (u8)atomic_compare_exchange_weak_explicit(a, expected, desired, (memory_order)order_success, (memory_order)order_failure);                    \
	}                                                                                                                                                        \
	SK_FINLINE u8 sk_atomic_is_lock_free_##w(const sk_atomic_##w##_t* a) {                                                                                   \
		return (u8)atomic_is_lock_free(a);                                                                                                                   \
	}

SK_ATOMIC_DEFINE(u8, u8)
SK_ATOMIC_DEFINE(u16, u16)
SK_ATOMIC_DEFINE(u32, u32)
SK_ATOMIC_DEFINE(u64, u64)
SK_ATOMIC_DEFINE(i8, i8)
SK_ATOMIC_DEFINE(i16, i16)
SK_ATOMIC_DEFINE(i32, i32)
SK_ATOMIC_DEFINE(i64, i64)

/* ------------------------------------------------------------------------- */
/* Fences                                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Insert a hardware memory fence at @p order.
 * Rarely needed directly — prefer ordering through load/store/CAS operations.
 *
 * @param order Fence strength (SK_MEMORY_ORDER_*).
 */
SK_FINLINE void sk_atomic_thread_fence(sk_memory_order_t order) {
	atomic_thread_fence((memory_order)order);
}

#ifdef __cplusplus
}
#endif
