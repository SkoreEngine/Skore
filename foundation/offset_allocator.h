/*
 * MIT License
 *
 * Copyright (c) 2023 Sebastian Aaltonen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

/**
 * @file offset_allocator.h
 * @brief Offset-based sub-allocator with bin-indexed free lists and neighbor
 *        coalescing (pure C port of sebbbi's OffsetAllocator).
 *
 * Allocates regions inside a caller-declared storage space of @p size bytes.
 * An allocation is an (offset, metadata) pair: @p offset is the region's
 * position in the storage space, @p metadata is opaque internal state
 * (a node index) that free() and allocationSize() require. When no region
 * fits, allocate() returns an allocation whose offset is
 * SK_OFFSET_ALLOCATOR_NO_SPACE (0xffffffff) — there are no exceptions.
 *
 * Free regions are tracked in 256 size-class bins whose sizes follow an
 * 8-bit float-like distribution (3 exponent bits + 3 mantissa bits), so the
 * relative fragmentation overhead is bounded per size class. A two-level
 * (top/leaf) bitmask finds the smallest sufficient bin in O(1)-ish time.
 * free() merges the released region with its adjacent free neighbors
 * (tracked via per-node neighbor links), so the allocator returns to a
 * single free region when everything is released.
 *
 * All internal arrays (node storage, free-node index stack, bin heads) are
 * allocated once at create() through the caller-provided sk_allocator_t;
 * allocate()/free() never touch the heap.
 */

#include "allocator.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Size-class bin encoding (see SmallFloat in offset_allocator.c)            */
/* ------------------------------------------------------------------------- */

/** Number of top-level bins (one bit in the top-level bitmask each). */
#define SK_OFFSET_ALLOCATOR_NUM_TOP_BINS 32u

/** Number of leaf bins per top-level bin. */
#define SK_OFFSET_ALLOCATOR_BINS_PER_LEAF 8u

/** Shift from a bin index to its top-level bin index (log2(BINS_PER_LEAF)). */
#define SK_OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT 3u

/** Mask from a bin index to its leaf-bin index within the top-level bin. */
#define SK_OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK 7u

/** Total number of size-class bins. */
#define SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS (SK_OFFSET_ALLOCATOR_NUM_TOP_BINS * SK_OFFSET_ALLOCATOR_BINS_PER_LEAF)

/** Default maximum number of live allocations (node capacity) when 0 is passed to create(). */
#define SK_OFFSET_ALLOCATOR_DEFAULT_MAX_ALLOCS (128u * 1024u)

/**
 * Invalid offset / metadata sentinel. An allocation with
 * offset == SK_OFFSET_ALLOCATOR_NO_SPACE means the allocator had no space.
 */
#define SK_OFFSET_ALLOCATOR_NO_SPACE 0xffffffffu

/* ------------------------------------------------------------------------- */
/* Public types                                                              */
/* ------------------------------------------------------------------------- */

/**
 * One allocation result: a region inside the allocator's storage space.
 * @field offset   Byte offset of the region; SK_OFFSET_ALLOCATOR_NO_SPACE when the
 *                 allocator could not satisfy the request.
 * @field metadata Opaque internal node index; pass back to free() / allocationSize().
 */
typedef struct sk_offset_allocator_allocation_t {
	u32 offset;
	u32 metadata;
} sk_offset_allocator_allocation_t;

/**
 * Summary storage report.
 * @field total_free_space   Total free bytes (sum of all free regions).
 * @field largest_free_region Size of the largest single free region.
 */
typedef struct sk_offset_allocator_storage_report_t {
	u32 total_free_space;
	u32 largest_free_region;
} sk_offset_allocator_storage_report_t;

/**
 * One size-class bin's free-region inventory (storageReportFull).
 * @field size  Smallest region size that lands in this bin (bin lower bound).
 * @field count Number of free regions currently in this bin.
 */
typedef struct sk_offset_allocator_free_region_t {
	u32 size;
	u32 count;
} sk_offset_allocator_free_region_t;

