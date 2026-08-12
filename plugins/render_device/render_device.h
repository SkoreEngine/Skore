#pragma once

#include <stdbool.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_render_device_api_t in the app registry. */
#define SK_RENDER_DEVICE_API_TYPE_ID SK_TYPE_ID("sk.render_device_api", 0x4a1e8c3f92d6b501ULL, 0x8f3d72ae51c0b9e4ULL)

/** Device creation result code. */
typedef enum sk_device_result_t {
	SK_DEVICE_RESULT_SUCCESS = 0,
	SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE,
	SK_DEVICE_RESULT_ERROR,
} sk_device_result_t;

/** Underlying graphics API the render device was created for. */
typedef enum sk_graphics_api_t {
	SK_GRAPHICS_API_NONE = 0,
	SK_GRAPHICS_API_VULKAN,
	SK_GRAPHICS_API_D3D12,
	SK_GRAPHICS_API_METAL,
} sk_graphics_api_t;

/** Physical device class reported by the adapter. */
typedef enum sk_device_type_t {
	SK_DEVICE_TYPE_OTHER = 0,
	SK_DEVICE_TYPE_DISCRETE,
	SK_DEVICE_TYPE_INTEGRATED,
	SK_DEVICE_TYPE_VIRTUAL,
	SK_DEVICE_TYPE_CPU,
} sk_device_type_t;

/* ------------------------------------------------------------------ */
/* Resource handles (opaque IDs bound to the device)                   */
/* ------------------------------------------------------------------ */

SK_HANDLER(sk_render_device_t)
SK_HANDLER(sk_adapter_t)
SK_HANDLER(sk_texture_t)
SK_HANDLER(sk_texture_view_t)
SK_HANDLER(sk_buffer_t)
SK_HANDLER(sk_shader_t)
SK_HANDLER(sk_render_pass_t)
SK_HANDLER(sk_framebuffer_t)
SK_HANDLER(sk_swapchain_t)
SK_HANDLER(sk_command_buffer_t)
SK_HANDLER(sk_sampler_t)
SK_HANDLER(sk_pipeline_t)
SK_HANDLER(sk_descriptor_set_t)
SK_HANDLER(sk_query_pool_t)
SK_HANDLER(sk_blas_t)
SK_HANDLER(sk_tlas_t)
SK_HANDLER(sk_memory_t)
SK_HANDLER(sk_queue_t)
SK_HANDLER(sk_fence_t)
SK_HANDLER(sk_semaphore_t)

/* ------------------------------------------------------------------ */
/* Public POD types                                                    */
/* ------------------------------------------------------------------ */

/** Full texture / surface pixel format. */
typedef enum sk_pixel_format_t {
	SK_PIXEL_FORMAT_UNKNOWN = 0,

	/* 8-bit formats */
	SK_PIXEL_FORMAT_R8_UNORM,
	SK_PIXEL_FORMAT_R8_SNORM,
	SK_PIXEL_FORMAT_R8_UINT,
	SK_PIXEL_FORMAT_R8_SINT,
	SK_PIXEL_FORMAT_R8_SRGB,

	/* 16-bit formats */
	SK_PIXEL_FORMAT_R16_UNORM,
	SK_PIXEL_FORMAT_R16_SNORM,
	SK_PIXEL_FORMAT_R16_UINT,
	SK_PIXEL_FORMAT_R16_SINT,
	SK_PIXEL_FORMAT_R16_FLOAT,
	SK_PIXEL_FORMAT_RG8_UNORM,
	SK_PIXEL_FORMAT_RG8_SNORM,
	SK_PIXEL_FORMAT_RG8_UINT,
	SK_PIXEL_FORMAT_RG8_SINT,
	SK_PIXEL_FORMAT_RG8_SRGB,

	SK_PIXEL_FORMAT_RGB16_UNORM,
	SK_PIXEL_FORMAT_RGB16_SNORM,
	SK_PIXEL_FORMAT_RGB16_UINT,
	SK_PIXEL_FORMAT_RGB16_SINT,
	SK_PIXEL_FORMAT_RGB16_FLOAT,

	/* 32-bit formats */
	SK_PIXEL_FORMAT_R32_UINT,
	SK_PIXEL_FORMAT_R32_SINT,
	SK_PIXEL_FORMAT_R32_FLOAT,
	SK_PIXEL_FORMAT_RG16_UNORM,
	SK_PIXEL_FORMAT_RG16_SNORM,
	SK_PIXEL_FORMAT_RG16_UINT,
	SK_PIXEL_FORMAT_RG16_SINT,
	SK_PIXEL_FORMAT_RG16_FLOAT,
	SK_PIXEL_FORMAT_RGBA8_UNORM,
	SK_PIXEL_FORMAT_RGBA8_SNORM,
	SK_PIXEL_FORMAT_RGBA8_UINT,
	SK_PIXEL_FORMAT_RGBA8_SINT,
	SK_PIXEL_FORMAT_RGBA8_SRGB,
	SK_PIXEL_FORMAT_BGRA8_UNORM,
	SK_PIXEL_FORMAT_BGRA8_SNORM,
	SK_PIXEL_FORMAT_BGRA8_UINT,
	SK_PIXEL_FORMAT_BGRA8_SINT,
	SK_PIXEL_FORMAT_BGRA8_SRGB,
	SK_PIXEL_FORMAT_RGB10A2_UNORM,
	SK_PIXEL_FORMAT_RGB10A2_UINT,
	SK_PIXEL_FORMAT_RG11B10_FLOAT,
	SK_PIXEL_FORMAT_RGB9E5_FLOAT,

	/* 64-bit formats */
	SK_PIXEL_FORMAT_RG32_UINT,
	SK_PIXEL_FORMAT_RG32_SINT,
	SK_PIXEL_FORMAT_RG32_FLOAT,
	SK_PIXEL_FORMAT_RGBA16_UNORM,
	SK_PIXEL_FORMAT_RGBA16_SNORM,
	SK_PIXEL_FORMAT_RGBA16_UINT,
	SK_PIXEL_FORMAT_RGBA16_SINT,
	SK_PIXEL_FORMAT_RGBA16_FLOAT,

	/* 96-bit formats */
	SK_PIXEL_FORMAT_RGB32_UINT,
	SK_PIXEL_FORMAT_RGB32_SINT,
	SK_PIXEL_FORMAT_RGB32_FLOAT,

	/* 128-bit formats */
	SK_PIXEL_FORMAT_RGBA32_UINT,
	SK_PIXEL_FORMAT_RGBA32_SINT,
	SK_PIXEL_FORMAT_RGBA32_FLOAT,

	/* Depth/stencil formats */
	SK_PIXEL_FORMAT_D16_UNORM,
	SK_PIXEL_FORMAT_D24_UNORM_S8_UINT,
	SK_PIXEL_FORMAT_D32_FLOAT,
	SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT,

	/* BC compressed (4x4 block) */
	SK_PIXEL_FORMAT_BC1_UNORM,
	SK_PIXEL_FORMAT_BC1_SRGB,
	SK_PIXEL_FORMAT_BC2_UNORM,
	SK_PIXEL_FORMAT_BC2_SRGB,
	SK_PIXEL_FORMAT_BC3_UNORM,
	SK_PIXEL_FORMAT_BC3_SRGB,
	SK_PIXEL_FORMAT_BC4_UNORM,
	SK_PIXEL_FORMAT_BC4_SNORM,
	SK_PIXEL_FORMAT_BC5_UNORM,
	SK_PIXEL_FORMAT_BC5_SNORM,
	SK_PIXEL_FORMAT_BC6H_UF16,
	SK_PIXEL_FORMAT_BC6H_SF16,
	SK_PIXEL_FORMAT_BC7_UNORM,
	SK_PIXEL_FORMAT_BC7_SRGB,

	/* ETC compressed (4x4 block) */
	SK_PIXEL_FORMAT_ETC1_UNORM,
	SK_PIXEL_FORMAT_ETC2_UNORM,
	SK_PIXEL_FORMAT_ETC2_SRGB,
	SK_PIXEL_FORMAT_ETC2A_UNORM,
	SK_PIXEL_FORMAT_ETC2A_SRGB,

	/* ASTC compressed (block size varies) */
	SK_PIXEL_FORMAT_ASTC_4x4_UNORM,
	SK_PIXEL_FORMAT_ASTC_4x4_SRGB,
	SK_PIXEL_FORMAT_ASTC_5x4_UNORM,
	SK_PIXEL_FORMAT_ASTC_5x4_SRGB,
	SK_PIXEL_FORMAT_ASTC_5x5_UNORM,
	SK_PIXEL_FORMAT_ASTC_5x5_SRGB,
	SK_PIXEL_FORMAT_ASTC_6x5_UNORM,
	SK_PIXEL_FORMAT_ASTC_6x5_SRGB,
	SK_PIXEL_FORMAT_ASTC_6x6_UNORM,
	SK_PIXEL_FORMAT_ASTC_6x6_SRGB,
	SK_PIXEL_FORMAT_ASTC_8x5_UNORM,
	SK_PIXEL_FORMAT_ASTC_8x5_SRGB,
	SK_PIXEL_FORMAT_ASTC_8x6_UNORM,
	SK_PIXEL_FORMAT_ASTC_8x6_SRGB,
	SK_PIXEL_FORMAT_ASTC_8x8_UNORM,
	SK_PIXEL_FORMAT_ASTC_8x8_SRGB,
	SK_PIXEL_FORMAT_ASTC_10x5_UNORM,
	SK_PIXEL_FORMAT_ASTC_10x5_SRGB,
	SK_PIXEL_FORMAT_ASTC_10x6_UNORM,
	SK_PIXEL_FORMAT_ASTC_10x6_SRGB,
	SK_PIXEL_FORMAT_ASTC_10x8_UNORM,
	SK_PIXEL_FORMAT_ASTC_10x8_SRGB,
	SK_PIXEL_FORMAT_ASTC_10x10_UNORM,
	SK_PIXEL_FORMAT_ASTC_10x10_SRGB,
	SK_PIXEL_FORMAT_ASTC_12x10_UNORM,
	SK_PIXEL_FORMAT_ASTC_12x10_SRGB,
	SK_PIXEL_FORMAT_ASTC_12x12_UNORM,
	SK_PIXEL_FORMAT_ASTC_12x12_SRGB,
} sk_pixel_format_t;

