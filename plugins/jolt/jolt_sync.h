#pragma once

/**
 * @file jolt_sync.h
 * @brief Plugin-private ECS ↔ Jolt bind surface (C ABI).
 *
 * The C++ TU owns the entity↔BodyID map and Jolt body create/update. The C
 * TU walks the ECS world (entities.h / jolt_components.h are not safe to
 * include from the Jolt C++ TU under clang-tidy). Not a public host header.
 */

#include "common.h"
#include "jolt.h"

typedef struct sk_app_context_t sk_app_context_t;
typedef struct sk_app_api_t sk_app_api_t;

#ifdef __cplusplus
extern "C" {
#endif

enum { SK_JOLT_SYNC_SHAPE_BOX = 1u, SK_JOLT_SYNC_SHAPE_SPHERE = 2u, SK_JOLT_SYNC_SHAPE_CAPSULE = 4u };

typedef struct sk_jolt_sync_spec_t {
	u32 index;
	u32 generation;
	i32 motion_type;
	f32 mass;
	f32 friction;
	f32 restitution;
	f32 linear_damping;
	f32 angular_damping;
	f32 gravity_factor;
	u32 object_layer;
	u32 flags;
	f32 pos_x;
	f32 pos_y;
	f32 pos_z;
	f32 rot_x;
	f32 rot_y;
	f32 rot_z;
	f32 rot_w;
	f32 lv_x;
	f32 lv_y;
	f32 lv_z;
	f32 av_x;
	f32 av_y;
	f32 av_z;
	u32 shape_mask;
	f32 box_x;
	f32 box_y;
	f32 box_z;
	f32 sphere_r;
	f32 cap_hh;
	f32 cap_r;
} sk_jolt_sync_spec_t;

typedef struct sk_jolt_sync_pose_t {
	u32 index;
	u32 generation;
	f32 pos_x;
	f32 pos_y;
	f32 pos_z;
	f32 rot_x;
	f32 rot_y;
	f32 rot_z;
	f32 rot_w;
	f32 lv_x;
	f32 lv_y;
	f32 lv_z;
	f32 av_x;
	f32 av_y;
	f32 av_z;
} sk_jolt_sync_pose_t;

void jolt_internal_sync_begin(void); // NOLINT(modernize-redundant-void-arg) — C ABI
void jolt_internal_bind(const sk_jolt_sync_spec_t* spec, sk_jolt_body_t** out_body);
void jolt_internal_sync_end(void);		 // NOLINT(modernize-redundant-void-arg) — C ABI
void jolt_internal_clear_bindings(void); // NOLINT(modernize-redundant-void-arg) — C ABI
u32 jolt_internal_writeback_count(void); // NOLINT(modernize-redundant-void-arg) — C ABI
i32 jolt_internal_writeback_at(u32 i, sk_jolt_sync_pose_t* out);

/* APX-308: mark the entity (slot index + generation, validated against the
 * entity map on the C++ side) as requiring a component-data re-application on
 * the next sync. The dirty set is consumed (cleared) at the end of the next
 * sync walk. See jolt.h entity_require_update for when to call it. */
void jolt_internal_mark_dirty(u32 index, u32 generation);

void jolt_ecs_sync_world(sk_world_t* world);
void jolt_ecs_write_back(sk_world_t* world);
void jolt_ecs_require_update(sk_world_t* world, sk_entity_t entity);
void jolt_ecs_reset(void); // NOLINT(modernize-redundant-void-arg) — C ABI
void sk_jolt_sync_app(sk_app_context_t** out_context, const sk_app_api_t** out_app_api);

#ifdef __cplusplus
}
#endif
