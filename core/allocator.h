#pragma once

#include "common.h"

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generic heap allocator (function-pointer table + optional instance).
 *
 * Callers allocate/free/resize through these pointers so backends
 * (mimalloc, arenas, test stubs) can be swapped without call-site changes.
 * @p instance is opaque backend state; pass it as the first argument to every
 * function pointer. Size arguments use size_t (host pointer width).
 */
typedef struct sk_allocator_t {
	/** Opaque backend state (NULL when the backend needs none). */
	void_ptr_t instance;

	/**
     * Allocate @p size bytes.
     * @param instance Allocator instance (from sk_allocator_t::instance).
     * @param size Number of bytes (0 may return NULL or a unique non-null pointer).
     * @return Pointer to memory, or NULL on failure.
     */
	void_ptr_t (*alloc)(void_ptr_t instance, size_t size);

	/**
     * Free a pointer previously returned by alloc/realloc of this allocator.
     * @param instance Allocator instance (from sk_allocator_t::instance).
     * @param ptr Pointer to free; NULL is a no-op.
     */
	void (*free)(void_ptr_t instance, void_ptr_t ptr);

	/**
     * Grow or shrink a block.
     * @param instance Allocator instance (from sk_allocator_t::instance).
     * @param ptr Existing block (NULL acts like alloc).
     * @param size New size in bytes.
     * @return Pointer to resized memory (may move), or NULL on failure
     *         (original block remains valid when @p ptr was non-NULL).
     */
	void_ptr_t (*realloc)(void_ptr_t instance, void_ptr_t ptr, size_t size);
} sk_allocator_t;

/**
 * Fill @p out with the default general-purpose allocator (mimalloc backend).
 * Mimalloc headers/symbols are not part of the public surface; only this table is.
 *
 * @param out Destination table; must not be NULL.
 */
void sk_allocator_get_default(sk_allocator_t* out);

/**
 * Return a process-lifetime pointer to the default allocator table.
 * Same backend as sk_allocator_get_default.
 *
 * @return Non-NULL pointer to a static sk_allocator_t.
 */
const sk_allocator_t* sk_allocator_default(void);

#ifdef __cplusplus
}
#endif
