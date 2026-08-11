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

#include "offset_allocator.h"

#include <assert.h>

/*
 * Pure C port of sebbbi's OffsetAllocator (https://github.com/sebbbi/OffsetAllocator).
 * The algorithm, constants, and control flow mirror the upstream
 * implementation; only the language plumbing (types, allocator, naming) is
 * skore v2 style.
 *
 * Free regions live in 256 size-class bins. Bin sizes follow a floating
 * point (exponent + mantissa) distribution: 3 exponent-ish MSB bits plus
 * 3 mantissa bits, encoded as an 8-bit "small float" (see SmallFloat below).
 * A two-level bitmask (32-bit top level + 32 bytes of leaf bitmasks) finds
 * the smallest sufficient bin quickly. Each free region is a node in a
 * doubly linked list per bin; nodes also carry neighbor links so that
 * free() can coalesce adjacent free regions.
 */

/* ------------------------------------------------------------------------- */
/* SmallFloat: 8-bit float-like bin encoding (3 exponent bits + 3 mantissa)  */
/* ------------------------------------------------------------------------- */

#define OA_MANTISSA_BITS 3u
#define OA_MANTISSA_VALUE (1u << OA_MANTISSA_BITS) /* 8 */
#define OA_MANTISSA_MASK (OA_MANTISSA_VALUE - 1u)  /* 7 */

/* ------------------------------------------------------------------------- */
/* Bit intrinsics                                                            */
/* ------------------------------------------------------------------------- */

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>

/* Count leading zeros of a non-zero u32 (31 - position of highest set bit). */
static u32 oa_lzcnt_nonzero(u32 v) {
	unsigned long ret_val;
	_BitScanReverse(&ret_val, v);
	return 31u - (u32)ret_val;
}

/* Count trailing zeros of a non-zero u32 (position of lowest set bit). */
static u32 oa_tzcnt_nonzero(u32 v) {
	unsigned long ret_val;
	_BitScanForward(&ret_val, v);
	return (u32)ret_val;
}
#else
static u32 oa_lzcnt_nonzero(u32 v) {
	return (u32)__builtin_clz(v);
}

static u32 oa_tzcnt_nonzero(u32 v) {
	return (u32)__builtin_ctz(v);
}
#endif

/* ------------------------------------------------------------------------- */
/* SmallFloat encoding                                                       */
/* ------------------------------------------------------------------------- */

/*
 * Bin sizes follow a floating point (exponent + mantissa) distribution
 * (piecewise linear log approximation). This ensures that for each size
 * class, the average overhead percentage stays the same.
 */

/* Round @p size up to the smallest bin index whose regions all fit it. */
static u32 oa_uint_to_float_round_up(u32 size) {
	u32 exp = 0u;
	u32 mantissa = 0u;

	if (size < OA_MANTISSA_VALUE) {
		/* Denorm: 0..(OA_MANTISSA_VALUE-1) */
		mantissa = size;
	} else {
		/* Normalized: hidden high bit always 1. Not stored. Just like float. */
		u32 leading_zeros = oa_lzcnt_nonzero(size);
		u32 highest_set_bit = 31u - leading_zeros;

		u32 mantissa_start_bit = highest_set_bit - OA_MANTISSA_BITS;
		exp = mantissa_start_bit + 1u;
		mantissa = (size >> mantissa_start_bit) & OA_MANTISSA_MASK;

		u32 low_bits_mask = (1u << mantissa_start_bit) - 1u;

		/* Round up! */
		if ((size & low_bits_mask) != 0u) {
			mantissa++;
		}
	}

	return (exp << OA_MANTISSA_BITS) + mantissa; /* + allows mantissa->exp overflow for round up */
}

/* Round @p size down to the largest bin index whose regions are all at least it. */
static u32 oa_uint_to_float_round_down(u32 size) {
	u32 exp = 0u;
	u32 mantissa = 0u;

	if (size < OA_MANTISSA_VALUE) {
		/* Denorm: 0..(OA_MANTISSA_VALUE-1) */
		mantissa = size;
	} else {
		/* Normalized: hidden high bit always 1. Not stored. Just like float. */
		u32 leading_zeros = oa_lzcnt_nonzero(size);
		u32 highest_set_bit = 31u - leading_zeros;

		u32 mantissa_start_bit = highest_set_bit - OA_MANTISSA_BITS;
		exp = mantissa_start_bit + 1u;
		mantissa = (size >> mantissa_start_bit) & OA_MANTISSA_MASK;
	}

	return (exp << OA_MANTISSA_BITS) | mantissa;
}

/* Decode a bin index back to the smallest region size it holds. */
static u32 oa_float_to_uint(u32 float_value) {
	u32 exponent = float_value >> OA_MANTISSA_BITS;
	u32 mantissa = float_value & OA_MANTISSA_MASK;
	if (exponent == 0u) {
		/* Denorms */
		return mantissa;
	}
	return (mantissa | OA_MANTISSA_VALUE) << (exponent - 1u);
}

/* Lowest set bit of @p bit_mask at or after @p start_bit_index; NO_SPACE when none. */
static u32 oa_find_lowest_set_bit_after(u32 bit_mask, u32 start_bit_index) {
	u32 mask_before_start_index = (1u << start_bit_index) - 1u;
	u32 mask_after_start_index = ~mask_before_start_index;
	u32 bits_after = bit_mask & mask_after_start_index;
	if (bits_after == 0u) {
		return SK_OFFSET_ALLOCATOR_NO_SPACE;
	}
	return oa_tzcnt_nonzero(bits_after);
}

/* ------------------------------------------------------------------------- */
/* Internal state                                                            */
/* ------------------------------------------------------------------------- */

/* Node index sentinel: "no node". */
#define OA_NODE_UNUSED SK_OFFSET_ALLOCATOR_NO_SPACE

/*
 * One node = one region (live allocation or free region).
 * used == 0: free region, listed in its bin's free list.
 * used == 1: live allocation; neighbor links still let free() find its
 *            neighbors for coalescing.
 */
typedef struct oa_node_t {
	u32 data_offset;
	u32 data_size;
	u32 bin_list_prev;
	u32 bin_list_next;
	u32 neighbor_prev;
	u32 neighbor_next;
	u8 used; /* 0/1 flag */
} oa_node_t;

