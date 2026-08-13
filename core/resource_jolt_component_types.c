/**
 * @file resource_jolt_component_types.c
 * @brief Physics component payload type descriptors (APX-306).
 *
 * Repository payload blobs and manual field descriptors behind
 * resource_jolt_component_types.h. Defaults mirror the vendored Jolt
 * BodyCreationSettings values (friction 0.2, restitution 0.0, damping 0.05
 * each, gravity factor 1.0, sleeping allowed) so an un-authored config maps
 * 1:1 onto Jolt body creation; mass defaults to 1.0 kg, the motion type to
 * Dynamic (= 2, JPH::EMotionType) and the object layer to MOVING (= 1, see
 * jolt.h SK_JOLT_OBJECT_LAYER_MOVING) because the config describes a movable
 * rigid body — static geometry explicitly picks NON_MOVING.
 *
 * Kept free of app-only symbols (no filesystem / app API) and of plugin
 * headers so the sk-jolt plugin (statically linked sk-core only) and host
 * tests can register these types into a repository without pulling in
 * sk-app symbols.
 */

#include "resource_jolt_component_types.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Repository payload blobs (private; storage mirrors field types)   */
/* ------------------------------------------------------------------ */

typedef struct sk_rigid_body_config_resource_t {
	u64 motion_type;	 /* ENUM (jolt.h sk_jolt_motion_type_t) */
	f64 mass;			 /* FLOAT */
	f64 friction;		 /* FLOAT */
	f64 restitution;	 /* FLOAT */
	f64 linear_damping;	 /* FLOAT */
	f64 angular_damping; /* FLOAT */
	f64 gravity_factor;	 /* FLOAT */
	u64 object_layer;	 /* UINT (jolt.h SK_JOLT_OBJECT_LAYER_*) */
	u64 flags;			 /* UINT (jolt_components.h sk_rigid_body_flag_t) */
} sk_rigid_body_config_resource_t;

typedef struct sk_rigid_body_state_resource_t {
	sk_vec3_t linear_velocity;	/* VEC3 */
	sk_vec3_t angular_velocity; /* VEC3 */
} sk_rigid_body_state_resource_t;

typedef struct sk_box_collider_resource_t {
	sk_vec3_t half_extent; /* VEC3 */
} sk_box_collider_resource_t;

typedef struct sk_sphere_collider_resource_t {
	f64 radius; /* FLOAT */
} sk_sphere_collider_resource_t;

typedef struct sk_capsule_collider_resource_t {
	f64 half_height; /* FLOAT */
	f64 radius;		 /* FLOAT */
} sk_capsule_collider_resource_t;

typedef struct sk_character_config_resource_t {
	f64 radius;			 /* FLOAT */
	f64 height;			 /* FLOAT */
	f64 max_slope_angle; /* FLOAT (radians) */
	f64 step_height;	 /* FLOAT */
	f64 mass;			 /* FLOAT */
	u64 object_layer;	 /* UINT */
} sk_character_config_resource_t;

typedef struct sk_character_state_resource_t {
	sk_vec3_t velocity; /* VEC3 */
	u64 ground_state;	/* ENUM (jolt.h sk_jolt_ground_state_t) */
} sk_character_state_resource_t;

/* ------------------------------------------------------------------ */
/*  Field descriptors + defaults                                      */
/* ------------------------------------------------------------------ */

