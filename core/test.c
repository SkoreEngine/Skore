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

/* ---- atomics.h tests (C11 <stdatomic.h> wrapper; header-only module) ---- */

SK_TEST(atomics_u32_all_orders) {
	sk_atomic_u32_t a;
	sk_atomic_u32_init(&a, 7u);
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_u32_load_acquire(&a));
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_u32_load_relaxed(&a));

	sk_atomic_u32_store(&a, 10u);
	TEST_ASSERT_EQUAL_UINT32(10u, sk_atomic_u32_load(&a));
	sk_atomic_u32_store_release(&a, 11u);
	TEST_ASSERT_EQUAL_UINT32(11u, sk_atomic_u32_load_acquire(&a));
	sk_atomic_u32_store_relaxed(&a, 12u);
	TEST_ASSERT_EQUAL_UINT32(12u, sk_atomic_u32_load_relaxed(&a));

	TEST_ASSERT_EQUAL_UINT32(12u, sk_atomic_u32_exchange(&a, 20u));
	TEST_ASSERT_EQUAL_UINT32(20u, sk_atomic_u32_exchange_acq_rel(&a, 21u));
	TEST_ASSERT_EQUAL_UINT32(21u, sk_atomic_u32_exchange_relaxed(&a, 22u));
	TEST_ASSERT_EQUAL_UINT32(22u, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_cas_strong_updates_expected_on_failure) {
	sk_atomic_u32_t a;
	sk_atomic_u32_init(&a, 30u);

	u32 expected = 30u;
	TEST_ASSERT_TRUE(sk_atomic_u32_cas_strong(&a, &expected, 40u));
	TEST_ASSERT_EQUAL_UINT32(40u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(30u, expected);

	TEST_ASSERT_FALSE(sk_atomic_u32_cas_strong(&a, &expected, 50u));
	TEST_ASSERT_EQUAL_UINT32(40u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(40u, expected);

	expected = 40u;
	TEST_ASSERT_TRUE(sk_atomic_u32_cas_strong_acquire(&a, &expected, 60u));
	expected = 60u;
	TEST_ASSERT_TRUE(sk_atomic_u32_cas_strong_release(&a, &expected, 61u));
	expected = 61u;
	TEST_ASSERT_TRUE(sk_atomic_u32_cas_strong_acq_rel(&a, &expected, 62u));
	expected = 62u;
	TEST_ASSERT_TRUE(sk_atomic_u32_cas_strong_relaxed(&a, &expected, 63u));
	TEST_ASSERT_EQUAL_UINT32(63u, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_u32_fetch_ops) {
	sk_atomic_u32_t a;
	sk_atomic_u32_init(&a, 30u);

	TEST_ASSERT_EQUAL_UINT32(30u, sk_atomic_u32_fetch_add(&a, 5u));
	TEST_ASSERT_EQUAL_UINT32(35u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(35u, sk_atomic_u32_fetch_sub(&a, 3u));
	TEST_ASSERT_EQUAL_UINT32(32u, sk_atomic_u32_load(&a));

	TEST_ASSERT_EQUAL_UINT32(32u, sk_atomic_u32_fetch_or(&a, 0x0Fu));
	TEST_ASSERT_EQUAL_UINT32(0x2Fu, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(0x2Fu, sk_atomic_u32_fetch_and(&a, 0x33u));
	TEST_ASSERT_EQUAL_UINT32(0x23u, sk_atomic_u32_load(&a));

	TEST_ASSERT_EQUAL_UINT32(0x23u, sk_atomic_u32_fetch_add_acquire(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0x24u, sk_atomic_u32_fetch_add_release(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0x25u, sk_atomic_u32_fetch_add_acq_rel(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0x26u, sk_atomic_u32_fetch_add_relaxed(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0x27u, sk_atomic_u32_load(&a));

	TEST_ASSERT_EQUAL_UINT32(0x27u, sk_atomic_u32_fetch_sub_relaxed(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0x26u, sk_atomic_u32_fetch_or_release(&a, 0x40u));
	TEST_ASSERT_EQUAL_UINT32(0x66u, sk_atomic_u32_fetch_and_acquire(&a, 0x7Fu));
	TEST_ASSERT_EQUAL_UINT32(0x66u, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_u32_fetch_wraps_unsigned) {
	sk_atomic_u32_t a;
	sk_atomic_u32_init(&a, 0xFFFFFFFFu);
	TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, sk_atomic_u32_fetch_add(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_atomic_u32_load(&a));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_atomic_u32_fetch_sub(&a, 1u));
	TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, sk_atomic_u32_load(&a));
}

SK_TEST(atomics_i32_signed_ops) {
	sk_atomic_i32_t a;
	sk_atomic_i32_init(&a, -7);
	TEST_ASSERT_EQUAL_INT32(-7, sk_atomic_i32_load(&a));
	TEST_ASSERT_EQUAL_INT32(-7, sk_atomic_i32_fetch_add(&a, 3));
	TEST_ASSERT_EQUAL_INT32(-4, sk_atomic_i32_load(&a));
	TEST_ASSERT_EQUAL_INT32(-4, sk_atomic_i32_fetch_sub(&a, -2));
	TEST_ASSERT_EQUAL_INT32(-2, sk_atomic_i32_load(&a));

	i32 expected = -2;
	TEST_ASSERT_TRUE(sk_atomic_i32_cas_strong(&a, &expected, 5));
	TEST_ASSERT_EQUAL_INT32(5, sk_atomic_i32_load(&a));
}

SK_TEST(atomics_u64_ops) {
	sk_atomic_u64_t a;
	sk_atomic_u64_init(&a, 0x1234567890ABCDEFull);
	TEST_ASSERT_EQUAL_UINT64(0x1234567890ABCDEFull, sk_atomic_u64_load(&a));
	TEST_ASSERT_EQUAL_UINT64(0x1234567890ABCDEFull, sk_atomic_u64_load_acquire(&a));
	sk_atomic_u64_store(&a, 0x0FEDCBA987654321ull);
	TEST_ASSERT_EQUAL_UINT64(0x0FEDCBA987654321ull, sk_atomic_u64_load(&a));

	TEST_ASSERT_EQUAL_UINT64(0x0FEDCBA987654321ull, sk_atomic_u64_exchange(&a, 1000ull));
	TEST_ASSERT_EQUAL_UINT64(1000ull, sk_atomic_u64_load(&a));
	TEST_ASSERT_EQUAL_UINT64(1000ull, sk_atomic_u64_fetch_add(&a, 25ull));
	TEST_ASSERT_EQUAL_UINT64(1025ull, sk_atomic_u64_fetch_sub(&a, 5ull));
	TEST_ASSERT_EQUAL_UINT64(1020ull, sk_atomic_u64_load(&a));

	u64 expected = 1020ull;
	TEST_ASSERT_TRUE(sk_atomic_u64_cas_strong(&a, &expected, 1ull));
	TEST_ASSERT_EQUAL_UINT64(1ull, sk_atomic_u64_load(&a));
}

SK_TEST(atomics_i64_signed_ops) {
	sk_atomic_i64_t a;
	sk_atomic_i64_init(&a, -1000000ll);
	TEST_ASSERT_EQUAL_INT64(-1000000ll, sk_atomic_i64_load(&a));
	TEST_ASSERT_EQUAL_INT64(-1000000ll, sk_atomic_i64_fetch_add(&a, 1000000ll));
	TEST_ASSERT_EQUAL_INT64(0ll, sk_atomic_i64_load(&a));
	TEST_ASSERT_EQUAL_INT64(0ll, sk_atomic_i64_fetch_sub(&a, -42ll));
	TEST_ASSERT_EQUAL_INT64(42ll, sk_atomic_i64_load(&a));
}

SK_TEST(atomics_ptr_ops) {
	int value_a = 11;
	int value_b = 22;
	sk_atomic_ptr_t p;
	sk_atomic_ptr_init(&p, NULL);
	TEST_ASSERT_NULL(sk_atomic_ptr_load(&p));
	TEST_ASSERT_NULL(sk_atomic_ptr_load_acquire(&p));

	sk_atomic_ptr_store(&p, &value_a);
	TEST_ASSERT_EQUAL_PTR(&value_a, sk_atomic_ptr_load(&p));
	sk_atomic_ptr_store_release(&p, &value_b);
	TEST_ASSERT_EQUAL_PTR(&value_b, sk_atomic_ptr_load_acquire(&p));
	sk_atomic_ptr_store_relaxed(&p, NULL);
	TEST_ASSERT_NULL(sk_atomic_ptr_load_relaxed(&p));

	TEST_ASSERT_NULL(sk_atomic_ptr_exchange(&p, &value_a));
	TEST_ASSERT_EQUAL_PTR(&value_a, sk_atomic_ptr_exchange_acq_rel(&p, &value_b));
	TEST_ASSERT_EQUAL_PTR(&value_b, sk_atomic_ptr_exchange_relaxed(&p, NULL));
	TEST_ASSERT_NULL(sk_atomic_ptr_load(&p));

	void_ptr_t expected = NULL;
	TEST_ASSERT_TRUE(sk_atomic_ptr_cas_strong(&p, &expected, &value_a));
	TEST_ASSERT_EQUAL_PTR(&value_a, sk_atomic_ptr_load(&p));

	expected = NULL;
	TEST_ASSERT_FALSE(sk_atomic_ptr_cas_strong(&p, &expected, &value_b));
	TEST_ASSERT_EQUAL_PTR(&value_a, expected);

	expected = &value_a;
	TEST_ASSERT_TRUE(sk_atomic_ptr_cas_strong_acquire(&p, &expected, &value_b));
	expected = &value_b;
	TEST_ASSERT_TRUE(sk_atomic_ptr_cas_strong_release(&p, &expected, &value_a));
	expected = &value_a;
	TEST_ASSERT_TRUE(sk_atomic_ptr_cas_strong_acq_rel(&p, &expected, &value_b));
	expected = &value_b;
	TEST_ASSERT_TRUE(sk_atomic_ptr_cas_strong_relaxed(&p, &expected, NULL));
	TEST_ASSERT_NULL(sk_atomic_ptr_load(&p));
}

SK_TEST(atomics_fence_helpers_are_callable) {
	sk_atomic_thread_fence_acquire();
	sk_atomic_thread_fence_release();
	sk_atomic_thread_fence_seq_cst();
	TEST_PASS();
}

#endif /* SK_TESTS */