/*
 * Fixed node capacity (max_allocs) is allocated once at create().
 * free_nodes is a stack of recyclable node indices; the whole storage space
 * starts as one free node and splits/merges over the allocator's lifetime.
 */
struct sk_offset_allocator_t {
	const sk_allocator_t* allocator;					/* backing heap for the arrays below */
	oa_node_t* nodes;									/* node array (max_allocs entries) */
	u32* free_nodes;									/* free node index stack (max_allocs entries) */
	u32 size;											/* storage space size in bytes */
	u32 max_allocs;										/* node array capacity */
	u32 free_storage;									/* total free bytes */
	u32 used_bins_top;									/* top-level bin bitmask */
	u8 used_bins[SK_OFFSET_ALLOCATOR_NUM_TOP_BINS];		/* leaf bin bitmask per top-level bin (32 bytes) */
	u32 bin_indices[SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS]; /* bin free-list heads */
	u32 free_offset;									/* top of the free node index stack */
	u32 _pad;											/* struct alignment to 8 bytes */
};

static u32 oa_insert_node_into_bin(sk_offset_allocator_t* allocator, u32 size, u32 data_offset);
static void oa_remove_node_from_bin(sk_offset_allocator_t* allocator, u32 node_index);

/* ------------------------------------------------------------------------- */
/* Lifetime                                                                  */
/* ------------------------------------------------------------------------- */

sk_offset_allocator_t* sk_offset_allocator_create(u32 size, u32 max_allocs, const sk_allocator_t* allocator) {
	if (max_allocs == 0u) {
		max_allocs = SK_OFFSET_ALLOCATOR_DEFAULT_MAX_ALLOCS;
	}

	sk_offset_allocator_t* oa = (sk_offset_allocator_t*)allocator->alloc(allocator->instance, sizeof(*oa));
	if (oa == NULL) {
		return NULL;
	}
	oa->allocator = allocator;
	oa->size = size;
	oa->max_allocs = max_allocs;

	oa->nodes = (oa_node_t*)allocator->alloc(allocator->instance, sizeof(*oa->nodes) * max_allocs);
	if (oa->nodes == NULL) {
		allocator->free(allocator->instance, oa);
		return NULL;
	}
	oa->free_nodes = (u32*)allocator->alloc(allocator->instance, sizeof(*oa->free_nodes) * max_allocs);
	if (oa->free_nodes == NULL) {
		allocator->free(allocator->instance, oa->nodes);
		allocator->free(allocator->instance, oa);
		return NULL;
	}

	sk_offset_allocator_reset(oa);
	return oa;
}

void sk_offset_allocator_destroy(sk_offset_allocator_t* allocator) {
	const sk_allocator_t* a = allocator->allocator;
	a->free(a->instance, allocator->free_nodes);
	a->free(a->instance, allocator->nodes);
	a->free(a->instance, allocator);
}

void sk_offset_allocator_reset(sk_offset_allocator_t* allocator) {
	allocator->free_storage = 0u;
	allocator->used_bins_top = 0u;
	allocator->free_offset = allocator->max_allocs - 1u;

	for (u32 i = 0u; i < SK_OFFSET_ALLOCATOR_NUM_TOP_BINS; i++) {
		allocator->used_bins[i] = 0u;
	}
	for (u32 i = 0u; i < SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++) {
		allocator->bin_indices[i] = OA_NODE_UNUSED;
	}

	/* Free list is a stack. Nodes in inverse order so that index 0 pops first. */
	for (u32 i = 0u; i < allocator->max_allocs; i++) {
		allocator->free_nodes[i] = allocator->max_allocs - i - 1u;
	}

	/* Start state: the whole storage space is one free node. The algorithm
	 * splits remainders and pushes them back as smaller nodes. */
	oa_insert_node_into_bin(allocator, allocator->size, 0u);
}

/* ------------------------------------------------------------------------- */
/* Allocate / free                                                           */
/* ------------------------------------------------------------------------- */

sk_offset_allocator_allocation_t sk_offset_allocator_allocate(sk_offset_allocator_t* allocator, u32 size) {
	sk_offset_allocator_allocation_t allocation = {SK_OFFSET_ALLOCATOR_NO_SPACE, SK_OFFSET_ALLOCATOR_NO_SPACE};

	/* Out of node slots? */
	if (allocator->free_offset == 0u) {
		return allocation;
	}

	/* Round up to a bin index to ensure that region >= size.
	 * Gives us the min bin index that fits the size. */
	u32 min_bin_index = oa_uint_to_float_round_up(size);

	u32 min_top_bin_index = min_bin_index >> SK_OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
	u32 min_leaf_bin_index = min_bin_index & SK_OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK;

	u32 top_bin_index = min_top_bin_index;
	u32 leaf_bin_index = SK_OFFSET_ALLOCATOR_NO_SPACE;

	/* If the top bin has free regions, scan its leaf bins. This can fail (NO_SPACE). */
	if ((allocator->used_bins_top & (1u << top_bin_index)) != 0u) {
		leaf_bin_index = oa_find_lowest_set_bit_after(allocator->used_bins[top_bin_index], min_leaf_bin_index);
	}

	/* If we didn't find space in the top bin, search top bins from +1. */
	if (leaf_bin_index == SK_OFFSET_ALLOCATOR_NO_SPACE) {
		top_bin_index = oa_find_lowest_set_bit_after(allocator->used_bins_top, min_top_bin_index + 1u);

		/* Out of space? */
		if (top_bin_index == SK_OFFSET_ALLOCATOR_NO_SPACE) {
			return allocation;
		}

		/* All leaf bins here fit the alloc, since the top bin was rounded up.
		 * Start the leaf search from bit 0.
		 * NOTE: This search can't fail since at least one leaf bit was set because the top bit was set. */
		leaf_bin_index = oa_tzcnt_nonzero(allocator->used_bins[top_bin_index]);
	}

	u32 bin_index = (top_bin_index << SK_OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT) | leaf_bin_index;

	/* Pop the top node of the bin. Bin top = node.bin_list_next. */
	u32 node_index = allocator->bin_indices[bin_index];
	oa_node_t* node = &allocator->nodes[node_index];
	u32 node_total_size = node->data_size;
	node->data_size = size;
	node->used = 1u;
	allocator->bin_indices[bin_index] = node->bin_list_next;
	if (node->bin_list_next != OA_NODE_UNUSED) {
		allocator->nodes[node->bin_list_next].bin_list_prev = OA_NODE_UNUSED;
	}
	allocator->free_storage -= node_total_size;

	/* Bin empty? */
	if (allocator->bin_indices[bin_index] == OA_NODE_UNUSED) {
		/* Remove a leaf bin mask bit. */
		allocator->used_bins[top_bin_index] = (u8)(allocator->used_bins[top_bin_index] & ~(1u << leaf_bin_index));

		/* All leaf bins empty? */
		if (allocator->used_bins[top_bin_index] == 0u) {
			/* Remove a top bin mask bit. */
			allocator->used_bins_top &= ~(1u << top_bin_index);
		}
	}

	/* Push back the remainder N bytes to a lower bin. */
	u32 reminder_size = node_total_size - size;
	if (reminder_size > 0u) {
		u32 new_node_index = oa_insert_node_into_bin(allocator, reminder_size, node->data_offset + size);

		/* Link the nodes next to each other so that we can merge them later
		 * if both are free. Update the old next neighbor to point to the
		 * new node (in the middle). */
		if (node->neighbor_next != OA_NODE_UNUSED) {
			allocator->nodes[node->neighbor_next].neighbor_prev = new_node_index;
		}
		allocator->nodes[new_node_index].neighbor_prev = node_index;
		allocator->nodes[new_node_index].neighbor_next = node->neighbor_next;
		node->neighbor_next = new_node_index;
	}

	allocation.offset = node->data_offset;
	allocation.metadata = node_index;
	return allocation;
}

