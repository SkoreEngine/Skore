/**
 * @file jolt_sync.c
 * @brief ECS walk for Jolt body reconciliation (APX-307).
 *
 * Iterates rigid-body entities and feeds the plugin-private C++ bind map
 * (jolt.cpp). Kept in C so the Jolt C++ TU does not include entities.h /
 * math3d.h (clang-tidy modernize-* on those C headers).
 */

#include "jolt_sync.h"

#include "app.h"
#include "entities.h"
#include "jolt_components.h"

#include <string.h>

static sk_world_t* bound_world = NULL;
static sk_query_t* bound_query = NULL;
static sk_query_t* bound_character_query = NULL;

void jolt_ecs_reset(void) {
	bound_world = NULL;
	bound_query = NULL;
	bound_character_query = NULL;
}

static const sk_entities_api_t* jolt_sync_ecs(void) {
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_sync_app(&context, &app_api);
	if (context == NULL || app_api == NULL) {
		return NULL;
	}
	return (const sk_entities_api_t*)app_api->get_api(context, SK_ENTITIES_API_TYPE_ID);
}

static void jolt_sync_fill_spec(sk_jolt_sync_spec_t* spec, sk_entity_t entity, const sk_rigid_body_config_t* cfg, const sk_transform_t* xform, const sk_rigid_body_state_t* state,
								const sk_box_collider_t* box, const sk_sphere_collider_t* sphere, const sk_capsule_collider_t* capsule) {
	spec->index = entity.index;
	spec->generation = entity.generation;
	spec->motion_type = (i32)cfg->motion_type;
	spec->mass = cfg->mass;
	spec->friction = cfg->friction;
	spec->restitution = cfg->restitution;
	spec->linear_damping = cfg->linear_damping;
	spec->angular_damping = cfg->angular_damping;
	spec->gravity_factor = cfg->gravity_factor;
	spec->object_layer = cfg->object_layer;
	spec->flags = cfg->flags;
	if (xform != NULL) {
		spec->pos_x = xform->position.x;
		spec->pos_y = xform->position.y;
		spec->pos_z = xform->position.z;
		spec->rot_x = xform->rotation.x;
		spec->rot_y = xform->rotation.y;
		spec->rot_z = xform->rotation.z;
		spec->rot_w = xform->rotation.w;
	} else {
		spec->pos_x = 0.0f;
		spec->pos_y = 0.0f;
		spec->pos_z = 0.0f;
		spec->rot_x = 0.0f;
		spec->rot_y = 0.0f;
		spec->rot_z = 0.0f;
		spec->rot_w = 1.0f;
	}
	if (state != NULL) {
		spec->lv_x = state->linear_velocity.x;
		spec->lv_y = state->linear_velocity.y;
		spec->lv_z = state->linear_velocity.z;
		spec->av_x = state->angular_velocity.x;
		spec->av_y = state->angular_velocity.y;
		spec->av_z = state->angular_velocity.z;
	} else {
		spec->lv_x = 0.0f;
		spec->lv_y = 0.0f;
		spec->lv_z = 0.0f;
		spec->av_x = 0.0f;
		spec->av_y = 0.0f;
		spec->av_z = 0.0f;
	}
	spec->shape_mask = 0u;
	spec->box_x = 0.0f;
	spec->box_y = 0.0f;
	spec->box_z = 0.0f;
	spec->sphere_r = 0.0f;
	spec->cap_hh = 0.0f;
	spec->cap_r = 0.0f;
	if (box != NULL && box->half_extent.x > 0.0f && box->half_extent.y > 0.0f && box->half_extent.z > 0.0f) {
		spec->shape_mask |= (u32)SK_JOLT_SYNC_SHAPE_BOX;
		spec->box_x = box->half_extent.x;
		spec->box_y = box->half_extent.y;
		spec->box_z = box->half_extent.z;
	}
	if (sphere != NULL && sphere->radius > 0.0f) {
		spec->shape_mask |= (u32)SK_JOLT_SYNC_SHAPE_SPHERE;
		spec->sphere_r = sphere->radius;
	}
	if (capsule != NULL && capsule->radius > 0.0f && capsule->half_height >= 0.0f) {
		spec->shape_mask |= (u32)SK_JOLT_SYNC_SHAPE_CAPSULE;
		spec->cap_hh = capsule->half_height;
		spec->cap_r = capsule->radius;
	}
}