static const sk_resource_field_t rigid_body_config_fields[] = {
	{"MotionType",
	 SK_RIGID_BODY_CONFIG_FIELD_MOTION_TYPE,
	 SK_RESOURCE_FIELD_TYPE_ENUM,
	 (u32)offsetof(sk_rigid_body_config_resource_t, motion_type),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
	{"Mass", SK_RIGID_BODY_CONFIG_FIELD_MASS, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_rigid_body_config_resource_t, mass), (u32)sizeof(f64), {0ull, 0ull}},
	{"Friction", SK_RIGID_BODY_CONFIG_FIELD_FRICTION, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_rigid_body_config_resource_t, friction), (u32)sizeof(f64), {0ull, 0ull}},
	{"Restitution",
	 SK_RIGID_BODY_CONFIG_FIELD_RESTITUTION,
	 SK_RESOURCE_FIELD_TYPE_FLOAT,
	 (u32)offsetof(sk_rigid_body_config_resource_t, restitution),
	 (u32)sizeof(f64),
	 {0ull, 0ull}},
	{"LinearDamping",
	 SK_RIGID_BODY_CONFIG_FIELD_LINEAR_DAMPING,
	 SK_RESOURCE_FIELD_TYPE_FLOAT,
	 (u32)offsetof(sk_rigid_body_config_resource_t, linear_damping),
	 (u32)sizeof(f64),
	 {0ull, 0ull}},
	{"AngularDamping",
	 SK_RIGID_BODY_CONFIG_FIELD_ANGULAR_DAMPING,
	 SK_RESOURCE_FIELD_TYPE_FLOAT,
	 (u32)offsetof(sk_rigid_body_config_resource_t, angular_damping),
	 (u32)sizeof(f64),
	 {0ull, 0ull}},
	{"GravityFactor",
	 SK_RIGID_BODY_CONFIG_FIELD_GRAVITY_FACTOR,
	 SK_RESOURCE_FIELD_TYPE_FLOAT,
	 (u32)offsetof(sk_rigid_body_config_resource_t, gravity_factor),
	 (u32)sizeof(f64),
	 {0ull, 0ull}},
	{"ObjectLayer",
	 SK_RIGID_BODY_CONFIG_FIELD_OBJECT_LAYER,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_rigid_body_config_resource_t, object_layer),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
	{"Flags", SK_RIGID_BODY_CONFIG_FIELD_FLAGS, SK_RESOURCE_FIELD_TYPE_UINT, (u32)offsetof(sk_rigid_body_config_resource_t, flags), (u32)sizeof(u64), {0ull, 0ull}},
};

/* Values mirror Jolt BodyCreationSettings (see file comment): Dynamic motion
 * type (2), moving object layer (1), and the allow-sleeping flag (1 << 1). */
static const sk_rigid_body_config_resource_t rigid_body_config_defaults = {
	2u,	  /* motion_type: SK_JOLT_MOTION_TYPE_DYNAMIC */
	1.0,  /* mass */
	0.2,  /* friction */
	0.0,  /* restitution */
	0.05, /* linear_damping */
	0.05, /* angular_damping */
	1.0,  /* gravity_factor */
	1u,	  /* object_layer: SK_JOLT_OBJECT_LAYER_MOVING */
	2u,	  /* flags: SK_RIGID_BODY_FLAG_ALLOW_SLEEPING */
};

static const sk_resource_type_desc_t rigid_body_config_type_desc = {
	{SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_LO, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_HI},
	"RigidBodyConfigResource",
	(u32)sizeof(sk_rigid_body_config_resource_t),
	rigid_body_config_fields,
	(u32)(sizeof(rigid_body_config_fields) / sizeof(rigid_body_config_fields[0])),
	&rigid_body_config_defaults,
};

static const sk_resource_field_t rigid_body_state_fields[] = {
	{"LinearVelocity",
	 SK_RIGID_BODY_STATE_FIELD_LINEAR_VELOCITY,
	 SK_RESOURCE_FIELD_TYPE_VEC3,
	 (u32)offsetof(sk_rigid_body_state_resource_t, linear_velocity),
	 (u32)sizeof(sk_vec3_t),
	 {0ull, 0ull}},
	{"AngularVelocity",
	 SK_RIGID_BODY_STATE_FIELD_ANGULAR_VELOCITY,
	 SK_RESOURCE_FIELD_TYPE_VEC3,
	 (u32)offsetof(sk_rigid_body_state_resource_t, angular_velocity),
	 (u32)sizeof(sk_vec3_t),
	 {0ull, 0ull}},
};

static const sk_resource_type_desc_t rigid_body_state_type_desc = {
	{SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_LO, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_HI},
	"RigidBodyStateResource",
	(u32)sizeof(sk_rigid_body_state_resource_t),
	rigid_body_state_fields,
	(u32)(sizeof(rigid_body_state_fields) / sizeof(rigid_body_state_fields[0])),
	NULL,
};

static const sk_resource_field_t box_collider_fields[] = {
	{"HalfExtent", SK_BOX_COLLIDER_FIELD_HALF_EXTENT, SK_RESOURCE_FIELD_TYPE_VEC3, (u32)offsetof(sk_box_collider_resource_t, half_extent), (u32)sizeof(sk_vec3_t), {0ull, 0ull}},
};

/* Default half extents {0.5, 0.5, 0.5} = a 1x1x1 box (matches the reference
 * engine's default collider size). */
static const sk_box_collider_resource_t box_collider_defaults = {
	{0.5f, 0.5f, 0.5f},
};

static const sk_resource_type_desc_t box_collider_type_desc = {
	{SK_BOX_COLLIDER_COMPONENT_TYPE_ID_LO, SK_BOX_COLLIDER_COMPONENT_TYPE_ID_HI}, "BoxColliderResource",  (u32)sizeof(sk_box_collider_resource_t), box_collider_fields,
	(u32)(sizeof(box_collider_fields) / sizeof(box_collider_fields[0])),		  &box_collider_defaults,
};

