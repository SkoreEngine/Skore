#include "hashmap.h"

#include <string.h>

/* Control bytes: empty / live / deleted (tombstone). */
enum { SK_HM_EMPTY = 0, SK_HM_OCCUPIED = 1, SK_HM_TOMBSTONE = 2 };

/* Grow when live + tombstones exceed this fraction of capacity. */
#define SK_HM_MAX_LOAD_NUM 3u
#define SK_HM_MAX_LOAD_DEN 4u
#define SK_HM_INITIAL_CAP 8u

/* ------------------------------------------------------------------------- */
/* Hash helpers                                                              */
/* ------------------------------------------------------------------------- */

u64 sk_hash_bytes(const void* data, size_t len) {
	const u8* p = (const u8*)data;
	u64 h = 14695981039346656037ull; /* FNV-1a offset basis */

	if (p == NULL) {
		return h;
	}
	for (size_t i = 0; i < len; i++) {
		h ^= (u64)p[i];
		h *= 1099511628211ull; /* FNV prime */
	}
	return h;
}

u64 sk_hash_pod(const void* key, u32 key_size) {
	return sk_hash_bytes(key, (size_t)key_size);
}

u64 sk_hash_u32(u32 v) {
	/* SplitMix64-style finalizer on a 32-bit key. */
	u64 x = (u64)v;
	x += 0x9e3779b97f4a7c15ull;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	return x ^ (x >> 31);
}

u64 sk_hash_u64(u64 v) {
	u64 x = v + 0x9e3779b97f4a7c15ull;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	return x ^ (x >> 31);
}

u64 sk_hash_i32(i32 v) {
	return sk_hash_u32((u32)v);
}

u64 sk_hash_cstr(const void* key) {
	if (key == NULL) {
		return sk_hash_bytes(NULL, 0);
	}
	const char* s = *(const char* const*)key;
	if (s == NULL) {
		return sk_hash_bytes(NULL, 0);
	}
	return sk_hash_bytes(s, strlen(s));
}

i32 sk_equals_cstr(const void* a, const void* b) {
	if (a == NULL || b == NULL) {
		return 0;
	}
	const char* sa = *(const char* const*)a;
	const char* sb = *(const char* const*)b;
	if (sa == sb) {
		return 1;
	}
	if (sa == NULL || sb == NULL) {
		return 0;
	}
	return strcmp(sa, sb) == 0 ? 1 : 0;
}

/* ------------------------------------------------------------------------- */
/* Internals                                                                 */
/* ------------------------------------------------------------------------- */

static u64 sk_hm_hash_key(const sk_hash_map_t* map, const void* key) {
	if (map->hash != NULL) {
		return map->hash(key);
	}
	return sk_hash_bytes(key, (size_t)map->key_size);
}

static i32 sk_hm_keys_equal(const sk_hash_map_t* map, const void* a, const void* b) {
	if (map->equals != NULL) {
		return map->equals(a, b) != 0 ? 1 : 0;
	}
	return memcmp(a, b, (size_t)map->key_size) == 0 ? 1 : 0;
}

static void* sk_hm_key_ptr(const sk_hash_map_t* map, u32 slot) {
	return (u8*)map->keys + (size_t)slot * (size_t)map->key_size;
}

static void* sk_hm_val_ptr(const sk_hash_map_t* map, u32 slot) {
	if (map->values == NULL || map->value_size == 0u) {
		return NULL;
	}
	return (u8*)map->values + (size_t)slot * (size_t)map->value_size;
}

static void sk_hm_free_buffers(sk_hash_map_t* map) {
	const sk_allocator_t* a = map->allocator;

	if (a == NULL) {
		return;
	}
	if (map->keys != NULL) {
		a->free(a->instance, map->keys);
	}
	if (map->values != NULL) {
		a->free(a->instance, map->values);
	}
	if (map->controls != NULL) {
		a->free(a->instance, map->controls);
	}
	map->keys = NULL;
	map->values = NULL;
	map->controls = NULL;
	map->capacity = 0u;
	map->count = 0u;
	map->tombstones = 0u;
}

/**
 * Insert into a table that already has spare capacity (no grow).
 * Used by rehash to avoid recursive ensure_load.
 */
