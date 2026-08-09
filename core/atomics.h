#pragma once

/**
 * @file atomics.h
 * @brief Owned atomic operations for u32, i32, u64, i64, and void_ptr_t.
 *
 * First-party replacement for C11 <stdatomic.h> so the engine does not depend
 * on a C11 atomics toolchain/standard mode. The public surface is:
 *
 *   - sk_atomic_order_t  (relaxed / acquire / release / acq_rel / seq_cst)
 *   - sk_atomic_thread_fence(order)
 *   - per-type families: init, load, store, exchange, strong compare_exchange,
 *     and (integers only) fetch_add / fetch_sub / fetch_or / fetch_and.
 *     Every op has a seq_cst default and an explicit "_ordered" variant.
 *
 * Implementation backends (header-only, no OS/windowing headers):
 *   - GCC / Clang (and clang-cl outside MSVC mode): the __atomic_* builtins
 *     with an explicit memory-order argument.
 *   - MSVC: the _Interlocked* intrinsics. MSVC exposes no per-operation memory
 *     order, so every order is satisfied with the full-barrier interlocked op
 *     (acquire loads read volatily and verify with an interlocked compare-exchange,
 *     stores/RMWs use interlocked ops). This is correct — it simply synchronizes
 *     more strongly than requested.
 *
 * Atomic objects are plain values (u32, i32, u64, i64, void_ptr_t). They must
 * be naturally aligned (guaranteed for plain variables, struct members, and
 * arrays of these types) and are addressed by pointer at each call site.
 * sk_atomic_*_init performs a plain store and must only run before the object
 * is published to other threads.
 */

#include "common.h"

#if defined(_MSC_VER)
/*
 * MSVC <intrin.h> uses __declspec(noreturn). C11 <stdnoreturn.h> (and some
 * clang-tidy + UCRT paths) define `noreturn` as `_Noreturn`, which then breaks
 * that attribute. Drop the macro for the include only.
 */
#ifdef noreturn
#undef noreturn
#endif
#include <intrin.h> /* _Interlocked* */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Memory order                                                              */
/* ------------------------------------------------------------------------- */

/**
 * Memory ordering for atomic operations and fences.
 * Ops documented for a given order map onto the hardware barrier(s) that
 * implement it on the active backend.
 */
typedef enum sk_atomic_order_t {
	SK_ATOMIC_ORDER_RELAXED = 0,
	SK_ATOMIC_ORDER_ACQUIRE = 1,
	SK_ATOMIC_ORDER_RELEASE = 2,
	SK_ATOMIC_ORDER_ACQ_REL = 3,
	SK_ATOMIC_ORDER_SEQ_CST = 4
} sk_atomic_order_t;

#if !defined(_MSC_VER)

/** Map an sk_atomic_order_t to the matching GCC/Clang __atomic constant. */
SK_FINLINE int sk_atomic_gcc_order(sk_atomic_order_t order) {
	switch (order) {
	case SK_ATOMIC_ORDER_RELAXED:
		return __ATOMIC_RELAXED;
	case SK_ATOMIC_ORDER_ACQUIRE:
		return __ATOMIC_ACQUIRE;
	case SK_ATOMIC_ORDER_RELEASE:
		return __ATOMIC_RELEASE;
	case SK_ATOMIC_ORDER_ACQ_REL:
		return __ATOMIC_ACQ_REL;
	case SK_ATOMIC_ORDER_SEQ_CST:
		return __ATOMIC_SEQ_CST;
	}
	return __ATOMIC_SEQ_CST;
}

#endif /* !_MSC_VER */

/**
 * Thread fence at the given ordering.
 * @param order Ordering strength for the fence.
 */
SK_FINLINE void sk_atomic_thread_fence(sk_atomic_order_t order) {
#if defined(_MSC_VER)
	(void)order;
	{
		volatile u64 fence_word = 0ull;
		_InterlockedExchange64((volatile __int64*)&fence_word, 0);
	}
#else
	__atomic_thread_fence(sk_atomic_gcc_order(order));
#endif
}

/* ------------------------------------------------------------------------- */
/* GCC / Clang backend: __atomic_* builtins                                  */
/* ------------------------------------------------------------------------- */

#if !defined(_MSC_VER)