/** Shader compilation stage bitmask (u32 bits). */
typedef enum sk_shader_stage_bit_t {
	SK_SHADER_STAGE_NONE = 0,
	SK_SHADER_STAGE_VERTEX = 1u << 0,
	SK_SHADER_STAGE_HULL = 1u << 1,
	SK_SHADER_STAGE_DOMAIN = 1u << 2,
	SK_SHADER_STAGE_GEOMETRY = 1u << 3,
	SK_SHADER_STAGE_PIXEL = 1u << 4,
	SK_SHADER_STAGE_COMPUTE = 1u << 5,
	SK_SHADER_STAGE_AMPLIFICATION = 1u << 6,
	SK_SHADER_STAGE_MESH = 1u << 7,
	SK_SHADER_STAGE_RAYGEN = 1u << 8,
	SK_SHADER_STAGE_ANY_HIT = 1u << 9,
	SK_SHADER_STAGE_CLOSEST_HIT = 1u << 10,
	SK_SHADER_STAGE_MISS = 1u << 11,
	SK_SHADER_STAGE_INTERSECTION = 1u << 12,
	SK_SHADER_STAGE_CALLABLE = 1u << 13,
	SK_SHADER_STAGE_ALL = 1u << 14,
} sk_shader_stage_bit_t;

/** Resource usage bitmask (u32 bits). */
typedef enum sk_resource_usage_bit_t {
	SK_RESOURCE_USAGE_NONE = 0,
	SK_RESOURCE_USAGE_SHADER_RESOURCE = 1u << 0,
	SK_RESOURCE_USAGE_RENDER_TARGET = 1u << 1,
	SK_RESOURCE_USAGE_DEPTH_STENCIL = 1u << 2,
	SK_RESOURCE_USAGE_UNORDERED_ACCESS = 1u << 3,
	SK_RESOURCE_USAGE_VERTEX_BUFFER = 1u << 4,
	SK_RESOURCE_USAGE_INDEX_BUFFER = 1u << 5,
	SK_RESOURCE_USAGE_CONSTANT_BUFFER = 1u << 6,
	SK_RESOURCE_USAGE_INDIRECT_BUFFER = 1u << 7,
	SK_RESOURCE_USAGE_COPY_DEST = 1u << 8,
	SK_RESOURCE_USAGE_COPY_SOURCE = 1u << 9,
	SK_RESOURCE_USAGE_ACCELERATION_STRUCTURE = 1u << 10,
	SK_RESOURCE_USAGE_RAY_TRACING = 1u << 11,
} sk_resource_usage_bit_t;

/** Resource state for barriers. */
typedef enum sk_resource_state_t {
	SK_RESOURCE_STATE_UNDEFINED = 0,
	SK_RESOURCE_STATE_GENERAL = 1,
	SK_RESOURCE_STATE_COLOR_ATTACHMENT = 2,
	SK_RESOURCE_STATE_DEPTH_STENCIL_ATTACH = 3,
	SK_RESOURCE_STATE_DEPTH_STENCIL_READ = 4,
	SK_RESOURCE_STATE_SHADER_READ = 5,
	SK_RESOURCE_STATE_COPY_DEST = 6,
	SK_RESOURCE_STATE_COPY_SOURCE = 7,
	SK_RESOURCE_STATE_PRESENT = 8,
	SK_RESOURCE_STATE_INDIRECT_ARGUMENT = 9,
	SK_RESOURCE_STATE_UNORDERED_ACCESS = 10,
	SK_RESOURCE_STATE_ACCELERATION_STRUCTURE = 11,
} sk_resource_state_t;

/** Command buffer primary vs secondary (secondary is executed by primary). */
typedef enum sk_command_buffer_level_t {
	SK_COMMAND_BUFFER_LEVEL_PRIMARY = 0,
	SK_COMMAND_BUFFER_LEVEL_SECONDARY,
} sk_command_buffer_level_t;

/** Command buffer begin usage flags (u32 bits). */
typedef enum sk_command_buffer_usage_bit_t {
	SK_COMMAND_BUFFER_USAGE_NONE = 0,
	SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT = 1u << 0,
	SK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE = 1u << 1,
	SK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE = 1u << 2,
} sk_command_buffer_usage_bit_t;

/** Barrier sync scope for resource transitions. */
typedef enum sk_barrier_sync_scope_t {
	SK_BARRIER_SYNC_AUTOMATIC = 0,
	SK_BARRIER_SYNC_GRAPHICS,
	SK_BARRIER_SYNC_COMPUTE,
	SK_BARRIER_SYNC_RAYTRACE,
	SK_BARRIER_SYNC_TRANSFER,
} sk_barrier_sync_scope_t;

/** Index type for indexed draw calls. */
typedef enum sk_index_type_t {
	SK_INDEX_TYPE_UINT16 = 0,
	SK_INDEX_TYPE_UINT32,
} sk_index_type_t;

/** Primitive topology for draw calls. */
typedef enum sk_primitive_topology_t {
	SK_PRIMITIVE_TOPOLOGY_POINT_LIST = 0,
	SK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	SK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
	SK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	SK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
} sk_primitive_topology_t;

/** Texture filtering mode. */
typedef enum sk_filter_mode_t {
	SK_FILTER_MODE_NEAREST = 0,
	SK_FILTER_MODE_LINEAR,
} sk_filter_mode_t;

/** Texture addressing (wrap) mode. */
typedef enum sk_texture_address_mode_t {
	SK_TEXTURE_ADDRESS_REPEAT = 0,
	SK_TEXTURE_ADDRESS_MIRRORED_REPEAT,
	SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE,
	SK_TEXTURE_ADDRESS_CLAMP_TO_BORDER,
	SK_TEXTURE_ADDRESS_MIRROR_CLAMP_TO_EDGE,
} sk_texture_address_mode_t;

/** Texture comparison operation. */
typedef enum sk_compare_op_t {
	SK_COMPARE_OP_NEVER = 0,
	SK_COMPARE_OP_LESS,
	SK_COMPARE_OP_EQUAL,
	SK_COMPARE_OP_LESS_EQUAL,
	SK_COMPARE_OP_GREATER,
	SK_COMPARE_OP_NOT_EQUAL,
	SK_COMPARE_OP_GREATER_EQUAL,
	SK_COMPARE_OP_ALWAYS,
} sk_compare_op_t;

/** Sampler border color. */
typedef enum sk_border_color_t {
	SK_BORDER_COLOR_TRANSPARENT_BLACK = 0,
	SK_BORDER_COLOR_OPAQUE_BLACK,
	SK_BORDER_COLOR_OPAQUE_WHITE,
} sk_border_color_t;

/** Blend factor. */
typedef enum sk_blend_factor_t {
	SK_BLEND_FACTOR_ZERO = 0,
	SK_BLEND_FACTOR_ONE,
	SK_BLEND_FACTOR_SRC_COLOR,
	SK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	SK_BLEND_FACTOR_DST_COLOR,
	SK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	SK_BLEND_FACTOR_SRC_ALPHA,
	SK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	SK_BLEND_FACTOR_DST_ALPHA,
	SK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	SK_BLEND_FACTOR_CONSTANT_COLOR,
	SK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
	SK_BLEND_FACTOR_CONSTANT_ALPHA,
	SK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	SK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
} sk_blend_factor_t;

/** Blend operation. */
typedef enum sk_blend_op_t {
	SK_BLEND_OP_ADD = 0,
	SK_BLEND_OP_SUBTRACT,
	SK_BLEND_OP_REVERSE_SUBTRACT,
	SK_BLEND_OP_MIN,
	SK_BLEND_OP_MAX,
} sk_blend_op_t;

/** Color mask component flags. */
typedef enum sk_color_mask_component_t {
	SK_COLOR_MASK_COMPONENT_RED = 1u << 0,
	SK_COLOR_MASK_COMPONENT_GREEN = 1u << 1,
	SK_COLOR_MASK_COMPONENT_BLUE = 1u << 2,
	SK_COLOR_MASK_COMPONENT_ALPHA = 1u << 3,
	SK_COLOR_MASK_COMPONENT_ALL = SK_COLOR_MASK_COMPONENT_RED | SK_COLOR_MASK_COMPONENT_GREEN | SK_COLOR_MASK_COMPONENT_BLUE | SK_COLOR_MASK_COMPONENT_ALPHA,
} sk_color_mask_component_t;

