/**
 * @file notify.c
 * @brief add_impl observer dispatch (APX-365).
 *
 * Each sk_editor_notify_* copies the impl list for that type id, sorts by
 * (order, insertion index), then calls the function pointer. This is not an
 * event bus: publishers name the kind they emit and walk only that list.
 */

#include "notify.h"

#include "allocator.h"

#define SK_EDITOR_NOTIFY_STACK_CAP 32u

typedef struct sk_editor_notify_slot_t {
	i32 order;
	u32 index;
	const void* impl;
} sk_editor_notify_slot_t;

typedef void (*sk_editor_notify_visit_fn)(const void* observer, void* payload);

static void notify_sort(sk_editor_notify_slot_t* slots, u32 count) {
	u32 i;
	for (i = 1u; i < count; ++i) {
		sk_editor_notify_slot_t key = slots[i];
		i32 j = (i32)i - 1;
		while (j >= 0 && (slots[j].order > key.order || (slots[j].order == key.order && slots[j].index > key.index))) {
			slots[j + 1] = slots[j];
			j -= 1;
		}
		slots[j + 1] = key;
	}
}

static void notify_emit(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t type_id, sk_editor_notify_visit_fn visit, void* payload) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const_ptr_t stack_impls[SK_EDITOR_NOTIFY_STACK_CAP];
	sk_editor_notify_slot_t stack_slots[SK_EDITOR_NOTIFY_STACK_CAP];
	const_ptr_t* impls = stack_impls;
	sk_editor_notify_slot_t* slots = stack_slots;
	const_ptr_t* heap_impls = NULL;
	sk_editor_notify_slot_t* heap_slots = NULL;
	u32 count = app_api->impl_count(app_context, type_id);
	u32 i;

	if (count == 0u) {
		return;
	}

	if (count > SK_EDITOR_NOTIFY_STACK_CAP) {
		heap_impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
		heap_slots = (sk_editor_notify_slot_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(sk_editor_notify_slot_t));
		if (heap_impls != NULL && heap_slots != NULL) {
			impls = heap_impls;
			slots = heap_slots;
		} else {
			if (heap_impls != NULL) {
				alloc->free(alloc->instance, heap_impls);
				heap_impls = NULL;
			}
			if (heap_slots != NULL) {
				alloc->free(alloc->instance, heap_slots);
				heap_slots = NULL;
			}
			count = SK_EDITOR_NOTIFY_STACK_CAP;
		}
	}

	(void)app_api->get_all_impls(app_context, type_id, impls, count);
	for (i = 0u; i < count; ++i) {
		const i32* order = (const i32*)impls[i];
		slots[i].order = *order;
		slots[i].index = i;
		slots[i].impl = impls[i];
	}
	notify_sort(slots, count);
	for (i = 0u; i < count; ++i) {
		visit(slots[i].impl, payload);
	}

	if (heap_impls != NULL) {
		alloc->free(alloc->instance, heap_impls);
	}
	if (heap_slots != NULL) {
		alloc->free(alloc->instance, heap_slots);
	}
}

static void visit_selection_changed(const void* observer, void* payload) {
	const sk_editor_on_selection_changed_t* o = (const sk_editor_on_selection_changed_t*)observer;
	(void)payload;
	if (o->on_selection_changed != NULL) {
		o->on_selection_changed(o->user);
	}
}

typedef struct sk_editor_notify_rid_payload_t {
	u32 workspace_id;
	sk_rid_t rid;
} sk_editor_notify_rid_payload_t;

typedef struct sk_editor_notify_entity_payload_t {
	u32 workspace_id;
	void* entity;
} sk_editor_notify_entity_payload_t;

typedef struct sk_editor_notify_rename_payload_t {
	u32 workspace_id;
	sk_rid_t rid;
	const_chr_t name;
} sk_editor_notify_rename_payload_t;

static void visit_entity_selection(const void* observer, void* payload) {
	const sk_editor_on_entity_selection_t* o = (const sk_editor_on_entity_selection_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_entity_selection != NULL) {
		o->on_entity_selection(o->user, p->workspace_id, p->rid);
	}
}

static void visit_entity_deselection(const void* observer, void* payload) {
	const sk_editor_on_entity_deselection_t* o = (const sk_editor_on_entity_deselection_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_entity_deselection != NULL) {
		o->on_entity_deselection(o->user, p->workspace_id, p->rid);
	}
}