void sk_offset_allocator_free(sk_offset_allocator_t* allocator, sk_offset_allocator_allocation_t allocation) {
	assert(allocation.metadata != SK_OFFSET_ALLOCATOR_NO_SPACE);

	u32 node_index = allocation.metadata;
	oa_node_t* node = &allocator->nodes[node_index];

	/* Double-free check. */
	assert(node->used != 0u);

	/* Merge with neighbors... */
	u32 offset = node->data_offset;
	u32 size = node->data_size;

	if ((node->neighbor_prev != OA_NODE_UNUSED) && (allocator->nodes[node->neighbor_prev].used == 0u)) {
		/* Previous (contiguous) free node: change offset to the previous node's offset. Sum sizes. */
		oa_node_t* prev_node = &allocator->nodes[node->neighbor_prev];
		offset = prev_node->data_offset;
		size += prev_node->data_size;

		/* Remove the node from the bin linked list and put it in the free list. */
		oa_remove_node_from_bin(allocator, node->neighbor_prev);

		assert(prev_node->neighbor_next == node_index);
		node->neighbor_prev = prev_node->neighbor_prev;
	}

	if ((node->neighbor_next != OA_NODE_UNUSED) && (allocator->nodes[node->neighbor_next].used == 0u)) {
		/* Next (contiguous) free node: offset remains the same. Sum sizes. */
		oa_node_t* next_node = &allocator->nodes[node->neighbor_next];
		size += next_node->data_size;

		/* Remove the node from the bin linked list and put it in the free list. */
		oa_remove_node_from_bin(allocator, node->neighbor_next);

		assert(next_node->neighbor_prev == node_index);
		node->neighbor_next = next_node->neighbor_next;
	}

	u32 neighbor_next = node->neighbor_next;
	u32 neighbor_prev = node->neighbor_prev;

	/* Insert the removed node into the free list. */
	allocator->free_nodes[++allocator->free_offset] = node_index;

	/* Insert the (combined) free node into a bin. */
	u32 combined_node_index = oa_insert_node_into_bin(allocator, size, offset);

	/* Connect the neighbors with the new combined node. */
	if (neighbor_next != OA_NODE_UNUSED) {
		allocator->nodes[combined_node_index].neighbor_next = neighbor_next;
		allocator->nodes[neighbor_next].neighbor_prev = combined_node_index;
	}
	if (neighbor_prev != OA_NODE_UNUSED) {
		allocator->nodes[combined_node_index].neighbor_prev = neighbor_prev;
		allocator->nodes[neighbor_prev].neighbor_next = combined_node_index;
	}
}

u32 sk_offset_allocator_allocation_size(const sk_offset_allocator_t* allocator, sk_offset_allocator_allocation_t allocation) {
	if (allocation.metadata == SK_OFFSET_ALLOCATOR_NO_SPACE) {
		return 0u;
	}
	return allocator->nodes[allocation.metadata].data_size;
}

/* ------------------------------------------------------------------------- */
/* Storage reports                                                           */
/* ------------------------------------------------------------------------- */

sk_offset_allocator_storage_report_t sk_offset_allocator_storage_report(const sk_offset_allocator_t* allocator) {
	sk_offset_allocator_storage_report_t report = {0u, 0u};

	/* Out of node slots? -> zero free space */
	if (allocator->free_offset > 0u) {
		report.total_free_space = allocator->free_storage;
		if (allocator->used_bins_top != 0u) {
			u32 top_bin_index = 31u - oa_lzcnt_nonzero(allocator->used_bins_top);
			u32 leaf_bin_index = 31u - oa_lzcnt_nonzero(allocator->used_bins[top_bin_index]);
			report.largest_free_region = oa_float_to_uint((top_bin_index << SK_OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT) | leaf_bin_index);
			assert(report.total_free_space >= report.largest_free_region);
		}
	}

	return report;
}

sk_offset_allocator_storage_report_full_t sk_offset_allocator_storage_report_full(const sk_offset_allocator_t* allocator) {
	sk_offset_allocator_storage_report_full_t report;
	for (u32 i = 0u; i < SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++) {
		u32 count = 0u;
		u32 node_index = allocator->bin_indices[i];
		while (node_index != OA_NODE_UNUSED) {
			node_index = allocator->nodes[node_index].bin_list_next;
			count++;
		}
		report.free_regions[i].size = oa_float_to_uint(i);
		report.free_regions[i].count = count;
	}
	return report;
}

/* ------------------------------------------------------------------------- */
/* Free-list bookkeeping                                                      */
/* ------------------------------------------------------------------------- */