/** Cull mode for rasterization. */
typedef enum sk_cull_mode_t {
	SK_CULL_MODE_NONE = 0,
	SK_CULL_MODE_FRONT,
	SK_CULL_MODE_BACK,
} sk_cull_mode_t;

/** Winding order for front face detection. */
typedef enum sk_front_face_t {
	SK_FRONT_FACE_CLOCKWISE = 0,
	SK_FRONT_FACE_COUNTER_CLOCKWISE,
} sk_front_face_t;

/** Stencil operation. */
typedef enum sk_stencil_op_t {
	SK_STENCIL_OP_KEEP = 0,
	SK_STENCIL_OP_ZERO,
	SK_STENCIL_OP_REPLACE,
	SK_STENCIL_OP_INCREMENT_CLAMP,
	SK_STENCIL_OP_DECREMENT_CLAMP,
	SK_STENCIL_OP_INVERT,
	SK_STENCIL_OP_INCREMENT_WRAP,
	SK_STENCIL_OP_DECREMENT_WRAP,
} sk_stencil_op_t;

/** Polygon rasterization mode. */
typedef enum sk_polygon_mode_t {
	SK_POLYGON_MODE_FILL = 0,
	SK_POLYGON_MODE_LINE,
	SK_POLYGON_MODE_POINT,
} sk_polygon_mode_t;

/** Conservative rasterization mode (extension gated on the adapter). */
typedef enum sk_conservative_rasterization_mode_t {
	SK_CONSERVATIVE_RASTERIZATION_DISABLED = 0,
	SK_CONSERVATIVE_RASTERIZATION_OVERESTIMATE,
	SK_CONSERVATIVE_RASTERIZATION_UNDERESTIMATE,
} sk_conservative_rasterization_mode_t;

/** Query type. */
typedef enum sk_query_type_t {
	SK_QUERY_TYPE_OCCLUSION = 0,
	SK_QUERY_TYPE_PIPELINE_STATISTICS,
	SK_QUERY_TYPE_TIMESTAMP,
} sk_query_type_t;

/** Attachment load / store operations. */
typedef enum sk_attachment_load_op_t {
	SK_ATTACHMENT_LOAD_OP_LOAD = 0,
	SK_ATTACHMENT_LOAD_OP_CLEAR,
	SK_ATTACHMENT_LOAD_OP_DONT_CARE,
} sk_attachment_load_op_t;

typedef enum sk_attachment_store_op_t {
	SK_ATTACHMENT_STORE_OP_STORE = 0,
	SK_ATTACHMENT_STORE_OP_DONT_CARE,
} sk_attachment_store_op_t;

/** Descriptor type in a descriptor set layout binding. */
typedef enum sk_descriptor_type_t {
	SK_DESCRIPTOR_TYPE_NONE = 0,
	SK_DESCRIPTOR_TYPE_SAMPLER,
	SK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	SK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	SK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
	SK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
	SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	SK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	SK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
	SK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
	SK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
	SK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE,
} sk_descriptor_type_t;

/** Render-time type of a shader interface variable / descriptor binding. */
typedef enum sk_render_type_t {
	SK_RENDER_TYPE_NONE = 0,
	SK_RENDER_TYPE_VOID,
	SK_RENDER_TYPE_BOOL,
	SK_RENDER_TYPE_INT,
	SK_RENDER_TYPE_FLOAT,
	SK_RENDER_TYPE_VECTOR,
	SK_RENDER_TYPE_MATRIX,
	SK_RENDER_TYPE_IMAGE,
	SK_RENDER_TYPE_SAMPLER,
	SK_RENDER_TYPE_SAMPLED_IMAGE,
	SK_RENDER_TYPE_ARRAY,
	SK_RENDER_TYPE_RUNTIME_ARRAY,
	SK_RENDER_TYPE_STRUCT,
} sk_render_type_t;

/** Pipeline bind point. */
typedef enum sk_pipeline_bind_point_t {
	SK_PIPELINE_BIND_POINT_GRAPHICS = 0,
	SK_PIPELINE_BIND_POINT_COMPUTE,
	SK_PIPELINE_BIND_POINT_RAY_TRACING,
} sk_pipeline_bind_point_t;

/** Acceleration structure geometry type. */
typedef enum sk_geometry_type_t {
	SK_GEOMETRY_TYPE_TRIANGLES = 0,
	SK_GEOMETRY_TYPE_AABBs,
} sk_geometry_type_t;

/** Acceleration structure build flags (u32 bits). */
typedef enum sk_acceleration_structure_build_flag_bit_t {
	SK_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE = 1u << 0,
	SK_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION = 1u << 1,
	SK_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE = 1u << 2,
	SK_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD = 1u << 3,
	SK_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY = 1u << 4,
	SK_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE = 1u << 5,
} sk_acceleration_structure_build_flag_bit_t;

/** Queue type bitmask (u32 bits). */
typedef enum sk_queue_type_bit_t {
	SK_QUEUE_TYPE_NONE = 0,
	SK_QUEUE_TYPE_GRAPHICS = 1u << 0,
	SK_QUEUE_TYPE_COMPUTE = 1u << 1,
	SK_QUEUE_TYPE_TRANSFER = 1u << 2,
	SK_QUEUE_TYPE_ALL = SK_QUEUE_TYPE_GRAPHICS | SK_QUEUE_TYPE_COMPUTE | SK_QUEUE_TYPE_TRANSFER,
} sk_queue_type_bit_t;

/** Pipeline statistics flags (u32 bits). */
typedef enum sk_pipeline_statistic_flag_bit_t {
	SK_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES = 1u << 0,
	SK_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES = 1u << 1,
	SK_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS = 1u << 2,
	SK_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS = 1u << 3,
	SK_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES = 1u << 4,
	SK_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS = 1u << 5,
	SK_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES = 1u << 6,
	SK_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS = 1u << 7,
	SK_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES = 1u << 8,
	SK_PIPELINE_STATISTIC_TESSELLATION_EVAL_SHADER_INVOCATIONS = 1u << 9,
	SK_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS = 1u << 10,
} sk_pipeline_statistic_flag_bit_t;

/* ------------------------------------------------------------------ */
/* POD structs                                                         */
/* ------------------------------------------------------------------ */

/** 3D extent (width, height, depth). */
typedef struct sk_extent3d_t {
	u32 width;
	u32 height;
	u32 depth;
} sk_extent3d_t;

/** 2D offset. */
typedef struct sk_offset3d_t {
	i32 x;
	i32 y;
	i32 z;
} sk_offset3d_t;

/** Device initialization parameters. */
typedef struct sk_device_init_desc_t {
	bool enable_debug_layers;
} sk_device_init_desc_t;

/**
 * Adapter capabilities (mirror of main's DeviceFeatures).
 * Filled from the physical device's feature chain; the device enables the
 * requested features at select_adapter time.
 */
typedef struct sk_device_features_t {
	bool tessellation_shader;
	bool geometry_shader;
	bool compute_shader;
	bool multi_viewport;
	bool texture_compression_bc;
	bool texture_compression_etc2;
	bool texture_compression_astc;
	bool independent_blend;
	bool bindless_texture_supported;
	bool multiview_enabled;
	bool bindless_sampler_supported;
	bool bindless_buffer_supported;
	bool buffer_device_address;
	bool draw_indirect_count;
	bool ray_tracing;
	bool resolve_depth;
	bool memory_budget;
	bool fragment_shader_barycentric;
} sk_device_features_t;

/** Adapter hardware limits (mirror of main's DeviceLimits). */
typedef struct sk_device_limits_t {
	u32 max_texture_size;
	u32 max_texture_3d_size;
	u32 max_cube_map_size;
	u32 max_viewport_dimensions[2];
	u32 max_compute_work_group_count[3];
	u32 max_compute_work_group_size[3];
	u32 max_compute_invocations;
	u32 max_vertex_input_bindings;
	u32 max_vertex_input_attributes;
	u32 max_attachment_samples;
	u64 min_memory_map_alignment;
	u64 min_uniform_buffer_offset_alignment;
	f32 timestamp_period;
} sk_device_limits_t;

/** Adapter / device properties (type, name, vendor, driver, features, limits). */
typedef struct sk_device_properties_t {
	sk_device_type_t device_type;
	char device_name[256];
	char vendor_name[128];
	char driver_version[64];
	sk_device_features_t features;
	sk_device_limits_t limits;
} sk_device_properties_t;

/** Per-heap memory usage/budget snapshot (see get_memory_budgets). */
typedef struct sk_memory_heap_budget_t {
	u64 usage;		   /* estimated bytes currently used by the program in this heap */
	u64 budget;		   /* estimated bytes available to the program in this heap */
	bool device_local; /* true for dedicated VRAM heaps, false for host/shared heaps */
} sk_memory_heap_budget_t;

