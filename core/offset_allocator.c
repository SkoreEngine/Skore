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