static void visit_entity_debug_selection(const void* observer, void* payload) {
	const sk_editor_on_entity_debug_selection_t* o = (const sk_editor_on_entity_debug_selection_t*)observer;
	const sk_editor_notify_entity_payload_t* p = (const sk_editor_notify_entity_payload_t*)payload;
	if (o->on_entity_debug_selection != NULL) {
		o->on_entity_debug_selection(o->user, p->workspace_id, p->entity);
	}
}

static void visit_entity_debug_deselection(const void* observer, void* payload) {
	const sk_editor_on_entity_debug_deselection_t* o = (const sk_editor_on_entity_debug_deselection_t*)observer;
	const sk_editor_notify_entity_payload_t* p = (const sk_editor_notify_entity_payload_t*)payload;
	if (o->on_entity_debug_deselection != NULL) {
		o->on_entity_debug_deselection(o->user, p->workspace_id, p->entity);
	}
}

static void visit_asset_selection(const void* observer, void* payload) {
	const sk_editor_on_asset_selection_t* o = (const sk_editor_on_asset_selection_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_asset_selection != NULL) {
		o->on_asset_selection(o->user, p->workspace_id, p->rid);
	}
}

static void visit_resource_selection(const void* observer, void* payload) {
	const sk_editor_on_resource_selection_t* o = (const sk_editor_on_resource_selection_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_resource_selection != NULL) {
		o->on_resource_selection(o->user, p->workspace_id, p->rid);
	}
}

static void visit_material_node_selection(const void* observer, void* payload) {
	const sk_editor_on_material_node_selection_t* o = (const sk_editor_on_material_node_selection_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_material_node_selection != NULL) {
		o->on_material_node_selection(o->user, p->workspace_id, p->rid);
	}
}

static void visit_drop_file(const void* observer, void* payload) {
	const sk_editor_on_drop_file_t* o = (const sk_editor_on_drop_file_t*)observer;
	const_chr_t path = (const_chr_t)payload;
	if (o->on_drop_file != NULL) {
		o->on_drop_file(o->user, path);
	}
}

static void visit_asset_opened(const void* observer, void* payload) {
	const sk_editor_on_asset_opened_t* o = (const sk_editor_on_asset_opened_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_asset_opened != NULL) {
		o->on_asset_opened(o->user, p->workspace_id, p->rid);
	}
}

static void visit_asset_activated(const void* observer, void* payload) {
	const sk_editor_on_asset_activated_t* o = (const sk_editor_on_asset_activated_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_asset_activated != NULL) {
		o->on_asset_activated(o->user, p->workspace_id, p->rid);
	}
}

static void visit_entity_created(const void* observer, void* payload) {
	const sk_editor_on_entity_created_t* o = (const sk_editor_on_entity_created_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_entity_created != NULL) {
		o->on_entity_created(o->user, p->workspace_id, p->rid);
	}
}

static void visit_entity_renamed(const void* observer, void* payload) {
	const sk_editor_on_entity_renamed_t* o = (const sk_editor_on_entity_renamed_t*)observer;
	const sk_editor_notify_rename_payload_t* p = (const sk_editor_notify_rename_payload_t*)payload;
	if (o->on_entity_renamed != NULL) {
		o->on_entity_renamed(o->user, p->workspace_id, p->rid, p->name);
	}
}

static void visit_entity_deleted(const void* observer, void* payload) {
	const sk_editor_on_entity_deleted_t* o = (const sk_editor_on_entity_deleted_t*)observer;
	const sk_editor_notify_rid_payload_t* p = (const sk_editor_notify_rid_payload_t*)payload;
	if (o->on_entity_deleted != NULL) {
		o->on_entity_deleted(o->user, p->workspace_id, p->rid);
	}
}

void sk_editor_notify_selection_changed(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_SELECTION_CHANGED, visit_selection_changed, NULL);
}

void sk_editor_notify_entity_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_SELECTION, visit_entity_selection, &payload);
}

void sk_editor_notify_entity_deselection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_DESELECTION, visit_entity_deselection, &payload);
}

void sk_editor_notify_entity_debug_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, void* entity) {
	sk_editor_notify_entity_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.entity = entity;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_DEBUG_SELECTION, visit_entity_debug_selection, &payload);
}

void sk_editor_notify_entity_debug_deselection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, void* entity) {
	sk_editor_notify_entity_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.entity = entity;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_DEBUG_DESELECTION, visit_entity_debug_deselection, &payload);
}

void sk_editor_notify_asset_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ASSET_SELECTION, visit_asset_selection, &payload);
}