/** Buffer descriptor. */
typedef struct sk_buffer_desc_t {
	u64 size;
	u32 usage_flags;		/* sk_resource_usage_bit_t */
	bool host_visible;		/* default true */
	bool persistent_mapped; /* default false */
	const_chr_t debug_name; /* optional, for debug tools */
} sk_buffer_desc_t;

/** Texture descriptor. */
typedef struct sk_texture_desc_t {
	sk_extent3d_t extent;	  /* width=height=depth=1 default */
	u32 mip_levels;			  /* default 1 */
	u32 array_layers;		  /* default 1 */
	u32 sample_count;		  /* default 1 (MSAA) */
	sk_pixel_format_t format; /* default RGBA8_UNORM */
	u32 usage_flags;		  /* sk_resource_usage_bit_t */
	bool cubemap;			  /* default false */
	const_chr_t debug_name;	  /* optional */
} sk_texture_desc_t;

/** Texture view descriptor. */
typedef enum sk_texture_view_type_t {
	SK_TEXTURE_VIEW_TYPE_1D = 0,
	SK_TEXTURE_VIEW_TYPE_2D = 1,
	SK_TEXTURE_VIEW_TYPE_3D = 2,
	SK_TEXTURE_VIEW_TYPE_CUBE = 3,
	SK_TEXTURE_VIEW_TYPE_1D_ARRAY = 4,
	SK_TEXTURE_VIEW_TYPE_2D_ARRAY = 5,
	SK_TEXTURE_VIEW_TYPE_CUBE_ARRAY = 6,
	SK_TEXTURE_VIEW_TYPE_UNDEFINED = 7,
} sk_texture_view_type_t;

typedef struct sk_texture_view_desc_t {
	sk_texture_t texture;		 /* the parent texture */
	sk_texture_view_type_t type; /* default TYPE_2D */
	u32 base_mip_level;			 /* default 0 */
	u32 mip_level_count;		 /* U32_MAX = all remaining mips */
	u32 base_array_layer;		 /* default 0 */
	u32 array_layer_count;		 /* U32_MAX = all remaining layers */
	const_chr_t debug_name;		 /* optional */
} sk_texture_view_desc_t;

/** Texture memory requirements. */
typedef struct sk_texture_memory_requirements_t {
	u64 size;
	u64 alignment;
	u32 memory_type_bits;
} sk_texture_memory_requirements_t;

/** Sampler descriptor. */
typedef struct sk_sampler_desc_t {
	sk_filter_mode_t min_filter;			  /* default LINEAR */
	sk_filter_mode_t mag_filter;			  /* default LINEAR */
	sk_filter_mode_t mipmap_filter;			  /* default LINEAR */
	sk_texture_address_mode_t address_mode_u; /* default REPEAT */
	sk_texture_address_mode_t address_mode_v; /* default REPEAT */
	sk_texture_address_mode_t address_mode_w; /* default REPEAT */
	f32 mip_lod_bias;						  /* default 0.0f */
	bool anisotropy_enable;					  /* default false */
	f32 max_anisotropy;						  /* default 1.0f */
	bool compare_enable;					  /* default false */
	sk_compare_op_t compare_op;				  /* default NEVER */
	f32 min_lod;							  /* default 0.0f */
	f32 max_lod;							  /* default 1000.0f */
	sk_border_color_t border_color;			  /* default OPAQUE_BLACK */
	const_chr_t debug_name;					  /* optional */
} sk_sampler_desc_t;

/** Shader interface variable (vertex input or pipeline output). */
typedef struct sk_interface_variable_t {
	u32 location;	  /* shader layout(location) index */
	u32 offset;		  /* byte offset within the vertex / per-output */
	const_chr_t name; /* optional, for debug tools */
	sk_pixel_format_t format;
	u32 size; /* element size in bytes */
} sk_interface_variable_t;

/** Push constant range for pipeline uniforms. */
typedef struct sk_push_constant_range_t {
	const_chr_t name;  /* optional, for debug tools */
	u32 offset;		   /* byte offset into push constant block */
	u32 size;		   /* bytes (multiple of 4) */
	u32 shader_stages; /* sk_shader_stage_bit_t OR'd across stages */
} sk_push_constant_range_t;

/** Descriptor set layout binding. */
typedef struct sk_descriptor_set_layout_binding_t {
	u32 binding;		  /* descriptor set binding index */
	u32 descriptor_count; /* default 1; >1 for arrays, MaxBindless for runtime arrays */
	const_chr_t name;	  /* optional, for debug tools */
	sk_descriptor_type_t descriptor_type;
	sk_render_type_t render_type;	  /* default SK_RENDER_TYPE_NONE; drives bindless sizing */
	u32 shader_stages;				  /* sk_shader_stage_bit_t OR'd across stages */
	sk_texture_view_type_t view_type; /* TEXTURE_VIEW_TYPE_2D if sampled image, else N/A */
	u32 size;						  /* element size in bytes for render_type (runtime arrays) */
} sk_descriptor_set_layout_binding_t;

/** Per-set descriptor layout group (one per descriptor set index). */
typedef struct sk_descriptor_set_layout_t {
	u32 set; /* descriptor set index this layout belongs to */
	sk_descriptor_set_layout_binding_t* bindings;
	u32 binding_count;
	const_chr_t debug_name; /* optional */
} sk_descriptor_set_layout_t;

/** Shared pipeline description: variables, descriptor sets, push constants, stride. */
typedef struct sk_pipeline_desc_t {
	sk_interface_variable_t* input_variables; /* vertex attributes; optional for compute */
	u32 input_variable_count;
	sk_interface_variable_t* output_variables; /* shader outputs; optional */
	u32 output_variable_count;
	u32 stride;								 /* vertex input stride in bytes; 0 = tightly packed */
	sk_descriptor_set_layout_t* descriptors; /* one entry per descriptor set */
	u32 descriptor_count;
	sk_push_constant_range_t* push_constants; /* ranges for each push constant block */
	u32 push_constant_count;
	sk_extent3d_t num_threads; /* compute thread group size [1,1,1] default */
} sk_pipeline_desc_t;

/** Blend state for a single render-target attachment. */
typedef struct sk_blend_state_desc_t {
	bool blend_enable;
	sk_blend_factor_t src_color_blend_factor; /* default SRC_ALPHA */
	sk_blend_factor_t dst_color_blend_factor; /* default ONE_MINUS_SRC_ALPHA */
	sk_blend_op_t color_blend_op;			  /* default ADD */
	sk_blend_factor_t src_alpha_blend_factor; /* default ONE */
	sk_blend_factor_t dst_alpha_blend_factor; /* default ZERO */
	sk_blend_op_t alpha_blend_op;			  /* default MAX */
	u32 color_write_mask;					  /* sk_color_mask_component_t OR'd */
} sk_blend_state_desc_t;

/** Rasterizer state. */
typedef struct sk_rasterizer_state_desc_t {
	sk_polygon_mode_t polygon_mode; /* default FILL */
	sk_cull_mode_t cull_mode;		/* default NONE */
	sk_front_face_t front_face;		/* default CCW */
	bool depth_clamp_enable;		/* default false */
	bool rasterizer_discard_enable; /* default false */
	bool depth_bias_enable;			/* default false */
	f32 depth_bias_constant_factor; /* default 0.0f */
	f32 depth_bias_clamp;			/* default 0.0f */
	f32 depth_bias_slope_factor;	/* default 0.0f */
	f32 line_width;					/* default 1.0f */
} sk_rasterizer_state_desc_t;

/** Single-side stencil state for depth-stencil test. */
typedef struct sk_stencil_op_state_desc_t {
	sk_stencil_op_t fail_op;	   /* default KEEP */
	sk_stencil_op_t pass_op;	   /* default KEEP */
	sk_stencil_op_t depth_fail_op; /* default KEEP */
	sk_compare_op_t compare_op;	   /* default ALWAYS */
	u32 compare_mask;			   /* default 0xFF */
	u32 write_mask;				   /* default 0xFF */
	u32 reference;				   /* default 0 */
} sk_stencil_op_state_desc_t;

/** Depth-stencil state. */
typedef struct sk_depth_stencil_state_desc_t {
	bool depth_test_enable;			  /* default true */
	bool depth_write_enable;		  /* default true */
	sk_compare_op_t depth_compare_op; /* default GREATER (reverse-Z) */
	bool depth_bounds_test_enable;	  /* default false */
	bool stencil_test_enable;		  /* default false */
	sk_stencil_op_state_desc_t front;
	sk_stencil_op_state_desc_t back;
	f32 min_depth_bounds; /* default 1.0f */
	f32 max_depth_bounds; /* default 0.0f */
} sk_depth_stencil_state_desc_t;

/** Descriptor set override for a pipeline (caller-resolved, full data). */
typedef struct sk_descriptor_set_override_t {
	u32 set_index;
	sk_descriptor_set_t descriptor_set;
} sk_descriptor_set_override_t;

