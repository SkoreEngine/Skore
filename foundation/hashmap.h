#pragma once

/**
 * @file hashmap.h
 * @brief Typed hash map / hash set via SK_HASH_MAP(K,V) and SK_HASH_SET(K).
 *
 * Usage:
 *   typedef SK_HASH_MAP(int, char*) sk_my_map_t;
 *   typedef SK_HASH_SET(int) sk_my_set_t;
 *
 *   sk_my_map_t map;
 *   sk_hash_map_init(&map, allocator, NULL, NULL);  // POD key: byte hash + memcmp
 *   sk_hash_map_put(&map, 1, "one");
 *   sk_hash_map_free(&map);
 *
 * Custom hash / equals (required for pointer keys that compare by value):
 *   u64 hash_fn(const void* key);
 *   i32 equals_fn(const void* a, const void* b);  // non-zero if equal
 *   sk_hash_map_init(&map, allocator, hash_fn, equals_fn);
 *
 * All storage goes through the allocator passed to init — never malloc/free.
 */

#include "allocator.h"
#include "common.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hash a key; @p key points at a key-sized object. */
typedef u64 (*sk_hash_fn)(const void* key);

/**
 * Compare two keys; return non-zero if equal, 0 if not.
 * @p a and @p b point at key-sized objects.
 */
typedef i32 (*sk_equals_fn)(const void* a, const void* b);

/**
 * Untyped open-addressing hash map (power-of-two capacity, linear probe).
 * Prefer the typed macros below for normal use.
 */
typedef struct sk_hash_map_t {
	void* keys;						 /**< Key array (key_size * capacity) */
	void* values;					 /**< Value array (NULL for sets) */
	u8* controls;					 /**< Per-slot state (empty/occupied/tombstone) */
	u32 count;						 /**< Live entries */
	u32 capacity;					 /**< Slot count (0 or power of two) */
	u32 tombstones;					 /**< Deleted slots (for load factor) */
	u32 key_size;					 /**< Bytes per key */
	u32 value_size;					 /**< Bytes per value (0 for set) */
	sk_hash_fn hash;				 /**< Key hash (NULL → sk_hash_bytes) */
	sk_equals_fn equals;			 /**< Key equality (NULL → memcmp) */
	const sk_allocator_t* allocator; /**< Required; used for all heap ops */
} sk_hash_map_t;

/**
 * Typed map: key type K, value type V.
 * `_key_tmp` / `_val_tmp` are staging slots used by put/get/remove macros.
 */
#define SK_HASH_MAP(K, V)  \
	struct {               \
		sk_hash_map_t _hm; \
		K _key_tmp;        \
		V _val_tmp;        \
	}

/**
 * Typed set: key type K only (no values stored).
 */
#define SK_HASH_SET(K)     \
	struct {               \
		sk_hash_map_t _hm; \
		K _key_tmp;        \
	}

/* ------------------------------------------------------------------------- */
/* Hash helpers (usable as sk_hash_fn / sk_equals_fn)                        */
/* ------------------------------------------------------------------------- */

/** FNV-1a 64-bit over an arbitrary byte range. */
u64 sk_hash_bytes(const void* data, size_t len);

/** Hash a POD key of the map's key_size (for default hash when fn is NULL). */
u64 sk_hash_pod(const void* key, u32 key_size);

/** Hash helpers for common scalar keys (pass as sk_hash_fn via cast wrappers). */
u64 sk_hash_u32(u32 v);
u64 sk_hash_u64(u64 v);
u64 sk_hash_i32(i32 v);

/**
 * Hash a C string pointed to by a `const char*` / `char*` key.
 * Use with SK_HASH_MAP(const char*, T) and sk_equals_cstr.
 */
u64 sk_hash_cstr(const void* key);

/** String equality for `const char*` / `char*` keys (NULL-safe). */
i32 sk_equals_cstr(const void* a, const void* b);

/* ------------------------------------------------------------------------- */
/* Untyped map API                                                           */
/* ------------------------------------------------------------------------- */

/**
 * Initialize an empty map/set.
 *
 * @param map         Destination (must not be NULL)
 * @param allocator   Non-NULL allocator
 * @param key_size    Size of each key in bytes (> 0)
 * @param value_size  Size of each value (0 for set)
 * @param hash        Optional custom hash; NULL uses sk_hash_bytes over key_size
 * @param equals      Optional custom equals; NULL uses memcmp over key_size
 * @return 0 on success, non-zero on bad args
 */
i32 sk_hash_map_init_(sk_hash_map_t* map, const sk_allocator_t* allocator, u32 key_size, u32 value_size, sk_hash_fn hash, sk_equals_fn equals);

/** Free all storage and zero the map. Safe on NULL or already-freed maps. */
void sk_hash_map_free_(sk_hash_map_t* map);

/** Remove all entries; keeps capacity. */
void sk_hash_map_clear_(sk_hash_map_t* map);