void sk_editor_notify_resource_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_RESOURCE_SELECTION, visit_resource_selection, &payload);
}

void sk_editor_notify_material_node_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_MATERIAL_NODE_SELECTION, visit_material_node_selection, &payload);
}

void sk_editor_notify_drop_file(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path) {
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_DROP_FILE, visit_drop_file, SK_CONST_CAST(void*, path));
}

void sk_editor_notify_asset_opened(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ASSET_OPENED, visit_asset_opened, &payload);
}

void sk_editor_notify_asset_activated(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ASSET_ACTIVATED, visit_asset_activated, &payload);
}

void sk_editor_notify_entity_created(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_CREATED, visit_entity_created, &payload);
}

void sk_editor_notify_entity_renamed(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid, const_chr_t name) {
	sk_editor_notify_rename_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	payload.name = name;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_RENAMED, visit_entity_renamed, &payload);
}

void sk_editor_notify_entity_deleted(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid) {
	sk_editor_notify_rid_payload_t payload;
	payload.workspace_id = workspace_id;
	payload.rid = rid;
	notify_emit(app_context, app_api, SK_EDITOR_NOTIFY_ENTITY_DELETED, visit_entity_deleted, &payload);
}

#ifdef SK_TESTS

#include "test.h"

#include <string.h>

#define EN_TRACE_CAP 16u

typedef struct en_trace_t {
	sk_app_context_t* ctx;
	const sk_app_api_t* api;
	u32 count;
	i32 tags[EN_TRACE_CAP];
	u32 workspaces[EN_TRACE_CAP];
	u64 rids[EN_TRACE_CAP];
	void* entities[EN_TRACE_CAP];
	const_chr_t names[EN_TRACE_CAP];
	i32 remove_self_tag;
	sk_editor_on_selection_changed_t* remove_self;
	sk_editor_on_selection_changed_t* add_during;
	i32 added_during;
} en_trace_t;

static void en_trace_push(en_trace_t* t, i32 tag, u32 workspace_id, sk_rid_t rid, void* entity, const_chr_t name) {
	if (t->count >= EN_TRACE_CAP) {
		return;
	}
	t->tags[t->count] = tag;
	t->workspaces[t->count] = workspace_id;
	t->rids[t->count] = rid.id;
	t->entities[t->count] = entity;
	t->names[t->count] = name;
	t->count++;
}

static void en_on_selection(void* user) {
	en_trace_t* t = (en_trace_t*)user;
	en_trace_push(t, 1, 0u, SK_RID_ZERO, NULL, NULL);
	if (t->remove_self != NULL && t->remove_self_tag == 1) {
		t->api->remove_impl(t->ctx, SK_EDITOR_NOTIFY_SELECTION_CHANGED, t->remove_self);
		t->remove_self = NULL;
	}
	if (t->add_during != NULL && t->added_during == 0) {
		t->api->add_impl(t->ctx, SK_EDITOR_NOTIFY_SELECTION_CHANGED, t->add_during);
		t->added_during = 1;
	}
}

static void en_on_selection_tag(void* user) {
	en_trace_t* t = (en_trace_t*)user;
	en_trace_push(t, 2, 0u, SK_RID_ZERO, NULL, NULL);
}

static void en_on_selection_tag3(void* user) {
	en_trace_t* t = (en_trace_t*)user;
	en_trace_push(t, 3, 0u, SK_RID_ZERO, NULL, NULL);
}

static void en_on_asset(void* user, u32 workspace_id, sk_rid_t rid) {
	en_trace_push((en_trace_t*)user, 10, workspace_id, rid, NULL, NULL);
}

static void en_on_entity(void* user, u32 workspace_id, sk_rid_t rid) {
	en_trace_push((en_trace_t*)user, 20, workspace_id, rid, NULL, NULL);
}

static void en_on_debug(void* user, u32 workspace_id, void* entity) {
	en_trace_push((en_trace_t*)user, 30, workspace_id, SK_RID_ZERO, entity, NULL);
}

static void en_on_drop(void* user, const_chr_t path) {
	en_trace_push((en_trace_t*)user, 40, 0u, SK_RID_ZERO, NULL, path);
}

static void en_on_opened(void* user, u32 workspace_id, sk_rid_t rid) {
	en_trace_push((en_trace_t*)user, 50, workspace_id, rid, NULL, NULL);
}

static void en_on_activated(void* user, u32 workspace_id, sk_rid_t rid) {
	en_trace_push((en_trace_t*)user, 51, workspace_id, rid, NULL, NULL);
}