/** Graphics pipeline creation parameters. */
typedef struct sk_graphics_pipeline_desc_t {
	sk_pipeline_desc_t pipeline;		 /* shared pipeline info (variables, descriptors, push constants, stride) */
	sk_shader_t vertex_shader;			 /* RHI-created shader handle (caller resolves RID to handle) */
	const_chr_t vertex_shader_variant;	 /* default "Default" */
	sk_shader_t fragment_shader;		 /* RHI-created shader handle */
	const_chr_t fragment_shader_variant; /* default "Default" */
	void_ptr_t material;				 /* caller-owned material handle; RHI never dereferences it (no RID) */
	sk_primitive_topology_t topology;	 /* default TRIANGLE_LIST */
	sk_rasterizer_state_desc_t rasterizer_state;
	sk_depth_stencil_state_desc_t depth_stencil_state;
	sk_blend_state_desc_t* blend_states; /* one per RT attachment, 0 = disabled blend */
	u32 blend_state_count;
	sk_render_pass_t render_pass;
	const_chr_t debug_name;									/* optional */
	sk_pipeline_t previous_pipeline;						/* for pipeline caching/derivation */
	u32 vertex_input_stride;								/* U32_MAX = use pipeline.stride */
	bool allow_immediate_set;								/* default false; enables update-after-bind for the immediate set */
	sk_descriptor_set_override_t* descriptor_sets_override; /* full overrides, not RIDs */
	u32 descriptor_sets_override_count;
	sk_conservative_rasterization_mode_t conservative_rasterization_mode; /* default DISABLED */
} sk_graphics_pipeline_desc_t;

/** Compute pipeline creation parameters. */
typedef struct sk_compute_pipeline_desc_t {
	sk_pipeline_desc_t pipeline;							/* shared pipeline info (descriptors, push constants, num_threads) */
	sk_shader_t compute_shader;								/* RHI-created shader handle (caller resolves RID to handle) */
	const_chr_t variant;									/* default "Default" */
	sk_pipeline_t previous_pipeline;						/* for derivation */
	const_chr_t debug_name;									/* optional */
	bool allow_immediate_set;								/* default false */
	sk_descriptor_set_override_t* descriptor_sets_override; /* full overrides, not RIDs */
	u32 descriptor_sets_override_count;
} sk_compute_pipeline_desc_t;

/** Ray tracing pipeline creation parameters. */
typedef struct sk_ray_tracing_pipeline_desc_t {
	sk_pipeline_desc_t pipeline;							/* shared pipeline info (descriptors, push constants) */
	sk_shader_t shader;										/* RHI-created ray tracing shader handle */
	const_chr_t variant;									/* default "Default" */
	u32 max_recursion_depth;								/* default 1 */
	sk_pipeline_t previous_pipeline;						/* for derivation */
	const_chr_t debug_name;									/* optional */
	sk_descriptor_set_override_t* descriptor_sets_override; /* full overrides, not RIDs */
	u32 descriptor_sets_override_count;
} sk_ray_tracing_pipeline_desc_t;

/** Attachment description for a render pass. */
typedef struct sk_attachment_desc_t {
	sk_resource_state_t initial_state;		   /* default UNDEFINED */
	sk_resource_state_t final_state;		   /* default UNDEFINED */
	sk_attachment_load_op_t load_op;		   /* default LOAD */
	sk_attachment_store_op_t store_op;		   /* default STORE */
	sk_attachment_load_op_t stencil_load_op;   /* default DONT_CARE */
	sk_attachment_store_op_t stencil_store_op; /* default DONT_CARE */
	u32 sample_count;						   /* default 1 */
	sk_pixel_format_t format;				   /* RGBA8_UNORM default */
} sk_attachment_desc_t;

/** Render pass descriptor. */
typedef struct sk_render_pass_desc_t {
	sk_attachment_desc_t* attachments;
	u32 attachment_count;
	sk_attachment_desc_t* resolve_attachments; /* MSAA resolve attachments, parallel to attachments */
	u32 resolve_attachment_count;
	const_chr_t debug_name; /* optional */
} sk_render_pass_desc_t;

/** Clear values for render pass. */
typedef struct sk_clear_values_t {
	struct {
		f32 r, g, b, a;
	} color;	 /* default {0,0,0,0} */
	f32 depth;	 /* reverse-Z: far plane = 0 */
	u32 stencil; /* default 0 */
} sk_clear_values_t;

/** Begin render pass info. */
typedef struct sk_begin_render_pass_info_t {
	sk_render_pass_t render_pass;
	sk_framebuffer_t framebuffer;
	const sk_clear_values_t* clear_values;
} sk_begin_render_pass_info_t;

/** Framebuffer descriptor. */
typedef struct sk_framebuffer_desc_t {
	sk_render_pass_t render_pass;
	sk_texture_view_t* attachments; /* array of texture views */
	u32 attachment_count;
	const_chr_t debug_name; /* optional */
} sk_framebuffer_desc_t;

/** Query pool descriptor. */
typedef struct sk_query_pool_desc_t {
	sk_query_type_t type;
	u32 query_count;
	bool allow_partial_results;
	bool return_availability;
	u32 pipeline_statistics; /* sk_pipeline_statistic_flag_bit_t OR'd */
	const_chr_t debug_name;	 /* optional */
} sk_query_pool_desc_t;

/** Acceleration structure geometry triangles. */
typedef struct sk_geometry_triangles_desc_t {
	sk_buffer_t vertex_buffer; /* optional */
	u64 vertex_offset;		   /* bytes into buffer */
	u32 vertex_count;
	u32 vertex_stride;				 /* bytes per vertex */
	sk_pixel_format_t vertex_format; /* default RGB32_FLOAT */

	sk_buffer_t index_buffer; /* optional */
	u64 index_offset;
	u32 index_count;
	sk_index_type_t index_type; /* default UINT32 */

	sk_buffer_t transform_buffer; /* optional 3x4 transform per geometry */
	u64 transform_offset;		  /* bytes into transform buffer */

	bool opaque; /* default true */
} sk_geometry_triangles_desc_t;

/** Acceleration structure geometry AABBs. */
typedef struct sk_geometry_aabbs_desc_t {
	sk_buffer_t aabb_buffer; /* optional */
	u64 aabb_offset;
	u32 aabb_count;
	u32 aabb_stride; /* bytes per AABB (6 * 4 = 24) */
	bool opaque;	 /* default true */
} sk_geometry_aabbs_desc_t;

/** Acceleration structure geometry. */
typedef struct sk_geometry_desc_t {
	sk_geometry_type_t type; /* default TRIANGLES */
	sk_geometry_triangles_desc_t triangles;
	sk_geometry_aabbs_desc_t aabbs;
} sk_geometry_desc_t;

/** Bottom-level acceleration structure. */
typedef struct sk_blas_desc_t {
	sk_geometry_desc_t* geometries;
	u32 geometry_count;
	u32 build_flags;		/* sk_acceleration_structure_build_flag_bit_t */
	const_chr_t debug_name; /* optional */
} sk_blas_desc_t;

/** Top-level acceleration structure instance. */
typedef struct sk_as_instance_desc_t {
	sk_blas_t bottom_level_as;
	f32 transform[12];						/* 3x4 affine transform in row-major */
	u32 instance_id;						/* default 0 */
	u32 instance_mask;						/* default 0xFF */
	u32 shader_binding_table_record_offset; /* default 0 */
	bool front_counter_clockwise;			/* default false */
	bool force_opaque;						/* default false */
	bool force_non_opaque;					/* default false */
} sk_as_instance_desc_t;

/** Top-level acceleration structure. */
typedef struct sk_tlas_desc_t {
	sk_as_instance_desc_t* instances;
	u32 instance_count;
	u32 max_instances;
	u32 build_flags;		/* sk_acceleration_structure_build_flag_bit_t */
	const_chr_t debug_name; /* optional */
} sk_tlas_desc_t;

/** Acceleration structure build info. */
typedef struct sk_as_build_info_t {
	bool is_update; /* default false */
	sk_buffer_t scratch_buffer;
	u64 scratch_offset; /* bytes into scratch buffer */
} sk_as_build_info_t;

/** Queue descriptor. */
typedef struct sk_queue_desc_t {
	u32 queue_type; /* sk_queue_type_bit_t OR'd */
} sk_queue_desc_t;

/** Command buffer allocation parameters. */
typedef struct sk_command_buffer_desc_t {
	sk_command_buffer_level_t level; /* default PRIMARY */
	u32 queue_type;					 /* sk_queue_type_bit_t; which queue family may submit/execute */
	const_chr_t debug_name;			 /* optional */
} sk_command_buffer_desc_t;

/**
 * Parameters for begin_command_buffer.
 * Secondary buffers continuing a render pass set usage RENDER_PASS_CONTINUE
 * and fill render_pass / subpass (framebuffer optional for inheritance).
 */
typedef struct sk_command_buffer_begin_info_t {
	u32 usage_flags;			  /* sk_command_buffer_usage_bit_t */
	sk_render_pass_t render_pass; /* secondary inheritance */
	u32 subpass;
	sk_framebuffer_t framebuffer; /* optional inheritance */
} sk_command_buffer_begin_info_t;

/** Viewport with depth range (needed for reverse-Z). */
typedef struct sk_viewport_t {
	f32 x;
	f32 y;
	f32 width;
	f32 height;
	f32 min_depth; /* reverse-Z far plane often 0 */
	f32 max_depth; /* reverse-Z near plane often 1 */
} sk_viewport_t;

/** 2D scissor rectangle. */
typedef struct sk_rect2d_t {
	i32 x;
	i32 y;
	u32 width;
	u32 height;
} sk_rect2d_t;

