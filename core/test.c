/**
 * @file test.c
 * @brief Test registry + common.h unit tests (only active with SK_TESTS).
 *
 * Built as sk-test STATIC and linked only into sk-tests host and Debug plugins.
 * Never linked into Release plugins or production sk-core / sk-player.
 */

#include "test.h"

#ifdef SK_TESTS

#include "atomics.h"
#include <string.h>

enum { SK_TEST_MAX = 512 };

typedef struct sk_test_entry_t {
	const_chr_t name;
	void (*fn)(void);
} sk_test_entry_t;

static sk_test_entry_t tests[SK_TEST_MAX];
static u32 test_count = 0u;

void setUp(void) {}
void tearDown(void) {}

void sk_test_register(const_chr_t name, void (*fn)(void)) {
	if (test_count >= (u32)SK_TEST_MAX) {
		return;
	}
	if (name == NULL || fn == NULL) {
		return;
	}
	tests[test_count].name = name;
	tests[test_count].fn = fn;
	test_count += 1u;
}

void sk_test_run_all(sk_test_report_t* out) {
	UNITY_BEGIN();
	for (u32 i = 0u; i < test_count; ++i) {
		UnityDefaultTestRun(tests[i].fn, tests[i].name, (int)i);
	}
	i32 failed = (i32)UNITY_END();

	if (out != NULL) {
		out->ran = (i32)test_count;
		out->failed = failed;
	}
}

i32 sk_test_run_all_status(sk_test_report_t* out) {
	sk_test_report_t local;
	sk_test_report_t* report = (out != NULL) ? out : &local;

	memset(report, 0, sizeof(*report));
	sk_test_run_all(report);
	return (report->failed != 0) ? 1 : 0;
}

/* ---- common.h / type-id tests (header-only module) ---- */

/* Expanded at configure time by cmake/cmake_functions.cmake (MD5 → two u64s). */
#define SK_TEST_TYPE_ID_HASH SK_TYPE_ID("sk_test_type_id", 0x3bdb6060c8246ef6ULL, 0xc3bae8cf6ba01c8eULL)
#define SK_TEST_TYPE_A_HASH SK_TYPE_ID("sk_type_a", 0xbd8f348f4b2a0bb4ULL, 0x5730474b0fe57800ULL)
#define SK_TEST_TYPE_B_HASH SK_TYPE_ID("sk_type_b", 0xfe49b7ed8fc83379ULL, 0xb570d86f5e0ee794ULL)

SK_TEST(common_fixed_width_sizes) {
	TEST_ASSERT_EQUAL_size_t(1u, sizeof(u8));
	TEST_ASSERT_EQUAL_size_t(2u, sizeof(u16));
	TEST_ASSERT_EQUAL_size_t(4u, sizeof(u32));
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(u64));
	TEST_ASSERT_EQUAL_size_t(1u, sizeof(i8));
	TEST_ASSERT_EQUAL_size_t(2u, sizeof(i16));
	TEST_ASSERT_EQUAL_size_t(4u, sizeof(i32));
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(i64));
	TEST_ASSERT_EQUAL_size_t(4u, sizeof(f32));
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(f64));
}

SK_TEST(type_id_is_128_bit) {
	TEST_ASSERT_EQUAL_size_t(16u, sizeof(sk_type_id_t));
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(((sk_type_id_t*)0)->lo));
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(((sk_type_id_t*)0)->hi));
}

SK_TEST(type_id_macro_and_equality) {
	sk_type_id_t a = SK_TYPE_ID("manual", 0x1111111111111111ULL, 0x2222222222222222ULL);
	sk_type_id_t b = SK_TYPE_ID("manual", 0x1111111111111111ULL, 0x2222222222222222ULL);
	sk_type_id_t c = SK_TYPE_ID("other", 0x1111111111111111ULL, 0x3333333333333333ULL);
	sk_type_id_t z = SK_TYPE_ID_ZERO;

	TEST_ASSERT_EQUAL_UINT64(0x1111111111111111ULL, a.lo);
	TEST_ASSERT_EQUAL_UINT64(0x2222222222222222ULL, a.hi);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(a, b));
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(a, c));
	TEST_ASSERT_EQUAL_UINT64(0ull, z.lo);
	TEST_ASSERT_EQUAL_UINT64(0ull, z.hi);
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(a, z));
}

SK_TEST(type_id_cmake_embedded_hash) {
	sk_type_id_t id = SK_TEST_TYPE_ID_HASH;
	sk_type_id_t expected = SK_TYPE_ID("sk_test_type_id", 0x3bdb6060c8246ef6ULL, 0xc3bae8cf6ba01c8eULL);

	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, expected));
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_TEST_TYPE_A_HASH, SK_TEST_TYPE_B_HASH));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TEST_TYPE_A_HASH, SK_TEST_TYPE_A_HASH));
}

SK_TEST(common_pointer_aliases) {
	int value = 42;
	void_ptr_t vp = &value;
	const_ptr_t cp = &value;
	char buf[] = "skore";
	char_ptr_t cstr = buf;
	const_chr_t cchr = "ok";

	TEST_ASSERT_NOT_NULL(vp);
	TEST_ASSERT_NOT_NULL(cp);
	TEST_ASSERT_EQUAL_INT(42, *(int*)vp);
	TEST_ASSERT_EQUAL_STRING("skore", cstr);
	TEST_ASSERT_EQUAL_STRING("ok", cchr);
	(void)cchr;
}

SK_TEST(common_win_export_macro_defined_on_win64) {
#if defined(_WIN64)
	TEST_ASSERT_EQUAL_INT(1, SK_WIN);
#else
	TEST_PASS_MESSAGE("SK_WIN only required on Win64");
#endif
}

/* ---- core/atomics.h (header-only module) ---- */

