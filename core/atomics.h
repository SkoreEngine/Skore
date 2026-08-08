#pragma once

/**
 * @file atomics.h
 * @brief Lock-free atomics for u32/u64/i32/i64 and data pointers.
 *
 * Thin, typed wrappers over C11 <stdatomic.h>. First-party C is compiled as
 * C11 (CMAKE_C_STANDARD 11; MSVC /std:c11) so the standard atomics are
 * available on every supported platform. This header includes only
 * common.h and <stdatomic.h> — no OS/UI/platform headers.
 *
 * Every function carries its memory order in the name, following the engine
 * convention used for lock-free publishing:
 *   - publish / hand off with RELEASE (or seq_cst for store/exchange default)
 *   - snapshot / take ownership with ACQUIRE
 *   - _relaxed  -> memory_order_relaxed  (no ordering, plain counter)
 *   - _acquire  -> memory_order_acquire  (loads / RMW acquire side)
 *   - _release  -> memory_order_release  (stores / RMW release side)
 *   - _acq_rel  -> memory_order_acq_rel  (RMW both sides)
 *   - no suffix -> memory_order_seq_cst  (total order)
 *
 * Types:
 *   - sk_atomic_u32_t / sk_atomic_i32_t / sk_atomic_u64_t / sk_atomic_i64_t
 *     provide init, load/store (seq_cst/acquire/release/relaxed), exchange,
 *     strong CAS, and fetch add/sub/or/and.
 *   - sk_atomic_ptr_t (void_ptr_t) provides the same minus fetch
 *     add/sub/or/and: void* has no complete pointee, so pointer arithmetic
 *     via atomic_fetch_* is not portable.
 *
 * Values are plain C scalars; only the storage object is _Atomic-qualified,
 * so atomics can live in structs that are otherwise ordinary data.
 */

#include "common.h"

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generate the shared (non-fetch) operation family for an atomic type.
 *
 * @param name    Suffix of the generated identifiers (e.g. u32 -> sk_atomic_u32_*).
 * @param atom_ty C11 atomic type the typedef wraps (e.g. atomic_uint).
 * @param val_ty  Plain value type the atomic stores (e.g. u32).
 */
#define SK_ATOMIC_DEFINE_CORE(name, atom_ty, val_ty)                                                                           \
	typedef atom_ty sk_atomic_##name##_t;                                                                                      \
	SK_FINLINE void sk_atomic_##name##_init(sk_atomic_##name##_t* a, val_ty v) {                                               \
		atomic_init(a, v);                                                                                                     \
	}                                                                                                                          \
	SK_FINLINE val_ty sk_atomic_##name##_load(const sk_atomic_##name##_t* a) {                                                 \
		return atomic_load_explicit(a, memory_order_seq_cst);                                                                  \
	}                                                                                                                          \
	SK_FINLINE val_ty sk_atomic_##name##_load_acquire(const sk_atomic_##name##_t* a) {                                         \
		return atomic_load_explicit(a, memory_order_acquire);                                                                  \
	}                                                                                                                          \
	SK_FINLINE val_ty sk_atomic_##name##_load_relaxed(const sk_atomic_##name##_t* a) {                                         \
		return atomic_load_explicit(a, memory_order_relaxed);                                                                  \
	}                                                                                                                          \
	SK_FINLINE void sk_atomic_##name##_store(sk_atomic_##name##_t* a, val_ty v) {                                              \
		atomic_store_explicit(a, v, memory_order_seq_cst);                                                                     \
	}                                                                                                                          \
	SK_FINLINE void sk_atomic_##name##_store_release(sk_atomic_##name##_t* a, val_ty v) {                                      \
		atomic_store_explicit(a, v, memory_order_release);                                                                     \
	}                                                                                                                          \
	SK_FINLINE void sk_atomic_##name##_store_relaxed(sk_atomic_##name##_t* a, val_ty v) {                                      \
		atomic_store_explicit(a, v, memory_order_relaxed);                                                                     \
	}                                                                                                                          \
	SK_FINLINE val_ty sk_atomic_##name##_exchange(sk_atomic_##name##_t* a, val_ty v) {                                         \
		return atomic_exchange_explicit(a, v, memory_order_seq_cst);                                                           \
	}                                                                                                                          \
	SK_FINLINE val_ty sk_atomic_##name##_exchange_acq_rel(sk_atomic_##name##_t* a, val_ty v) {                                 \
		return atomic_exchange_explicit(a, v, memory_order_acq_rel);                                                           \
	}                                                                                                                          \
	SK_FINLINE val_ty sk_atomic_##name##_exchange_relaxed(sk_atomic_##name##_t* a, val_ty v) {                                 \
		return atomic_exchange_explicit(a, v, memory_order_relaxed);                                                           \
	}                                                                                                                          \
	SK_FINLINE int sk_atomic_##name##_cas_strong(sk_atomic_##name##_t* a, val_ty* expected, val_ty desired) {                  \
		return (int)atomic_compare_exchange_strong_explicit(a, expected, desired, memory_order_seq_cst, memory_order_seq_cst); \
	}                                                                                                                          \
	SK_FINLINE int sk_atomic_##name##_cas_strong_acquire(sk_atomic_##name##_t* a, val_ty* expected, val_ty desired) {          \
		return (int)atomic_compare_exchange_strong_explicit(a, expected, desired, memory_order_acquire, memory_order_acquire); \
	}                                                                                                                          \
	SK_FINLINE int sk_atomic_##name##_cas_strong_release(sk_atomic_##name##_t* a, val_ty* expected, val_ty desired) {          \
		return (int)atomic_compare_exchange_strong_explicit(a, expected, desired, memory_order_release, memory_order_relaxed); \
	}                                                                                                                          \
	SK_FINLINE int sk_atomic_##name##_cas_strong_acq_rel(sk_atomic_##name##_t* a, val_ty* expected, val_ty desired) {          \
		return (int)atomic_compare_exchange_strong_explicit(a, expected, desired, memory_order_acq_rel, memory_order_acquire); \
	}                                                                                                                          \
	SK_FINLINE int sk_atomic_##name##_cas_strong_relaxed(sk_atomic_##name##_t* a, val_ty* expected, val_ty desired) {          \
		return (int)atomic_compare_exchange_strong_explicit(a, expected, desired, memory_order_relaxed, memory_order_relaxed); \
	}