/* Insert a free region into its bin; takes a node index from the free list. */
static u32 oa_insert_node_into_bin(sk_offset_allocator_t* allocator, u32 size, u32 data_offset) {
	/* Round down to a bin index to ensure that region >= size. */
	u32 bin_index = oa_uint_to_float_round_down(size);

	u32 top_bin_index = bin_index >> SK_OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
	u32 leaf_bin_index = bin_index & SK_OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK;

	/* Bin was empty before? */
	if (allocator->bin_indices[bin_index] == OA_NODE_UNUSED) {
		/* Set the bin mask bits. */
		allocator->used_bins[top_bin_index] = (u8)(allocator->used_bins[top_bin_index] | (1u << leaf_bin_index));
		allocator->used_bins_top |= 1u << top_bin_index;
	}

	/* Take a free-list node and insert it on top of the bin linked list (next = old top). */
	u32 top_node_index = allocator->bin_indices[bin_index];
	u32 node_index = allocator->free_nodes[allocator->free_offset--];
	oa_node_t* node = &allocator->nodes[node_index];
	node->data_offset = data_offset;
	node->data_size = size;
	node->bin_list_prev = OA_NODE_UNUSED;
	node->bin_list_next = top_node_index;
	node->neighbor_prev = OA_NODE_UNUSED;
	node->neighbor_next = OA_NODE_UNUSED;
	node->used = 0u;
	if (top_node_index != OA_NODE_UNUSED) {
		allocator->nodes[top_node_index].bin_list_prev = node_index;
	}
	allocator->bin_indices[bin_index] = node_index;

	allocator->free_storage += size;

	return node_index;
}

/* Remove a free region from its bin list and recycle its node index. */
static void oa_remove_node_from_bin(sk_offset_allocator_t* allocator, u32 node_index) {
	oa_node_t* node = &allocator->nodes[node_index];

	if (node->bin_list_prev != OA_NODE_UNUSED) {
		/* Easy case: we have a previous node. Just remove this node from the middle of the list. */
		allocator->nodes[node->bin_list_prev].bin_list_next = node->bin_list_next;
		if (node->bin_list_next != OA_NODE_UNUSED) {
			allocator->nodes[node->bin_list_next].bin_list_prev = node->bin_list_prev;
		}
	} else {
		/* Hard case: we are the first node in a bin. Find the bin. */

		/* Round down to a bin index to ensure that region >= size. */
		u32 bin_index = oa_uint_to_float_round_down(node->data_size);

		u32 top_bin_index = bin_index >> SK_OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
		u32 leaf_bin_index = bin_index & SK_OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK;

		allocator->bin_indices[bin_index] = node->bin_list_next;
		if (node->bin_list_next != OA_NODE_UNUSED) {
			allocator->nodes[node->bin_list_next].bin_list_prev = OA_NODE_UNUSED;
		}

		/* Bin empty? */
		if (allocator->bin_indices[bin_index] == OA_NODE_UNUSED) {
			/* Remove a leaf bin mask bit. */
			allocator->used_bins[top_bin_index] = (u8)(allocator->used_bins[top_bin_index] & ~(1u << leaf_bin_index));

			/* All leaf bins empty? */
			if (allocator->used_bins[top_bin_index] == 0u) {
				/* Remove a top bin mask bit. */
				allocator->used_bins_top &= ~(1u << top_bin_index);
			}
		}
	}

	/* Insert the node into the free list. */
	allocator->free_nodes[++allocator->free_offset] = node_index;

	allocator->free_storage -= node->data_size;
}

#ifdef SK_TESTS
#include "test.h"

#include <stdlib.h> /* malloc/free for the leak-checking allocator stub */
#include <string.h> /* memset */

/*
 * Unit tests: every upstream scenario from sebbbi's offsetAllocatorTests.cpp
 * plus the extra coverage the pure C port needs (NO_SPACE, node-pool
 * exhaustion, reset, boundary sizes, destroy-with-live-allocations).
 *
 * Expected values are the upstream ones verbatim: they are what make the
 * 8-bit float-like bin-rounding logic verifiable.
 *
 * Notes on deliberate upstream semantics that the expectations encode:
 * - storageReport().largest_free_region is the lower bound of the bin the
 *   largest free region landed in (floatToUint of the bin index), not the
 *   exact region byte count; storageReport().total_free_space is exact.
 * - A request is served only when roundUp(request) <= roundDown(region): a
 *   free region of exactly N bytes can only serve a request of N when N is
 *   bin-exact (e.g. a power of two). This is upstream behavior, not a port
 *   artifact — that is why the full-arena reallocations below use bin-exact
 *   arena sizes.
 * - A request larger than 0xf0000000 rounds up past the last usable bin
 *   (239); bin 240's lower bound would overflow u32. Same as upstream.
 */

/* Sum of free-region counts across all bins (total number of free regions). */
static u32 oa_test_free_region_count(const sk_offset_allocator_t* oa) {
	sk_offset_allocator_storage_report_full_t full = sk_offset_allocator_storage_report_full(oa);
	u32 count = 0u;
	for (u32 i = 0u; i < SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++) {
		count += full.free_regions[i].count;
	}
	return count;
}

SK_TEST(offset_allocator_small_float_numbers) {
	/* Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
	 * Assumes an 8 value (3 bit) mantissa. */
	for (u32 i = 0u; i < 17u; i++) {
		TEST_ASSERT_EQUAL_UINT32(i, oa_uint_to_float_round_up(i));
		TEST_ASSERT_EQUAL_UINT32(i, oa_uint_to_float_round_down(i));
		TEST_ASSERT_EQUAL_UINT32(i, oa_float_to_uint(i));
	}

	/* Randomly picked numbers (upstream test vectors, exact up/down bins). */
	static const struct {
		u32 number;
		u32 up;
		u32 down;
	} test_data[] = {
		{17u, 17u, 16u}, {118u, 39u, 38u}, {1024u, 64u, 64u}, {65536u, 112u, 112u}, {529445u, 137u, 136u}, {1048575u, 144u, 143u},
	};
	for (u32 i = 0u; i < (u32)(sizeof(test_data) / sizeof(test_data[0])); i++) {
		TEST_ASSERT_EQUAL_UINT32(test_data[i].up, oa_uint_to_float_round_up(test_data[i].number));
		TEST_ASSERT_EQUAL_UINT32(test_data[i].down, oa_uint_to_float_round_down(test_data[i].number));
	}

	/* float -> uint -> float round trip for every bin index.
	 * Values < 240 only: bin 240 decodes to 2^32, which overflows u32. */
	for (u32 i = 0u; i < 240u; i++) {
		u32 v = oa_float_to_uint(i);
		TEST_ASSERT_EQUAL_UINT32(i, oa_uint_to_float_round_up(v));
		TEST_ASSERT_EQUAL_UINT32(i, oa_uint_to_float_round_down(v));
	}
}