static void en_on_created(void* user, u32 workspace_id, sk_rid_t rid) {
	en_trace_push((en_trace_t*)user, 60, workspace_id, rid, NULL, NULL);
}

static void en_on_renamed(void* user, u32 workspace_id, sk_rid_t rid, const_chr_t name) {
	en_trace_push((en_trace_t*)user, 61, workspace_id, rid, NULL, name);
}

static void en_on_deleted(void* user, u32 workspace_id, sk_rid_t rid) {
	en_trace_push((en_trace_t*)user, 62, workspace_id, rid, NULL, NULL);
}

SK_TEST(editor_notify_empty_emit_is_noop) {
	sk_app_boot_t boot = sk_app_create();
	TEST_ASSERT_NOT_NULL(boot.context);
	sk_editor_notify_selection_changed(boot.context, boot.api);
	sk_editor_notify_drop_file(boot.context, boot.api, "x");
	sk_app_shutdown(boot.context);
}

SK_TEST(editor_notify_dispatches_sorted_by_order) {
	sk_app_boot_t boot = sk_app_create();
	en_trace_t trace;
	sk_editor_on_selection_changed_t late;
	sk_editor_on_selection_changed_t early;
	sk_editor_on_selection_changed_t mid;
	sk_editor_on_selection_changed_t mid_tie;

	TEST_ASSERT_NOT_NULL(boot.context);
	memset(&trace, 0, sizeof(trace));
	memset(&late, 0, sizeof(late));
	memset(&early, 0, sizeof(early));
	memset(&mid, 0, sizeof(mid));
	memset(&mid_tie, 0, sizeof(mid_tie));

	late.order = 20;
	late.user = &trace;
	late.on_selection_changed = en_on_selection_tag3;
	early.order = 0;
	early.user = &trace;
	early.on_selection_changed = en_on_selection;
	mid.order = 10;
	mid.user = &trace;
	mid.on_selection_changed = en_on_selection_tag;
	mid_tie.order = 10;
	mid_tie.user = &trace;
	mid_tie.on_selection_changed = en_on_selection_tag3;

	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &late);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &early);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &mid);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &mid_tie);

	sk_editor_notify_selection_changed(boot.context, boot.api);
	TEST_ASSERT_EQUAL_UINT(4u, trace.count);
	TEST_ASSERT_EQUAL_INT(1, trace.tags[0]);
	TEST_ASSERT_EQUAL_INT(2, trace.tags[1]);
	TEST_ASSERT_EQUAL_INT(3, trace.tags[2]);
	TEST_ASSERT_EQUAL_INT(3, trace.tags[3]);

	sk_app_shutdown(boot.context);
}

SK_TEST(editor_notify_remove_and_copy_before_walk) {
	sk_app_boot_t boot = sk_app_create();
	en_trace_t trace;
	sk_editor_on_selection_changed_t a;
	sk_editor_on_selection_changed_t b;
	sk_editor_on_selection_changed_t extra;

	TEST_ASSERT_NOT_NULL(boot.context);
	memset(&trace, 0, sizeof(trace));
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&extra, 0, sizeof(extra));
	trace.ctx = boot.context;
	trace.api = boot.api;

	a.order = 0;
	a.user = &trace;
	a.on_selection_changed = en_on_selection;
	b.order = 1;
	b.user = &trace;
	b.on_selection_changed = en_on_selection_tag;
	extra.order = 2;
	extra.user = &trace;
	extra.on_selection_changed = en_on_selection_tag3;

	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &a);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &b);
	trace.remove_self = &a;
	trace.remove_self_tag = 1;
	trace.add_during = &extra;

	sk_editor_notify_selection_changed(boot.context, boot.api);
	/* a ran (and removed itself) and b ran; extra was added mid-walk and is not called. */
	TEST_ASSERT_EQUAL_UINT(2u, trace.count);
	TEST_ASSERT_EQUAL_INT(1, trace.tags[0]);
	TEST_ASSERT_EQUAL_INT(2, trace.tags[1]);
	TEST_ASSERT_EQUAL_UINT(2u, boot.api->impl_count(boot.context, SK_EDITOR_NOTIFY_SELECTION_CHANGED));

	trace.count = 0u;
	sk_editor_notify_selection_changed(boot.context, boot.api);
	TEST_ASSERT_EQUAL_UINT(2u, trace.count);
	TEST_ASSERT_EQUAL_INT(2, trace.tags[0]);
	TEST_ASSERT_EQUAL_INT(3, trace.tags[1]);

	sk_app_shutdown(boot.context);
}