/**
 * Clear one color or depth/stencil attachment inside an active render pass.
 * color_attachment is the color attachment index when is_depth_stencil is false.
 */
typedef struct sk_clear_attachment_t {
	bool is_depth_stencil;
	u32 color_attachment; /* ignored when is_depth_stencil */
	sk_clear_values_t clear;
	sk_rect2d_t rect; /* region to clear; width/height 0 = full attachment */
} sk_clear_attachment_t;

/** Fence creation (CPU-waitable GPU completion). */
typedef struct sk_fence_desc_t {
	bool signaled;			/* create already signaled; default false */
	const_chr_t debug_name; /* optional */
} sk_fence_desc_t;

/** Binary semaphore creation (GPU↔GPU queue sync). */
typedef struct sk_semaphore_desc_t {
	const_chr_t debug_name; /* optional */
} sk_semaphore_desc_t;

/** Swapchain creation (presentation surface). */
typedef struct sk_swapchain_desc_t {
	void_ptr_t window; /* platform sk_window_t (or native surface) */
	u32 width;		   /* preferred extent; 0 = query from window */
	u32 height;
	sk_pixel_format_t format; /* preferred surface format; UNKNOWN = pick */
	u32 image_count;		  /* preferred image count (2–3 typical); 0 = default */
	u32 usage_flags;		  /* sk_resource_usage_bit_t on swap images */
	bool vsync;				  /* default true (FIFO); false prefers mailbox/immediate */
	const_chr_t debug_name;	  /* optional */
} sk_swapchain_desc_t;

/* ------------------------------------------------------------------ */
/* POD types that use handles (defined after SK_HANDLER)               */
/* ------------------------------------------------------------------ */

/** Buffer memory requirements (mirrors texture path). */
typedef struct sk_buffer_memory_requirements_t {
	u64 size;
	u64 alignment;
	u32 memory_type_bits;
} sk_buffer_memory_requirements_t;

/** Acceleration structure scratch / AS size query result. */
typedef struct sk_as_build_sizes_t {
	u64 acceleration_structure_size;
	u64 build_scratch_size;
	u64 update_scratch_size;
} sk_as_build_sizes_t;

/** Descriptor set allocation / layout. */
typedef struct sk_descriptor_set_desc_t {
	sk_descriptor_set_layout_binding_t* bindings;
	u32 binding_count;
	const_chr_t debug_name; /* optional */
} sk_descriptor_set_desc_t;

/**
 * Single write into a descriptor set (one binding slot / array element).
 * Only the fields matching `type` are read by the backend.
 */
typedef struct sk_descriptor_write_t {
	u32 binding;
	u32 array_index; /* default 0 */
	sk_descriptor_type_t type;

	/* buffer descriptors */
	sk_buffer_t buffer;
	u64 buffer_offset;
	u64 buffer_range; /* 0 = whole remaining buffer */

	/* image / sampler descriptors */
	sk_texture_view_t texture_view;
	sk_sampler_t sampler;

	/* ray tracing */
	sk_tlas_t acceleration_structure;
} sk_descriptor_write_t;

/** Buffer ↔ texture region copy. */
typedef struct sk_buffer_texture_copy_t {
	sk_buffer_t buffer;
	u64 buffer_offset;
	u32 buffer_row_length;	 /* texels; 0 = tightly packed */
	u32 buffer_image_height; /* rows; 0 = tightly packed */
	sk_texture_t texture;
	u32 mip_level;
	u32 array_layer;
	sk_offset3d_t texture_offset;
	sk_extent3d_t texture_extent;
} sk_buffer_texture_copy_t;

/** Texture → texture region copy (same format / 1:1 extent). */
typedef struct sk_texture_copy_t {
	sk_texture_t src;
	sk_texture_t dst;
	u32 src_mip_level;
	u32 dst_mip_level;
	u32 src_array_layer;
	u32 dst_array_layer;
	sk_offset3d_t src_offset;
	sk_offset3d_t dst_offset;
	sk_extent3d_t extent;
} sk_texture_copy_t;

/** Texture blit (may scale / filter). Offsets are [min, max) corners. */
typedef struct sk_texture_blit_t {
	sk_texture_t src;
	sk_texture_t dst;
	u32 src_mip_level;
	u32 dst_mip_level;
	u32 src_array_layer;
	u32 dst_array_layer;
	sk_offset3d_t src_offsets[2];
	sk_offset3d_t dst_offsets[2];
	sk_filter_mode_t filter; /* default LINEAR */
} sk_texture_blit_t;

/** MSAA resolve region (src multisampled → dst single-sample). */
typedef struct sk_texture_resolve_t {
	sk_texture_t src;
	sk_texture_t dst;
	u32 src_mip_level;
	u32 dst_mip_level;
	u32 src_array_layer;
	u32 dst_array_layer;
} sk_texture_resolve_t;

/** Queue submit wait / signal attachments. */
typedef struct sk_submit_info_t {
	sk_command_buffer_t* command_buffers;
	u32 command_buffer_count;
	sk_semaphore_t* wait_semaphores;
	u32 wait_semaphore_count;
	sk_semaphore_t* signal_semaphores;
	u32 signal_semaphore_count;
	sk_fence_t signal_fence; /* optional; zero = no fence */
} sk_submit_info_t;

/** Acquire next swapchain image. */
typedef struct sk_acquire_info_t {
	sk_swapchain_t swapchain;
	u64 timeout_ns;					 /* UINT64_MAX = wait forever */
	sk_semaphore_t signal_semaphore; /* signaled when the image is ready (preferred) */
	sk_fence_t signal_fence;		 /* CPU-waitable; Vulkan requires semaphore or fence */
} sk_acquire_info_t;

/** Present swapchain image(s). */
typedef struct sk_present_info_t {
	sk_swapchain_t swapchain;
	u32 image_index;
	sk_semaphore_t* wait_semaphores;
	u32 wait_semaphore_count;
} sk_present_info_t;

/**
 * Shader binding table region for ray tracing dispatch.
 * Addresses are device buffer + offset; stride/size in bytes.
 */
typedef struct sk_shader_binding_table_region_t {
	sk_buffer_t buffer;
	u64 offset;
	u64 stride;
	u64 size;
} sk_shader_binding_table_region_t;

/** Ray tracing dispatch parameters. */
typedef struct sk_trace_rays_info_t {
	sk_shader_binding_table_region_t raygen;
	sk_shader_binding_table_region_t miss;
	sk_shader_binding_table_region_t hit;
	sk_shader_binding_table_region_t callable;
	u32 width;
	u32 height;
	u32 depth; /* default 1 */
} sk_trace_rays_info_t;

/* ------------------------------------------------------------------ */
/* Complete API table passed through the app registry                  */
/* ------------------------------------------------------------------ */

/**
 * All function pointers are guaranteed non-NULL by the plugin.
 *
 * Coverage notes (KAL-36 review + APX-47/48 gap closure):
 * - Stub backend only; no GPU work until a real Vulkan/D3D12/Metal backend.
 * - Pass-through design: pipelines and descriptor sets receive FULL info
 *   (typed handles + complete descs). The RHI never resolves RID-typed
 *   pipeline/descriptor resources; callers resolve shader/material RIDs and
 *   pass the resulting handles/data. RID migration is deferred.
 * - Gap A: adapter enumeration/selection, device properties/features/limits,
 *   memory budgets, and resource desc/mapped-data getters.
 * - Gap B: descriptor bindings now carry render_type/size and are grouped per
 *   set (sk_descriptor_set_layout_t); pipeline descs carry output variables
 *   and vertex stride; graphics/compute/raytracing descs carry material,
 *   vertexInputStride, allowImmediateSet, descriptorSetsOverride, and
 *   conservative rasterization; query pools expose returnAvailability;
 *   attachments expose stencil ops + sampleCount; render passes expose
 *   resolveAttachments; triangles expose transform buffer.
 * - Gap C: memory_barrier, BLAS/TLAS resource barriers, BLAS/TLAS copy, and
 *   GPU-side copy_query_pool_results.
 * - Gap D: swapchain extent/format/image-count/current-index/textures,
 *   BLAS compacted queries, TLAS instance update, queue submit_and_wait.
 * - Command buffer: create/begin/end/reset/destroy lifecycle, secondary
 *   execute, viewport depth range, dynamic state, update_buffer, MSAA
 *   resolve, clear_attachments, indirect count draws, debug labels.
 * - Still deferred: multi-queue ownership transfer detail, timeline
 *   semaphores, mesh/task draws, dynamic rendering helpers, subpasses, and
 *   inline descriptor writes (SetTexture/SetBuffer/… stay RID-based in main
 *   and are replaced here by full update_descriptor_set).
 */