SK_TEST(offset_allocator_basic_alloc_free) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	/* Pristine: the whole arena is one free region. */
	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);

	/* Single allocation at offset 0; 256 is bin-exact so both report fields
	 * stay exact after the split. */
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 256u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	TEST_ASSERT_EQUAL_UINT32(256u, sk_offset_allocator_allocation_size(oa, a));

	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(768u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(768u, report.largest_free_region);

	/* Free restores the full region. */
	sk_offset_allocator_free(oa, a);
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);
	sk_offset_allocator_destroy(oa);

	/* Upstream "basic": 256MB arena, allocate 1337 at offset 0, free. */
	sk_offset_allocator_t* big = sk_offset_allocator_create(1024u * 1024u * 256u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(big);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(big, 1337u);
	TEST_ASSERT_EQUAL_UINT32(0u, b.offset);
	sk_offset_allocator_free(big, b);
	sk_offset_allocator_destroy(big);
}

SK_TEST(offset_allocator_allocate_simple) {
	/* Upstream "simple": free merges neighbor empty nodes, so the next
	 * allocation lands at the previous free offset. */
	const u32 arena = 1024u * 1024u * 256u;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(arena, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 0u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, b.offset);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 123u);
	TEST_ASSERT_EQUAL_UINT32(1u, c.offset);
	sk_offset_allocator_allocation_t d = sk_offset_allocator_allocate(oa, 1234u);
	TEST_ASSERT_EQUAL_UINT32(124u, d.offset);

	sk_offset_allocator_free(oa, a);
	sk_offset_allocator_free(oa, b);
	sk_offset_allocator_free(oa, c);
	sk_offset_allocator_free(oa, d);

	/* Zero fragmentation: the full arena comes back as one region at 0. */
	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, arena);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_merge_trivial) {
	/* Upstream "merge trivial": alloc/free/alloc returns the same offset. */
	const u32 arena = 1024u * 1024u * 256u;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(arena, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 1337u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	sk_offset_allocator_free(oa, a);

	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 1337u);
	TEST_ASSERT_EQUAL_UINT32(0u, b.offset);
	sk_offset_allocator_free(oa, b);

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, arena);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_reuse_trivial) {
	/* Upstream "reuse trivial": allocation C fits in the same bin as the
	 * freed A (pow2 size), so A's node is reused at offset 0. */
	const u32 arena = 1024u * 1024u * 256u;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(arena, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 3456u);
	TEST_ASSERT_EQUAL_UINT32(1024u, b.offset);

	sk_offset_allocator_free(oa, a);

	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, c.offset);

	sk_offset_allocator_free(oa, c);
	sk_offset_allocator_free(oa, b);

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, arena);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_reuse_complex) {
	/* Upstream "reuse complex": C does not fit A's freed bin, but the
	 * smaller D and E do, so they reuse A's node. */
	const u32 arena = 1024u * 1024u * 256u;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(arena, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 3456u);
	TEST_ASSERT_EQUAL_UINT32(1024u, b.offset);

	sk_offset_allocator_free(oa, a);

	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 2345u);
	TEST_ASSERT_EQUAL_UINT32(1024u + 3456u, c.offset);
	sk_offset_allocator_allocation_t d = sk_offset_allocator_allocate(oa, 456u);
	TEST_ASSERT_EQUAL_UINT32(0u, d.offset);
	sk_offset_allocator_allocation_t e = sk_offset_allocator_allocate(oa, 512u);
	TEST_ASSERT_EQUAL_UINT32(456u, e.offset);

	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(arena - 3456u - 2345u - 456u - 512u, report.total_free_space);
	TEST_ASSERT_NOT_EQUAL_UINT32(report.total_free_space, report.largest_free_region);

	sk_offset_allocator_free(oa, c);
	sk_offset_allocator_free(oa, d);
	sk_offset_allocator_free(oa, b);
	sk_offset_allocator_free(oa, e);

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, arena);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_zero_fragmentation) {
	/* Upstream "zero fragmentation": 256x 1MB fills the 256MB arena; free
	 * four random slots plus four contiguous slots, reallocate (the
	 * contiguous four as one 4MB block) — all must stay zero-fragmentation. */
	const u32 arena = 1024u * 1024u * 256u;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(arena, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t allocations[256];
	for (u32 i = 0u; i < 256u; i++) {
		allocations[i] = sk_offset_allocator_allocate(oa, 1024u * 1024u);
		TEST_ASSERT_EQUAL_UINT32(i * 1024u * 1024u, allocations[i].offset);
	}

	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(0u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(0u, report.largest_free_region);

	/* Free four random slots. */
	sk_offset_allocator_free(oa, allocations[243]);
	sk_offset_allocator_free(oa, allocations[5]);
	sk_offset_allocator_free(oa, allocations[123]);
	sk_offset_allocator_free(oa, allocations[95]);

	/* Free four contiguous slots (the allocator must merge them). */
	sk_offset_allocator_free(oa, allocations[151]);
	sk_offset_allocator_free(oa, allocations[152]);
	sk_offset_allocator_free(oa, allocations[153]);
	sk_offset_allocator_free(oa, allocations[154]);

	allocations[243] = sk_offset_allocator_allocate(oa, 1024u * 1024u);
	allocations[5] = sk_offset_allocator_allocate(oa, 1024u * 1024u);
	allocations[123] = sk_offset_allocator_allocate(oa, 1024u * 1024u);
	allocations[95] = sk_offset_allocator_allocate(oa, 1024u * 1024u);
	allocations[151] = sk_offset_allocator_allocate(oa, 1024u * 1024u * 4u); /* 4x larger */
	TEST_ASSERT_NOT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, allocations[243].offset);
	TEST_ASSERT_NOT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, allocations[5].offset);
	TEST_ASSERT_NOT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, allocations[123].offset);
	TEST_ASSERT_NOT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, allocations[95].offset);
	TEST_ASSERT_NOT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, allocations[151].offset);

	for (u32 i = 0u; i < 256u; i++) {
		if (i < 152u || i > 154u) {
			sk_offset_allocator_free(oa, allocations[i]);
		}
	}

	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(arena, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(arena, report.largest_free_region);

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, arena);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_free_front_first) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t d = sk_offset_allocator_allocate(oa, 100u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	TEST_ASSERT_EQUAL_UINT32(100u, b.offset);
	TEST_ASSERT_EQUAL_UINT32(200u, c.offset);
	TEST_ASSERT_EQUAL_UINT32(300u, d.offset);

	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(624u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(576u, report.largest_free_region); /* bin lower bound */
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, a); /* front first */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(724u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(576u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(2u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, b); /* merges with a -> [0, 200) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(824u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(576u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(2u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, c); /* merges -> [0, 300) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(924u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(576u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(2u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, d); /* merges everything -> [0, 1024) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_free_middle_first) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t d = sk_offset_allocator_allocate(oa, 100u);

	sk_offset_allocator_free(oa, b); /* hole at [100, 200) */
	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(724u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(576u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(2u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, c); /* adjacent to b -> merged [100, 300) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(824u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(576u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(2u, oa_test_free_region_count(oa)); /* 2 regions, not 3: coalesced */

	sk_offset_allocator_free(oa, d); /* merges into [100, 1024) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(924u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(896u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, a); /* everything coalesced */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_free_back_first) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t d = sk_offset_allocator_allocate(oa, 100u);

	sk_offset_allocator_free(oa, d); /* merges with the tail remainder -> [300, 1024) */
	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(724u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(704u, report.largest_free_region); /* bin lower bound */
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, c); /* -> [200, 1024) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(824u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(768u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, b); /* -> [100, 1024) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(924u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(896u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_free(oa, a); /* -> [0, 1024) */
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_allocation_size) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	/* allocationSize returns the requested size (the port stores the request
	 * verbatim, like upstream), not the bin-rounded size. */
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 17u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	TEST_ASSERT_EQUAL_UINT32(17u, sk_offset_allocator_allocation_size(oa, a));

	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 118u);
	TEST_ASSERT_EQUAL_UINT32(17u, b.offset);
	TEST_ASSERT_EQUAL_UINT32(118u, sk_offset_allocator_allocation_size(oa, b));

	/* 135 bytes are used; the 889-byte remainder cannot serve 1024. */
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, c.offset);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_offset_allocator_allocation_size(oa, c));

	sk_offset_allocator_free(oa, a);
	sk_offset_allocator_free(oa, b);

	/* Coalescing restored the whole arena. */
	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset);
	TEST_ASSERT_EQUAL_UINT32(1024u, sk_offset_allocator_allocation_size(oa, full));
	sk_offset_allocator_free(oa, full);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_storage_report_full) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	/* Bin lower bounds (floatToUint) must be exact at the spot checks. */
	sk_offset_allocator_storage_report_full_t full = sk_offset_allocator_storage_report_full(oa);
	TEST_ASSERT_EQUAL_UINT32(0u, full.free_regions[0].size);
	TEST_ASSERT_EQUAL_UINT32(7u, full.free_regions[7].size);
	TEST_ASSERT_EQUAL_UINT32(8u, full.free_regions[8].size);
	TEST_ASSERT_EQUAL_UINT32(512u, full.free_regions[56].size);
	TEST_ASSERT_EQUAL_UINT32(960u, full.free_regions[63].size);
	TEST_ASSERT_EQUAL_UINT32(1024u, full.free_regions[64].size);
	TEST_ASSERT_EQUAL_UINT32(268435456u, full.free_regions[208].size);

	/* Pristine 1024-byte arena: exactly one region, in bin 64 (holds 1024). */
	TEST_ASSERT_EQUAL_UINT32(1u, full.free_regions[64].count);
	for (u32 i = 0u; i < SK_OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++) {
		if (i != 64u) {
			TEST_ASSERT_EQUAL_UINT32(0u, full.free_regions[i].count);
		}
	}

	/* allocate(1) splits off a 1023-byte remainder, which rounds down to
	 * bin 63 (holds 960..1023). */
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	full = sk_offset_allocator_storage_report_full(oa);
	TEST_ASSERT_EQUAL_UINT32(1u, full.free_regions[63].count);
	TEST_ASSERT_EQUAL_UINT32(0u, full.free_regions[64].count);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));

	/* Free puts the merged region back into bin 64. */
	sk_offset_allocator_free(oa, a);
	full = sk_offset_allocator_storage_report_full(oa);
	TEST_ASSERT_EQUAL_UINT32(0u, full.free_regions[63].count);
	TEST_ASSERT_EQUAL_UINT32(1u, full.free_regions[64].count);
	TEST_ASSERT_EQUAL_UINT32(1u, oa_test_free_region_count(oa));
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_no_space_arena_exhausted) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	/* Consume the whole arena (1024 is bin-exact). */
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);

	/* No space: the NO_SPACE sentinel, not a crash or a bogus offset. */
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, b.offset);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, b.metadata);

	/* Node slots remain, so the report is a truthful 0/0. */
	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(0u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(0u, report.largest_free_region);

	/* The allocator stays usable after the failed request. */
	sk_offset_allocator_free(oa, a);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, c.offset);
	sk_offset_allocator_free(oa, c);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_no_space_node_pool_exhausted) {
	const sk_allocator_t* alloc = sk_allocator_default();

	/* max_allocs = 3 is the documented minimum: one live allocation plus its
	 * split remainder already fill the pool, even though space is free. */
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 3u, alloc);
	TEST_ASSERT_NOT_NULL(oa);
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, b.offset);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, b.metadata);

	/* Freeing recycles the nodes; the allocator works again. */
	sk_offset_allocator_free(oa, a);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, c.offset);
	sk_offset_allocator_free(oa, c);
	sk_offset_allocator_destroy(oa);

	/* max_allocs = 5: three 1-byte allocations exhaust the pool. */
	sk_offset_allocator_t* oa5 = sk_offset_allocator_create(1024u, 5u, alloc);
	TEST_ASSERT_NOT_NULL(oa5);
	sk_offset_allocator_allocation_t x = sk_offset_allocator_allocate(oa5, 1u);
	sk_offset_allocator_allocation_t y = sk_offset_allocator_allocate(oa5, 1u);
	sk_offset_allocator_allocation_t z = sk_offset_allocator_allocate(oa5, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, x.offset);
	TEST_ASSERT_EQUAL_UINT32(1u, y.offset);
	TEST_ASSERT_EQUAL_UINT32(2u, z.offset);
	sk_offset_allocator_allocation_t w = sk_offset_allocator_allocate(oa5, 1u);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, w.offset);

	/* All frees coalesce back to a single region; the pool recovers. */
	sk_offset_allocator_free(oa5, x);
	sk_offset_allocator_free(oa5, y);
	sk_offset_allocator_free(oa5, z);
	sk_offset_allocator_allocation_t v = sk_offset_allocator_allocate(oa5, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, v.offset);
	sk_offset_allocator_free(oa5, v);
	sk_offset_allocator_destroy(oa5);
}