static i32 sk_hm_insert_raw(sk_hash_map_t* map, const void* key, const void* value) {
	if (map->capacity == 0u || map->controls == NULL) {
		return -1;
	}

	u32 mask = map->capacity - 1u;
	u64 h = sk_hm_hash_key(map, key);
	u32 idx = (u32)h & mask;
	u32 probed = 0u;

	while (probed < map->capacity) {
		u8 c = map->controls[idx];
		if (c == SK_HM_EMPTY || c == SK_HM_TOMBSTONE) {
			map->controls[idx] = SK_HM_OCCUPIED;
			memcpy(sk_hm_key_ptr(map, idx), key, (size_t)map->key_size);
			if (map->value_size > 0u && value != NULL) {
				memcpy(sk_hm_val_ptr(map, idx), value, (size_t)map->value_size);
			}
			map->count++;
			return 0;
		}
		/* Occupied: skip (rehash inserts unique keys only). */
		idx = (idx + 1u) & mask;
		probed++;
	}
	return -1;
}

/**
 * Allocate new tables of @p new_cap and reinsert all live entries.
 * @return 0 on success (old tables freed), non-zero on failure (map unchanged).
 */
static i32 sk_hm_rehash(sk_hash_map_t* map, u32 new_cap) {
	const sk_allocator_t* a = map->allocator;
	void* new_keys = NULL;
	void* new_values = NULL;
	u8* new_controls = NULL;

	if (new_cap == 0u || (new_cap & (new_cap - 1u)) != 0u) {
		return -1; /* must be power of two */
	}

	new_keys = a->alloc(a->instance, (size_t)new_cap * (size_t)map->key_size);
	new_controls = (u8*)a->alloc(a->instance, (size_t)new_cap);
	if (new_keys == NULL || new_controls == NULL) {
		if (new_keys != NULL) {
			a->free(a->instance, new_keys);
		}
		if (new_controls != NULL) {
			a->free(a->instance, new_controls);
		}
		return -1;
	}
	memset(new_controls, SK_HM_EMPTY, (size_t)new_cap);

	if (map->value_size > 0u) {
		new_values = a->alloc(a->instance, (size_t)new_cap * (size_t)map->value_size);
		if (new_values == NULL) {
			a->free(a->instance, new_keys);
			a->free(a->instance, new_controls);
			return -1;
		}
	}

	void* old_keys = map->keys;
	void* old_values = map->values;
	u8* old_controls = map->controls;
	u32 old_cap = map->capacity;
	u32 old_count = map->count;
	u32 old_tombstones = map->tombstones;

	map->keys = new_keys;
	map->values = new_values;
	map->controls = new_controls;
	map->capacity = new_cap;
	map->count = 0u;
	map->tombstones = 0u;

	if (old_controls != NULL && old_keys != NULL) {
		for (u32 i = 0; i < old_cap; i++) {
			if (old_controls[i] == SK_HM_OCCUPIED) {
				const void* k = (const u8*)old_keys + (size_t)i * (size_t)map->key_size;
				const void* v = NULL;
				if (old_values != NULL && map->value_size > 0u) {
					v = (const u8*)old_values + (size_t)i * (size_t)map->value_size;
				}
				if (sk_hm_insert_raw(map, k, v) != 0) {
					/* Roll back to old tables. */
					a->free(a->instance, new_keys);
					a->free(a->instance, new_controls);
					if (new_values != NULL) {
						a->free(a->instance, new_values);
					}
					map->keys = old_keys;
					map->values = old_values;
					map->controls = old_controls;
					map->capacity = old_cap;
					map->count = old_count;
					map->tombstones = old_tombstones;
					return -1;
				}
			}
		}
	}

	if (old_keys != NULL) {
		a->free(a->instance, old_keys);
	}
	if (old_values != NULL) {
		a->free(a->instance, old_values);
	}
	if (old_controls != NULL) {
		a->free(a->instance, old_controls);
	}
	return 0;
}

static i32 sk_hm_ensure_load(sk_hash_map_t* map) {
	if (map->capacity == 0u) {
		return sk_hm_rehash(map, SK_HM_INITIAL_CAP);
	}

	u32 used = map->count + map->tombstones;
	u32 limit = (map->capacity * SK_HM_MAX_LOAD_NUM) / SK_HM_MAX_LOAD_DEN;
	if (used + 1u > limit) {
		u32 new_cap = map->capacity * 2u;
		if (new_cap < SK_HM_INITIAL_CAP) {
			new_cap = SK_HM_INITIAL_CAP;
		}
		/* If mostly tombstones, rehash at same size to compact. */
		if (map->tombstones > map->count && map->capacity >= SK_HM_INITIAL_CAP) {
			new_cap = map->capacity;
		}
		return sk_hm_rehash(map, new_cap);
	}
	return 0;
}

/**
 * Find slot for key.
 * @param out_slot       Set to occupied slot, or empty/tombstone insert slot
 * @param out_found      1 if key present
 * @return 0 on success (search completed), -1 if empty map / bad args
 */
