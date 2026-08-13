#include "array.h"

#include <string.h>

i32 sk_array_reserve_(void** items, u32* capacity, size_t elem_size, u32 min_capacity, const sk_allocator_t* allocator) {
	if (min_capacity <= *capacity) {
		return 0;
	}
	if (elem_size == 0u) {
		return -1;
	}

	u32 new_cap = *capacity;
	if (new_cap == 0u) {
		new_cap = 8u;
	}
	while (new_cap < min_capacity) {
		/* Cap at u32 max / 2 to avoid overflow on *2. */
		if (new_cap > (((u32)-1) / 2u)) {
			new_cap = min_capacity;
			break;
		}
		new_cap *= 2u;
	}
	if (new_cap < min_capacity) {
		new_cap = min_capacity;
	}

	size_t bytes = (size_t)new_cap * elem_size;
	/* Overflow guard: elem_size * new_cap must fit in size_t. */
	if (elem_size != 0u && bytes / elem_size != (size_t)new_cap) {
		return -1;
	}

	void* new_items = allocator->realloc(allocator->instance, *items, bytes);
	if (new_items == NULL) {
		return -1;
	}

	*items = new_items;
	*capacity = new_cap;
	return 0;
}

i32 sk_array_resize_(void** items, u32* count, u32* capacity, size_t elem_size, u32 new_count, const sk_allocator_t* allocator) {
	if (new_count > *capacity) {
		if (sk_array_reserve_(items, capacity, elem_size, new_count, allocator) != 0) {
			return -1;
		}
	}

	u32 old_count = *count;
	if (new_count > old_count && *items != NULL && elem_size > 0u) {
		size_t grow_bytes = (size_t)(new_count - old_count) * elem_size;
		memset((u8*)(*items) + (size_t)old_count * elem_size, 0, grow_bytes);
	}

	*count = new_count;
	return 0;
}

#ifdef SK_TESTS
#include "test.h"
#include "allocator.h"
#include "array.h"
#include <string.h>

typedef SK_ARRAY(int) sk_int_array_t;
typedef SK_ARRAY(f32) sk_f32_array_t;

SK_TEST(array_init_push_pop_free) {
	sk_int_array_t a;
	const sk_allocator_t* alloc = sk_allocator_default();

	sk_array_init(&a, alloc);
	TEST_ASSERT_NULL(a.items);
	TEST_ASSERT_EQUAL_UINT32(0u, a.count);
	TEST_ASSERT_EQUAL_UINT32(0u, a.capacity);
	TEST_ASSERT_EQUAL_PTR(alloc, a.allocator);

	TEST_ASSERT_EQUAL_INT(0, sk_array_push(&a, 10));
	TEST_ASSERT_EQUAL_INT(0, sk_array_push(&a, 20));
	TEST_ASSERT_EQUAL_INT(0, sk_array_push(&a, 30));
	TEST_ASSERT_EQUAL_UINT32(3u, a.count);
	TEST_ASSERT_TRUE(a.capacity >= 3u);
	TEST_ASSERT_EQUAL_INT(10, a.items[0]);
	TEST_ASSERT_EQUAL_INT(20, sk_array_at(&a, 1));
	TEST_ASSERT_EQUAL_INT(30, a.items[2]);

	TEST_ASSERT_EQUAL_INT(30, sk_array_pop(&a));
	TEST_ASSERT_EQUAL_UINT32(2u, a.count);

	sk_array_free(&a);
	TEST_ASSERT_NULL(a.items);
	TEST_ASSERT_EQUAL_UINT32(0u, a.count);
	TEST_ASSERT_NULL(a.allocator);
}

SK_TEST(array_reserve_and_resize) {
	sk_int_array_t a;
	sk_array_init(&a, sk_allocator_default());

	TEST_ASSERT_EQUAL_INT(0, sk_array_reserve(&a, 64));
	TEST_ASSERT_TRUE(a.capacity >= 64u);
	TEST_ASSERT_EQUAL_UINT32(0u, a.count);

	TEST_ASSERT_EQUAL_INT(0, sk_array_resize(&a, 5));
	TEST_ASSERT_EQUAL_UINT32(5u, a.count);
	TEST_ASSERT_EQUAL_INT(0, a.items[0]);
	TEST_ASSERT_EQUAL_INT(0, a.items[4]);

	a.items[2] = 99;
	TEST_ASSERT_EQUAL_INT(0, sk_array_resize(&a, 2));
	TEST_ASSERT_EQUAL_UINT32(2u, a.count);

	sk_array_free(&a);
}

SK_TEST(array_grow_many) {
	sk_int_array_t a;
	sk_array_init(&a, sk_allocator_default());

	for (int i = 0; i < 200; i++) {
		TEST_ASSERT_EQUAL_INT(0, sk_array_push(&a, i));
	}
	TEST_ASSERT_EQUAL_UINT32(200u, a.count);
	for (int i = 0; i < 200; i++) {
		TEST_ASSERT_EQUAL_INT(i, a.items[i]);
	}

	sk_array_clear(&a);
	TEST_ASSERT_EQUAL_UINT32(0u, a.count);
	TEST_ASSERT_TRUE(a.capacity >= 200u);

	sk_array_free(&a);
}

SK_TEST(array_swap_remove) {
	sk_int_array_t a;
	sk_array_init(&a, sk_allocator_default());
	sk_array_push(&a, 1);
	sk_array_push(&a, 2);
	sk_array_push(&a, 3);

	sk_array_swap_remove(&a, 0);
	TEST_ASSERT_EQUAL_UINT32(2u, a.count);
	TEST_ASSERT_EQUAL_INT(3, a.items[0]);
	TEST_ASSERT_EQUAL_INT(2, a.items[1]);

	sk_array_free(&a);
}

SK_TEST(array_float_elements) {
	sk_f32_array_t a;
	sk_array_init(&a, sk_allocator_default());
	TEST_ASSERT_EQUAL_INT(0, sk_array_push(&a, 1.5f));
	TEST_ASSERT_EQUAL_INT(0, sk_array_push(&a, 2.5f));
	TEST_ASSERT_EQUAL_FLOAT(1.5f, a.items[0]);
	TEST_ASSERT_EQUAL_FLOAT(2.5f, a.items[1]);
	sk_array_free(&a);
}
#endif /* SK_TESTS */