void jolt_ecs_sync_world(sk_world_t* world) {
	const sk_entities_api_t* ecs = jolt_sync_ecs();
	sk_query_desc_t desc;
	sk_type_id_t required[1];

	if (world == NULL || ecs == NULL) {
		return;
	}
	if (bound_world != world) {
		jolt_internal_clear_bindings();
		bound_world = world;
		bound_query = NULL;
		bound_character_query = NULL;
	}
	if (bound_query == NULL) {
		required[0] = SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID;
		memset(&desc, 0, sizeof(desc));
		desc.required = required;
		desc.required_count = 1u;
		bound_query = ecs->world_query_create(world, &desc);
		if (bound_query == NULL) {
			return;
		}
	}

	jolt_internal_sync_begin();
	SK_ECS_QUERY_EACH(ecs, bound_query, it) {
		const sk_entity_t entity = SK_ECS_ITER_ENTITY_ROW(it);
		sk_rigid_body_config_t* cfg = (sk_rigid_body_config_t*)ecs->world_component(world, entity, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
		sk_jolt_sync_spec_t spec;
		sk_jolt_body_t* body = NULL;
		if (cfg == NULL) {
			continue;
		}
		memset(&spec, 0, sizeof(spec));
		jolt_sync_fill_spec(&spec, entity, cfg, (const sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID),
							(const sk_rigid_body_state_t*)ecs->world_component(world, entity, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID),
							(const sk_box_collider_t*)ecs->world_component(world, entity, SK_BOX_COLLIDER_COMPONENT_TYPE_ID),
							(const sk_sphere_collider_t*)ecs->world_component(world, entity, SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID),
							(const sk_capsule_collider_t*)ecs->world_component(world, entity, SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID));
		if (spec.shape_mask == 0u) {
			cfg->body = NULL;
			continue;
		}
		jolt_internal_bind(&spec, &body);
		cfg->body = body;
	}
	jolt_internal_sync_end();

	if (bound_character_query == NULL) {
		required[0] = SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID;
		memset(&desc, 0, sizeof(desc));
		desc.required = required;
		desc.required_count = 1u;
		bound_character_query = ecs->world_query_create(world, &desc);
		if (bound_character_query == NULL) {
			return;
		}
	}

	jolt_internal_character_sync_begin();
	SK_ECS_QUERY_EACH(ecs, bound_character_query, it) {
		const sk_entity_t entity = SK_ECS_ITER_ENTITY_ROW(it);
		sk_character_config_t* cfg = (sk_character_config_t*)ecs->world_component(world, entity, SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID);
		const sk_transform_t* xform = (const sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID);
		const sk_character_state_t* state = (const sk_character_state_t*)ecs->world_component(world, entity, SK_CHARACTER_STATE_COMPONENT_TYPE_ID);
		sk_jolt_character_sync_spec_t spec;
		sk_jolt_character_t* character = NULL;
		if (cfg == NULL) {
			continue;
		}
		memset(&spec, 0, sizeof(spec));
		spec.index = entity.index;
		spec.generation = entity.generation;
		spec.radius = cfg->radius;
		spec.height = cfg->height;
		spec.max_slope_angle = cfg->max_slope_angle;
		spec.step_height = cfg->step_height;
		spec.mass = cfg->mass;
		spec.object_layer = cfg->object_layer;
		if (xform != NULL) {
			spec.pos_x = xform->position.x;
			spec.pos_y = xform->position.y;
			spec.pos_z = xform->position.z;
		}
		if (state != NULL) {
			spec.vel_x = state->velocity.x;
			spec.vel_y = state->velocity.y;
			spec.vel_z = state->velocity.z;
		}
		jolt_internal_character_bind(&spec, &character);
		cfg->character = character;
	}
	jolt_internal_character_sync_end();
}

void jolt_ecs_write_back(sk_world_t* world) {
	const sk_entities_api_t* ecs = jolt_sync_ecs();
	u32 i;
	u32 count;
	if (world == NULL || ecs == NULL) {
		return;
	}
	count = jolt_internal_writeback_count();
	for (i = 0u; i < count; ++i) {
		sk_jolt_sync_pose_t pose;
		sk_entity_t entity;
		if (jolt_internal_writeback_at(i, &pose) != 0) {
			continue;
		}
		entity.index = pose.index;
		entity.generation = pose.generation;
		if (ecs->world_alive(world, entity) == 0) {
			continue;
		}
		{
			sk_transform_t* xform = (sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID);
			if (xform != NULL) {
				xform->position.x = pose.pos_x;
				xform->position.y = pose.pos_y;
				xform->position.z = pose.pos_z;
				xform->rotation.x = pose.rot_x;
				xform->rotation.y = pose.rot_y;
				xform->rotation.z = pose.rot_z;
				xform->rotation.w = pose.rot_w;
			}
		}
		{
			sk_rigid_body_state_t* state = (sk_rigid_body_state_t*)ecs->world_component(world, entity, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
			if (state != NULL) {
				state->linear_velocity.x = pose.lv_x;
				state->linear_velocity.y = pose.lv_y;
				state->linear_velocity.z = pose.lv_z;
				state->angular_velocity.x = pose.av_x;
				state->angular_velocity.y = pose.av_y;
				state->angular_velocity.z = pose.av_z;
			}
		}
	}

	count = jolt_internal_character_writeback_count();
	for (i = 0u; i < count; ++i) {
		sk_jolt_character_sync_pose_t pose;
		sk_entity_t entity;
		if (jolt_internal_character_writeback_at(i, &pose) != 0) {
			continue;
		}
		entity.index = pose.index;
		entity.generation = pose.generation;
		if (ecs->world_alive(world, entity) == 0) {
			continue;
		}
		{
			sk_transform_t* xform = (sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID);
			if (xform != NULL) {
				xform->position.x = pose.pos_x;
				xform->position.y = pose.pos_y;
				xform->position.z = pose.pos_z;
			}
		}
		{
			sk_character_state_t* state = (sk_character_state_t*)ecs->world_component(world, entity, SK_CHARACTER_STATE_COMPONENT_TYPE_ID);
			if (state != NULL) {
				state->velocity.x = pose.vel_x;
				state->velocity.y = pose.vel_y;
				state->velocity.z = pose.vel_z;
				state->ground_state = (sk_jolt_ground_state_t)pose.ground_state;
			}
		}
	}
}

/* APX-308: mark an entity's physics state dirty after gameplay mutated its
 * cold / authored component data. The entity must be alive (the caller passed
 * a valid handle); the C++ side cross-checks it against the entity map and
 * the sync consumes the dirty set at the end of the next walk (jolt.h
 * entity_require_update documents the exact contract). Implemented here in C
 * (after jolt_sync_ecs) so the Jolt C++ TU never has to include entities.h. */
void jolt_ecs_require_update(sk_world_t* world, sk_entity_t entity) {
	const sk_entities_api_t* ecs = jolt_sync_ecs();
	if (world == NULL || ecs == NULL) {
		return;
	}
	if (ecs->world_alive(world, entity) == 0) {
		return;
	}
	jolt_internal_mark_dirty(entity.index, entity.generation);
}
