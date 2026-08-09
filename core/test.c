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

/* ---- atomics.h (header-only module) ---- */

SK_TEST(atomics_order_enum_has_expected_strength) {
	/* Ordering strength must strictly increase for backend mapping. */
	TEST_ASSERT_TRUE(SK_ATOMIC_ORDER_RELAXED < SK_ATOMIC_ORDER_ACQUIRE);
	TEST_ASSERT_TRUE(SK_ATOMIC_ORDER_ACQUIRE < SK_ATOMIC_ORDER_RELEASE);
	TEST_ASSERT_TRUE(SK_ATOMIC_ORDER_RELEASE < SK_ATOMIC_ORDER_ACQ_REL);
	TEST_ASSERT_TRUE(SK_ATOMIC_ORDER_ACQ_REL < SK_ATOMIC_ORDER_SEQ_CST);
	sk_atomic_thread_fence(SK_ATOMIC_ORDER_SEQ_CST);
	sk_atomic_thread_fence(SK_ATOMIC_ORDER_ACQ_REL);
	sk_atomic_thread_fence(SK_ATOMIC_ORDER_ACQUIRE);
}

SK_TEST(atomics_u32_init_load_store) {
	u32 counter = 0u;
	sk_atomic_u32_init(&counter, 41u);
	TEST_ASSERT_EQUAL_UINT32(41u, sk_atomic_u32_load(&counter));
	TEST_ASSERT_EQUAL_UINT32(41u, sk_atomic_u32_load_ordered(&counter, SK_ATOMIC_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(41u, sk_atomic_u32_load_ordered(&counter, SK_ATOMIC_ORDER_ACQUIRE));
	sk_atomic_u32_store(&counter, 7u);
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_u32_load(&counter));
	sk_atomic_u32_store_ordered(&counter, 8u, SK_ATOMIC_ORDER_RELEASE);
	TEST_ASSERT_EQUAL_UINT32(8u, sk_atomic_u32_load(&counter));
	sk_atomic_u32_store_ordered(&counter, 9u, SK_ATOMIC_ORDER_RELAXED);
	TEST_ASSERT_EQUAL_UINT32(9u, sk_atomic_u32_load(&counter));
}

SK_TEST(atomics_u32_exchange_returns_old) {
	u32 slot = 1u;
	TEST_ASSERT_EQUAL_UINT32(1u, sk_atomic_u32_exchange(&slot, 9u));
	TEST_ASSERT_EQUAL_UINT32(9u, sk_atomic_u32_exchange(&slot, 2u));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_atomic_u32_load(&slot));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_atomic_u32_exchange_ordered(&slot, 5u, SK_ATOMIC_ORDER_ACQ_REL));
	TEST_ASSERT_EQUAL_UINT32(5u, sk_atomic_u32_load(&slot));
}