static i32 sk_hm_find_slot(const sk_hash_map_t* map, const void* key, u32* out_slot, i32* out_found) {
	*out_found = 0;
	if (map->capacity == 0u || map->controls == NULL) {
		*out_slot = 0u;
		return -1;
	}

	u32 mask = map->capacity - 1u;
	u64 h = sk_hm_hash_key(map, key);
	u32 idx = (u32)h & mask;
	u32 first_tombstone = (u32)-1;
	u32 probed = 0u;

	while (probed < map->capacity) {
		u8 c = map->controls[idx];
		if (c == SK_HM_EMPTY) {
			*out_slot = (first_tombstone != (u32)-1) ? first_tombstone : idx;
			return 0;
		}
		if (c == SK_HM_TOMBSTONE) {
			if (first_tombstone == (u32)-1) {
				first_tombstone = idx;
			}
		} else if (c == SK_HM_OCCUPIED) {
			if (sk_hm_keys_equal(map, sk_hm_key_ptr(map, idx), key)) {
				*out_slot = idx;
				*out_found = 1;
				return 0;
			}
		}
		idx = (idx + 1u) & mask;
		probed++;
	}

	/* Table full of tombstones/occupied — insert at first tombstone if any. */
	if (first_tombstone != (u32)-1) {
		*out_slot = first_tombstone;
		return 0;
	}
	return -1;
}

/* ------------------------------------------------------------------------- */
/* Public untyped API                                                        */
/* ------------------------------------------------------------------------- */

i32 sk_hash_map_init_(sk_hash_map_t* map, const sk_allocator_t* allocator, u32 key_size, u32 value_size, sk_hash_fn hash, sk_equals_fn equals) {
	if (key_size == 0u) {
		return -1;
	}

	memset(map, 0, sizeof(*map));
	map->key_size = key_size;
	map->value_size = value_size;
	map->hash = hash;
	map->equals = equals;
	map->allocator = allocator;
	return 0;
}

void sk_hash_map_free_(sk_hash_map_t* map) {
	sk_hm_free_buffers(map);
	map->hash = NULL;
	map->equals = NULL;
	map->allocator = NULL;
	map->key_size = 0u;
	map->value_size = 0u;
}

void sk_hash_map_clear_(sk_hash_map_t* map) {
	if (map->controls == NULL || map->capacity == 0u) {
		map->count = 0u;
		map->tombstones = 0u;
		return;
	}
	memset(map->controls, SK_HM_EMPTY, (size_t)map->capacity);
	map->count = 0u;
	map->tombstones = 0u;
}

i32 sk_hash_map_put_(sk_hash_map_t* map, const void* key, const void* value) {
	u32 slot;
	i32 found;

	if (sk_hm_ensure_load(map) != 0) {
		return -1;
	}

	if (sk_hm_find_slot(map, key, &slot, &found) != 0) {
		return -1;
	}

	if (found) {
		memcpy(sk_hm_key_ptr(map, slot), key, (size_t)map->key_size);
		if (map->value_size > 0u && value != NULL) {
			memcpy(sk_hm_val_ptr(map, slot), value, (size_t)map->value_size);
		}
		return 0;
	}

	if (map->controls[slot] == SK_HM_TOMBSTONE) {
		map->tombstones--;
	}
	map->controls[slot] = SK_HM_OCCUPIED;
	memcpy(sk_hm_key_ptr(map, slot), key, (size_t)map->key_size);
	if (map->value_size > 0u && value != NULL) {
		memcpy(sk_hm_val_ptr(map, slot), value, (size_t)map->value_size);
	}
	map->count++;
	return 0;
}

i32 sk_hash_map_get_(const sk_hash_map_t* map, const void* key, void* out_value) {
	u32 slot;
	i32 found;

	if (sk_hm_find_slot(map, key, &slot, &found) != 0 || !found) {
		return -1;
	}
	if (out_value != NULL && map->value_size > 0u) {
		memcpy(out_value, sk_hm_val_ptr(map, slot), (size_t)map->value_size);
	}
	return 0;
}

void* sk_hash_map_get_ptr_(sk_hash_map_t* map, const void* key) {
	u32 slot;
	i32 found;

	if (sk_hm_find_slot(map, key, &slot, &found) != 0 || !found) {
		return NULL;
	}
	return sk_hm_val_ptr(map, slot);
}

i32 sk_hash_map_contains_(const sk_hash_map_t* map, const void* key) {
	u32 slot;
	i32 found;

	if (sk_hm_find_slot(map, key, &slot, &found) != 0) {
		return 0;
	}
	return found;
}

