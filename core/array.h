#pragma once

/**
 * @file array.h
 * @brief Typed dynamic array via SK_ARRAY(T).
 *
 * Usage:
 *   typedef SK_ARRAY(int) my_array_t;
 *   my_array_t a;
 *   sk_array_init(&a, allocator);  // allocator required; never uses malloc/free
 *   sk_array_push(&a, 42);
 *   sk_array_free(&a);
 */

#include "allocator.h"
#include "common.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Declare a typed growable array struct.
 * @param T Element type (must be assignable / copyable by value).
 *
 * Fields:
 * - items     Contiguous element buffer (NULL when capacity == 0)
 * - count     Live element count
 * - capacity  Allocated element slots
 * - allocator Allocator used for reserve/grow/free (set on init)
 */
#define SK_ARRAY(T)                      \
	struct {                             \
		T* items;                        \
		u32 count;                       \
		u32 capacity;                    \
		const sk_allocator_t* allocator; \
	}

/**
 * Initialize an empty array. Does not allocate.
 * @param arr   Pointer to SK_ARRAY(...) instance
 * @param alloc Non-NULL allocator table (required for any later growth)
 */
#define sk_array_init(arr, alloc)   \
	do {                            \
		(arr)->items = NULL;        \
		(arr)->count = 0u;          \
		(arr)->capacity = 0u;       \
		(arr)->allocator = (alloc); \
	} while (0)

/**
 * Free backing storage and reset to empty (allocator pointer cleared).
 * Safe on an already-freed or zeroed array.
 */
#define sk_array_free(arr)                                                    \
	do {                                                                      \
		if ((arr)->items != NULL) {                                           \
			(arr)->allocator->free((arr)->allocator->instance, (arr)->items); \
		}                                                                     \
		(arr)->items = NULL;                                                  \
		(arr)->count = 0u;                                                    \
		(arr)->capacity = 0u;                                                 \
		(arr)->allocator = NULL;                                              \
	} while (0)

/** Set count to 0 without freeing capacity. */
#define sk_array_clear(arr) ((void)((arr)->count = 0u))

/**
 * Ensure capacity is at least @p n elements.
 * @return 0 on success, non-zero on failure (OOM / missing allocator).
 */
#define sk_array_reserve(arr, n) sk_array_reserve_((void**)&(arr)->items, &(arr)->capacity, sizeof(*(arr)->items), (u32)(n), (arr)->allocator)

/**
 * Grow or shrink logical length. New elements past the old count are zeroed.
 * @return 0 on success, non-zero on failure.
 */
#define sk_array_resize(arr, n) sk_array_resize_((void**)&(arr)->items, &(arr)->count, &(arr)->capacity, sizeof(*(arr)->items), (u32)(n), (arr)->allocator)

/**
 * Append @p value by copy. Grows capacity as needed (8, then *2).
 * @return 0 on success, non-zero on failure.
 */
#define sk_array_push(arr, value) (sk_array_reserve((arr), (arr)->count + 1u) == 0 ? ((void)((arr)->items[(arr)->count++] = (value)), 0) : -1)

/**
 * Remove and return the last element. Undefined if count == 0.
 * Does not shrink capacity.
 */
#define sk_array_pop(arr) ((arr)->items[--(arr)->count])

/** Lvalue access to element at index @p i (no bounds check). */
#define sk_array_at(arr, i) ((arr)->items[(i)])

/**
 * Remove element at index @p i by swapping with the last element (O(1)).
 * Order is not preserved. Undefined if i >= count.
 */
#define sk_array_swap_remove(arr, i)                         \
	do {                                                     \
		(arr)->items[(i)] = (arr)->items[(arr)->count - 1u]; \
		(arr)->count--;                                      \
	} while (0)

/**
 * Ensure @p *items has room for at least @p min_capacity elements of
 * @p elem_size. Does not change logical count.
 *
 * @param items        Address of the element pointer field
 * @param capacity     Address of the capacity field
 * @param elem_size    sizeof(element)
 * @param min_capacity Required minimum slots
 * @param allocator    Allocator (must not be NULL when growth is needed)
 * @return 0 on success, non-zero on failure
 */
i32 sk_array_reserve_(void** items, u32* capacity, size_t elem_size, u32 min_capacity, const sk_allocator_t* allocator);

/**
 * Resize logical length to @p new_count (reserves then updates count).
 * Newly exposed bytes are zero-filled.
 *
 * @return 0 on success, non-zero on failure
 */
i32 sk_array_resize_(void** items, u32* count, u32* capacity, size_t elem_size, u32 new_count, const sk_allocator_t* allocator);

#ifdef __cplusplus
}
#endif