SK_TEST(atomics_u32_compare_exchange_strong) {
	u32 slot = 10u;
	u32 expected = 10u;

	TEST_ASSERT_EQUAL_INT(1, sk_atomic_u32_compare_exchange(&slot, &expected, 11u));
	TEST_ASSERT_EQUAL_UINT32(11u, slot);

	/* Mismatch: expected is updated to the observed value, object unchanged. */
	expected = 10u;
	TEST_ASSERT_EQUAL_INT(0, sk_atomic_u32_compare_exchange(&slot, &expected, 12u));
	TEST_ASSERT_EQUAL_UINT32(11u, slot);
	TEST_ASSERT_EQUAL_UINT32(11u, expected);

	expected = 11u;
	TEST_ASSERT_EQUAL_INT(1, sk_atomic_u32_compare_exchange_ordered(&slot, &expected, 13u, SK_ATOMIC_ORDER_RELAXED, SK_ATOMIC_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(13u, slot);
}

SK_TEST(atomics_u32_fetch_ops) {
	u32 value = 100u;
	TEST_ASSERT_EQUAL_UINT32(100u, sk_atomic_u32_fetch_add(&value, 5u));
	TEST_ASSERT_EQUAL_UINT32(105u, value);
	TEST_ASSERT_EQUAL_UINT32(105u, sk_atomic_u32_fetch_sub(&value, 15u));
	TEST_ASSERT_EQUAL_UINT32(90u, value);
	TEST_ASSERT_EQUAL_UINT32(90u, sk_atomic_u32_fetch_or(&value, 0xFu));
	TEST_ASSERT_EQUAL_UINT32(95u, value);
	TEST_ASSERT_EQUAL_UINT32(95u, sk_atomic_u32_fetch_and(&value, 0x11u));
	TEST_ASSERT_EQUAL_UINT32(17u, value);

	TEST_ASSERT_EQUAL_UINT32(17u, sk_atomic_u32_fetch_add_ordered(&value, 3u, SK_ATOMIC_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(20u, value);
	TEST_ASSERT_EQUAL_UINT32(20u, sk_atomic_u32_fetch_sub_ordered(&value, 4u, SK_ATOMIC_ORDER_ACQUIRE));
	TEST_ASSERT_EQUAL_UINT32(16u, sk_atomic_u32_fetch_or_ordered(&value, 1u, SK_ATOMIC_ORDER_RELEASE));
	TEST_ASSERT_EQUAL_UINT32(17u, value);
	TEST_ASSERT_EQUAL_UINT32(17u, sk_atomic_u32_fetch_and_ordered(&value, 15u, SK_ATOMIC_ORDER_ACQ_REL));
	TEST_ASSERT_EQUAL_UINT32(1u, value);
}

SK_TEST(atomics_u32_wraparound_ops) {
	u32 value = 0xFFFFFFFFu;
	TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, sk_atomic_u32_fetch_add(&value, 1u));
	TEST_ASSERT_EQUAL_UINT32(0u, value);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_atomic_u32_fetch_sub(&value, 1u));
	TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, value);
}

SK_TEST(atomics_i32_signed_ops) {
	i32 value = -10;
	sk_atomic_i32_init(&value, -10);
	TEST_ASSERT_EQUAL_INT(-10, sk_atomic_i32_load(&value));
	sk_atomic_i32_store(&value, 5);
	TEST_ASSERT_EQUAL_INT(5, sk_atomic_i32_exchange(&value, -3));
	TEST_ASSERT_EQUAL_INT(-3, sk_atomic_i32_fetch_add(&value, 7));
	TEST_ASSERT_EQUAL_INT(4, value);
	TEST_ASSERT_EQUAL_INT(4, sk_atomic_i32_fetch_sub(&value, 9));
	TEST_ASSERT_EQUAL_INT(-5, value);
	TEST_ASSERT_EQUAL_INT(-5, sk_atomic_i32_fetch_or(&value, 1));
	TEST_ASSERT_EQUAL_INT(-5, value);
	TEST_ASSERT_EQUAL_INT(-5, sk_atomic_i32_fetch_and(&value, 7));
	TEST_ASSERT_EQUAL_INT(3, value);
}

SK_TEST(atomics_i32_compare_exchange_strong) {
	i32 slot = 4;
	i32 expected = 4;
	TEST_ASSERT_EQUAL_INT(1, sk_atomic_i32_compare_exchange(&slot, &expected, 8));
	TEST_ASSERT_EQUAL_INT(8, slot);
	expected = 0;
	TEST_ASSERT_EQUAL_INT(0, sk_atomic_i32_compare_exchange(&slot, &expected, 9));
	TEST_ASSERT_EQUAL_INT(8, expected);
}

SK_TEST(atomics_u64_ops) {
	u64 value = 0ull;
	sk_atomic_u64_init(&value, 2ull);
	TEST_ASSERT_EQUAL_UINT64(2ull, sk_atomic_u64_load(&value));
	sk_atomic_u64_store(&value, 5ull);
	TEST_ASSERT_EQUAL_UINT64(5ull, sk_atomic_u64_fetch_add(&value, 3ull));
	TEST_ASSERT_EQUAL_UINT64(8ull, value);
	TEST_ASSERT_EQUAL_UINT64(8ull, sk_atomic_u64_fetch_sub(&value, 6ull));
	TEST_ASSERT_EQUAL_UINT64(2ull, sk_atomic_u64_fetch_or(&value, 4ull));
	TEST_ASSERT_EQUAL_UINT64(6ull, sk_atomic_u64_fetch_and(&value, 7ull));
	TEST_ASSERT_EQUAL_UINT64(6ull, value);
	TEST_ASSERT_EQUAL_UINT64(6ull, sk_atomic_u64_exchange(&value, 0xFFFFFFFFFFFFFFFFull));
	TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFFFFFull, sk_atomic_u64_load(&value));

	u64 expected = 0xFFFFFFFFFFFFFFFFull;
	TEST_ASSERT_EQUAL_INT(1, sk_atomic_u64_compare_exchange(&value, &expected, 9ull));
	TEST_ASSERT_EQUAL_UINT64(9ull, value);
	expected = 1ull;
	TEST_ASSERT_EQUAL_INT(0, sk_atomic_u64_compare_exchange(&value, &expected, 2ull));
	TEST_ASSERT_EQUAL_UINT64(9ull, expected);
}