SK_TEST(offset_allocator_reset_pristine) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);

	/* Fragment the allocator first: three live allocations, middle freed. */
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 100u);
	sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 100u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	TEST_ASSERT_EQUAL_UINT32(100u, b.offset);
	TEST_ASSERT_EQUAL_UINT32(200u, c.offset);
	sk_offset_allocator_free(oa, b);
	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(824u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(704u, report.largest_free_region);

	/* reset() restores the pristine state regardless of the fragmentation. */
	sk_offset_allocator_reset(oa);
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);
	sk_offset_allocator_allocation_t full = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, full.offset); /* offsets restart at 0 */
	sk_offset_allocator_free(oa, full);

	/* reset() with live allocations outstanding also returns to pristine. */
	sk_offset_allocator_allocation_t live = sk_offset_allocator_allocate(oa, 777u);
	TEST_ASSERT_EQUAL_UINT32(0u, live.offset);
	sk_offset_allocator_reset(oa);
	report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(1024u, report.largest_free_region);
	sk_offset_allocator_allocation_t full2 = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, full2.offset);
	sk_offset_allocator_free(oa, full2);
	sk_offset_allocator_destroy(oa);
}

SK_TEST(offset_allocator_boundary_sizes) {
	const sk_allocator_t* alloc = sk_allocator_default();

	/* Size 1 and size == arena size (1024 is bin-exact). */
	sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);
	sk_offset_allocator_allocation_t s1 = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, s1.offset);
	TEST_ASSERT_EQUAL_UINT32(1u, sk_offset_allocator_allocation_size(oa, s1));
	sk_offset_allocator_free(oa, s1);
	sk_offset_allocator_allocation_t whole = sk_offset_allocator_allocate(oa, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, whole.offset);
	sk_offset_allocator_allocation_t nope = sk_offset_allocator_allocate(oa, 1u);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, nope.offset);
	sk_offset_allocator_free(oa, whole);
	sk_offset_allocator_destroy(oa);

	/* Sizes straddling the 2^8 and 2^10 rounding boundaries. Requests are
	 * served contiguously from the tail remainder; offsets are exact. */
	sk_offset_allocator_t* oa2 = sk_offset_allocator_create(4096u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa2);
	static const u32 straddle_sizes[] = {255u, 256u, 257u, 1023u, 1024u, 1025u};
	static const u32 straddle_offsets[] = {0u, 255u, 511u, 768u, 1791u, 2815u};
	sk_offset_allocator_allocation_t straddle[6];
	for (u32 i = 0u; i < 6u; i++) {
		straddle[i] = sk_offset_allocator_allocate(oa2, straddle_sizes[i]);
		TEST_ASSERT_EQUAL_UINT32(straddle_offsets[i], straddle[i].offset);
		TEST_ASSERT_EQUAL_UINT32(straddle_sizes[i], sk_offset_allocator_allocation_size(oa2, straddle[i]));
	}
	for (u32 i = 0u; i < 6u; i++) {
		sk_offset_allocator_free(oa2, straddle[i]);
	}
	sk_offset_allocator_allocation_t full2 = sk_offset_allocator_allocate(oa2, 4096u);
	TEST_ASSERT_EQUAL_UINT32(0u, full2.offset);
	sk_offset_allocator_free(oa2, full2);
	sk_offset_allocator_destroy(oa2);

	/* Sizes straddling the 2^20 boundary in a 16MB arena. */
	const u32 arena16m = 16u * 1024u * 1024u;
	sk_offset_allocator_t* oa3 = sk_offset_allocator_create(arena16m, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa3);
	static const u32 big_straddle[] = {1048575u, 1048576u, 1048577u};
	u32 expected_offset = 0u;
	sk_offset_allocator_allocation_t big_alloc[3];
	for (u32 i = 0u; i < 3u; i++) {
		big_alloc[i] = sk_offset_allocator_allocate(oa3, big_straddle[i]);
		TEST_ASSERT_EQUAL_UINT32(expected_offset, big_alloc[i].offset);
		TEST_ASSERT_EQUAL_UINT32(big_straddle[i], sk_offset_allocator_allocation_size(oa3, big_alloc[i]));
		expected_offset += big_straddle[i];
	}
	for (u32 i = 0u; i < 3u; i++) {
		sk_offset_allocator_free(oa3, big_alloc[i]);
	}
	sk_offset_allocator_allocation_t full3 = sk_offset_allocator_allocate(oa3, arena16m);
	TEST_ASSERT_EQUAL_UINT32(0u, full3.offset);
	sk_offset_allocator_free(oa3, full3);
	sk_offset_allocator_destroy(oa3);

	/* 3-bit mantissa step at exponent 10: bins 56..64 step by 64 bytes
	 * (512, 576, ..., 1024). A 576-byte region (bin 57, lower bound 576)
	 * serves a 576-byte request but not a 577-byte one (rounds up to
	 * bin 58, lower bound 640). */
	sk_offset_allocator_t* oa4 = sk_offset_allocator_create(1600u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa4);
	sk_offset_allocator_allocation_t base = sk_offset_allocator_allocate(oa4, 1024u);
	TEST_ASSERT_EQUAL_UINT32(0u, base.offset); /* leaves exactly 576 free */
	sk_offset_allocator_allocation_t step_over = sk_offset_allocator_allocate(oa4, 577u);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, step_over.offset);
	sk_offset_allocator_allocation_t step_exact = sk_offset_allocator_allocate(oa4, 576u);
	TEST_ASSERT_EQUAL_UINT32(1024u, step_exact.offset);
	sk_offset_allocator_free(oa4, step_exact);
	sk_offset_allocator_free(oa4, base);
	/* 1600 is not bin-exact, so the full-arena request must be 1536 (bin 68
	 * lower bound), which the coalesced 1600-byte region serves. */
	sk_offset_allocator_allocation_t step_full = sk_offset_allocator_allocate(oa4, 1536u);
	TEST_ASSERT_EQUAL_UINT32(0u, step_full.offset);
	sk_offset_allocator_free(oa4, step_full);
	sk_offset_allocator_destroy(oa4);
}