static const sk_resource_field_t sphere_collider_fields[] = {
	{"Radius", SK_SPHERE_COLLIDER_FIELD_RADIUS, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_sphere_collider_resource_t, radius), (u32)sizeof(f64), {0ull, 0ull}},
};

static const sk_sphere_collider_resource_t sphere_collider_defaults = {
	0.5, /* radius */
};

static const sk_resource_type_desc_t sphere_collider_type_desc = {
	{SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_LO, SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_HI},
	"SphereColliderResource",
	(u32)sizeof(sk_sphere_collider_resource_t),
	sphere_collider_fields,
	(u32)(sizeof(sphere_collider_fields) / sizeof(sphere_collider_fields[0])),
	&sphere_collider_defaults,
};

static const sk_resource_field_t capsule_collider_fields[] = {
	{"HalfHeight", SK_CAPSULE_COLLIDER_FIELD_HALF_HEIGHT, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_capsule_collider_resource_t, half_height), (u32)sizeof(f64), {0ull, 0ull}},
	{"Radius", SK_CAPSULE_COLLIDER_FIELD_RADIUS, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_capsule_collider_resource_t, radius), (u32)sizeof(f64), {0ull, 0ull}},
};

/* Default capsule: radius 0.5, half height 0.5 (1.0 total height incl. caps;
 * matches the reference engine's default capsule). */
static const sk_capsule_collider_resource_t capsule_collider_defaults = {
	0.5, /* half_height */
	0.5, /* radius */
};

static const sk_resource_type_desc_t capsule_collider_type_desc = {
	{SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_LO, SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_HI},
	"CapsuleColliderResource",
	(u32)sizeof(sk_capsule_collider_resource_t),
	capsule_collider_fields,
	(u32)(sizeof(capsule_collider_fields) / sizeof(capsule_collider_fields[0])),
	&capsule_collider_defaults,
};

static const sk_resource_field_t character_config_fields[] = {
	{"Radius", SK_CHARACTER_CONFIG_FIELD_RADIUS, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_character_config_resource_t, radius), (u32)sizeof(f64), {0ull, 0ull}},
	{"Height", SK_CHARACTER_CONFIG_FIELD_HEIGHT, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_character_config_resource_t, height), (u32)sizeof(f64), {0ull, 0ull}},
	{"MaxSlopeAngle",
	 SK_CHARACTER_CONFIG_FIELD_MAX_SLOPE_ANGLE,
	 SK_RESOURCE_FIELD_TYPE_FLOAT,
	 (u32)offsetof(sk_character_config_resource_t, max_slope_angle),
	 (u32)sizeof(f64),
	 {0ull, 0ull}},
	{"StepHeight", SK_CHARACTER_CONFIG_FIELD_STEP_HEIGHT, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_character_config_resource_t, step_height), (u32)sizeof(f64), {0ull, 0ull}},
	{"Mass", SK_CHARACTER_CONFIG_FIELD_MASS, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_character_config_resource_t, mass), (u32)sizeof(f64), {0ull, 0ull}},
	{"ObjectLayer",
	 SK_CHARACTER_CONFIG_FIELD_OBJECT_LAYER,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_character_config_resource_t, object_layer),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
};

/* Defaults match character_settings_defaults() in the jolt plugin (radius
 * 0.3, height 1.8, 50-degree max slope, 0.4 m step, 70 kg, MOVING layer). */
static const sk_character_config_resource_t character_config_defaults = {
	0.3,				/* radius */
	1.8,				/* height */
	0.8726646259971648, /* max_slope_angle: 50 degrees in radians */
	0.4,				/* step_height */
	70.0,				/* mass */
	1u,					/* object_layer: SK_JOLT_OBJECT_LAYER_MOVING */
};

static const sk_resource_type_desc_t character_config_type_desc = {
	{SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID_LO, SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID_HI},
	"CharacterConfigResource",
	(u32)sizeof(sk_character_config_resource_t),
	character_config_fields,
	(u32)(sizeof(character_config_fields) / sizeof(character_config_fields[0])),
	&character_config_defaults,
};

static const sk_resource_field_t character_state_fields[] = {
	{"Velocity", SK_CHARACTER_STATE_FIELD_VELOCITY, SK_RESOURCE_FIELD_TYPE_VEC3, (u32)offsetof(sk_character_state_resource_t, velocity), (u32)sizeof(sk_vec3_t), {0ull, 0ull}},
	{"GroundState", SK_CHARACTER_STATE_FIELD_GROUND_STATE, SK_RESOURCE_FIELD_TYPE_ENUM, (u32)offsetof(sk_character_state_resource_t, ground_state), (u32)sizeof(u64), {0ull, 0ull}},
};