/**
 * Insert or replace. Copies key and value by bytes.
 * @return 0 on success, non-zero on OOM / bad args
 */
i32 sk_hash_map_put_(sk_hash_map_t* map, const void* key, const void* value);

/**
 * Lookup. If found and @p out_value is non-NULL and value_size > 0, copies value.
 * @return 0 if found, non-zero if missing / bad args
 */
i32 sk_hash_map_get_(const sk_hash_map_t* map, const void* key, void* out_value);

/**
 * Lookup pointer into the value array (NULL if missing or set with value_size 0).
 * Pointer is invalidated by put/remove/rehash/free.
 */
void* sk_hash_map_get_ptr_(sk_hash_map_t* map, const void* key);

/** @return non-zero if key is present. */
i32 sk_hash_map_contains_(const sk_hash_map_t* map, const void* key);

/**
 * Remove key if present.
 * @return 0 if removed, non-zero if missing / bad args
 */
i32 sk_hash_map_remove_(sk_hash_map_t* map, const void* key);

/** Live entry count. */
u32 sk_hash_map_count_(const sk_hash_map_t* map);

/**
 * Slot inspection for iteration (0 .. capacity-1).
 * @return non-zero if slot holds a live entry
 */
i32 sk_hash_map_slot_occupied_(const sk_hash_map_t* map, u32 slot);

/** Pointer to key at slot (NULL if empty/tombstone or bad args). */
const void* sk_hash_map_key_at_(const sk_hash_map_t* map, u32 slot);

/** Pointer to value at slot (NULL if set / empty / bad args). */
void* sk_hash_map_value_at_(sk_hash_map_t* map, u32 slot);

/* ------------------------------------------------------------------------- */
/* Typed map macros                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Init typed map. @p hash_fn / @p equals_fn may be NULL for POD keys.
 * @param map   Pointer to SK_HASH_MAP(...) instance
 * @param alloc Non-NULL allocator
 */
#define sk_hash_map_init(map, alloc, hash_fn, equals_fn) sk_hash_map_init_(&(map)->_hm, (alloc), (u32)sizeof((map)->_key_tmp), (u32)sizeof((map)->_val_tmp), (hash_fn), (equals_fn))

#define sk_hash_map_free(map) sk_hash_map_free_(&(map)->_hm)

#define sk_hash_map_clear(map) sk_hash_map_clear_(&(map)->_hm)

#define sk_hash_map_count(map) sk_hash_map_count_(&(map)->_hm)

/** Insert or replace; returns 0 on success. */
#define sk_hash_map_put(map, key, value) (((map)->_key_tmp = (key)), ((map)->_val_tmp = (value)), sk_hash_map_put_(&(map)->_hm, &(map)->_key_tmp, &(map)->_val_tmp))

/**
 * Copy value into @p out_value if found.
 * @param out_value Pointer to a V (not optional for typed get)
 * @return 0 if found
 */
#define sk_hash_map_get(map, key, out_value) (((map)->_key_tmp = (key)), sk_hash_map_get_(&(map)->_hm, &(map)->_key_tmp, (out_value)))

/** @return pointer to stored V, or NULL. */
#define sk_hash_map_get_ptr(map, key) (((map)->_key_tmp = (key)), (void*)sk_hash_map_get_ptr_(&(map)->_hm, &(map)->_key_tmp))

#define sk_hash_map_contains(map, key) (((map)->_key_tmp = (key)), sk_hash_map_contains_(&(map)->_hm, &(map)->_key_tmp))

#define sk_hash_map_remove(map, key) (((map)->_key_tmp = (key)), sk_hash_map_remove_(&(map)->_hm, &(map)->_key_tmp))

/* ------------------------------------------------------------------------- */
/* Typed set macros                                                          */
/* ------------------------------------------------------------------------- */

#define sk_hash_set_init(set, alloc, hash_fn, equals_fn) sk_hash_map_init_(&(set)->_hm, (alloc), (u32)sizeof((set)->_key_tmp), 0u, (hash_fn), (equals_fn))

#define sk_hash_set_free(set) sk_hash_map_free_(&(set)->_hm)

#define sk_hash_set_clear(set) sk_hash_map_clear_(&(set)->_hm)

#define sk_hash_set_count(set) sk_hash_map_count_(&(set)->_hm)

/** Insert key; returns 0 on success. */
#define sk_hash_set_add(set, key) (((set)->_key_tmp = (key)), sk_hash_map_put_(&(set)->_hm, &(set)->_key_tmp, NULL))

#define sk_hash_set_contains(set, key) (((set)->_key_tmp = (key)), sk_hash_map_contains_(&(set)->_hm, &(set)->_key_tmp))

#define sk_hash_set_remove(set, key) (((set)->_key_tmp = (key)), sk_hash_map_remove_(&(set)->_hm, &(set)->_key_tmp))

#ifdef __cplusplus
}
#endif