SK_TEST(offset_allocator_large_size_near_u32) {
	const sk_allocator_t* alloc = sk_allocator_default();

	/* 0xf0000000 is the largest bin-representable arena (bin 239 lower
	 * bound). The arena type is u32, so this is the near-u32 case. */
	sk_offset_allocator_t* oa = sk_offset_allocator_create(0xf0000000u, 0u, alloc);
	TEST_ASSERT_NOT_NULL(oa);
	sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 0xf0000000u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
	TEST_ASSERT_EQUAL_UINT32(0xf0000000u, sk_offset_allocator_allocation_size(oa, a));

	/* Requests above 0xf0000000 round up past the last usable bin (239) —
	 * bin 240's lower bound would be 2^32, overflowing u32. Same inherent
	 * limitation as upstream; the sentinel comes back, not a bogus offset. */
	sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 0xffffffffu);
	TEST_ASSERT_EQUAL_UINT32(SK_OFFSET_ALLOCATOR_NO_SPACE, b.offset);

	sk_offset_allocator_free(oa, a);
	sk_offset_allocator_storage_report_t report = sk_offset_allocator_storage_report(oa);
	TEST_ASSERT_EQUAL_UINT32(0xf0000000u, report.total_free_space);
	TEST_ASSERT_EQUAL_UINT32(0xf0000000u, report.largest_free_region);
	sk_offset_allocator_destroy(oa);
}