i32 sk_hash_map_remove_(sk_hash_map_t* map, const void* key) {
	u32 slot;
	i32 found;

	if (sk_hm_find_slot(map, key, &slot, &found) != 0 || !found) {
		return -1;
	}

	map->controls[slot] = SK_HM_TOMBSTONE;
	map->count--;
	map->tombstones++;
	return 0;
}

u32 sk_hash_map_count_(const sk_hash_map_t* map) {
	return map->count;
}

i32 sk_hash_map_slot_occupied_(const sk_hash_map_t* map, u32 slot) {
	if (map->controls == NULL || slot >= map->capacity) {
		return 0;
	}
	return map->controls[slot] == SK_HM_OCCUPIED ? 1 : 0;
}

const void* sk_hash_map_key_at_(const sk_hash_map_t* map, u32 slot) {
	if (!sk_hash_map_slot_occupied_(map, slot)) {
		return NULL;
	}
	return sk_hm_key_ptr(map, slot);
}

void* sk_hash_map_value_at_(sk_hash_map_t* map, u32 slot) {
	if (!sk_hash_map_slot_occupied_(map, slot)) {
		return NULL;
	}
	return sk_hm_val_ptr(map, slot);
}

#ifdef SK_TESTS
#include "test.h"
#include "allocator.h"
#include "hashmap.h"
#include <string.h>

typedef SK_HASH_MAP(int, const char*) sk_int_cstr_map_t;
typedef SK_HASH_MAP(int, int) sk_int_int_map_t;
typedef SK_HASH_SET(int) sk_int_set_t;
typedef SK_HASH_MAP(const char*, int) sk_cstr_int_map_t;

/* Custom hash/equals for int keys (exercise custom function path). */
static u64 test_int_hash(const void* key) {
	return sk_hash_i32(*(const int*)key);
}

static i32 test_int_equals(const void* a, const void* b) {
	return *(const int*)a == *(const int*)b ? 1 : 0;
}

SK_TEST(hash_map_pod_put_get_remove) {
	sk_int_cstr_map_t map;
	const char* out = NULL;
	const sk_allocator_t* alloc = sk_allocator_default();

	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_init(&map, alloc, NULL, NULL));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_hash_map_count(&map));

	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, 1, "one"));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, 2, "two"));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_hash_map_count(&map));

	TEST_ASSERT_TRUE(sk_hash_map_contains(&map, 1));
	TEST_ASSERT_FALSE(sk_hash_map_contains(&map, 99));

	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, 1, &out));
	TEST_ASSERT_EQUAL_STRING("one", out);
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, 2, &out));
	TEST_ASSERT_EQUAL_STRING("two", out);

	/* Replace existing. */
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, 1, "uno"));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_hash_map_count(&map));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, 1, &out));
	TEST_ASSERT_EQUAL_STRING("uno", out);

	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_remove(&map, 2));
	TEST_ASSERT_FALSE(sk_hash_map_contains(&map, 2));
	TEST_ASSERT_EQUAL_UINT32(1u, sk_hash_map_count(&map));
	TEST_ASSERT_NOT_EQUAL(0, sk_hash_map_remove(&map, 2));

	sk_hash_map_free(&map);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_hash_map_count(&map));
}

SK_TEST(hash_map_custom_hash_equals) {
	sk_int_int_map_t map;
	int out = 0;

	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_init(&map, sk_allocator_default(), test_int_hash, test_int_equals));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, 10, 100));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, 20, 200));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, 10, &out));
	TEST_ASSERT_EQUAL_INT(100, out);
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, 20, &out));
	TEST_ASSERT_EQUAL_INT(200, out);

	sk_hash_map_free(&map);
}

SK_TEST(hash_map_grow_and_lookup) {
	sk_int_int_map_t map;
	int out = 0;

	sk_hash_map_init(&map, sk_allocator_default(), NULL, NULL);
	for (int i = 0; i < 300; i++) {
		TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, i, i * 10));
	}
	TEST_ASSERT_EQUAL_UINT32(300u, sk_hash_map_count(&map));

	for (int i = 0; i < 300; i++) {
		TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, i, &out));
		TEST_ASSERT_EQUAL_INT(i * 10, out);
	}

	sk_hash_map_clear(&map);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_hash_map_count(&map));
	TEST_ASSERT_FALSE(sk_hash_map_contains(&map, 0));

	sk_hash_map_free(&map);
}