SK_TEST(atomics_i64_ops) {
	i64 value = 8;
	sk_atomic_i64_init(&value, 8);
	TEST_ASSERT_EQUAL_INT64(8, sk_atomic_i64_load(&value));
	TEST_ASSERT_EQUAL_INT64(8, sk_atomic_i64_fetch_add(&value, -3));
	TEST_ASSERT_EQUAL_INT64(5, value);
	TEST_ASSERT_EQUAL_INT64(5, sk_atomic_i64_fetch_sub(&value, 7));
	TEST_ASSERT_EQUAL_INT64(-2, value);
	TEST_ASSERT_EQUAL_INT64(-2, sk_atomic_i64_exchange(&value, 11));
	TEST_ASSERT_EQUAL_INT64(11, sk_atomic_i64_fetch_and(&value, 5));
	TEST_ASSERT_EQUAL_INT64(1, sk_atomic_i64_fetch_or(&value, 6));
	TEST_ASSERT_EQUAL_INT64(7, value);
}

SK_TEST(atomics_ptr_ops) {
	int a = 1;
	int b = 2;
	void_ptr_t slot = &a;
	sk_atomic_ptr_init(&slot, &a);
	TEST_ASSERT_EQUAL_PTR(&a, sk_atomic_ptr_load(&slot));
	TEST_ASSERT_EQUAL_PTR(&a, sk_atomic_ptr_load_ordered(&slot, SK_ATOMIC_ORDER_ACQUIRE));
	TEST_ASSERT_EQUAL_PTR(&a, sk_atomic_ptr_exchange(&slot, &b));
	TEST_ASSERT_EQUAL_PTR(&b, sk_atomic_ptr_load(&slot));
	sk_atomic_ptr_store(&slot, &a);
	TEST_ASSERT_EQUAL_PTR(&a, sk_atomic_ptr_load(&slot));
	sk_atomic_ptr_store_ordered(&slot, &b, SK_ATOMIC_ORDER_RELEASE);
	TEST_ASSERT_EQUAL_PTR(&b, sk_atomic_ptr_load(&slot));
	TEST_ASSERT_EQUAL_PTR(&b, sk_atomic_ptr_exchange_ordered(&slot, &a, SK_ATOMIC_ORDER_ACQ_REL));
	TEST_ASSERT_EQUAL_PTR(&a, sk_atomic_ptr_load(&slot));

	void_ptr_t expected = &a;
	TEST_ASSERT_EQUAL_INT(1, sk_atomic_ptr_compare_exchange(&slot, &expected, &b));
	TEST_ASSERT_EQUAL_PTR(&b, slot);
	expected = &a;
	TEST_ASSERT_EQUAL_INT(0, sk_atomic_ptr_compare_exchange(&slot, &expected, 0));
	TEST_ASSERT_EQUAL_PTR(&b, slot);
	TEST_ASSERT_EQUAL_PTR(&b, expected);

	expected = &b;
	TEST_ASSERT_EQUAL_INT(1, sk_atomic_ptr_compare_exchange_ordered(&slot, &expected, &a, SK_ATOMIC_ORDER_RELAXED, SK_ATOMIC_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_PTR(&a, slot);
}

#endif /* SK_TESTS */