/* ---- leak-checking allocator stub (the project has no sanitizer CI job) ---- */

#define OA_TEST_MAX_SLOTS 16u

typedef struct oa_test_counting_allocator_t {
	void_ptr_t slots[OA_TEST_MAX_SLOTS];
	size_t slot_sizes[OA_TEST_MAX_SLOTS];
	u32 slot_count;
	u32 alloc_calls;
	u32 free_calls;
} oa_test_counting_allocator_t;

static void_ptr_t oa_test_counting_alloc(void_ptr_t instance, size_t size) {
	oa_test_counting_allocator_t* counter = (oa_test_counting_allocator_t*)instance;
	TEST_ASSERT_TRUE(counter->slot_count < OA_TEST_MAX_SLOTS);
	void_ptr_t p = malloc(size);
	TEST_ASSERT_NOT_NULL(p);
	counter->slots[counter->slot_count] = p;
	counter->slot_sizes[counter->slot_count] = size;
	counter->slot_count++;
	counter->alloc_calls++;
	return p;
}

static void oa_test_counting_free(void_ptr_t instance, void_ptr_t ptr) {
	oa_test_counting_allocator_t* counter = (oa_test_counting_allocator_t*)instance;
	for (u32 i = 0u; i < counter->slot_count; i++) {
		if (counter->slots[i] == ptr) {
			counter->slots[i] = counter->slots[counter->slot_count - 1u];
			counter->slot_sizes[i] = counter->slot_sizes[counter->slot_count - 1u];
			counter->slot_count--;
			free(ptr);
			counter->free_calls++;
			return;
		}
	}
	TEST_FAIL_MESSAGE("counting allocator: free of unknown pointer");
}

static void_ptr_t oa_test_counting_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	(void)instance;
	(void)ptr;
	(void)size;
	TEST_FAIL_MESSAGE("counting allocator: unexpected realloc");
	return NULL;
}

SK_TEST(offset_allocator_destroy_live_allocations_no_leak) {
	/* destroy() must release every internal array even when allocations are
	 * still live. The counting allocator proves alloc/free balance and zero
	 * live bytes deterministically (no sanitizer job in this project). */
	oa_test_counting_allocator_t counter;
	sk_allocator_t counting;
	counting.instance = &counter;
	counting.alloc = oa_test_counting_alloc;
	counting.free = oa_test_counting_free;
	counting.realloc = oa_test_counting_realloc;

	for (u32 cycle = 0u; cycle < 3u; cycle++) {
		memset(&counter, 0, sizeof(counter));

		sk_offset_allocator_t* oa = sk_offset_allocator_create(1024u, 64u, &counting);
		TEST_ASSERT_NOT_NULL(oa);
		u32 created_allocations = counter.alloc_calls; /* the 3 internal arrays */
		TEST_ASSERT_EQUAL_UINT32(3u, created_allocations);

		sk_offset_allocator_allocation_t a = sk_offset_allocator_allocate(oa, 100u);
		sk_offset_allocator_allocation_t b = sk_offset_allocator_allocate(oa, 200u);
		sk_offset_allocator_allocation_t c = sk_offset_allocator_allocate(oa, 300u);
		TEST_ASSERT_EQUAL_UINT32(0u, a.offset);
		TEST_ASSERT_EQUAL_UINT32(100u, b.offset);
		TEST_ASSERT_EQUAL_UINT32(300u, c.offset);

		/* allocate()/free() never touch the heap: call counts unchanged. */
		TEST_ASSERT_EQUAL_UINT32(created_allocations, counter.alloc_calls);

		/* a, b, c are still live when destroy() runs. */
		sk_offset_allocator_destroy(oa);
		TEST_ASSERT_EQUAL_UINT32(created_allocations, counter.free_calls);
		TEST_ASSERT_EQUAL_UINT32(0u, counter.slot_count);
	}
}

#endif /* SK_TESTS */