SK_TEST(editor_notify_payloads_match_cpp_events) {
	sk_app_boot_t boot = sk_app_create();
	en_trace_t trace;
	sk_editor_on_asset_selection_t asset;
	sk_editor_on_entity_selection_t entity;
	sk_editor_on_entity_debug_selection_t debug;
	sk_editor_on_drop_file_t drop;
	sk_editor_on_asset_opened_t opened;
	sk_editor_on_asset_activated_t activated;
	sk_editor_on_entity_created_t created;
	sk_editor_on_entity_renamed_t renamed;
	sk_editor_on_entity_deleted_t deleted;
	sk_rid_t rid;
	i32 entity_obj = 7;

	TEST_ASSERT_NOT_NULL(boot.context);
	memset(&trace, 0, sizeof(trace));
	memset(&asset, 0, sizeof(asset));
	memset(&entity, 0, sizeof(entity));
	memset(&debug, 0, sizeof(debug));
	memset(&drop, 0, sizeof(drop));
	memset(&opened, 0, sizeof(opened));
	memset(&activated, 0, sizeof(activated));
	memset(&created, 0, sizeof(created));
	memset(&renamed, 0, sizeof(renamed));
	memset(&deleted, 0, sizeof(deleted));
	rid.id = 42ull;

	asset.user = &trace;
	asset.on_asset_selection = en_on_asset;
	entity.user = &trace;
	entity.on_entity_selection = en_on_entity;
	debug.user = &trace;
	debug.on_entity_debug_selection = en_on_debug;
	drop.user = &trace;
	drop.on_drop_file = en_on_drop;
	opened.user = &trace;
	opened.on_asset_opened = en_on_opened;
	activated.user = &trace;
	activated.on_asset_activated = en_on_activated;
	created.user = &trace;
	created.on_entity_created = en_on_created;
	renamed.user = &trace;
	renamed.on_entity_renamed = en_on_renamed;
	deleted.user = &trace;
	deleted.on_entity_deleted = en_on_deleted;

	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ASSET_SELECTION, &asset);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ENTITY_SELECTION, &entity);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ENTITY_DEBUG_SELECTION, &debug);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_DROP_FILE, &drop);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ASSET_OPENED, &opened);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ASSET_ACTIVATED, &activated);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ENTITY_CREATED, &created);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ENTITY_RENAMED, &renamed);
	boot.api->add_impl(boot.context, SK_EDITOR_NOTIFY_ENTITY_DELETED, &deleted);

	sk_editor_notify_asset_selection(boot.context, boot.api, 1u, rid);
	sk_editor_notify_entity_selection(boot.context, boot.api, 1u, rid);
	sk_editor_notify_entity_debug_selection(boot.context, boot.api, 2u, &entity_obj);
	sk_editor_notify_drop_file(boot.context, boot.api, "/tmp/mesh.fbx");
	sk_editor_notify_asset_opened(boot.context, boot.api, 1u, rid);
	sk_editor_notify_asset_activated(boot.context, boot.api, 1u, rid);
	sk_editor_notify_entity_created(boot.context, boot.api, 1u, rid);
	sk_editor_notify_entity_renamed(boot.context, boot.api, 1u, rid, "Hero");
	sk_editor_notify_entity_deleted(boot.context, boot.api, 1u, rid);

	TEST_ASSERT_EQUAL_UINT(9u, trace.count);
	TEST_ASSERT_EQUAL_INT(10, trace.tags[0]);
	TEST_ASSERT_EQUAL_UINT(1u, trace.workspaces[0]);
	TEST_ASSERT_EQUAL_UINT64(42ull, trace.rids[0]);
	TEST_ASSERT_EQUAL_INT(20, trace.tags[1]);
	TEST_ASSERT_EQUAL_PTR(&entity_obj, trace.entities[2]);
	TEST_ASSERT_EQUAL_STRING("/tmp/mesh.fbx", trace.names[3]);
	TEST_ASSERT_EQUAL_INT(50, trace.tags[4]);
	TEST_ASSERT_EQUAL_INT(51, trace.tags[5]);
	TEST_ASSERT_EQUAL_INT(60, trace.tags[6]);
	TEST_ASSERT_EQUAL_STRING("Hero", trace.names[7]);
	TEST_ASSERT_EQUAL_INT(62, trace.tags[8]);

	sk_app_shutdown(boot.context);
}

#endif /* SK_TESTS */
