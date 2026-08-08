/**
 * @file test.c
 * @brief Test registry + common.h unit tests (only active with SK_TESTS).
 *
 * Built as sk-test STATIC and linked only into sk-tests host and Debug plugins.
 * Never linked into Release plugins or production sk-core / sk-player.
 */

#include "test.h"

#ifdef SK_TESTS

#include "atomic.h"

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

/* ---- atomic.h (header-only module) ---- */

SK_TEST(atomic_types_match_integer_sizes) {
	TEST_ASSERT_EQUAL_size_t(sizeof(u8), sizeof(sk_atomic_u8_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(u16), sizeof(sk_atomic_u16_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(u32), sizeof(sk_atomic_u32_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(u64), sizeof(sk_atomic_u64_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(i8), sizeof(sk_atomic_i8_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(i16), sizeof(sk_atomic_i16_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(i32), sizeof(sk_atomic_i32_t));
	TEST_ASSERT_EQUAL_size_t(sizeof(i64), sizeof(sk_atomic_i64_t));
}

SK_TEST(atomic_memory_order_enum_maps_to_c11) {
	TEST_ASSERT_EQUAL_INT((int)memory_order_relaxed, (int)SK_MEMORY_ORDER_RELAXED);
	TEST_ASSERT_EQUAL_INT((int)memory_order_consume, (int)SK_MEMORY_ORDER_CONSUME);
	TEST_ASSERT_EQUAL_INT((int)memory_order_acquire, (int)SK_MEMORY_ORDER_ACQUIRE);
	TEST_ASSERT_EQUAL_INT((int)memory_order_release, (int)SK_MEMORY_ORDER_RELEASE);
	TEST_ASSERT_EQUAL_INT((int)memory_order_acq_rel, (int)SK_MEMORY_ORDER_ACQ_REL);
	TEST_ASSERT_EQUAL_INT((int)memory_order_seq_cst, (int)SK_MEMORY_ORDER_SEQ_CST);
}

SK_TEST(atomic_load_store_roundtrip) {
	sk_atomic_u32_t counter = 0u;
	sk_atomic_store_u32(&counter, 42u, SK_MEMORY_ORDER_RELAXED);
	TEST_ASSERT_EQUAL_UINT32(42u, sk_atomic_load_u32(&counter, SK_MEMORY_ORDER_RELAXED));
	sk_atomic_store_u32(&counter, 7u, SK_MEMORY_ORDER_RELEASE);
	TEST_ASSERT_EQUAL_UINT32(7u, sk_atomic_load_u32(&counter, SK_MEMORY_ORDER_ACQUIRE));
	sk_atomic_store_u32(&counter, 65536u, SK_MEMORY_ORDER_SEQ_CST);
	TEST_ASSERT_EQUAL_UINT32(65536u, sk_atomic_load_u32(&counter, SK_MEMORY_ORDER_SEQ_CST));
}

SK_TEST(atomic_fetch_add_sub_return_previous) {
	sk_atomic_u64_t counter = 100ull;
	TEST_ASSERT_EQUAL_UINT64(100ull, sk_atomic_fetch_add_u64(&counter, 25ull, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT64(125ull, sk_atomic_load_u64(&counter, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT64(125ull, sk_atomic_fetch_sub_u64(&counter, 30ull, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT64(95ull, sk_atomic_load_u64(&counter, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_signed_add_sub) {
	sk_atomic_i64_t counter = 100;
	TEST_ASSERT_EQUAL_INT64(100, sk_atomic_fetch_add_i64(&counter, -25, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_INT64(75, sk_atomic_load_i64(&counter, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_INT64(75, sk_atomic_fetch_sub_i64(&counter, -25, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_INT64(100, sk_atomic_load_i64(&counter, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_cas_strong_success) {
	sk_atomic_u32_t a = 1u;
	u32 expected = 1u;
	TEST_ASSERT_EQUAL_UINT8(1u, sk_atomic_cas_strong_u32(&a, &expected, 2u, SK_MEMORY_ORDER_ACQ_REL, SK_MEMORY_ORDER_ACQUIRE));
	TEST_ASSERT_EQUAL_UINT32(2u, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(1u, expected);
}

SK_TEST(atomic_cas_strong_failure_updates_expected) {
	sk_atomic_u32_t a = 1u;
	u32 expected = 9u;
	TEST_ASSERT_EQUAL_UINT8(0u, sk_atomic_cas_strong_u32(&a, &expected, 2u, SK_MEMORY_ORDER_ACQ_REL, SK_MEMORY_ORDER_ACQUIRE));
	TEST_ASSERT_EQUAL_UINT32(1u, expected);
	TEST_ASSERT_EQUAL_UINT32(1u, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_cas_weak_retries_until_success) {
	sk_atomic_u32_t a = 1u;
	u32 expected = 1u;
	while (!sk_atomic_cas_weak_u32(&a, &expected, 2u, SK_MEMORY_ORDER_ACQ_REL, SK_MEMORY_ORDER_ACQUIRE)) {
		expected = sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED);
	}
	TEST_ASSERT_EQUAL_UINT32(2u, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_exchange) {
	sk_atomic_u32_t a = 1u;
	TEST_ASSERT_EQUAL_UINT32(1u, sk_atomic_exchange_u32(&a, 9u, SK_MEMORY_ORDER_SEQ_CST));
	TEST_ASSERT_EQUAL_UINT32(9u, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_bitwise_fetch_ops) {
	sk_atomic_u32_t a = 0x0F0Fu;
	TEST_ASSERT_EQUAL_UINT32(0x0F0Fu, sk_atomic_fetch_or_u32(&a, 0xF0F0u, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(0xFFFFu, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(0xFFFFu, sk_atomic_fetch_and_u32(&a, 0xFF00u, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(0xFF00u, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(0xFF00u, sk_atomic_fetch_xor_u32(&a, 0xFFFFu, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT32(0x00FFu, sk_atomic_load_u32(&a, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_small_widths) {
	sk_atomic_u8_t flag = 0u;
	sk_atomic_store_u8(&flag, 1u, SK_MEMORY_ORDER_RELEASE);
	TEST_ASSERT_EQUAL_UINT8(1u, sk_atomic_load_u8(&flag, SK_MEMORY_ORDER_ACQUIRE));

	sk_atomic_u16_t index = 0u;
	TEST_ASSERT_EQUAL_UINT16(0u, sk_atomic_fetch_add_u16(&index, 1u, SK_MEMORY_ORDER_RELAXED));
	TEST_ASSERT_EQUAL_UINT16(1u, sk_atomic_load_u16(&index, SK_MEMORY_ORDER_RELAXED));
}

SK_TEST(atomic_lock_free_on_desktop) {
	sk_atomic_u32_t a = 0u;
	TEST_ASSERT_EQUAL_UINT8(1u, sk_atomic_is_lock_free_u32(&a));
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
	sk_atomic_u64_t b = 0ull;
	TEST_ASSERT_EQUAL_UINT8(1u, sk_atomic_is_lock_free_u64(&b));
#else
	TEST_PASS_MESSAGE("64-bit lock-free not asserted on 32-bit targets");
#endif
}

SK_TEST(atomic_thread_fence_runs) {
	sk_atomic_thread_fence(SK_MEMORY_ORDER_ACQUIRE);
	sk_atomic_thread_fence(SK_MEMORY_ORDER_RELEASE);
	sk_atomic_thread_fence(SK_MEMORY_ORDER_SEQ_CST);
	TEST_PASS_MESSAGE("thread fence executed");
}

#endif /* SK_TESTS */