SK_TEST(hash_map_get_ptr) {
	sk_int_int_map_t map;

	sk_hash_map_init(&map, sk_allocator_default(), NULL, NULL);
	sk_hash_map_put(&map, 7, 42);
	int* p = (int*)sk_hash_map_get_ptr(&map, 7);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_INT(42, *p);
	*p = 99;
	{
		int out = 0;
		TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, 7, &out));
		TEST_ASSERT_EQUAL_INT(99, out);
	}
	TEST_ASSERT_NULL(sk_hash_map_get_ptr(&map, 8));
	sk_hash_map_free(&map);
}

SK_TEST(hash_map_string_keys_custom) {
	sk_cstr_int_map_t map;
	int out = 0;

	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_init(&map, sk_allocator_default(), sk_hash_cstr, sk_equals_cstr));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, "alpha", 1));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, "beta", 2));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, "alpha", &out));
	TEST_ASSERT_EQUAL_INT(1, out);
	TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, "beta", &out));
	TEST_ASSERT_EQUAL_INT(2, out);
	/* Different pointer, same content. */
	{
		char local[] = "alpha";
		TEST_ASSERT_TRUE(sk_hash_map_contains(&map, (const char*)local));
	}

	sk_hash_map_free(&map);
}

SK_TEST(hash_set_add_contains_remove) {
	sk_int_set_t set;

	TEST_ASSERT_EQUAL_INT(0, sk_hash_set_init(&set, sk_allocator_default(), NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_set_add(&set, 5));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_set_add(&set, 10));
	TEST_ASSERT_EQUAL_INT(0, sk_hash_set_add(&set, 5)); /* idempotent insert */
	TEST_ASSERT_EQUAL_UINT32(2u, sk_hash_set_count(&set));
	TEST_ASSERT_TRUE(sk_hash_set_contains(&set, 5));
	TEST_ASSERT_TRUE(sk_hash_set_contains(&set, 10));
	TEST_ASSERT_FALSE(sk_hash_set_contains(&set, 7));

	TEST_ASSERT_EQUAL_INT(0, sk_hash_set_remove(&set, 5));
	TEST_ASSERT_FALSE(sk_hash_set_contains(&set, 5));
	TEST_ASSERT_EQUAL_UINT32(1u, sk_hash_set_count(&set));

	sk_hash_set_free(&set);
}

SK_TEST(hash_set_custom_hash_equals) {
	sk_int_set_t set;

	sk_hash_set_init(&set, sk_allocator_default(), test_int_hash, test_int_equals);
	for (int i = 0; i < 50; i++) {
		TEST_ASSERT_EQUAL_INT(0, sk_hash_set_add(&set, i));
	}
	TEST_ASSERT_EQUAL_UINT32(50u, sk_hash_set_count(&set));
	for (int i = 0; i < 50; i++) {
		TEST_ASSERT_TRUE(sk_hash_set_contains(&set, i));
	}
	sk_hash_set_clear(&set);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_hash_set_count(&set));
	sk_hash_set_free(&set);
}

SK_TEST(hash_map_remove_then_reinsert) {
	sk_int_int_map_t map;
	int out = 0;

	sk_hash_map_init(&map, sk_allocator_default(), NULL, NULL);
	for (int i = 0; i < 40; i++) {
		sk_hash_map_put(&map, i, i);
	}
	for (int i = 0; i < 40; i += 2) {
		TEST_ASSERT_EQUAL_INT(0, sk_hash_map_remove(&map, i));
	}
	TEST_ASSERT_EQUAL_UINT32(20u, sk_hash_map_count(&map));

	for (int i = 0; i < 40; i += 2) {
		TEST_ASSERT_EQUAL_INT(0, sk_hash_map_put(&map, i, i + 1000));
	}
	TEST_ASSERT_EQUAL_UINT32(40u, sk_hash_map_count(&map));

	for (int i = 0; i < 40; i++) {
		TEST_ASSERT_EQUAL_INT(0, sk_hash_map_get(&map, i, &out));
		if ((i % 2) == 0) {
			TEST_ASSERT_EQUAL_INT(i + 1000, out);
		} else {
			TEST_ASSERT_EQUAL_INT(i, out);
		}
	}

	sk_hash_map_free(&map);
}

SK_TEST(hash_helpers_cstr) {
	const char* a = "hello";
	const char* b = "hello";
	const char* c = "world";
	u64 ha = sk_hash_cstr(&a);
	u64 hb = sk_hash_cstr(&b);
	u64 hc = sk_hash_cstr(&c);

	TEST_ASSERT_EQUAL_UINT64(ha, hb);
	TEST_ASSERT_TRUE(ha != hc);
	TEST_ASSERT_TRUE(sk_equals_cstr(&a, &b));
	TEST_ASSERT_FALSE(sk_equals_cstr(&a, &c));
}
#endif /* SK_TESTS */