static const sk_resource_type_desc_t character_state_type_desc = {
	{SK_CHARACTER_STATE_COMPONENT_TYPE_ID_LO, SK_CHARACTER_STATE_COMPONENT_TYPE_ID_HI},
	"CharacterStateResource",
	(u32)sizeof(sk_character_state_resource_t),
	character_state_fields,
	(u32)(sizeof(character_state_fields) / sizeof(character_state_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t* const jolt_component_type_descs[] = {
	&rigid_body_config_type_desc, &rigid_body_state_type_desc, &box_collider_type_desc,	   &sphere_collider_type_desc,
	&capsule_collider_type_desc,  &character_config_type_desc, &character_state_type_desc,
};

static const u32 jolt_component_type_desc_count = (u32)(sizeof(jolt_component_type_descs) / sizeof(jolt_component_type_descs[0]));

i32 sk_jolt_component_types_register(sk_repository_t* repository) {
	const sk_repository_api_t* api = sk_repository_api();
	for (u32 i = 0u; i < jolt_component_type_desc_count; ++i) {
		i32 rc = api->register_type(repository, jolt_component_type_descs[i]);
		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

#ifdef SK_TESTS
#include "test.h"

SK_TEST(jolt_component_types_register_and_list) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_jolt_component_types_register(repo));

	static const struct {
		const_chr_t name;
		u64 id_lo;
		u64 id_hi;
		u32 fields;
	} expected[] = {
		{"RigidBodyConfigResource", SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_LO, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_HI, 9u},
		{"RigidBodyStateResource", SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_LO, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_HI, 2u},
		{"BoxColliderResource", SK_BOX_COLLIDER_COMPONENT_TYPE_ID_LO, SK_BOX_COLLIDER_COMPONENT_TYPE_ID_HI, 1u},
		{"SphereColliderResource", SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_LO, SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_HI, 1u},
		{"CapsuleColliderResource", SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_LO, SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_HI, 2u},
		{"CharacterConfigResource", SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID_LO, SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID_HI, 6u},
		{"CharacterStateResource", SK_CHARACTER_STATE_COMPONENT_TYPE_ID_LO, SK_CHARACTER_STATE_COMPONENT_TYPE_ID_HI, 2u},
	};

	for (u32 i = 0u; i < (u32)(sizeof(expected) / sizeof(expected[0])); ++i) {
		sk_type_id_t id;
		id.lo = expected[i].id_lo;
		id.hi = expected[i].id_hi;
		const sk_resource_type_t* by_name = api->find_type_by_name(repo, expected[i].name);
		const sk_resource_type_t* by_id = api->find_type(repo, id);
		TEST_ASSERT_NOT_NULL(by_name);
		TEST_ASSERT_EQUAL_PTR(by_name, by_id);
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, api->type_id(by_name)));
		TEST_ASSERT_EQUAL_UINT32(expected[i].fields, api->type_field_count(by_name));
	}

	/* Un-authored config maps onto Jolt BodyCreationSettings defaults. */
	const sk_resource_type_t* cfg_type = api->find_type_by_name(repo, "RigidBodyConfigResource");
	sk_rid_t rid = api->create_resource(repo, cfg_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	sk_resource_object_t r = api->read(repo, rid);
	TEST_ASSERT_EQUAL_UINT64(2u, api->get_enum(r, SK_RIGID_BODY_CONFIG_FIELD_MOTION_TYPE));
	TEST_ASSERT_EQUAL_DOUBLE(1.0, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_MASS));
	TEST_ASSERT_EQUAL_DOUBLE(0.2, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_FRICTION));
	TEST_ASSERT_EQUAL_DOUBLE(0.0, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_RESTITUTION));
	TEST_ASSERT_EQUAL_DOUBLE(0.05, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_LINEAR_DAMPING));
	TEST_ASSERT_EQUAL_DOUBLE(0.05, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_ANGULAR_DAMPING));
	TEST_ASSERT_EQUAL_DOUBLE(1.0, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_GRAVITY_FACTOR));
	TEST_ASSERT_EQUAL_UINT64(1u, api->get_uint(r, SK_RIGID_BODY_CONFIG_FIELD_OBJECT_LAYER));
	TEST_ASSERT_EQUAL_UINT64(2u, api->get_uint(r, SK_RIGID_BODY_CONFIG_FIELD_FLAGS));

	api->destroy(repo);
}

#endif /* SK_TESTS */