SK_TEST(atomics_layout_matches_backing_type) {
	TEST_ASSERT_EQUAL_size_t(sizeof(u32), sizeof(sk_atomic_u32_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(u64), sizeof(sk_atomic_u64_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(i32), sizeof(sk_atomic_i32_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(i64), sizeof(sk_atomic_i64_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(void_ptr_t), sizeof(sk_atomic_ptr_t));
}

SK_TEST(atomics_load_store_roundtrip) {
	sk_atomic_u32_t a = {0};
	sk_atomic_u32_init(&a, 7u);
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_u32_load(&a));
	sk_atomic_u32_store(&a, 42u);
	TEST_ASSERT_EQUAL_UINT32(42u, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_cas_success_and_failure) {
	sk_atomic_u32_t a = {0};
	sk_atomic_u32_init(&a, 1u);

	u32 expected = 1u;
	TEST_ASSERT_TRUE(sk_atomic_u32_cas(&a, &expected, 2u));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(1u, expected);

	expected = 99u;
	TEST_ASSERT_FALSE(sk_atomic_u32_cas(&a, &expected, 3u));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(2u, expected);
}

SK_TEST(atomics_fetch_add_sub) {
	sk_atomic_i32_t a = {0};
	sk_atomic_i32_init(&a, 10);

	TEST_ASSERT_EQUAL_INT(10, sk_atomic_i32_fetch_add(&a, 5));
	TEST_ASSERT_EQUAL_INT(15, sk_atomic_i32_load(&a));
	TEST_ASSERT_EQUAL_INT(15, sk_atomic_i32_fetch_sub(&a, 3));
	TEST_ASSERT_EQUAL_INT(12, sk_atomic_i32_load(&a));
}

SK_TEST(atomics_fetch_or_and) {
	sk_atomic_u32_t a = {0};
	sk_atomic_u32_init(&a, 0x0f0fu);

	TEST_ASSERT_EQUAL_UINT32(0x0f0fu, sk_atomic_u32_fetch_or(&a, 0x00f0u));
	TEST_ASSERT_EQUAL_UINT32(0x0fffu, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(0x0fffu, sk_atomic_u32_fetch_and(&a, 0x0f00u));
	TEST_ASSERT_EQUAL_UINT32(0x0f00u, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_exchange) {
	sk_atomic_u32_t a = {0};
	sk_atomic_u32_init(&a, 1u);
	TEST_ASSERT_EQUAL_UINT32(1u, sk_atomic_u32_exchange(&a, 8u));
	TEST_ASSERT_EQUAL_UINT32(8u, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_relaxed_and_acquire_release) {
	sk_atomic_u32_t a = {0};
	sk_atomic_u32_init(&a, 5u);
	TEST_ASSERT_EQUAL_UINT32(5u, sk_atomic_u32_load_relaxed(&a));
	sk_atomic_u32_store_relaxed(&a, 6u);
	TEST_ASSERT_EQUAL_UINT32(6u, sk_atomic_u32_load_relaxed(&a));

	sk_atomic_u32_store_release(&a, 7u);
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_u32_load_acquire(&a));
}

SK_TEST(atomics_wide_types) {
	sk_atomic_u64_t a = {0};
	sk_atomic_u64_init(&a, 0x1122334455667788ull);
	TEST_ASSERT_EQUAL_UINT64(0x1122334455667788ull, sk_atomic_u64_load(&a));
	TEST_ASSERT_EQUAL_UINT64(0x1122334455667788ull, sk_atomic_u64_fetch_add(&a, 1ull));
	TEST_ASSERT_EQUAL_UINT64(0x1122334455667789ull, sk_atomic_u64_load(&a));

	sk_atomic_i64_t b = {0};
	sk_atomic_i64_init(&b, -42);
	TEST_ASSERT_EQUAL_INT64(-42, sk_atomic_i64_load(&b));
	sk_atomic_i64_store(&b, 0x7fffffffffffffffll);
	TEST_ASSERT_EQUAL_INT64(0x7fffffffffffffffll, sk_atomic_i64_load(&b));
}

SK_TEST(atomics_ptr_load_store_cas) {
	sk_atomic_ptr_t a = {0};
	int x = 0;
	int y = 0;

	sk_atomic_ptr_init(&a, (void_ptr_t)&x);
	TEST_ASSERT_EQUAL_PTR((void_ptr_t)&x, sk_atomic_ptr_load(&a));

	void_ptr_t expected = (void_ptr_t)&x;
	TEST_ASSERT_TRUE(sk_atomic_ptr_cas(&a, &expected, (void_ptr_t)&y));
	TEST_ASSERT_EQUAL_PTR((void_ptr_t)&y, sk_atomic_ptr_load(&a));

	expected = (void_ptr_t)&x;
	TEST_ASSERT_FALSE(sk_atomic_ptr_cas(&a, &expected, (void_ptr_t)&x));
	TEST_ASSERT_EQUAL_PTR((void_ptr_t)&y, expected);

	sk_atomic_ptr_store_release(&a, (void_ptr_t)&x);
	TEST_ASSERT_EQUAL_PTR((void_ptr_t)&x, sk_atomic_ptr_load_acquire(&a));
}

SK_TEST(atomics_ptr_exchange) {
	sk_atomic_ptr_t a = {0};
	int x = 0;
	int y = 0;

	sk_atomic_ptr_init(&a, (void_ptr_t)&x);
	TEST_ASSERT_EQUAL_PTR((void_ptr_t)&x, sk_atomic_ptr_exchange(&a, (void_ptr_t)&y));
	TEST_ASSERT_EQUAL_PTR((void_ptr_t)&y, sk_atomic_ptr_load_relaxed(&a));
}

#endif /* SK_TESTS */