/**
 * Generate the full integer atomic family: SK_ATOMIC_DEFINE_CORE plus fetch
 * add/sub/or/and in seq_cst, acquire, release, acq_rel and relaxed orders.
 * Each fetch op returns the previous value.
 *
 * @param name    Suffix of the generated identifiers.
 * @param atom_ty C11 atomic type the typedef wraps.
 * @param val_ty  Plain integer value type the atomic stores.
 */
#define SK_ATOMIC_DEFINE_INT(name, atom_ty, val_ty)                                             \
	SK_ATOMIC_DEFINE_CORE(name, atom_ty, val_ty)                                                \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_add(sk_atomic_##name##_t* a, val_ty v) {         \
		return atomic_fetch_add_explicit(a, v, memory_order_seq_cst);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_add_acquire(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_add_explicit(a, v, memory_order_acquire);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_add_release(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_add_explicit(a, v, memory_order_release);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_add_acq_rel(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_add_explicit(a, v, memory_order_acq_rel);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_add_relaxed(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_add_explicit(a, v, memory_order_relaxed);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_sub(sk_atomic_##name##_t* a, val_ty v) {         \
		return atomic_fetch_sub_explicit(a, v, memory_order_seq_cst);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_sub_acquire(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_sub_explicit(a, v, memory_order_acquire);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_sub_release(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_sub_explicit(a, v, memory_order_release);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_sub_acq_rel(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_sub_explicit(a, v, memory_order_acq_rel);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_sub_relaxed(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_sub_explicit(a, v, memory_order_relaxed);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_or(sk_atomic_##name##_t* a, val_ty v) {          \
		return atomic_fetch_or_explicit(a, v, memory_order_seq_cst);                            \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_or_acquire(sk_atomic_##name##_t* a, val_ty v) {  \
		return atomic_fetch_or_explicit(a, v, memory_order_acquire);                            \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_or_release(sk_atomic_##name##_t* a, val_ty v) {  \
		return atomic_fetch_or_explicit(a, v, memory_order_release);                            \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_or_acq_rel(sk_atomic_##name##_t* a, val_ty v) {  \
		return atomic_fetch_or_explicit(a, v, memory_order_acq_rel);                            \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_or_relaxed(sk_atomic_##name##_t* a, val_ty v) {  \
		return atomic_fetch_or_explicit(a, v, memory_order_relaxed);                            \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_and(sk_atomic_##name##_t* a, val_ty v) {         \
		return atomic_fetch_and_explicit(a, v, memory_order_seq_cst);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_and_acquire(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_and_explicit(a, v, memory_order_acquire);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_and_release(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_and_explicit(a, v, memory_order_release);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_and_acq_rel(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_and_explicit(a, v, memory_order_acq_rel);                           \
	}                                                                                           \
	SK_FINLINE val_ty sk_atomic_##name##_fetch_and_relaxed(sk_atomic_##name##_t* a, val_ty v) { \
		return atomic_fetch_and_explicit(a, v, memory_order_relaxed);                           \
	}

/**
 * u32 atomic. Same family as sk_atomic_u32_* (see SK_ATOMIC_DEFINE_INT docs).
 */
SK_ATOMIC_DEFINE_INT(u32, atomic_uint, u32)

/**
 * i32 atomic. Same family as sk_atomic_u32_* (see SK_ATOMIC_DEFINE_INT docs).
 */
SK_ATOMIC_DEFINE_INT(i32, atomic_int, i32)

/**
 * u64 atomic. Same family as sk_atomic_u32_* (see SK_ATOMIC_DEFINE_INT docs).
 */
SK_ATOMIC_DEFINE_INT(u64, atomic_ullong, u64)

/**
 * i64 atomic. Same family as sk_atomic_u32_* (see SK_ATOMIC_DEFINE_INT docs).
 */
SK_ATOMIC_DEFINE_INT(i64, atomic_llong, i64)

/**
 * void_ptr_t atomic (pointer publish/snapshot). Core family only: no fetch
 * add/sub/or/and, because void has no complete pointee size.
 */
SK_ATOMIC_DEFINE_CORE(ptr, _Atomic(void_ptr_t), void_ptr_t)

/**
 * Acquire thread fence: no memory access after the fence may be reordered
 * before it. Pairs with a release store/fence from a producer.
 */
SK_FINLINE void sk_atomic_thread_fence_acquire(void) {
	atomic_thread_fence(memory_order_acquire);
}

/**
 * Release thread fence: no memory access before the fence may be reordered
 * after it. Pairs with an acquire load/fence from a consumer.
 */
SK_FINLINE void sk_atomic_thread_fence_release(void) {
	atomic_thread_fence(memory_order_release);
}

/**
 * Sequentially consistent thread fence (strongest ordering).
 */
SK_FINLINE void sk_atomic_thread_fence_seq_cst(void) {
	atomic_thread_fence(memory_order_seq_cst);
}

#ifdef __cplusplus
}
#endif