/**
 * Per-bin free-region inventory; index i covers bin i (0..SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS-1).
 * @field free_regions Per-bin counts; sizes follow the 8-bit float-like encoding.
 */
typedef struct sk_offset_allocator_storage_report_full_t {
	sk_offset_allocator_free_region_t free_regions[SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS];
} sk_offset_allocator_storage_report_full_t;

/** Opaque offset allocator instance. */
typedef struct sk_offset_allocator_t sk_offset_allocator_t;

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * Create an offset allocator over a storage space of @p size bytes.
 * The node/bin arrays are allocated immediately through @p allocator;
 * allocate()/free() never allocate afterwards.
 *
 * @param size       Storage space size in bytes (the sum of all live allocations
 *                   plus free regions never exceeds it).
 * @param max_allocs Maximum number of live allocations; 0 selects
 *                   SK_OFFSET_ALLOCATOR_DEFAULT_MAX_ALLOCS. Needs at least 3 to
 *                   hold the initial whole-storage region, one allocation, and
 *                   its split remainder.
 * @param allocator  Heap allocator backing the internal arrays; must outlive the
 *                   created instance (non-NULL).
 * @return New instance, or NULL when an internal allocation fails.
 */
sk_offset_allocator_t* sk_offset_allocator_create(u32 size, u32 max_allocs, const sk_allocator_t* allocator);

/**
 * Destroy an instance and release its internal arrays through the allocator
 * it was created with.
 * @param allocator Instance to destroy (must not be NULL).
 */
void sk_offset_allocator_destroy(sk_offset_allocator_t* allocator);

/**
 * Reset an instance to its initial state: the whole storage space becomes a
 * single free region and every node index is recycled. No heap traffic.
 * @param allocator Instance to reset (must not be NULL).
 */
void sk_offset_allocator_reset(sk_offset_allocator_t* allocator);

/**
 * Allocate a region of @p size bytes inside the storage space.
 *
 * @param allocator Instance to allocate from (must not be NULL).
 * @param size      Requested size in bytes (0 is legal and returns offset 0
 *                  when a node slot is available).
 * @return Allocation with the region offset; offset is
 *         SK_OFFSET_ALLOCATOR_NO_SPACE (and metadata likewise) when the
 *         storage space or the node capacity is exhausted.
 */
sk_offset_allocator_allocation_t sk_offset_allocator_allocate(sk_offset_allocator_t* allocator, u32 size);

/**
 * Free a region previously returned by allocate() (the allocation metadata
 * must be valid and the region must be live). Adjacent free regions are
 * coalesced into one.
 * @param allocator  Instance the allocation came from (must not be NULL).
 * @param allocation Allocation to free (must be a live allocation of this
 *                   instance; double-free is a debug assert).
 */
void sk_offset_allocator_free(sk_offset_allocator_t* allocator, sk_offset_allocator_allocation_t allocation);

/**
 * Return the size of a live allocation.
 * @param allocator  Instance the allocation came from (must not be NULL).
 * @param allocation Allocation to query (must be a live allocation of this
 *                   instance, or a NO_SPACE allocation).
 * @return Allocated size in bytes; 0 for a NO_SPACE allocation.
 */
u32 sk_offset_allocator_allocation_size(const sk_offset_allocator_t* allocator, sk_offset_allocator_allocation_t allocation);

/**
 * Summarize free storage.
 * @param allocator Instance to report on (must not be NULL).
 * @return Total free bytes and the largest single free region.
 */
sk_offset_allocator_storage_report_t sk_offset_allocator_storage_report(const sk_offset_allocator_t* allocator);

/**
 * Inventory every size-class bin: how many free regions each bin holds.
 * @param allocator Instance to report on (must not be NULL).
 * @return Per-bin region counts (see sk_offset_allocator_storage_report_full_t).
 */
sk_offset_allocator_storage_report_full_t sk_offset_allocator_storage_report_full(const sk_offset_allocator_t* allocator);

#ifdef __cplusplus
}
#endif
