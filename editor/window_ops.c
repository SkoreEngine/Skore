/**
 * @file window_ops.c
 * @brief Per-window public function table lookup (APX-365).
 *
 * Thin first-impl helper over app_api->get_all_impls. Window classes register
 * their ops tables with add_impl; callers never take a direct symbol.
 */

#include "window_ops.h"

const void* sk_editor_window_ops_lookup(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t ops_type_id) {
	const void* first = NULL;
	(void)app_api->get_all_impls(app_context, ops_type_id, &first, 1u);
	return first;
}

#ifdef SK_TESTS

#include "test.h"

typedef struct ew_ops_dummy_t {
	i32 (*value)(void);
} ew_ops_dummy_t;

static i32 ew_ops_dummy_a_value(void) {
	return 11;
}

static i32 ew_ops_dummy_b_value(void) {
	return 22;
}

static const ew_ops_dummy_t ew_ops_dummy_a = {ew_ops_dummy_a_value};
static const ew_ops_dummy_t ew_ops_dummy_b = {ew_ops_dummy_b_value};

#define EW_OPS_TEST_TYPE_ID SK_TYPE_ID("sk.editor.window_ops.test", 0x6c1f2a90b4d8e317ULL, 0xa0f45c89d2b17e64ULL)
#define EW_OPS_TEST_EMPTY_TYPE_ID SK_TYPE_ID("sk.editor.window_ops.test_empty", 0x19ae70c3d5f2846bULL, 0x8c31d0a4f6e59207ULL)

SK_TEST(editor_window_ops_lookup_first_impl) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const ew_ops_dummy_t* ops;

	TEST_ASSERT_NOT_NULL(app);
	TEST_ASSERT_NULL(sk_editor_window_ops_lookup(app, boot.api, EW_OPS_TEST_EMPTY_TYPE_ID));
	TEST_ASSERT_NULL(sk_editor_window_ops_lookup(app, boot.api, EW_OPS_TEST_TYPE_ID));

	boot.api->add_impl(app, EW_OPS_TEST_TYPE_ID, &ew_ops_dummy_a);
	boot.api->add_impl(app, EW_OPS_TEST_TYPE_ID, &ew_ops_dummy_b);

	ops = (const ew_ops_dummy_t*)sk_editor_window_ops_lookup(app, boot.api, EW_OPS_TEST_TYPE_ID);
	TEST_ASSERT_EQUAL_PTR(&ew_ops_dummy_a, ops);
	TEST_ASSERT_EQUAL_INT(11, ops->value());

	/* remove_impl drops the first exact pointer; lookup then returns the next. */
	boot.api->remove_impl(app, EW_OPS_TEST_TYPE_ID, &ew_ops_dummy_a);
	ops = (const ew_ops_dummy_t*)sk_editor_window_ops_lookup(app, boot.api, EW_OPS_TEST_TYPE_ID);
	TEST_ASSERT_EQUAL_PTR(&ew_ops_dummy_b, ops);
	TEST_ASSERT_EQUAL_INT(22, ops->value());

	boot.api->remove_impl(app, EW_OPS_TEST_TYPE_ID, &ew_ops_dummy_b);
	TEST_ASSERT_NULL(sk_editor_window_ops_lookup(app, boot.api, EW_OPS_TEST_TYPE_ID));

	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