typedef struct sk_render_device_api_t {
	/* --- Device lifecycle --- */
	/**
	 * Create the render device.
	 * @param context Host/app context (platform, window surface hooks).
	 * @param desc Optional init flags; NULL uses defaults (debug layers off).
	 */
	sk_render_device_t (*init)(void_ptr_t context, const sk_device_init_desc_t* desc);
	void (*destroy)(sk_render_device_t dev);
	/** Block until all device work completes. @return 0 on success. */
	i32 (*wait_idle)(sk_render_device_t dev);

	/* --- Adapter / device queries --- */
	/** Number of physical adapters enumerated at init. */
	u32 (*get_adapter_count)(sk_render_device_t dev);
	/** Adapter handle at index; valid for the device lifetime. */
	sk_adapter_t (*get_adapter)(sk_render_device_t dev, u32 index);
	/**
	 * Select the adapter used by the device (recreates device queues/limits).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*select_adapter)(sk_render_device_t dev, sk_adapter_t adapter);
	/** Adapter suitability score (higher = preferred). */
	u32 (*get_adapter_score)(sk_render_device_t dev, sk_adapter_t adapter);
	/** Adapter name (device name string). */
	const_chr_t (*get_adapter_name)(sk_render_device_t dev, sk_adapter_t adapter);
	/** Selected device properties (type, name, vendor, driver, features, limits). */
	sk_device_properties_t (*get_properties)(sk_render_device_t dev);
	/** Selected device features. */
	sk_device_features_t (*get_features)(sk_render_device_t dev);
	/** Graphics API this device was created for. */
	sk_graphics_api_t (*get_api)(sk_render_device_t dev);
	/**
	 * Fill memory heap budgets.
	 * @param out_budgets Caller array; receives heap snapshots.
	 * @param max_count Capacity of out_budgets.
	 * @return Number of heaps written.
	 */
	u32 (*get_memory_budgets)(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count);

	/* --- Buffer management --- */
	sk_buffer_t (*create_buffer)(sk_render_device_t dev, const sk_buffer_desc_t* desc);
	void (*destroy_buffer)(sk_render_device_t dev, sk_buffer_t buf);
	void* (*buffer_map)(sk_render_device_t dev, sk_buffer_t buf);
	void (*buffer_unmap)(sk_render_device_t dev, sk_buffer_t buf);
	/** Desc the buffer was created with. */
	sk_buffer_desc_t (*get_buffer_desc)(sk_render_device_t dev, sk_buffer_t buf);
	/** Persistently mapped data pointer (valid if persistent_mapped). */
	void_ptr_t (*get_buffer_mapped_data)(sk_render_device_t dev, sk_buffer_t buf);

	/* --- Texture management --- */
	sk_texture_t (*create_texture)(sk_render_device_t dev, const sk_texture_desc_t* desc);
	void (*destroy_texture)(sk_render_device_t dev, sk_texture_t tex);
	sk_texture_view_t (*create_texture_view)(sk_render_device_t dev, const sk_texture_view_desc_t* desc);
	void (*destroy_texture_view)(sk_render_device_t dev, sk_texture_view_t view);
	sk_sampler_t (*create_sampler)(sk_render_device_t dev, const sk_sampler_desc_t* desc);
	void (*destroy_sampler)(sk_render_device_t dev, sk_sampler_t sampler);
	sk_texture_desc_t (*get_texture_desc)(sk_render_device_t dev, sk_texture_t tex);
	sk_texture_view_desc_t (*get_texture_view_desc)(sk_render_device_t dev, sk_texture_view_t view);
	sk_sampler_desc_t (*get_sampler_desc)(sk_render_device_t dev, sk_sampler_t sampler);

	/* --- Memory allocation --- */
	sk_memory_t (*create_memory)(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits);
	void (*destroy_memory)(sk_render_device_t dev, sk_memory_t mem);
	sk_texture_t (*create_aliased_texture)(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset);
	u64 (*get_memory_size)(sk_render_device_t dev, sk_memory_t mem);

	/* --- Shader management --- */
	sk_shader_t (*create_shader)(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage);
	void (*destroy_shader)(sk_render_device_t dev, sk_shader_t shdr);

	/* --- Pipeline management --- */
	sk_pipeline_t (*create_graphics_pipeline)(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc);
	sk_pipeline_t (*create_compute_pipeline)(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc);
	sk_pipeline_t (*create_ray_tracing_pipeline)(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc);
	void (*destroy_pipeline)(sk_render_device_t dev, sk_pipeline_t pipeline);
	/** Shared pipeline desc the pipeline was created from. */
	sk_pipeline_desc_t (*get_pipeline_desc)(sk_render_device_t dev, sk_pipeline_t pipeline);
	/** Pipeline bind point. */
	sk_pipeline_bind_point_t (*get_pipeline_bind_point)(sk_render_device_t dev, sk_pipeline_t pipeline);

	/* --- Descriptor set management --- */
	sk_descriptor_set_t (*create_descriptor_set)(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc);
	void (*update_descriptor_set)(sk_render_device_t dev, sk_descriptor_set_t set, const sk_descriptor_write_t* writes, u32 write_count);
	void (*destroy_descriptor_set)(sk_render_device_t dev, sk_descriptor_set_t set);
	sk_descriptor_set_desc_t (*get_descriptor_set_desc)(sk_render_device_t dev, sk_descriptor_set_t set);

	/* --- Render pass / framebuffer --- */
	sk_render_pass_t (*create_render_pass)(sk_render_device_t dev, const sk_render_pass_desc_t* desc);
	void (*destroy_render_pass)(sk_render_device_t dev, sk_render_pass_t pass);
	sk_framebuffer_t (*create_framebuffer)(sk_render_device_t dev, const sk_framebuffer_desc_t* desc);
	void (*destroy_framebuffer)(sk_render_device_t dev, sk_framebuffer_t fb);
	sk_render_pass_desc_t (*get_render_pass_desc)(sk_render_device_t dev, sk_render_pass_t pass);
	sk_framebuffer_desc_t (*get_framebuffer_desc)(sk_render_device_t dev, sk_framebuffer_t fb);
	sk_extent3d_t (*get_framebuffer_extent)(sk_render_device_t dev, sk_framebuffer_t fb);

	/* --- Swapchain / present --- */
	sk_swapchain_t (*create_swapchain)(sk_render_device_t dev, const sk_swapchain_desc_t* desc);
	void (*destroy_swapchain)(sk_render_device_t dev, sk_swapchain_t swapchain);
	/** Recreate after resize / OUT_OF_DATE. @return 0 on success. */
	i32 (*resize_swapchain)(sk_render_device_t dev, sk_swapchain_t swapchain, u32 width, u32 height);
	/**
	 * Acquire next presentable image index.
	 * Prefer signal_semaphore and wait it on the first submit that uses the image.
	 * Vulkan requires a semaphore or a fence; backends wait an internal fence when both are omitted.
	 * @param out_image_index Receives image index on success.
	 * @return sk_device_result_t (SUCCESS, SWAPCHAIN_OUT_OF_DATE, or ERROR).
	 */
	sk_device_result_t (*acquire_next_image)(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index);
	/** Texture handle for a swapchain image (valid until resize/destroy). */
	sk_texture_t (*get_swapchain_image)(sk_render_device_t dev, sk_swapchain_t swapchain, u32 image_index);
	/** Current swapchain extent (0 on error / not created). */
	sk_extent3d_t (*get_swapchain_extent)(sk_render_device_t dev, sk_swapchain_t swapchain);
	/** Current swapchain image format. */
	sk_pixel_format_t (*get_swapchain_format)(sk_render_device_t dev, sk_swapchain_t swapchain);
	/** Number of swapchain images. */
	u32 (*get_swapchain_image_count)(sk_render_device_t dev, sk_swapchain_t swapchain);
	/** Index of the last acquired (current) image. */
	u32 (*get_swapchain_current_image_index)(sk_render_device_t dev, sk_swapchain_t swapchain);
	/**
	 * Fill the swapchain image textures.
	 * @param out_textures Caller array; receives texture handles.
	 * @param max_count Capacity of out_textures.
	 * @return Number of images written.
	 */
	u32 (*get_swapchain_textures)(sk_render_device_t dev, sk_swapchain_t swapchain, sk_texture_t* out_textures, u32 max_count);
	/**
	 * Present an acquired image.
	 * @return sk_device_result_t (SUCCESS, SWAPCHAIN_OUT_OF_DATE, or ERROR).
	 */
	sk_device_result_t (*present)(sk_render_device_t dev, sk_queue_t queue, const sk_present_info_t* info);

	/* --- Sync primitives --- */
	sk_fence_t (*create_fence)(sk_render_device_t dev, const sk_fence_desc_t* desc);
	void (*destroy_fence)(sk_render_device_t dev, sk_fence_t fence);
	/** @return 0 on success, non-zero on timeout/error. */
	i32 (*wait_fences)(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns);
	void (*reset_fences)(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count);
	sk_semaphore_t (*create_semaphore)(sk_render_device_t dev, const sk_semaphore_desc_t* desc);
	void (*destroy_semaphore)(sk_render_device_t dev, sk_semaphore_t semaphore);

	/* --- Command buffer lifecycle --- */
	/**
	 * Allocate a command buffer (does not begin recording).
	 * @param desc Level / queue type; NULL = primary, graphics-capable.
	 */
	sk_command_buffer_t (*create_command_buffer)(sk_render_device_t dev, const sk_command_buffer_desc_t* desc);
	void (*destroy_command_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd);
	/**
	 * Begin recording. @param info Usage / secondary inheritance; NULL = defaults.
	 * @return 0 on success.
	 */
	i32 (*begin_command_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info);
	void (*end_command_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd);
	void (*reset_command_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd);
	/** Record secondary command buffers into a primary (outside render pass unless secondaries continue one). */
	void (*execute_commands)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_t* secondary_cmds, u32 secondary_count);

	/* --- State setting (inside command buffer) --- */
	void (*set_viewport)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count);
	void (*set_scissor)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count);
	void (*set_blend_constants)(sk_render_device_t dev, sk_command_buffer_t cmd, f32 r, f32 g, f32 b, f32 a);
	void (*set_stencil_reference)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 reference);
	void (*set_depth_bias)(sk_render_device_t dev, sk_command_buffer_t cmd, f32 constant_factor, f32 clamp, f32 slope_factor);
	void (*set_depth_bounds)(sk_render_device_t dev, sk_command_buffer_t cmd, f32 min_depth, f32 max_depth);
	void (*bind_pipeline)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline);
	/**
	 * Bind a descriptor set. The set layout comes from the descriptor set
	 * handle (RHI-owned); the pipeline handle is only the pipeline-layout
	 * owner. No RID resolution inside the RHI.
	 */
	void (*bind_descriptor_set)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline, u32 set_index,
								sk_descriptor_set_t desc_set, const u32* dynamic_offsets, u32 offset_count);
	void (*bind_vertex_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count);
	void (*bind_index_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, sk_index_type_t index_type);
	/** Pipeline handle is only the pipeline-layout owner; stages/offset/size/data carry the full push range. */
	void (*push_constants)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_t pipeline, u32 shader_stages, u32 offset, u32 size, const void* data);

	/* --- Draw / dispatch --- */
	void (*draw)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
	void (*draw_indexed)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance);
	void (*draw_indirect)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
	void (*draw_indexed_indirect)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
	/**
	 * Indirect draw with GPU-side count buffer (draw_count from device memory).
	 * @param count_buffer Buffer holding a single u32 draw count.
	 * @param max_draw_count Upper bound on draws (clamps count buffer value).
	 */
	void (*draw_indirect_count)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset, u32 max_draw_count,
								u32 stride);
	void (*draw_indexed_indirect_count)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
										u32 max_draw_count, u32 stride);
	void (*dispatch)(sk_render_device_t dev, sk_command_buffer_t cmd, u32 group_count_x, u32 group_count_y, u32 group_count_z);
	void (*dispatch_indirect)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset);
	void (*trace_rays)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_trace_rays_info_t* info);

	/* --- Render pass (command buffer) --- */
	void (*begin_render_pass)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_begin_render_pass_info_t* info);
	void (*end_render_pass)(sk_render_device_t dev, sk_command_buffer_t cmd);
	/** Clear color/depth attachments mid-pass (after begin_render_pass). */
	void (*clear_attachments)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_clear_attachment_t* attachments, u32 attachment_count);

	/* --- Copy / transfer (command buffer) --- */
	void (*copy_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t src, sk_buffer_t dst, u64 size, u64 src_offset, u64 dst_offset);
	void (*copy_buffer_to_texture)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
	void (*copy_texture_to_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
	void (*copy_texture)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_copy_t* copy_info);
	void (*blit_texture)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_blit_t* blit_info);
	void (*resolve_texture)(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_resolve_t* resolve_info);
	/** Host→device buffer write. Vulkan backends split payloads above 65536 bytes. */
	void (*update_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, const void* data);
	void (*fill_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, u32 data);
	void (*clear_texture)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
						  u32 base_array_layer, u32 array_layer_count);

	/* --- Resource barriers (command buffer) --- */
	void (*resource_barrier_buffer)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, sk_resource_state_t old_state, sk_resource_state_t new_state, u32 src_scope,
									u32 dst_scope);
	void (*resource_barrier_texture)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
									 u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope);
	/** BLAS resource barrier (states: GENERAL / SHADER_READ / COPY_DEST / ACCELERATION_STRUCTURE). */
	void (*resource_barrier_bottom_level_as)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, sk_resource_state_t old_state, sk_resource_state_t new_state,
											 u32 src_scope, u32 dst_scope);
	/** TLAS resource barrier (states: GENERAL / SHADER_READ / COPY_DEST / ACCELERATION_STRUCTURE). */
	void (*resource_barrier_top_level_as)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, sk_resource_state_t old_state, sk_resource_state_t new_state,
										  u32 src_scope, u32 dst_scope);
	/** Full device memory barrier (all stages, all accesses). */
	void (*memory_barrier)(sk_render_device_t dev, sk_command_buffer_t cmd);

	/* --- Debug labels (command buffer; no-op when backend debug layers off) --- */
	void (*begin_debug_label)(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);
	void (*end_debug_label)(sk_render_device_t dev, sk_command_buffer_t cmd);
	void (*insert_debug_label)(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);

	/* --- Query pool --- */
	sk_query_pool_t (*create_query_pool)(sk_render_device_t dev, const sk_query_pool_desc_t* desc);
	void (*destroy_query_pool)(sk_render_device_t dev, sk_query_pool_t pool);
	void (*reset_query_pool)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count);
	void (*begin_query)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
	void (*end_query)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
	void (*write_timestamp)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
	/** GPU-side copy of query results into a device buffer (for GPU-driven feedback). */
	void (*copy_query_pool_results)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count, sk_buffer_t dst_buffer, u64 dst_offset,
									u64 stride);
	/**
	 * Read query results to host memory.
	 * @param data Destination buffer (layout depends on query type).
	 * @param data_size Bytes available at data.
	 * @param stride Bytes between consecutive results (0 = tightly packed).
	 * @return 0 on success, non-zero if unavailable / error.
	 */
	i32 (*get_query_pool_results)(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait);

	/* --- Acceleration structure --- */
	sk_blas_t (*create_bottom_level_as)(sk_render_device_t dev, const sk_blas_desc_t* desc);
	void (*destroy_bottom_level_as)(sk_render_device_t dev, sk_blas_t blas);
	sk_tlas_t (*create_top_level_as)(sk_render_device_t dev, const sk_tlas_desc_t* desc);
	void (*destroy_top_level_as)(sk_render_device_t dev, sk_tlas_t tlas);
	sk_as_build_sizes_t (*get_blas_build_sizes)(sk_render_device_t dev, const sk_blas_desc_t* desc);
	sk_as_build_sizes_t (*get_tlas_build_sizes)(sk_render_device_t dev, const sk_tlas_desc_t* desc);
	void (*build_bottom_level_as)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, const sk_blas_desc_t* desc, const sk_as_build_info_t* build_info);
	void (*build_top_level_as)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info);
	/** BLAS was built as compacted. */
	bool (*is_bottom_level_as_compacted)(sk_render_device_t dev, sk_blas_t blas);
	/** Size in bytes of the compacted BLAS (valid after compaction copy). */
	u64 (*get_bottom_level_as_compacted_size)(sk_render_device_t dev, sk_blas_t blas);
	sk_blas_desc_t (*get_blas_desc)(sk_render_device_t dev, sk_blas_t blas);
	sk_tlas_desc_t (*get_tlas_desc)(sk_render_device_t dev, sk_tlas_t tlas);
	/** Copy BLAS src→dst; compress=true requests compaction. */
	void (*copy_bottom_level_as)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t src, sk_blas_t dst, bool compress);
	/** Copy TLAS src→dst; compress=true requests compaction. */
	void (*copy_top_level_as)(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t src, sk_tlas_t dst, bool compress);
	/**
	 * Update all TLAS instances (CPU-side instance buffer rewrite).
	 * @return true on success.
	 */
	bool (*update_top_level_as_instances)(sk_render_device_t dev, sk_tlas_t tlas, const sk_as_instance_desc_t* instances, u32 instance_count);
	/** Update a single TLAS instance in-place. */
	void (*update_top_level_as_instance)(sk_render_device_t dev, sk_tlas_t tlas, u32 index, const sk_as_instance_desc_t* instance);
	/** Set active instance count for the TLAS build. */
	void (*set_top_level_as_instance_count)(sk_render_device_t dev, sk_tlas_t tlas, u32 count);

	/* --- Queue / submit --- */
	sk_queue_t (*create_queue)(sk_render_device_t dev, const sk_queue_desc_t* desc);
	void (*destroy_queue)(sk_render_device_t dev, sk_queue_t queue);
	/** Simple single-cmd submit (no wait/signal). Prefer submit for frame sync. */
	void (*submit_command_buffer)(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
	/** Full submit with optional wait/signal semaphores and fence. @return 0 on success. */
	i32 (*submit)(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* info);
	/** Submit and block until that submit completes. @return 0 on success. */
	i32 (*submit_and_wait)(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
	/** Block until queue work completes. @return 0 on success. */
	i32 (*queue_wait_idle)(sk_render_device_t dev, sk_queue_t queue);

	/* --- Device query --- */
	sk_texture_memory_requirements_t (*get_texture_memory_requirements)(sk_render_device_t dev, const sk_texture_desc_t* desc);
	sk_buffer_memory_requirements_t (*get_buffer_memory_requirements)(sk_render_device_t dev, const sk_buffer_desc_t* desc);

} sk_render_device_api_t;

#ifdef __cplusplus
}
#endif