/*
 * bugprone-macro-parentheses wants (type) around type-token args. That is valid
 * for casts/expressions but invalid in declarations (e.g. (u32)* obj). These
 * generators intentionally use type/prefix as tokens; expression uses of value/
 * order are parenthesized below where it matters for real precedence bugs.
 */
// NOLINTBEGIN(bugprone-macro-parentheses)

#define SK_ATOMIC_DEFINE_INTEGRAL_GCC(prefix, type)                                                                                                                           \
	SK_FINLINE void sk_atomic_##prefix##_init(type* obj, type value) {                                                                                                        \
		*obj = (value);                                                                                                                                                       \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_load_ordered(const type* obj, sk_atomic_order_t order) {                                                                             \
		return __atomic_load_n((obj), sk_atomic_gcc_order((order)));                                                                                                          \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_load(const type* obj) {                                                                                                              \
		return sk_atomic_##prefix##_load_ordered((obj), SK_ATOMIC_ORDER_SEQ_CST);                                                                                             \
	}                                                                                                                                                                         \
	SK_FINLINE void sk_atomic_##prefix##_store_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                      \
		__atomic_store_n((obj), (value), sk_atomic_gcc_order((order)));                                                                                                       \
	}                                                                                                                                                                         \
	SK_FINLINE void sk_atomic_##prefix##_store(type* obj, type value) {                                                                                                       \
		sk_atomic_##prefix##_store_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                                          \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_exchange_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                   \
		return __atomic_exchange_n((obj), (value), sk_atomic_gcc_order((order)));                                                                                             \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_exchange(type* obj, type value) {                                                                                                    \
		return sk_atomic_##prefix##_exchange_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                                \
	}                                                                                                                                                                         \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange_ordered(type* obj, type* expected, type desired, sk_atomic_order_t success_order, sk_atomic_order_t failure_order) { \
		return __atomic_compare_exchange_n((obj), (expected), (desired), 0, sk_atomic_gcc_order((success_order)), sk_atomic_gcc_order((failure_order)));                      \
	}                                                                                                                                                                         \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange(type* obj, type* expected, type desired) {                                                                           \
		return sk_atomic_##prefix##_compare_exchange_ordered((obj), (expected), (desired), SK_ATOMIC_ORDER_SEQ_CST, SK_ATOMIC_ORDER_SEQ_CST);                                 \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_add_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                  \
		return __atomic_fetch_add((obj), (value), sk_atomic_gcc_order((order)));                                                                                              \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_add(type* obj, type value) {                                                                                                   \
		return sk_atomic_##prefix##_fetch_add_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                               \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_sub_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                  \
		return __atomic_fetch_sub((obj), (value), sk_atomic_gcc_order((order)));                                                                                              \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_sub(type* obj, type value) {                                                                                                   \
		return sk_atomic_##prefix##_fetch_sub_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                               \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_or_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                   \
		return __atomic_fetch_or((obj), (value), sk_atomic_gcc_order((order)));                                                                                               \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_or(type* obj, type value) {                                                                                                    \
		return sk_atomic_##prefix##_fetch_or_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                                \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_and_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                  \
		return __atomic_fetch_and((obj), (value), sk_atomic_gcc_order((order)));                                                                                              \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_and(type* obj, type value) {                                                                                                   \
		return sk_atomic_##prefix##_fetch_and_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                               \
	}

#define SK_ATOMIC_DEFINE_PTR_GCC(prefix)                                                                                                                     \
	SK_FINLINE void sk_atomic_##prefix##_init(void_ptr_t* obj, void_ptr_t value) {                                                                           \
		*obj = (value);                                                                                                                                      \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_load_ordered(const void_ptr_t* obj, sk_atomic_order_t order) {                                                \
		return __atomic_load_n((obj), sk_atomic_gcc_order((order)));                                                                                         \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_load(const void_ptr_t* obj) {                                                                                 \
		return sk_atomic_##prefix##_load_ordered((obj), SK_ATOMIC_ORDER_SEQ_CST);                                                                            \
	}                                                                                                                                                        \
	SK_FINLINE void sk_atomic_##prefix##_store_ordered(void_ptr_t* obj, void_ptr_t value, sk_atomic_order_t order) {                                         \
		__atomic_store_n((obj), (value), sk_atomic_gcc_order((order)));                                                                                      \
	}                                                                                                                                                        \
	SK_FINLINE void sk_atomic_##prefix##_store(void_ptr_t* obj, void_ptr_t value) {                                                                          \
		sk_atomic_##prefix##_store_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                         \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_exchange_ordered(void_ptr_t* obj, void_ptr_t value, sk_atomic_order_t order) {                                \
		return __atomic_exchange_n((obj), (value), sk_atomic_gcc_order((order)));                                                                            \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_exchange(void_ptr_t* obj, void_ptr_t value) {                                                                 \
		return sk_atomic_##prefix##_exchange_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                               \
	}                                                                                                                                                        \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange_ordered(void_ptr_t* obj, void_ptr_t* expected, void_ptr_t desired, sk_atomic_order_t success_order, \
																 sk_atomic_order_t failure_order) {                                                          \
		return __atomic_compare_exchange_n((obj), (expected), (desired), 0, sk_atomic_gcc_order((success_order)), sk_atomic_gcc_order((failure_order)));     \
	}                                                                                                                                                        \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange(void_ptr_t* obj, void_ptr_t* expected, void_ptr_t desired) {                                        \
		return sk_atomic_##prefix##_compare_exchange_ordered((obj), (expected), (desired), SK_ATOMIC_ORDER_SEQ_CST, SK_ATOMIC_ORDER_SEQ_CST);                \
	}

// NOLINTEND(bugprone-macro-parentheses)

/*
 * readability-non-const-parameter cannot see through the __atomic_* builtins:
 * every generated op mutates *obj (store/exchange/fetch/cmpxchg) or *expected
 * (cmpxchg failure value), and the builtins require non-const pointers. There
 * is nothing to const-qualify, so silence the check for the generated bodies.
 */
// NOLINTBEGIN(readability-non-const-parameter)

SK_ATOMIC_DEFINE_INTEGRAL_GCC(u32, u32)
SK_ATOMIC_DEFINE_INTEGRAL_GCC(i32, i32)
SK_ATOMIC_DEFINE_INTEGRAL_GCC(u64, u64)
SK_ATOMIC_DEFINE_INTEGRAL_GCC(i64, i64)
SK_ATOMIC_DEFINE_PTR_GCC(ptr)

// NOLINTEND(readability-non-const-parameter)

#endif /* !_MSC_VER */

/* ------------------------------------------------------------------------- */
/* MSVC backend: _Interlocked* intrinsics                                    */
/*                                                                           */
/* MSVC has no per-operation memory order; every op uses the full-barrier    */
/* interlocked intrinsic (or a plain volatile access on x86/x64, which is    */
/* atomic by hardware guarantee). Correct, just stronger than requested.     */
/* neg_expr is the two's-complement negation of `value` in the object type,  */
/* used to implement fetch_sub through _InterlockedExchangeAdd.              */
/* ------------------------------------------------------------------------- */

#if defined(_MSC_VER)

/* See GCC block: type/msc_type tokens cannot be parenthesized in declarations. */
// NOLINTBEGIN(bugprone-macro-parentheses)

#define SK_ATOMIC_DEFINE_INTEGRAL_MSVC(prefix, type, msc_type, xchg_fn, cmpxchg_fn, add_fn, and_fn, or_fn, neg_expr)                                                          \
	SK_FINLINE void sk_atomic_##prefix##_init(type* obj, type value) {                                                                                                        \
		*obj = (value);                                                                                                                                                       \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_load_ordered(const type* obj, sk_atomic_order_t order) {                                                                             \
		(void)(order);                                                                                                                                                        \
		/* 64-bit atomics require 8-byte alignment (guaranteed for these types). */                                                                                           \
		type value = *(const volatile type*)(obj);                                                                                                                            \
		if ((order) != SK_ATOMIC_ORDER_RELAXED) {                                                                                                                             \
			while ((cmpxchg_fn)(SK_CONST_CAST(volatile msc_type*, (obj)), (msc_type)(value), (msc_type)(value)) != (msc_type)(value)) {                                       \
				value = *(const volatile type*)(obj);                                                                                                                         \
			}                                                                                                                                                                 \
		}                                                                                                                                                                     \
		return (value);                                                                                                                                                       \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_load(const type* obj) {                                                                                                              \
		return sk_atomic_##prefix##_load_ordered((obj), SK_ATOMIC_ORDER_SEQ_CST);                                                                                             \
	}                                                                                                                                                                         \
	SK_FINLINE void sk_atomic_##prefix##_store_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                      \
		(void)(order);                                                                                                                                                        \
		(xchg_fn)((volatile msc_type*)(obj), (msc_type)(value));                                                                                                              \
	}                                                                                                                                                                         \
	SK_FINLINE void sk_atomic_##prefix##_store(type* obj, type value) {                                                                                                       \
		sk_atomic_##prefix##_store_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                                          \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_exchange_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                   \
		(void)(order);                                                                                                                                                        \
		return (type)(xchg_fn)((volatile msc_type*)(obj), (msc_type)(value));                                                                                                 \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_exchange(type* obj, type value) {                                                                                                    \
		return sk_atomic_##prefix##_exchange_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                                \
	}                                                                                                                                                                         \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange_ordered(type* obj, type* expected, type desired, sk_atomic_order_t success_order, sk_atomic_order_t failure_order) { \
		(void)(success_order);                                                                                                                                                \
		(void)(failure_order);                                                                                                                                                \
		msc_type read = (cmpxchg_fn)((volatile msc_type*)(obj), (msc_type)(desired), (msc_type) * (expected));                                                                \
		if ((read) == (msc_type) * (expected)) {                                                                                                                              \
			return 1;                                                                                                                                                         \
		}                                                                                                                                                                     \
		*(expected) = (type)(read);                                                                                                                                           \
		return 0;                                                                                                                                                             \
	}                                                                                                                                                                         \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange(type* obj, type* expected, type desired) {                                                                           \
		return sk_atomic_##prefix##_compare_exchange_ordered((obj), (expected), (desired), SK_ATOMIC_ORDER_SEQ_CST, SK_ATOMIC_ORDER_SEQ_CST);                                 \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_add_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                  \
		(void)(order);                                                                                                                                                        \
		return (type)(add_fn)((volatile msc_type*)(obj), (msc_type)(value));                                                                                                  \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_add(type* obj, type value) {                                                                                                   \
		return sk_atomic_##prefix##_fetch_add_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                               \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_sub_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                  \
		(void)(order);                                                                                                                                                        \
		return (type)(add_fn)((volatile msc_type*)(obj), (msc_type)(neg_expr));                                                                                               \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_sub(type* obj, type value) {                                                                                                   \
		return sk_atomic_##prefix##_fetch_sub_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                               \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_or_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                   \
		(void)(order);                                                                                                                                                        \
		return (type)(or_fn)((volatile msc_type*)(obj), (msc_type)(value));                                                                                                   \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_or(type* obj, type value) {                                                                                                    \
		return sk_atomic_##prefix##_fetch_or_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                                \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_and_ordered(type* obj, type value, sk_atomic_order_t order) {                                                                  \
		(void)(order);                                                                                                                                                        \
		return (type)(and_fn)((volatile msc_type*)(obj), (msc_type)(value));                                                                                                  \
	}                                                                                                                                                                         \
	SK_FINLINE type sk_atomic_##prefix##_fetch_and(type* obj, type value) {                                                                                                   \
		return sk_atomic_##prefix##_fetch_and_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                               \
	}

#define SK_ATOMIC_DEFINE_PTR_MSVC(prefix)                                                                                                                    \
	SK_FINLINE void sk_atomic_##prefix##_init(void_ptr_t* obj, void_ptr_t value) {                                                                           \
		*obj = (value);                                                                                                                                      \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_load_ordered(const void_ptr_t* obj, sk_atomic_order_t order) {                                                \
		(void)(order);                                                                                                                                       \
		void_ptr_t value = *(const volatile void_ptr_t*)(obj);                                                                                               \
		if ((order) != SK_ATOMIC_ORDER_RELAXED) {                                                                                                            \
			while (_InterlockedCompareExchangePointer(SK_CONST_CAST(void_ptr_t volatile*, (obj)), (value), (value)) != (value)) {                            \
				value = *(const volatile void_ptr_t*)(obj);                                                                                                  \
			}                                                                                                                                                \
		}                                                                                                                                                    \
		return (value);                                                                                                                                      \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_load(const void_ptr_t* obj) {                                                                                 \
		return sk_atomic_##prefix##_load_ordered((obj), SK_ATOMIC_ORDER_SEQ_CST);                                                                            \
	}                                                                                                                                                        \
	SK_FINLINE void sk_atomic_##prefix##_store_ordered(void_ptr_t* obj, void_ptr_t value, sk_atomic_order_t order) {                                         \
		(void)(order);                                                                                                                                       \
		_InterlockedExchangePointer(SK_CONST_CAST(void_ptr_t volatile*, (obj)), (value));                                                                    \
	}                                                                                                                                                        \
	SK_FINLINE void sk_atomic_##prefix##_store(void_ptr_t* obj, void_ptr_t value) {                                                                          \
		sk_atomic_##prefix##_store_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                                         \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_exchange_ordered(void_ptr_t* obj, void_ptr_t value, sk_atomic_order_t order) {                                \
		(void)(order);                                                                                                                                       \
		return _InterlockedExchangePointer(SK_CONST_CAST(void_ptr_t volatile*, (obj)), (value));                                                             \
	}                                                                                                                                                        \
	SK_FINLINE void_ptr_t sk_atomic_##prefix##_exchange(void_ptr_t* obj, void_ptr_t value) {                                                                 \
		return sk_atomic_##prefix##_exchange_ordered((obj), (value), SK_ATOMIC_ORDER_SEQ_CST);                                                               \
	}                                                                                                                                                        \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange_ordered(void_ptr_t* obj, void_ptr_t* expected, void_ptr_t desired, sk_atomic_order_t success_order, \
																 sk_atomic_order_t failure_order) {                                                          \
		(void)(success_order);                                                                                                                               \
		(void)(failure_order);                                                                                                                               \
		void_ptr_t read = _InterlockedCompareExchangePointer(SK_CONST_CAST(void_ptr_t volatile*, (obj)), (desired), *(expected));                            \
		if ((read) == *(expected)) {                                                                                                                         \
			return 1;                                                                                                                                        \
		}                                                                                                                                                    \
		*(expected) = (read);                                                                                                                                \
		return 0;                                                                                                                                            \
	}                                                                                                                                                        \
	SK_FINLINE i32 sk_atomic_##prefix##_compare_exchange(void_ptr_t* obj, void_ptr_t* expected, void_ptr_t desired) {                                        \
		return sk_atomic_##prefix##_compare_exchange_ordered((obj), (expected), (desired), SK_ATOMIC_ORDER_SEQ_CST, SK_ATOMIC_ORDER_SEQ_CST);                \
	}

// NOLINTEND(bugprone-macro-parentheses)

/* Same false positive as the GCC block: _Interlocked* intrinsics mutate the
 * pointee, so the pointers are intentionally non-const. */
// NOLINTBEGIN(readability-non-const-parameter)

SK_ATOMIC_DEFINE_INTEGRAL_MSVC(u32, u32, long, _InterlockedExchange, _InterlockedCompareExchange, _InterlockedExchangeAdd, _InterlockedAnd, _InterlockedOr, (0u - value))
SK_ATOMIC_DEFINE_INTEGRAL_MSVC(i32, i32, long, _InterlockedExchange, _InterlockedCompareExchange, _InterlockedExchangeAdd, _InterlockedAnd, _InterlockedOr, (0 - value))
SK_ATOMIC_DEFINE_INTEGRAL_MSVC(u64, u64, __int64, _InterlockedExchange64, _InterlockedCompareExchange64, _InterlockedExchangeAdd64, _InterlockedAnd64, _InterlockedOr64,
							   (0ull - value))
SK_ATOMIC_DEFINE_INTEGRAL_MSVC(i64, i64, __int64, _InterlockedExchange64, _InterlockedCompareExchange64, _InterlockedExchangeAdd64, _InterlockedAnd64, _InterlockedOr64,
							   (0 - value))
SK_ATOMIC_DEFINE_PTR_MSVC(ptr)

// NOLINTEND(readability-non-const-parameter)

#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif
