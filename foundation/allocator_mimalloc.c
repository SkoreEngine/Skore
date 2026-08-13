#include "allocator.h"

/* Mimalloc is private to this translation unit — not re-exported. */
#include <mimalloc.h>

static void_ptr_t sk_mi_alloc(void_ptr_t instance, size_t size) {
	(void)instance;
	return mi_malloc(size);
}

static void sk_mi_free(void_ptr_t instance, void_ptr_t ptr) {
	(void)instance;
	mi_free(ptr);
}

static void_ptr_t sk_mi_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	(void)instance;
	return mi_realloc(ptr, size);
}

static const sk_allocator_t mimalloc_allocator = {
	NULL, /* no per-instance state for the process-default mimalloc heap */
	sk_mi_alloc,
	sk_mi_free,
	sk_mi_realloc,
};

void sk_allocator_get_default(sk_allocator_t* out) {
	*out = mimalloc_allocator;
}

const sk_allocator_t* sk_allocator_default(void) {
	return &mimalloc_allocator;
}

#ifdef SK_TESTS
#include "test.h"
#include "allocator.h"
#include <string.h>

SK_TEST(allocator_default_table_is_complete) {
	const sk_allocator_t* a = sk_allocator_default();
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(a->alloc);
	TEST_ASSERT_NOT_NULL(a->free);
	TEST_ASSERT_NOT_NULL(a->realloc);
}

SK_TEST(allocator_get_default_matches_static_table) {
	sk_allocator_t out;
	memset(&out, 0, sizeof(out));
	sk_allocator_get_default(&out);

	const sk_allocator_t* def = sk_allocator_default();
	TEST_ASSERT_EQUAL_PTR(def->instance, out.instance);
	TEST_ASSERT_EQUAL_PTR(def->alloc, out.alloc);
	TEST_ASSERT_EQUAL_PTR(def->free, out.free);
	TEST_ASSERT_EQUAL_PTR(def->realloc, out.realloc);
}

SK_TEST(allocator_alloc_free_roundtrip) {
	const sk_allocator_t* a = sk_allocator_default();
	void* p = a->alloc(a->instance, 64);
	TEST_ASSERT_NOT_NULL(p);
	memset(p, 0xAB, 64);
	a->free(a->instance, p);
}

SK_TEST(allocator_realloc_grows_and_preserves) {
	const sk_allocator_t* a = sk_allocator_default();
	char* p = (char*)a->alloc(a->instance, 16);
	TEST_ASSERT_NOT_NULL(p);
	memset(p, 'x', 16);

	char* q = (char*)a->realloc(a->instance, p, 128);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_EQUAL_MEMORY("xxxxxxxxxxxxxxxx", q, 16);

	a->free(a->instance, q);
}

SK_TEST(allocator_realloc_null_acts_like_alloc) {
	const sk_allocator_t* a = sk_allocator_default();
	void* p = a->realloc(a->instance, NULL, 32);
	TEST_ASSERT_NOT_NULL(p);
	a->free(a->instance, p);
}

SK_TEST(allocator_free_null_is_safe) {
	/* Mimalloc (and C free) document free(NULL) as a no-op. */
	const sk_allocator_t* a = sk_allocator_default();
	a->free(a->instance, NULL);
}
#endif /* SK_TESTS */
