/*
 * test_render_device.c — Test-double RHI (Render Hardware Interface) layer.
 *
 * Registers a complete sk_render_device_api_t table under
 * SK_RENDER_DEVICE_API_TYPE_ID. This is a pure CPU mock (ported from main's
 * TestRenderDevice): no GPU backend exists, but every resource is a real,
 * device-owned object with sensible no-GPU behavior so host/tests can exercise
 * render device call sites without a live backend:
 *
 *  - Buffer storage + map/unmap round-trip (create_buffer allocates CPU storage;
 *    buffer_map / update_buffer read and write it).
 *  - Texture subresource ResourceState tracking with a per-(mip, layer) state
 *    array, barrier history, and a mismatchCount incremented whenever a
 *    barrier's declared oldState != the tracked state (Undefined as oldState
 *    means "discard" and never mismatches).
 *  - Command-buffer stats (draw/dispatch/renderPass/copy/clear/barrier/bind
 *    counts) reset on begin.
 *  - Lifecycle for every other RHI object (pass, framebuffer, sampler,
 *    pipeline, descriptor set, query pool, BLAS/TLAS, swapchain, queue, fence,
 *    semaphore, memory) with sane defaults.
 *
 * All full resource structs are private to this translation unit; only opaque
 * sk_*_t handles cross the public surface.
 */

#include "app.h"
#include "allocator.h"
#include "common.h"
#include "render_device.h"

#include <stdio.h>
#include <string.h>

/* Registers the static API table (called from plugin_entry_point). */
void sk_test_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api);

/* ------------------------------------------------------------------ */
/* Private types (never leaked in public headers)                      */
/* ------------------------------------------------------------------ */

typedef struct test_array_t {
	void** data;
	u32 count;
	u32 capacity;
} test_array_t;

typedef struct test_texture_list_t {
	sk_texture_t* data;
	u32 count;
	u32 capacity;
} test_texture_list_t;

typedef struct test_barrier_record_t {
	sk_resource_state_t old_state;
	sk_resource_state_t new_state;
	u32 base_mip_level;
	u32 level_count;
	u32 base_array_layer;
	u32 layer_count;
} test_barrier_record_t;

typedef struct test_device_t {
	const sk_allocator_t* allocator;
	sk_device_properties_t properties;
	sk_device_features_t features;
	test_array_t buffers;
	test_array_t memories;
	test_array_t textures;
	test_array_t texture_views;
	test_array_t samplers;
	test_array_t shaders;
	test_array_t pipelines;
	test_array_t descriptor_sets;
	test_array_t render_passes;
	test_array_t framebuffers;
	test_array_t query_pools;
	test_array_t blas_list;
	test_array_t tlas_list;
	test_array_t swapchains;
	test_array_t queues;
	test_array_t command_buffers;
} test_device_t;

typedef struct test_buffer_t {
	test_device_t* device;
	sk_buffer_desc_t desc;
	u8* storage;
	u64 storage_size;
	sk_resource_state_t state;
	bool mapped;
} test_buffer_t;

typedef struct test_memory_t {
	test_device_t* device;
	u64 size;
	u64 alignment;
	u32 memory_type_bits;
} test_memory_t;

typedef struct test_texture_t {
	test_device_t* device;
	sk_texture_desc_t desc;
	sk_texture_view_t texture_view;
	u32 mip_levels;
	u32 array_layers;
	sk_resource_state_t* subresource_states;
	u32 subresource_count;
	test_barrier_record_t* barrier_history;
	u32 barrier_count;
	u32 barrier_capacity;
	u32 mismatch_count;
	test_memory_t* alias_memory;
	u64 alias_offset;
} test_texture_t;

typedef struct test_texture_view_t {
	test_device_t* device;
	sk_texture_view_desc_t desc;
} test_texture_view_t;

typedef struct test_sampler_t {
	test_device_t* device;
	sk_sampler_desc_t desc;
} test_sampler_t;

typedef struct test_shader_t {
	test_device_t* device;
	u32 stage;
} test_shader_t;

typedef struct test_pipeline_t {
	test_device_t* device;
	sk_pipeline_bind_point_t bind_point;
	sk_pipeline_desc_t pipeline_desc;
} test_pipeline_t;

typedef struct test_descriptor_set_t {
	test_device_t* device;
	sk_descriptor_set_desc_t desc;
	u32 update_count;
} test_descriptor_set_t;

typedef struct test_render_pass_t {
	test_device_t* device;
	sk_render_pass_desc_t desc;
} test_render_pass_t;

typedef struct test_framebuffer_t {
	test_device_t* device;
	sk_framebuffer_desc_t desc;
	sk_extent3d_t extent;
} test_framebuffer_t;

typedef struct test_query_pool_t {
	test_device_t* device;
	sk_query_pool_desc_t desc;
} test_query_pool_t;

typedef struct test_blas_t {
	test_device_t* device;
	sk_blas_desc_t desc;
} test_blas_t;

typedef struct test_tlas_t {
	test_device_t* device;
	sk_tlas_desc_t desc;
	u32 instance_count;
} test_tlas_t;

typedef struct test_swapchain_t {
	test_device_t* device;
	sk_swapchain_desc_t desc;
	sk_extent3d_t extent;
	sk_pixel_format_t format;
	test_texture_list_t textures;
	u32 image_index;
} test_swapchain_t;

typedef struct test_queue_t {
	test_device_t* device;
	sk_queue_desc_t desc;
	u32 submit_count;
} test_queue_t;

typedef struct test_fence_t {
	test_device_t* device;
	bool signaled;
} test_fence_t;

typedef struct test_semaphore_t {
	test_device_t* device;
} test_semaphore_t;

typedef struct test_command_stats_t {
	u32 draw_count;
	u32 dispatch_count;
	u32 trace_rays_count;
	u32 render_pass_count;
	u32 copy_count;
	u32 clear_count;
	u32 texture_barrier_count;
	u32 buffer_barrier_count;
	u32 memory_barrier_count;
	u32 bind_descriptor_set_count;
} test_command_stats_t;

typedef struct test_command_buffer_t {
	test_device_t* device;
	sk_command_buffer_desc_t desc;
	test_command_stats_t stats;
	bool recording;
	bool render_pass_active;
	sk_pipeline_t bound_pipeline;
} test_command_buffer_t;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static test_device_t* test_device(sk_render_device_t dev) {
	return (test_device_t*)sk_render_device_t_to_ptr(dev);
}

static void test_array_push(const sk_allocator_t* allocator, test_array_t* arr, void* item) {
	if (arr->count == arr->capacity) {
		u32 new_capacity = arr->capacity == 0u ? 8u : arr->capacity * 2u;
		void** grown = (void**)allocator->realloc(allocator->instance, arr->data, (size_t)new_capacity * sizeof(void*));
		if (grown == NULL) {
			return;
		}
		arr->data = grown;
		arr->capacity = new_capacity;
	}
	arr->data[arr->count++] = item;
}

static void test_array_remove(test_array_t* arr, void* item) {
	for (u32 i = 0u; i < arr->count; ++i) {
		if (arr->data[i] == item) {
			--arr->count;
			arr->data[i] = arr->data[arr->count];
			return;
		}
	}
}

static void test_array_free(const sk_allocator_t* allocator, test_array_t* arr) {
	allocator->free(allocator->instance, arr->data);
	arr->data = NULL;
	arr->count = 0u;
	arr->capacity = 0u;
}

static void test_texture_list_push(const sk_allocator_t* allocator, test_texture_list_t* list, sk_texture_t tex) {
	if (list->count == list->capacity) {
		u32 new_capacity = list->capacity == 0u ? 4u : list->capacity * 2u;
		sk_texture_t* grown = (sk_texture_t*)allocator->realloc(allocator->instance, list->data, (size_t)new_capacity * sizeof(sk_texture_t));
		if (grown == NULL) {
			return;
		}
		list->data = grown;
		list->capacity = new_capacity;
	}
	list->data[list->count++] = tex;
}

static void test_texture_list_free(const sk_allocator_t* allocator, test_texture_list_t* list) {
	allocator->free(allocator->instance, list->data);
	list->data = NULL;
	list->count = 0u;
	list->capacity = 0u;
}

/* Bytes per texel (BC/ETC/ASTC report per-block size, matching main). */
static u32 test_format_size(sk_pixel_format_t format) {
	switch (format) {
	case SK_PIXEL_FORMAT_R8_UNORM:
	case SK_PIXEL_FORMAT_R8_SNORM:
	case SK_PIXEL_FORMAT_R8_UINT:
	case SK_PIXEL_FORMAT_R8_SINT:
	case SK_PIXEL_FORMAT_R8_SRGB:
		return 1u;

	case SK_PIXEL_FORMAT_R16_UNORM:
	case SK_PIXEL_FORMAT_R16_SNORM:
	case SK_PIXEL_FORMAT_R16_UINT:
	case SK_PIXEL_FORMAT_R16_SINT:
	case SK_PIXEL_FORMAT_R16_FLOAT:
	case SK_PIXEL_FORMAT_RG8_UNORM:
	case SK_PIXEL_FORMAT_RG8_SNORM:
	case SK_PIXEL_FORMAT_RG8_UINT:
	case SK_PIXEL_FORMAT_RG8_SINT:
	case SK_PIXEL_FORMAT_RG8_SRGB:
	case SK_PIXEL_FORMAT_D16_UNORM:
		return 2u;

	case SK_PIXEL_FORMAT_RGB16_UNORM:
	case SK_PIXEL_FORMAT_RGB16_SNORM:
	case SK_PIXEL_FORMAT_RGB16_UINT:
	case SK_PIXEL_FORMAT_RGB16_SINT:
	case SK_PIXEL_FORMAT_RGB16_FLOAT:
	case SK_PIXEL_FORMAT_R32_UINT:
	case SK_PIXEL_FORMAT_R32_SINT:
	case SK_PIXEL_FORMAT_R32_FLOAT:
	case SK_PIXEL_FORMAT_RG16_UNORM:
	case SK_PIXEL_FORMAT_RG16_SNORM:
	case SK_PIXEL_FORMAT_RG16_UINT:
	case SK_PIXEL_FORMAT_RG16_SINT:
	case SK_PIXEL_FORMAT_RG16_FLOAT:
	case SK_PIXEL_FORMAT_RGB10A2_UNORM:
	case SK_PIXEL_FORMAT_RGB10A2_UINT:
	case SK_PIXEL_FORMAT_RG11B10_FLOAT:
	case SK_PIXEL_FORMAT_RGB9E5_FLOAT:
	case SK_PIXEL_FORMAT_D24_UNORM_S8_UINT:
	case SK_PIXEL_FORMAT_RGBA8_UNORM:
	case SK_PIXEL_FORMAT_RGBA8_SNORM:
	case SK_PIXEL_FORMAT_RGBA8_UINT:
	case SK_PIXEL_FORMAT_RGBA8_SINT:
	case SK_PIXEL_FORMAT_RGBA8_SRGB:
	case SK_PIXEL_FORMAT_BGRA8_UNORM:
	case SK_PIXEL_FORMAT_BGRA8_SNORM:
	case SK_PIXEL_FORMAT_BGRA8_UINT:
	case SK_PIXEL_FORMAT_BGRA8_SINT:
	case SK_PIXEL_FORMAT_BGRA8_SRGB:
	case SK_PIXEL_FORMAT_UNKNOWN:
		return 4u;

	case SK_PIXEL_FORMAT_RG32_UINT:
	case SK_PIXEL_FORMAT_RG32_SINT:
	case SK_PIXEL_FORMAT_RG32_FLOAT:
	case SK_PIXEL_FORMAT_RGBA16_UNORM:
	case SK_PIXEL_FORMAT_RGBA16_SNORM:
	case SK_PIXEL_FORMAT_RGBA16_UINT:
	case SK_PIXEL_FORMAT_RGBA16_SINT:
	case SK_PIXEL_FORMAT_RGBA16_FLOAT:
	case SK_PIXEL_FORMAT_D32_FLOAT:
	case SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT:
		return 8u;

	case SK_PIXEL_FORMAT_RGB32_UINT:
	case SK_PIXEL_FORMAT_RGB32_SINT:
	case SK_PIXEL_FORMAT_RGB32_FLOAT:
		return 12u;

	case SK_PIXEL_FORMAT_RGBA32_UINT:
	case SK_PIXEL_FORMAT_RGBA32_SINT:
	case SK_PIXEL_FORMAT_RGBA32_FLOAT:
		return 16u;

	/* 4x4 block formats (8 bytes per block). */
	case SK_PIXEL_FORMAT_BC1_UNORM:
	case SK_PIXEL_FORMAT_BC1_SRGB:
	case SK_PIXEL_FORMAT_BC4_UNORM:
	case SK_PIXEL_FORMAT_BC4_SNORM:
	case SK_PIXEL_FORMAT_ETC1_UNORM:
	case SK_PIXEL_FORMAT_ETC2_UNORM:
	case SK_PIXEL_FORMAT_ETC2_SRGB:
		return 8u;

	/* 4x4 block formats (16 bytes per block). */
	case SK_PIXEL_FORMAT_BC2_UNORM:
	case SK_PIXEL_FORMAT_BC2_SRGB:
	case SK_PIXEL_FORMAT_BC3_UNORM:
	case SK_PIXEL_FORMAT_BC3_SRGB:
	case SK_PIXEL_FORMAT_BC5_UNORM:
	case SK_PIXEL_FORMAT_BC5_SNORM:
	case SK_PIXEL_FORMAT_BC6H_UF16:
	case SK_PIXEL_FORMAT_BC6H_SF16:
	case SK_PIXEL_FORMAT_BC7_UNORM:
	case SK_PIXEL_FORMAT_BC7_SRGB:
	case SK_PIXEL_FORMAT_ETC2A_UNORM:
	case SK_PIXEL_FORMAT_ETC2A_SRGB:
	case SK_PIXEL_FORMAT_ASTC_4x4_UNORM:
	case SK_PIXEL_FORMAT_ASTC_4x4_SRGB:
	case SK_PIXEL_FORMAT_ASTC_5x4_UNORM:
	case SK_PIXEL_FORMAT_ASTC_5x4_SRGB:
	case SK_PIXEL_FORMAT_ASTC_5x5_UNORM:
	case SK_PIXEL_FORMAT_ASTC_5x5_SRGB:
	case SK_PIXEL_FORMAT_ASTC_6x5_UNORM:
	case SK_PIXEL_FORMAT_ASTC_6x5_SRGB:
	case SK_PIXEL_FORMAT_ASTC_6x6_UNORM:
	case SK_PIXEL_FORMAT_ASTC_6x6_SRGB:
	case SK_PIXEL_FORMAT_ASTC_8x5_UNORM:
	case SK_PIXEL_FORMAT_ASTC_8x5_SRGB:
	case SK_PIXEL_FORMAT_ASTC_8x6_UNORM:
	case SK_PIXEL_FORMAT_ASTC_8x6_SRGB:
	case SK_PIXEL_FORMAT_ASTC_8x8_UNORM:
	case SK_PIXEL_FORMAT_ASTC_8x8_SRGB:
	case SK_PIXEL_FORMAT_ASTC_10x5_UNORM:
	case SK_PIXEL_FORMAT_ASTC_10x5_SRGB:
	case SK_PIXEL_FORMAT_ASTC_10x6_UNORM:
	case SK_PIXEL_FORMAT_ASTC_10x6_SRGB:
	case SK_PIXEL_FORMAT_ASTC_10x8_UNORM:
	case SK_PIXEL_FORMAT_ASTC_10x8_SRGB:
	case SK_PIXEL_FORMAT_ASTC_10x10_UNORM:
	case SK_PIXEL_FORMAT_ASTC_10x10_SRGB:
	case SK_PIXEL_FORMAT_ASTC_12x10_UNORM:
	case SK_PIXEL_FORMAT_ASTC_12x10_SRGB:
	case SK_PIXEL_FORMAT_ASTC_12x12_UNORM:
	case SK_PIXEL_FORMAT_ASTC_12x12_SRGB:
		return 16u;

	default:
		return 4u;
	}
}

static sk_texture_view_type_t test_texture_view_type(const sk_texture_desc_t* desc) {
	if (desc->cubemap) {
		return desc->array_layers > 1u ? SK_TEXTURE_VIEW_TYPE_CUBE_ARRAY : SK_TEXTURE_VIEW_TYPE_CUBE;
	}
	if (desc->extent.depth > 1u) {
		return SK_TEXTURE_VIEW_TYPE_3D;
	}
	if (desc->array_layers > 1u) {
		return SK_TEXTURE_VIEW_TYPE_2D_ARRAY;
	}
	if (desc->extent.height > 1u) {
		return SK_TEXTURE_VIEW_TYPE_2D;
	}
	return SK_TEXTURE_VIEW_TYPE_1D;
}

static u32 test_texture_subresource_index(const test_texture_t* texture, u32 mip_level, u32 array_layer) {
	return mip_level * texture->array_layers + array_layer;
}

/* Apply a recorded barrier to the tracked subresource layout. */
static void test_texture_apply_barrier(test_texture_t* texture, sk_resource_state_t old_state, sk_resource_state_t new_state, u32 base_mip_level, u32 level_count,
									   u32 base_array_layer, u32 layer_count) {
	if (level_count == UINT32_MAX || base_mip_level + level_count > texture->mip_levels) {
		level_count = texture->mip_levels - base_mip_level;
	}
	if (layer_count == UINT32_MAX || base_array_layer + layer_count > texture->array_layers) {
		layer_count = texture->array_layers - base_array_layer;
	}

	for (u32 mip = base_mip_level; mip < base_mip_level + level_count; ++mip) {
		for (u32 layer = base_array_layer; layer < base_array_layer + layer_count; ++layer) {
			u32 index = test_texture_subresource_index(texture, mip, layer);
			/* Undefined as a source means "discard": any tracked state is valid to come from. */
			if (old_state != SK_RESOURCE_STATE_UNDEFINED && texture->subresource_states[index] != old_state) {
				++texture->mismatch_count;
			}
			texture->subresource_states[index] = new_state;
		}
	}

	if (texture->barrier_count == texture->barrier_capacity) {
		u32 new_capacity = texture->barrier_capacity == 0u ? 8u : texture->barrier_capacity * 2u;
		test_barrier_record_t* grown = (test_barrier_record_t*)texture->device->allocator->realloc(texture->device->allocator->instance, texture->barrier_history,
																								   (size_t)new_capacity * sizeof(test_barrier_record_t));
		if (grown != NULL) {
			texture->barrier_history = grown;
			texture->barrier_capacity = new_capacity;
		}
	}
	if (texture->barrier_count < texture->barrier_capacity) {
		test_barrier_record_t* record = &texture->barrier_history[texture->barrier_count++];
		record->old_state = old_state;
		record->new_state = new_state;
		record->base_mip_level = base_mip_level;
		record->level_count = level_count;
		record->base_array_layer = base_array_layer;
		record->layer_count = layer_count;
	}
}

/* Internal texture view create (used for the auto-view on create_texture). */
static sk_texture_view_t test_create_texture_view(test_device_t* device, const sk_texture_view_desc_t* desc) {
	test_texture_view_t* view = (test_texture_view_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_texture_view_t));
	if (view == NULL) {
		return sk_texture_view_t_zero();
	}
	view->device = device;
	view->desc = *desc;
	test_array_push(device->allocator, &device->texture_views, view);
	return sk_texture_view_t_from_ptr(view);
}

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static sk_render_device_t sk_trd_init(void_ptr_t context, const sk_device_init_desc_t* desc);
static void sk_trd_destroy(sk_render_device_t dev);
static i32 sk_trd_wait_idle(sk_render_device_t dev);

static u32 sk_trd_get_adapter_count(sk_render_device_t dev);
static sk_adapter_t sk_trd_get_adapter(sk_render_device_t dev, u32 index);
static i32 sk_trd_select_adapter(sk_render_device_t dev, sk_adapter_t adapter);
static u32 sk_trd_get_adapter_score(sk_render_device_t dev, sk_adapter_t adapter);
static const_chr_t sk_trd_get_adapter_name(sk_render_device_t dev, sk_adapter_t adapter);
static sk_device_properties_t sk_trd_get_properties(sk_render_device_t dev);
static sk_device_features_t sk_trd_get_features(sk_render_device_t dev);
static sk_graphics_api_t sk_trd_get_api(sk_render_device_t dev);
static u32 sk_trd_get_memory_budgets(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count);

static sk_buffer_t sk_trd_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc);
static void sk_trd_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf);
static void* sk_trd_buffer_map(sk_render_device_t dev, sk_buffer_t buf);
static void sk_trd_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf);
static sk_buffer_desc_t sk_trd_get_buffer_desc(sk_render_device_t dev, sk_buffer_t buf);
static void_ptr_t sk_trd_get_buffer_mapped_data(sk_render_device_t dev, sk_buffer_t buf);

static sk_texture_t sk_trd_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc);
static void sk_trd_destroy_texture(sk_render_device_t dev, sk_texture_t tex);
static sk_texture_view_t sk_trd_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc);
static void sk_trd_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view);
static sk_sampler_t sk_trd_create_sampler(sk_render_device_t dev, const sk_sampler_desc_t* desc);
static void sk_trd_destroy_sampler(sk_render_device_t dev, sk_sampler_t sampler);
static sk_texture_desc_t sk_trd_get_texture_desc(sk_render_device_t dev, sk_texture_t tex);
static sk_texture_view_desc_t sk_trd_get_texture_view_desc(sk_render_device_t dev, sk_texture_view_t view);
static sk_sampler_desc_t sk_trd_get_sampler_desc(sk_render_device_t dev, sk_sampler_t sampler);

static sk_memory_t sk_trd_create_memory(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits);
static void sk_trd_destroy_memory(sk_render_device_t dev, sk_memory_t mem);
static sk_texture_t sk_trd_create_aliased_texture(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset);
static u64 sk_trd_get_memory_size(sk_render_device_t dev, sk_memory_t mem);

static sk_shader_t sk_trd_create_shader(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage);
static void sk_trd_destroy_shader(sk_render_device_t dev, sk_shader_t shdr);

static sk_pipeline_t sk_trd_create_graphics_pipeline(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc);
static sk_pipeline_t sk_trd_create_compute_pipeline(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc);
static sk_pipeline_t sk_trd_create_ray_tracing_pipeline(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc);
static void sk_trd_destroy_pipeline(sk_render_device_t dev, sk_pipeline_t pipeline);
static sk_pipeline_desc_t sk_trd_get_pipeline_desc(sk_render_device_t dev, sk_pipeline_t pipeline);
static sk_pipeline_bind_point_t sk_trd_get_pipeline_bind_point(sk_render_device_t dev, sk_pipeline_t pipeline);

static sk_descriptor_set_t sk_trd_create_descriptor_set(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc);
static void sk_trd_update_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set, const sk_descriptor_write_t* writes, u32 write_count);
static void sk_trd_destroy_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set);
static sk_descriptor_set_desc_t sk_trd_get_descriptor_set_desc(sk_render_device_t dev, sk_descriptor_set_t set);

static sk_render_pass_t sk_trd_create_render_pass(sk_render_device_t dev, const sk_render_pass_desc_t* desc);
static void sk_trd_destroy_render_pass(sk_render_device_t dev, sk_render_pass_t pass);
static sk_framebuffer_t sk_trd_create_framebuffer(sk_render_device_t dev, const sk_framebuffer_desc_t* desc);
static void sk_trd_destroy_framebuffer(sk_render_device_t dev, sk_framebuffer_t fb);
static sk_render_pass_desc_t sk_trd_get_render_pass_desc(sk_render_device_t dev, sk_render_pass_t pass);
static sk_framebuffer_desc_t sk_trd_get_framebuffer_desc(sk_render_device_t dev, sk_framebuffer_t fb);
static sk_extent3d_t sk_trd_get_framebuffer_extent(sk_render_device_t dev, sk_framebuffer_t fb);

static sk_swapchain_t sk_trd_create_swapchain(sk_render_device_t dev, const sk_swapchain_desc_t* desc);
static void sk_trd_destroy_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain);
static i32 sk_trd_resize_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain, u32 width, u32 height);
static sk_device_result_t sk_trd_acquire_next_image(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index);
static sk_texture_t sk_trd_get_swapchain_image(sk_render_device_t dev, sk_swapchain_t swapchain, u32 image_index);
static sk_extent3d_t sk_trd_get_swapchain_extent(sk_render_device_t dev, sk_swapchain_t swapchain);
static sk_pixel_format_t sk_trd_get_swapchain_format(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_trd_get_swapchain_image_count(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_trd_get_swapchain_current_image_index(sk_render_device_t dev, sk_swapchain_t swapchain);
static u32 sk_trd_get_swapchain_textures(sk_render_device_t dev, sk_swapchain_t swapchain, sk_texture_t* out_textures, u32 max_count);
static sk_device_result_t sk_trd_present(sk_render_device_t dev, sk_queue_t queue, const sk_present_info_t* info);

static sk_fence_t sk_trd_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc);
static void sk_trd_destroy_fence(sk_render_device_t dev, sk_fence_t fence);
static i32 sk_trd_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns);
static void sk_trd_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count);
static sk_semaphore_t sk_trd_create_semaphore(sk_render_device_t dev, const sk_semaphore_desc_t* desc);
static void sk_trd_destroy_semaphore(sk_render_device_t dev, sk_semaphore_t semaphore);

static sk_command_buffer_t sk_trd_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc);
static void sk_trd_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
static i32 sk_trd_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info);
static void sk_trd_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_reset_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_execute_commands(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_t* secondary_cmds, u32 secondary_count);

static void sk_trd_set_viewport(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count);
static void sk_trd_set_scissor(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count);
static void sk_trd_set_blend_constants(sk_render_device_t dev, sk_command_buffer_t cmd, f32 r, f32 g, f32 b, f32 a);
static void sk_trd_set_stencil_reference(sk_render_device_t dev, sk_command_buffer_t cmd, u32 reference);
static void sk_trd_set_depth_bias(sk_render_device_t dev, sk_command_buffer_t cmd, f32 constant_factor, f32 clamp, f32 slope_factor);
static void sk_trd_set_depth_bounds(sk_render_device_t dev, sk_command_buffer_t cmd, f32 min_depth, f32 max_depth);
static void sk_trd_bind_pipeline(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline);
static void sk_trd_bind_descriptor_set(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline, u32 set_index,
									   sk_descriptor_set_t desc_set, const u32* dynamic_offsets, u32 offset_count);
static void sk_trd_bind_vertex_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count);
static void sk_trd_bind_index_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, sk_index_type_t index_type);
static void sk_trd_push_constants(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_t pipeline, u32 shader_stages, u32 offset, u32 size, const void* data);

static void sk_trd_draw(sk_render_device_t dev, sk_command_buffer_t cmd, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
static void sk_trd_draw_indexed(sk_render_device_t dev, sk_command_buffer_t cmd, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance);
static void sk_trd_draw_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
static void sk_trd_draw_indexed_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride);
static void sk_trd_draw_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
									   u32 max_draw_count, u32 stride);
static void sk_trd_draw_indexed_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
											   u32 max_draw_count, u32 stride);
static void sk_trd_dispatch(sk_render_device_t dev, sk_command_buffer_t cmd, u32 group_count_x, u32 group_count_y, u32 group_count_z);
static void sk_trd_dispatch_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset);
static void sk_trd_trace_rays(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_trace_rays_info_t* info);

static void sk_trd_begin_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_begin_render_pass_info_t* info);
static void sk_trd_end_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_clear_attachments(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_clear_attachment_t* attachments, u32 attachment_count);

static void sk_trd_copy_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t src, sk_buffer_t dst, u64 size, u64 src_offset, u64 dst_offset);
static void sk_trd_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
static void sk_trd_copy_texture_to_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info);
static void sk_trd_copy_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_copy_t* copy_info);
static void sk_trd_blit_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_blit_t* blit_info);
static void sk_trd_resolve_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_resolve_t* resolve_info);
static void sk_trd_update_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, const void* data);
static void sk_trd_fill_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, u32 data);
static void sk_trd_clear_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
								 u32 base_array_layer, u32 array_layer_count);

static void sk_trd_resource_barrier_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, sk_resource_state_t old_state, sk_resource_state_t new_state,
										   u32 src_scope, u32 dst_scope);
static void sk_trd_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
											u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope);
static void sk_trd_resource_barrier_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, sk_resource_state_t old_state, sk_resource_state_t new_state,
													u32 src_scope, u32 dst_scope);
static void sk_trd_resource_barrier_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, sk_resource_state_t old_state, sk_resource_state_t new_state,
												 u32 src_scope, u32 dst_scope);
static void sk_trd_memory_barrier(sk_render_device_t dev, sk_command_buffer_t cmd);

static void sk_trd_begin_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);
static void sk_trd_end_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd);
static void sk_trd_insert_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a);

static sk_query_pool_t sk_trd_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc);
static void sk_trd_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool);
static void sk_trd_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count);
static void sk_trd_begin_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
static void sk_trd_end_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
static void sk_trd_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query);
static void sk_trd_copy_query_pool_results(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count, sk_buffer_t dst_buffer,
										   u64 dst_offset, u64 stride);
static i32 sk_trd_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait);

static sk_blas_t sk_trd_create_bottom_level_as(sk_render_device_t dev, const sk_blas_desc_t* desc);
static void sk_trd_destroy_bottom_level_as(sk_render_device_t dev, sk_blas_t blas);
static sk_tlas_t sk_trd_create_top_level_as(sk_render_device_t dev, const sk_tlas_desc_t* desc);
static void sk_trd_destroy_top_level_as(sk_render_device_t dev, sk_tlas_t tlas);
static sk_as_build_sizes_t sk_trd_get_blas_build_sizes(sk_render_device_t dev, const sk_blas_desc_t* desc);
static sk_as_build_sizes_t sk_trd_get_tlas_build_sizes(sk_render_device_t dev, const sk_tlas_desc_t* desc);
static void sk_trd_build_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, const sk_blas_desc_t* desc, const sk_as_build_info_t* build_info);
static void sk_trd_build_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info);
static bool sk_trd_is_bottom_level_as_compacted(sk_render_device_t dev, sk_blas_t blas);
static u64 sk_trd_get_bottom_level_as_compacted_size(sk_render_device_t dev, sk_blas_t blas);
static sk_blas_desc_t sk_trd_get_blas_desc(sk_render_device_t dev, sk_blas_t blas);
static sk_tlas_desc_t sk_trd_get_tlas_desc(sk_render_device_t dev, sk_tlas_t tlas);
static void sk_trd_copy_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t src, sk_blas_t dst, bool compress);
static void sk_trd_copy_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t src, sk_tlas_t dst, bool compress);
static bool sk_trd_update_top_level_as_instances(sk_render_device_t dev, sk_tlas_t tlas, const sk_as_instance_desc_t* instances, u32 instance_count);
static void sk_trd_update_top_level_as_instance(sk_render_device_t dev, sk_tlas_t tlas, u32 index, const sk_as_instance_desc_t* instance);
static void sk_trd_set_top_level_as_instance_count(sk_render_device_t dev, sk_tlas_t tlas, u32 count);

static sk_queue_t sk_trd_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc);
static void sk_trd_destroy_queue(sk_render_device_t dev, sk_queue_t queue);
static void sk_trd_submit_command_buffer(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
static i32 sk_trd_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* info);
static i32 sk_trd_submit_and_wait(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd);
static i32 sk_trd_queue_wait_idle(sk_render_device_t dev, sk_queue_t queue);

static sk_texture_memory_requirements_t sk_trd_get_texture_memory_requirements(sk_render_device_t dev, const sk_texture_desc_t* desc);
static sk_buffer_memory_requirements_t sk_trd_get_buffer_memory_requirements(sk_render_device_t dev, const sk_buffer_desc_t* desc);

/* ------------------------------------------------------------------ */
/* API table                                                           */
/* ------------------------------------------------------------------ */

static const sk_render_device_api_t test_render_device_api = {
	/* Device lifecycle */
	sk_trd_init,
	sk_trd_destroy,
	sk_trd_wait_idle,

	/* Adapter / device queries */
	sk_trd_get_adapter_count,
	sk_trd_get_adapter,
	sk_trd_select_adapter,
	sk_trd_get_adapter_score,
	sk_trd_get_adapter_name,
	sk_trd_get_properties,
	sk_trd_get_features,
	sk_trd_get_api,
	sk_trd_get_memory_budgets,

	/* Buffer */
	sk_trd_create_buffer,
	sk_trd_destroy_buffer,
	sk_trd_buffer_map,
	sk_trd_buffer_unmap,
	sk_trd_get_buffer_desc,
	sk_trd_get_buffer_mapped_data,

	/* Texture */
	sk_trd_create_texture,
	sk_trd_destroy_texture,
	sk_trd_create_texture_view,
	sk_trd_destroy_texture_view,
	sk_trd_create_sampler,
	sk_trd_destroy_sampler,
	sk_trd_get_texture_desc,
	sk_trd_get_texture_view_desc,
	sk_trd_get_sampler_desc,

	/* Memory */
	sk_trd_create_memory,
	sk_trd_destroy_memory,
	sk_trd_create_aliased_texture,
	sk_trd_get_memory_size,

	/* Shader */
	sk_trd_create_shader,
	sk_trd_destroy_shader,

	/* Pipeline */
	sk_trd_create_graphics_pipeline,
	sk_trd_create_compute_pipeline,
	sk_trd_create_ray_tracing_pipeline,
	sk_trd_destroy_pipeline,
	sk_trd_get_pipeline_desc,
	sk_trd_get_pipeline_bind_point,

	/* Descriptor set */
	sk_trd_create_descriptor_set,
	sk_trd_update_descriptor_set,
	sk_trd_destroy_descriptor_set,
	sk_trd_get_descriptor_set_desc,

	/* Render pass / framebuffer */
	sk_trd_create_render_pass,
	sk_trd_destroy_render_pass,
	sk_trd_create_framebuffer,
	sk_trd_destroy_framebuffer,
	sk_trd_get_render_pass_desc,
	sk_trd_get_framebuffer_desc,
	sk_trd_get_framebuffer_extent,

	/* Swapchain */
	sk_trd_create_swapchain,
	sk_trd_destroy_swapchain,
	sk_trd_resize_swapchain,
	sk_trd_acquire_next_image,
	sk_trd_get_swapchain_image,
	sk_trd_get_swapchain_extent,
	sk_trd_get_swapchain_format,
	sk_trd_get_swapchain_image_count,
	sk_trd_get_swapchain_current_image_index,
	sk_trd_get_swapchain_textures,
	sk_trd_present,

	/* Sync */
	sk_trd_create_fence,
	sk_trd_destroy_fence,
	sk_trd_wait_fences,
	sk_trd_reset_fences,
	sk_trd_create_semaphore,
	sk_trd_destroy_semaphore,

	/* Command buffer lifecycle */
	sk_trd_create_command_buffer,
	sk_trd_destroy_command_buffer,
	sk_trd_begin_command_buffer,
	sk_trd_end_command_buffer,
	sk_trd_reset_command_buffer,
	sk_trd_execute_commands,

	/* State */
	sk_trd_set_viewport,
	sk_trd_set_scissor,
	sk_trd_set_blend_constants,
	sk_trd_set_stencil_reference,
	sk_trd_set_depth_bias,
	sk_trd_set_depth_bounds,
	sk_trd_bind_pipeline,
	sk_trd_bind_descriptor_set,
	sk_trd_bind_vertex_buffer,
	sk_trd_bind_index_buffer,
	sk_trd_push_constants,

	/* Draw / dispatch */
	sk_trd_draw,
	sk_trd_draw_indexed,
	sk_trd_draw_indirect,
	sk_trd_draw_indexed_indirect,
	sk_trd_draw_indirect_count,
	sk_trd_draw_indexed_indirect_count,
	sk_trd_dispatch,
	sk_trd_dispatch_indirect,
	sk_trd_trace_rays,

	/* Render pass cmds */
	sk_trd_begin_render_pass,
	sk_trd_end_render_pass,
	sk_trd_clear_attachments,

	/* Transfer */
	sk_trd_copy_buffer,
	sk_trd_copy_buffer_to_texture,
	sk_trd_copy_texture_to_buffer,
	sk_trd_copy_texture,
	sk_trd_blit_texture,
	sk_trd_resolve_texture,
	sk_trd_update_buffer,
	sk_trd_fill_buffer,
	sk_trd_clear_texture,

	/* Barriers */
	sk_trd_resource_barrier_buffer,
	sk_trd_resource_barrier_texture,
	sk_trd_resource_barrier_bottom_level_as,
	sk_trd_resource_barrier_top_level_as,
	sk_trd_memory_barrier,

	/* Debug labels */
	sk_trd_begin_debug_label,
	sk_trd_end_debug_label,
	sk_trd_insert_debug_label,

	/* Queries */
	sk_trd_create_query_pool,
	sk_trd_destroy_query_pool,
	sk_trd_reset_query_pool,
	sk_trd_begin_query,
	sk_trd_end_query,
	sk_trd_write_timestamp,
	sk_trd_copy_query_pool_results,
	sk_trd_get_query_pool_results,

	/* Acceleration structures */
	sk_trd_create_bottom_level_as,
	sk_trd_destroy_bottom_level_as,
	sk_trd_create_top_level_as,
	sk_trd_destroy_top_level_as,
	sk_trd_get_blas_build_sizes,
	sk_trd_get_tlas_build_sizes,
	sk_trd_build_bottom_level_as,
	sk_trd_build_top_level_as,
	sk_trd_is_bottom_level_as_compacted,
	sk_trd_get_bottom_level_as_compacted_size,
	sk_trd_get_blas_desc,
	sk_trd_get_tlas_desc,
	sk_trd_copy_bottom_level_as,
	sk_trd_copy_top_level_as,
	sk_trd_update_top_level_as_instances,
	sk_trd_update_top_level_as_instance,
	sk_trd_set_top_level_as_instance_count,

	/* Queue */
	sk_trd_create_queue,
	sk_trd_destroy_queue,
	sk_trd_submit_command_buffer,
	sk_trd_submit,
	sk_trd_submit_and_wait,
	sk_trd_queue_wait_idle,

	/* Device query */
	sk_trd_get_texture_memory_requirements,
	sk_trd_get_buffer_memory_requirements,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void sk_test_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_RENDER_DEVICE_API_TYPE_ID, &test_render_device_api);
}

/* ------------------------------------------------------------------ */
/* Device lifecycle                                                    */
/* ------------------------------------------------------------------ */

static sk_render_device_t sk_trd_init(void_ptr_t context, const sk_device_init_desc_t* desc) {
	(void)context;
	(void)desc;
	test_device_t* device = (test_device_t*)sk_allocator_default()->alloc(NULL, sizeof(test_device_t));
	if (device == NULL) {
		return sk_render_device_t_zero();
	}
	memset(device, 0, sizeof(*device));
	device->allocator = sk_allocator_default();

	sk_device_properties_t* props = &device->properties;
	props->device_type = SK_DEVICE_TYPE_OTHER;
	snprintf(props->device_name, sizeof(props->device_name), "%s", "TestRenderDevice");
	snprintf(props->vendor_name, sizeof(props->vendor_name), "%s", "Skore");
	snprintf(props->driver_version, sizeof(props->driver_version), "%s", "0.0.0");

	device->features.compute_shader = true;
	device->features.independent_blend = true;
	device->features.bindless_texture_supported = true;
	device->features.bindless_sampler_supported = true;
	device->features.bindless_buffer_supported = true;

	sk_device_limits_t* limits = &props->limits;
	limits->max_texture_size = 16384u;
	limits->max_texture_3d_size = 2048u;
	limits->max_cube_map_size = 16384u;
	limits->max_viewport_dimensions[0] = 16384u;
	limits->max_viewport_dimensions[1] = 16384u;
	limits->timestamp_period = 1.0f;

	props->features = device->features;
	return sk_render_device_t_from_ptr(device);
}

static void sk_trd_destroy(sk_render_device_t dev) {
	test_device_t* device = test_device(dev);
	if (device == NULL) {
		return;
	}
	const sk_allocator_t* allocator = device->allocator;

	for (u32 i = 0u; i < device->command_buffers.count; ++i) {
		allocator->free(allocator->instance, device->command_buffers.data[i]);
	}
	for (u32 i = 0u; i < device->queues.count; ++i) {
		allocator->free(allocator->instance, device->queues.data[i]);
	}
	/* Swapchains own swap images (in the device texture pool); free first. */
	for (u32 i = 0u; i < device->swapchains.count; ++i) {
		test_swapchain_t* swapchain = (test_swapchain_t*)device->swapchains.data[i];
		for (u32 j = 0u; j < swapchain->textures.count; ++j) {
			sk_trd_destroy_texture(dev, swapchain->textures.data[j]);
		}
		test_texture_list_free(allocator, &swapchain->textures);
		allocator->free(allocator->instance, swapchain);
	}
	for (u32 i = 0u; i < device->textures.count; ++i) {
		test_texture_t* texture = (test_texture_t*)device->textures.data[i];
		allocator->free(allocator->instance, texture->subresource_states);
		allocator->free(allocator->instance, texture->barrier_history);
		allocator->free(allocator->instance, texture);
	}
	/* All remaining views (user-created and texture auto-views) are freed here. */
	for (u32 i = 0u; i < device->texture_views.count; ++i) {
		allocator->free(allocator->instance, device->texture_views.data[i]);
	}
	for (u32 i = 0u; i < device->memories.count; ++i) {
		allocator->free(allocator->instance, device->memories.data[i]);
	}
	for (u32 i = 0u; i < device->buffers.count; ++i) {
		test_buffer_t* buffer = (test_buffer_t*)device->buffers.data[i];
		allocator->free(allocator->instance, buffer->storage);
		allocator->free(allocator->instance, buffer);
	}
	for (u32 i = 0u; i < device->samplers.count; ++i) {
		allocator->free(allocator->instance, device->samplers.data[i]);
	}
	for (u32 i = 0u; i < device->shaders.count; ++i) {
		allocator->free(allocator->instance, device->shaders.data[i]);
	}
	for (u32 i = 0u; i < device->pipelines.count; ++i) {
		allocator->free(allocator->instance, device->pipelines.data[i]);
	}
	for (u32 i = 0u; i < device->descriptor_sets.count; ++i) {
		allocator->free(allocator->instance, device->descriptor_sets.data[i]);
	}
	for (u32 i = 0u; i < device->render_passes.count; ++i) {
		allocator->free(allocator->instance, device->render_passes.data[i]);
	}
	for (u32 i = 0u; i < device->framebuffers.count; ++i) {
		allocator->free(allocator->instance, device->framebuffers.data[i]);
	}
	for (u32 i = 0u; i < device->query_pools.count; ++i) {
		allocator->free(allocator->instance, device->query_pools.data[i]);
	}
	for (u32 i = 0u; i < device->blas_list.count; ++i) {
		allocator->free(allocator->instance, device->blas_list.data[i]);
	}
	for (u32 i = 0u; i < device->tlas_list.count; ++i) {
		allocator->free(allocator->instance, device->tlas_list.data[i]);
	}

	test_array_free(allocator, &device->command_buffers);
	test_array_free(allocator, &device->queues);
	test_array_free(allocator, &device->swapchains);
	test_array_free(allocator, &device->textures);
	test_array_free(allocator, &device->texture_views);
	test_array_free(allocator, &device->memories);
	test_array_free(allocator, &device->buffers);
	test_array_free(allocator, &device->samplers);
	test_array_free(allocator, &device->shaders);
	test_array_free(allocator, &device->pipelines);
	test_array_free(allocator, &device->descriptor_sets);
	test_array_free(allocator, &device->render_passes);
	test_array_free(allocator, &device->framebuffers);
	test_array_free(allocator, &device->query_pools);
	test_array_free(allocator, &device->blas_list);
	test_array_free(allocator, &device->tlas_list);

	allocator->free(allocator->instance, device);
}

static i32 sk_trd_wait_idle(sk_render_device_t dev) {
	(void)dev;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Adapter / device queries                                            */
/* ------------------------------------------------------------------ */

static u32 sk_trd_get_adapter_count(sk_render_device_t dev) {
	(void)dev;
	return 0u;
}

static sk_adapter_t sk_trd_get_adapter(sk_render_device_t dev, u32 index) {
	(void)dev;
	(void)index;
	return sk_adapter_t_zero();
}

static i32 sk_trd_select_adapter(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	(void)adapter;
	return 0;
}

static u32 sk_trd_get_adapter_score(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	(void)adapter;
	return 0u;
}

static const_chr_t sk_trd_get_adapter_name(sk_render_device_t dev, sk_adapter_t adapter) {
	(void)dev;
	(void)adapter;
	return "TestRenderDevice";
}

static sk_device_properties_t sk_trd_get_properties(sk_render_device_t dev) {
	return test_device(dev)->properties;
}

static sk_device_features_t sk_trd_get_features(sk_render_device_t dev) {
	return test_device(dev)->features;
}

static sk_graphics_api_t sk_trd_get_api(sk_render_device_t dev) {
	(void)dev;
	return SK_GRAPHICS_API_NONE;
}

static u32 sk_trd_get_memory_budgets(sk_render_device_t dev, sk_memory_heap_budget_t* out_budgets, u32 max_count) {
	(void)dev;
	if (out_budgets != NULL && max_count >= 1u) {
		out_budgets[0].usage = 0u;
		out_budgets[0].budget = (u64)1 << 30;
		out_budgets[0].device_local = true;
	}
	return 1u;
}

/* ------------------------------------------------------------------ */
/* Buffer                                                              */
/* ------------------------------------------------------------------ */

static sk_buffer_t sk_trd_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_buffer_t* buffer = (test_buffer_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_buffer_t));
	if (buffer == NULL) {
		return sk_buffer_t_zero();
	}
	memset(buffer, 0, sizeof(*buffer));
	buffer->device = device;
	buffer->desc = *desc;
	buffer->storage_size = desc->size;
	buffer->state = SK_RESOURCE_STATE_UNDEFINED;
	if (desc->size > 0u) {
		buffer->storage = (u8*)device->allocator->alloc(device->allocator->instance, (size_t)desc->size);
		if (buffer->storage == NULL) {
			device->allocator->free(device->allocator->instance, buffer);
			return sk_buffer_t_zero();
		}
		memset(buffer->storage, 0, (size_t)desc->size);
	}
	test_array_push(device->allocator, &device->buffers, buffer);
	return sk_buffer_t_from_ptr(buffer);
}

static void sk_trd_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf) {
	test_device_t* device = test_device(dev);
	test_buffer_t* buffer = (test_buffer_t*)sk_buffer_t_to_ptr(buf);
	device->allocator->free(device->allocator->instance, buffer->storage);
	test_array_remove(&device->buffers, buffer);
	device->allocator->free(device->allocator->instance, buffer);
}

static void* sk_trd_buffer_map(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	test_buffer_t* buffer = (test_buffer_t*)sk_buffer_t_to_ptr(buf);
	buffer->mapped = true;
	return buffer->storage;
}

static void sk_trd_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	test_buffer_t* buffer = (test_buffer_t*)sk_buffer_t_to_ptr(buf);
	buffer->mapped = false;
}

static sk_buffer_desc_t sk_trd_get_buffer_desc(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	return ((test_buffer_t*)sk_buffer_t_to_ptr(buf))->desc;
}

static void_ptr_t sk_trd_get_buffer_mapped_data(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	return ((test_buffer_t*)sk_buffer_t_to_ptr(buf))->storage;
}

/* ------------------------------------------------------------------ */
/* Texture                                                             */
/* ------------------------------------------------------------------ */

static sk_texture_t sk_trd_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_texture_t* texture = (test_texture_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_texture_t));
	if (texture == NULL) {
		return sk_texture_t_zero();
	}
	memset(texture, 0, sizeof(*texture));
	texture->device = device;
	texture->desc = *desc;
	texture->mip_levels = desc->mip_levels > 0u ? desc->mip_levels : 1u;
	texture->array_layers = desc->array_layers > 0u ? desc->array_layers : 1u;
	texture->subresource_count = texture->mip_levels * texture->array_layers;
	if (texture->subresource_count > 0u) {
		texture->subresource_states = (sk_resource_state_t*)device->allocator->alloc(device->allocator->instance, (size_t)texture->subresource_count * sizeof(sk_resource_state_t));
		if (texture->subresource_states == NULL) {
			device->allocator->free(device->allocator->instance, texture);
			return sk_texture_t_zero();
		}
		for (u32 i = 0u; i < texture->subresource_count; ++i) {
			texture->subresource_states[i] = SK_RESOURCE_STATE_UNDEFINED;
		}
	}
	test_array_push(device->allocator, &device->textures, texture);

	sk_texture_view_desc_t view_desc = {0};
	view_desc.texture = sk_texture_t_from_ptr(texture);
	view_desc.type = test_texture_view_type(desc);
	view_desc.mip_level_count = UINT32_MAX;
	view_desc.array_layer_count = UINT32_MAX;
	texture->texture_view = test_create_texture_view(device, &view_desc);
	return sk_texture_t_from_ptr(texture);
}

static void sk_trd_destroy_texture(sk_render_device_t dev, sk_texture_t tex) {
	test_device_t* device = test_device(dev);
	test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);
	if (sk_texture_view_t_is_valid(texture->texture_view)) {
		sk_trd_destroy_texture_view(dev, texture->texture_view);
	}
	device->allocator->free(device->allocator->instance, texture->subresource_states);
	device->allocator->free(device->allocator->instance, texture->barrier_history);
	test_array_remove(&device->textures, texture);
	device->allocator->free(device->allocator->instance, texture);
}

static sk_texture_view_t sk_trd_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc) {
	return test_create_texture_view(test_device(dev), desc);
}

static void sk_trd_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view) {
	test_device_t* device = test_device(dev);
	test_texture_view_t* texture_view = (test_texture_view_t*)sk_texture_view_t_to_ptr(view);
	/* If this is a texture's auto-view, clear the parent's back-reference. */
	sk_texture_t parent = texture_view->desc.texture;
	if (sk_texture_t_is_valid(parent)) {
		test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(parent);
		if (sk_texture_view_t_eq(texture->texture_view, view)) {
			texture->texture_view = sk_texture_view_t_zero();
		}
	}
	test_array_remove(&device->texture_views, texture_view);
	device->allocator->free(device->allocator->instance, texture_view);
}

static sk_sampler_t sk_trd_create_sampler(sk_render_device_t dev, const sk_sampler_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_sampler_t* sampler = (test_sampler_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_sampler_t));
	if (sampler == NULL) {
		return sk_sampler_t_zero();
	}
	sampler->device = device;
	sampler->desc = *desc;
	test_array_push(device->allocator, &device->samplers, sampler);
	return sk_sampler_t_from_ptr(sampler);
}

static void sk_trd_destroy_sampler(sk_render_device_t dev, sk_sampler_t sampler) {
	test_device_t* device = test_device(dev);
	test_sampler_t* sampler_obj = (test_sampler_t*)sk_sampler_t_to_ptr(sampler);
	test_array_remove(&device->samplers, sampler_obj);
	device->allocator->free(device->allocator->instance, sampler_obj);
}

static sk_texture_desc_t sk_trd_get_texture_desc(sk_render_device_t dev, sk_texture_t tex) {
	(void)dev;
	return ((test_texture_t*)sk_texture_t_to_ptr(tex))->desc;
}

static sk_texture_view_desc_t sk_trd_get_texture_view_desc(sk_render_device_t dev, sk_texture_view_t view) {
	(void)dev;
	return ((test_texture_view_t*)sk_texture_view_t_to_ptr(view))->desc;
}

static sk_sampler_desc_t sk_trd_get_sampler_desc(sk_render_device_t dev, sk_sampler_t sampler) {
	(void)dev;
	return ((test_sampler_t*)sk_sampler_t_to_ptr(sampler))->desc;
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

static sk_memory_t sk_trd_create_memory(sk_render_device_t dev, u64 size, u64 alignment, u32 memory_type_bits) {
	test_device_t* device = test_device(dev);
	test_memory_t* memory = (test_memory_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_memory_t));
	if (memory == NULL) {
		return sk_memory_t_zero();
	}
	memory->device = device;
	memory->size = size;
	memory->alignment = alignment;
	memory->memory_type_bits = memory_type_bits;
	test_array_push(device->allocator, &device->memories, memory);
	return sk_memory_t_from_ptr(memory);
}

static void sk_trd_destroy_memory(sk_render_device_t dev, sk_memory_t mem) {
	test_device_t* device = test_device(dev);
	test_memory_t* memory = (test_memory_t*)sk_memory_t_to_ptr(mem);
	test_array_remove(&device->memories, memory);
	device->allocator->free(device->allocator->instance, memory);
}

static sk_texture_t sk_trd_create_aliased_texture(sk_render_device_t dev, const sk_texture_desc_t* desc, sk_memory_t mem, u64 offset) {
	sk_texture_t tex = sk_trd_create_texture(dev, desc);
	if (sk_texture_t_is_valid(tex)) {
		test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);
		texture->alias_memory = (test_memory_t*)sk_memory_t_to_ptr(mem);
		texture->alias_offset = offset;
	}
	return tex;
}

static u64 sk_trd_get_memory_size(sk_render_device_t dev, sk_memory_t mem) {
	(void)dev;
	return ((test_memory_t*)sk_memory_t_to_ptr(mem))->size;
}

/* ------------------------------------------------------------------ */
/* Shader                                                              */
/* ------------------------------------------------------------------ */

static sk_shader_t sk_trd_create_shader(sk_render_device_t dev, const_chr_t src, u32 src_size, u32 shader_stage) {
	(void)src;
	(void)src_size;
	test_device_t* device = test_device(dev);
	test_shader_t* shader = (test_shader_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_shader_t));
	if (shader == NULL) {
		return sk_shader_t_zero();
	}
	shader->device = device;
	shader->stage = shader_stage;
	test_array_push(device->allocator, &device->shaders, shader);
	return sk_shader_t_from_ptr(shader);
}

static void sk_trd_destroy_shader(sk_render_device_t dev, sk_shader_t shdr) {
	test_device_t* device = test_device(dev);
	test_shader_t* shader = (test_shader_t*)sk_shader_t_to_ptr(shdr);
	test_array_remove(&device->shaders, shader);
	device->allocator->free(device->allocator->instance, shader);
}

/* ------------------------------------------------------------------ */
/* Pipeline                                                            */
/* ------------------------------------------------------------------ */

static sk_pipeline_t test_create_pipeline(test_device_t* device, sk_pipeline_bind_point_t bind_point, const sk_pipeline_desc_t* pipeline_desc) {
	test_pipeline_t* pipeline = (test_pipeline_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_pipeline_t));
	if (pipeline == NULL) {
		return sk_pipeline_t_zero();
	}
	pipeline->device = device;
	pipeline->bind_point = bind_point;
	if (pipeline_desc != NULL) {
		pipeline->pipeline_desc = *pipeline_desc;
	}
	test_array_push(device->allocator, &device->pipelines, pipeline);
	return sk_pipeline_t_from_ptr(pipeline);
}

static sk_pipeline_t sk_trd_create_graphics_pipeline(sk_render_device_t dev, const sk_graphics_pipeline_desc_t* desc) {
	return test_create_pipeline(test_device(dev), SK_PIPELINE_BIND_POINT_GRAPHICS, &desc->pipeline);
}

static sk_pipeline_t sk_trd_create_compute_pipeline(sk_render_device_t dev, const sk_compute_pipeline_desc_t* desc) {
	return test_create_pipeline(test_device(dev), SK_PIPELINE_BIND_POINT_COMPUTE, &desc->pipeline);
}

static sk_pipeline_t sk_trd_create_ray_tracing_pipeline(sk_render_device_t dev, const sk_ray_tracing_pipeline_desc_t* desc) {
	return test_create_pipeline(test_device(dev), SK_PIPELINE_BIND_POINT_RAY_TRACING, &desc->pipeline);
}

static void sk_trd_destroy_pipeline(sk_render_device_t dev, sk_pipeline_t pipeline) {
	test_device_t* device = test_device(dev);
	test_pipeline_t* pipeline_obj = (test_pipeline_t*)sk_pipeline_t_to_ptr(pipeline);
	test_array_remove(&device->pipelines, pipeline_obj);
	device->allocator->free(device->allocator->instance, pipeline_obj);
}

static sk_pipeline_desc_t sk_trd_get_pipeline_desc(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	return ((test_pipeline_t*)sk_pipeline_t_to_ptr(pipeline))->pipeline_desc;
}

static sk_pipeline_bind_point_t sk_trd_get_pipeline_bind_point(sk_render_device_t dev, sk_pipeline_t pipeline) {
	(void)dev;
	return ((test_pipeline_t*)sk_pipeline_t_to_ptr(pipeline))->bind_point;
}

/* ------------------------------------------------------------------ */
/* Descriptor set                                                      */
/* ------------------------------------------------------------------ */

static sk_descriptor_set_t sk_trd_create_descriptor_set(sk_render_device_t dev, const sk_descriptor_set_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_descriptor_set_t* set = (test_descriptor_set_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_descriptor_set_t));
	if (set == NULL) {
		return sk_descriptor_set_t_zero();
	}
	set->device = device;
	set->desc = *desc;
	test_array_push(device->allocator, &device->descriptor_sets, set);
	return sk_descriptor_set_t_from_ptr(set);
}

static void sk_trd_update_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set, const sk_descriptor_write_t* writes, u32 write_count) {
	(void)dev;
	(void)writes;
	(void)write_count;
	test_descriptor_set_t* set_obj = (test_descriptor_set_t*)sk_descriptor_set_t_to_ptr(set);
	++set_obj->update_count;
}

static void sk_trd_destroy_descriptor_set(sk_render_device_t dev, sk_descriptor_set_t set) {
	test_device_t* device = test_device(dev);
	test_descriptor_set_t* set_obj = (test_descriptor_set_t*)sk_descriptor_set_t_to_ptr(set);
	test_array_remove(&device->descriptor_sets, set_obj);
	device->allocator->free(device->allocator->instance, set_obj);
}

static sk_descriptor_set_desc_t sk_trd_get_descriptor_set_desc(sk_render_device_t dev, sk_descriptor_set_t set) {
	(void)dev;
	return ((test_descriptor_set_t*)sk_descriptor_set_t_to_ptr(set))->desc;
}

/* ------------------------------------------------------------------ */
/* Render pass / framebuffer                                           */
/* ------------------------------------------------------------------ */

static sk_render_pass_t sk_trd_create_render_pass(sk_render_device_t dev, const sk_render_pass_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_render_pass_t* pass = (test_render_pass_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_render_pass_t));
	if (pass == NULL) {
		return sk_render_pass_t_zero();
	}
	pass->device = device;
	pass->desc = *desc;
	test_array_push(device->allocator, &device->render_passes, pass);
	return sk_render_pass_t_from_ptr(pass);
}

static void sk_trd_destroy_render_pass(sk_render_device_t dev, sk_render_pass_t pass) {
	test_device_t* device = test_device(dev);
	test_render_pass_t* pass_obj = (test_render_pass_t*)sk_render_pass_t_to_ptr(pass);
	test_array_remove(&device->render_passes, pass_obj);
	device->allocator->free(device->allocator->instance, pass_obj);
}

static sk_framebuffer_t sk_trd_create_framebuffer(sk_render_device_t dev, const sk_framebuffer_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_framebuffer_t* framebuffer = (test_framebuffer_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_framebuffer_t));
	if (framebuffer == NULL) {
		return sk_framebuffer_t_zero();
	}
	framebuffer->device = device;
	framebuffer->desc = *desc;
	if (desc->attachment_count > 0u && desc->attachments != NULL && sk_texture_view_t_is_valid(desc->attachments[0])) {
		test_texture_view_t* view = (test_texture_view_t*)sk_texture_view_t_to_ptr(desc->attachments[0]);
		test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(view->desc.texture);
		framebuffer->extent = texture->desc.extent;
	} else {
		framebuffer->extent.width = 1u;
		framebuffer->extent.height = 1u;
		framebuffer->extent.depth = 1u;
	}
	test_array_push(device->allocator, &device->framebuffers, framebuffer);
	return sk_framebuffer_t_from_ptr(framebuffer);
}

static void sk_trd_destroy_framebuffer(sk_render_device_t dev, sk_framebuffer_t fb) {
	test_device_t* device = test_device(dev);
	test_framebuffer_t* framebuffer = (test_framebuffer_t*)sk_framebuffer_t_to_ptr(fb);
	test_array_remove(&device->framebuffers, framebuffer);
	device->allocator->free(device->allocator->instance, framebuffer);
}

static sk_render_pass_desc_t sk_trd_get_render_pass_desc(sk_render_device_t dev, sk_render_pass_t pass) {
	(void)dev;
	return ((test_render_pass_t*)sk_render_pass_t_to_ptr(pass))->desc;
}

static sk_framebuffer_desc_t sk_trd_get_framebuffer_desc(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	return ((test_framebuffer_t*)sk_framebuffer_t_to_ptr(fb))->desc;
}

static sk_extent3d_t sk_trd_get_framebuffer_extent(sk_render_device_t dev, sk_framebuffer_t fb) {
	(void)dev;
	return ((test_framebuffer_t*)sk_framebuffer_t_to_ptr(fb))->extent;
}

/* ------------------------------------------------------------------ */
/* Swapchain                                                           */
/* ------------------------------------------------------------------ */

static sk_swapchain_t sk_trd_create_swapchain(sk_render_device_t dev, const sk_swapchain_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_swapchain_t* swapchain = (test_swapchain_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_swapchain_t));
	if (swapchain == NULL) {
		return sk_swapchain_t_zero();
	}
	memset(swapchain, 0, sizeof(*swapchain));
	swapchain->device = device;
	swapchain->desc = *desc;
	swapchain->extent.width = desc->width > 0u ? desc->width : 1u;
	swapchain->extent.height = desc->height > 0u ? desc->height : 1u;
	swapchain->extent.depth = 1u;
	swapchain->format = desc->format != SK_PIXEL_FORMAT_UNKNOWN ? desc->format : SK_PIXEL_FORMAT_RGBA8_UNORM;
	test_array_push(device->allocator, &device->swapchains, swapchain);

	u32 image_count = desc->image_count > 0u ? desc->image_count : 2u;
	sk_texture_desc_t texture_desc = {0};
	texture_desc.extent.width = swapchain->extent.width;
	texture_desc.extent.height = swapchain->extent.height;
	texture_desc.extent.depth = 1u;
	texture_desc.format = swapchain->format;
	texture_desc.usage_flags = SK_RESOURCE_USAGE_RENDER_TARGET | SK_RESOURCE_USAGE_COPY_DEST;
	for (u32 i = 0u; i < image_count; ++i) {
		test_texture_list_push(device->allocator, &swapchain->textures, sk_trd_create_texture(dev, &texture_desc));
	}
	return sk_swapchain_t_from_ptr(swapchain);
}

static void sk_trd_destroy_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	test_device_t* device = test_device(dev);
	test_swapchain_t* swapchain = (test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	for (u32 i = 0u; i < swapchain->textures.count; ++i) {
		sk_trd_destroy_texture(dev, swapchain->textures.data[i]);
	}
	test_texture_list_free(device->allocator, &swapchain->textures);
	test_array_remove(&device->swapchains, swapchain);
	device->allocator->free(device->allocator->instance, swapchain);
}

static i32 sk_trd_resize_swapchain(sk_render_device_t dev, sk_swapchain_t swapchain_handle, u32 width, u32 height) {
	(void)dev;
	test_swapchain_t* swapchain = (test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	swapchain->extent.width = width;
	swapchain->extent.height = height;
	return 0;
}

static sk_device_result_t sk_trd_acquire_next_image(sk_render_device_t dev, const sk_acquire_info_t* info, u32* out_image_index) {
	(void)dev;
	(void)info;
	test_swapchain_t* swapchain = (test_swapchain_t*)sk_swapchain_t_to_ptr(info->swapchain);
	u32 image_count = swapchain->textures.count;
	swapchain->image_index = image_count == 0u ? 0u : (swapchain->image_index + 1u) % image_count;
	if (out_image_index != NULL) {
		*out_image_index = swapchain->image_index;
	}
	return SK_DEVICE_RESULT_SUCCESS;
}

static sk_texture_t sk_trd_get_swapchain_image(sk_render_device_t dev, sk_swapchain_t swapchain_handle, u32 image_index) {
	(void)dev;
	test_swapchain_t* swapchain = (test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	if (image_index >= swapchain->textures.count) {
		return sk_texture_t_zero();
	}
	return swapchain->textures.data[image_index];
}

static sk_extent3d_t sk_trd_get_swapchain_extent(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	return ((test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle))->extent;
}

static sk_pixel_format_t sk_trd_get_swapchain_format(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	return ((test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle))->format;
}

static u32 sk_trd_get_swapchain_image_count(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	return ((test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle))->textures.count;
}

static u32 sk_trd_get_swapchain_current_image_index(sk_render_device_t dev, sk_swapchain_t swapchain_handle) {
	(void)dev;
	return ((test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle))->image_index;
}

static u32 sk_trd_get_swapchain_textures(sk_render_device_t dev, sk_swapchain_t swapchain_handle, sk_texture_t* out_textures, u32 max_count) {
	(void)dev;
	test_swapchain_t* swapchain = (test_swapchain_t*)sk_swapchain_t_to_ptr(swapchain_handle);
	u32 count = swapchain->textures.count < max_count ? swapchain->textures.count : max_count;
	for (u32 i = 0u; i < count; ++i) {
		out_textures[i] = swapchain->textures.data[i];
	}
	return count;
}

static sk_device_result_t sk_trd_present(sk_render_device_t dev, sk_queue_t queue, const sk_present_info_t* info) {
	(void)dev;
	(void)queue;
	(void)info;
	return SK_DEVICE_RESULT_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Sync primitives                                                     */
/* ------------------------------------------------------------------ */

static sk_fence_t sk_trd_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_fence_t* fence = (test_fence_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_fence_t));
	if (fence == NULL) {
		return sk_fence_t_zero();
	}
	fence->device = device;
	fence->signaled = desc != NULL && desc->signaled;
	return sk_fence_t_from_ptr(fence);
}

static void sk_trd_destroy_fence(sk_render_device_t dev, sk_fence_t fence) {
	(void)dev;
	test_fence_t* fence_obj = (test_fence_t*)sk_fence_t_to_ptr(fence);
	fence_obj->device->allocator->free(fence_obj->device->allocator->instance, fence_obj);
}

static i32 sk_trd_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns) {
	(void)dev;
	(void)fences;
	(void)fence_count;
	(void)wait_all;
	(void)timeout_ns;
	return 0;
}

static void sk_trd_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count) {
	(void)dev;
	for (u32 i = 0u; i < fence_count; ++i) {
		((test_fence_t*)sk_fence_t_to_ptr(fences[i]))->signaled = false;
	}
}

static sk_semaphore_t sk_trd_create_semaphore(sk_render_device_t dev, const sk_semaphore_desc_t* desc) {
	test_device_t* device = test_device(dev);
	(void)desc;
	test_semaphore_t* semaphore = (test_semaphore_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_semaphore_t));
	if (semaphore == NULL) {
		return sk_semaphore_t_zero();
	}
	semaphore->device = device;
	return sk_semaphore_t_from_ptr(semaphore);
}

static void sk_trd_destroy_semaphore(sk_render_device_t dev, sk_semaphore_t semaphore) {
	(void)dev;
	test_semaphore_t* semaphore_obj = (test_semaphore_t*)sk_semaphore_t_to_ptr(semaphore);
	semaphore_obj->device->allocator->free(semaphore_obj->device->allocator->instance, semaphore_obj);
}

/* ------------------------------------------------------------------ */
/* Command buffer lifecycle                                            */
/* ------------------------------------------------------------------ */

static sk_command_buffer_t sk_trd_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_command_buffer_t* cmd = (test_command_buffer_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_command_buffer_t));
	if (cmd == NULL) {
		return sk_command_buffer_t_zero();
	}
	memset(cmd, 0, sizeof(*cmd));
	cmd->device = device;
	if (desc != NULL) {
		cmd->desc = *desc;
	}
	test_array_push(device->allocator, &device->command_buffers, cmd);
	return sk_command_buffer_t_from_ptr(cmd);
}

static void sk_trd_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	test_device_t* device = test_device(dev);
	test_command_buffer_t* command = (test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd);
	test_array_remove(&device->command_buffers, command);
	device->allocator->free(device->allocator->instance, command);
}

static i32 sk_trd_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info) {
	(void)dev;
	(void)info;
	test_command_buffer_t* command = (test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd);
	command->recording = true;
	memset(&command->stats, 0, sizeof(command->stats));
	command->render_pass_active = false;
	command->bound_pipeline = sk_pipeline_t_zero();
	return 0;
}

static void sk_trd_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->recording = false;
}

static void sk_trd_reset_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	test_command_buffer_t* command = (test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd);
	memset(&command->stats, 0, sizeof(command->stats));
	command->render_pass_active = false;
	command->bound_pipeline = sk_pipeline_t_zero();
}

static void sk_trd_execute_commands(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_t* secondary_cmds, u32 secondary_count) {
	(void)dev;
	(void)cmd;
	(void)secondary_cmds;
	(void)secondary_count;
}

/* ------------------------------------------------------------------ */
/* State setting                                                       */
/* ------------------------------------------------------------------ */

static void sk_trd_set_viewport(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_viewport, const sk_viewport_t* viewports, u32 viewport_count) {
	(void)dev;
	(void)cmd;
	(void)first_viewport;
	(void)viewports;
	(void)viewport_count;
}

static void sk_trd_set_scissor(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_scissor, const sk_rect2d_t* scissors, u32 scissor_count) {
	(void)dev;
	(void)cmd;
	(void)first_scissor;
	(void)scissors;
	(void)scissor_count;
}

static void sk_trd_set_blend_constants(sk_render_device_t dev, sk_command_buffer_t cmd, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	(void)cmd;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
}

static void sk_trd_set_stencil_reference(sk_render_device_t dev, sk_command_buffer_t cmd, u32 reference) {
	(void)dev;
	(void)cmd;
	(void)reference;
}

static void sk_trd_set_depth_bias(sk_render_device_t dev, sk_command_buffer_t cmd, f32 constant_factor, f32 clamp, f32 slope_factor) {
	(void)dev;
	(void)cmd;
	(void)constant_factor;
	(void)clamp;
	(void)slope_factor;
}

static void sk_trd_set_depth_bounds(sk_render_device_t dev, sk_command_buffer_t cmd, f32 min_depth, f32 max_depth) {
	(void)dev;
	(void)cmd;
	(void)min_depth;
	(void)max_depth;
}

static void sk_trd_bind_pipeline(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline) {
	(void)dev;
	(void)bind_point;
	((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->bound_pipeline = pipeline;
}

static void sk_trd_bind_descriptor_set(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_bind_point_t bind_point, sk_pipeline_t pipeline, u32 set_index,
									   sk_descriptor_set_t desc_set, const u32* dynamic_offsets, u32 offset_count) {
	(void)dev;
	(void)bind_point;
	(void)pipeline;
	(void)set_index;
	(void)desc_set;
	(void)dynamic_offsets;
	(void)offset_count;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.bind_descriptor_set_count;
}

static void sk_trd_bind_vertex_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, u32 first_binding, const sk_buffer_t* buffers, const u64* offsets, u32 buffer_count) {
	(void)dev;
	(void)cmd;
	(void)first_binding;
	(void)buffers;
	(void)offsets;
	(void)buffer_count;
}

static void sk_trd_bind_index_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, sk_index_type_t index_type) {
	(void)dev;
	(void)cmd;
	(void)buf;
	(void)offset;
	(void)index_type;
}

static void sk_trd_push_constants(sk_render_device_t dev, sk_command_buffer_t cmd, sk_pipeline_t pipeline, u32 shader_stages, u32 offset, u32 size, const void* data) {
	(void)dev;
	(void)cmd;
	(void)pipeline;
	(void)shader_stages;
	(void)offset;
	(void)size;
	(void)data;
}

/* ------------------------------------------------------------------ */
/* Draw / dispatch                                                     */
/* ------------------------------------------------------------------ */

static void sk_trd_draw(sk_render_device_t dev, sk_command_buffer_t cmd, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) {
	(void)dev;
	(void)vertex_count;
	(void)instance_count;
	(void)first_vertex;
	(void)first_instance;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.draw_count;
}

static void sk_trd_draw_indexed(sk_render_device_t dev, sk_command_buffer_t cmd, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance) {
	(void)dev;
	(void)index_count;
	(void)instance_count;
	(void)first_index;
	(void)vertex_offset;
	(void)first_instance;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.draw_count;
}

static void sk_trd_draw_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride) {
	(void)dev;
	(void)buffer;
	(void)offset;
	(void)draw_count;
	(void)stride;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.draw_count;
}

static void sk_trd_draw_indexed_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, u32 draw_count, u32 stride) {
	(void)dev;
	(void)buffer;
	(void)offset;
	(void)draw_count;
	(void)stride;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.draw_count;
}

static void sk_trd_draw_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
									   u32 max_draw_count, u32 stride) {
	(void)dev;
	(void)buffer;
	(void)offset;
	(void)count_buffer;
	(void)count_offset;
	(void)max_draw_count;
	(void)stride;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.draw_count;
}

static void sk_trd_draw_indexed_indirect_count(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset, sk_buffer_t count_buffer, u64 count_offset,
											   u32 max_draw_count, u32 stride) {
	(void)dev;
	(void)buffer;
	(void)offset;
	(void)count_buffer;
	(void)count_offset;
	(void)max_draw_count;
	(void)stride;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.draw_count;
}

static void sk_trd_dispatch(sk_render_device_t dev, sk_command_buffer_t cmd, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	(void)dev;
	(void)group_count_x;
	(void)group_count_y;
	(void)group_count_z;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.dispatch_count;
}

static void sk_trd_dispatch_indirect(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buffer, u64 offset) {
	(void)dev;
	(void)buffer;
	(void)offset;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.dispatch_count;
}

static void sk_trd_trace_rays(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_trace_rays_info_t* info) {
	(void)dev;
	(void)info;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.trace_rays_count;
}

/* ------------------------------------------------------------------ */
/* Render pass (command buffer)                                        */
/* ------------------------------------------------------------------ */

static void sk_trd_begin_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_begin_render_pass_info_t* info) {
	(void)dev;
	(void)info;
	test_command_buffer_t* command = (test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd);
	command->render_pass_active = true;
	++command->stats.render_pass_count;
}

static void sk_trd_end_render_pass(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->render_pass_active = false;
}

static void sk_trd_clear_attachments(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_clear_attachment_t* attachments, u32 attachment_count) {
	(void)dev;
	(void)attachments;
	(void)attachment_count;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.clear_count;
}

/* ------------------------------------------------------------------ */
/* Copy / transfer                                                     */
/* ------------------------------------------------------------------ */

static void sk_trd_copy_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t src, sk_buffer_t dst, u64 size, u64 src_offset, u64 dst_offset) {
	(void)dev;
	(void)src;
	(void)dst;
	(void)size;
	(void)src_offset;
	(void)dst_offset;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.copy_count;
}

static void sk_trd_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info) {
	(void)dev;
	(void)copy_info;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.copy_count;
}

static void sk_trd_copy_texture_to_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info) {
	(void)dev;
	(void)copy_info;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.copy_count;
}

static void sk_trd_copy_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_copy_t* copy_info) {
	(void)dev;
	(void)copy_info;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.copy_count;
}

static void sk_trd_blit_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_blit_t* blit_info) {
	(void)dev;
	(void)blit_info;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.copy_count;
}

static void sk_trd_resolve_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_texture_resolve_t* resolve_info) {
	(void)dev;
	(void)resolve_info;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.copy_count;
}

static void sk_trd_update_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, const void* data) {
	(void)dev;
	(void)cmd;
	test_buffer_t* buffer = (test_buffer_t*)sk_buffer_t_to_ptr(buf);
	if (buffer != NULL && data != NULL && offset + size <= buffer->storage_size) {
		memcpy(buffer->storage + offset, data, size);
	}
}

static void sk_trd_fill_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, u64 offset, u64 size, u32 data) {
	(void)dev;
	(void)cmd;
	(void)buf;
	(void)offset;
	(void)size;
	(void)data;
}

static void sk_trd_clear_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, const sk_clear_values_t* clear, u32 base_mip_level, u32 mip_level_count,
								 u32 base_array_layer, u32 array_layer_count) {
	(void)dev;
	(void)tex;
	(void)clear;
	(void)base_mip_level;
	(void)mip_level_count;
	(void)base_array_layer;
	(void)array_layer_count;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.clear_count;
}

/* ------------------------------------------------------------------ */
/* Resource barriers                                                   */
/* ------------------------------------------------------------------ */

static void sk_trd_resource_barrier_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, sk_buffer_t buf, sk_resource_state_t old_state, sk_resource_state_t new_state,
										   u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)old_state;
	(void)src_scope;
	(void)dst_scope;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.buffer_barrier_count;
	((test_buffer_t*)sk_buffer_t_to_ptr(buf))->state = new_state;
}

static void sk_trd_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
											u32 base_mip_level, u32 mip_level_count, u32 base_array_layer, u32 array_layer_count, u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)src_scope;
	(void)dst_scope;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.texture_barrier_count;
	test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);
	test_texture_apply_barrier(texture, old_state, new_state, base_mip_level, mip_level_count, base_array_layer, array_layer_count);
}

static void sk_trd_resource_barrier_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, sk_resource_state_t old_state, sk_resource_state_t new_state,
													u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)cmd;
	(void)blas;
	(void)old_state;
	(void)new_state;
	(void)src_scope;
	(void)dst_scope;
}

static void sk_trd_resource_barrier_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, sk_resource_state_t old_state, sk_resource_state_t new_state,
												 u32 src_scope, u32 dst_scope) {
	(void)dev;
	(void)cmd;
	(void)tlas;
	(void)old_state;
	(void)new_state;
	(void)src_scope;
	(void)dst_scope;
}

static void sk_trd_memory_barrier(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	++((test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd))->stats.memory_barrier_count;
}

/* ------------------------------------------------------------------ */
/* Debug labels                                                        */
/* ------------------------------------------------------------------ */

static void sk_trd_begin_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	(void)cmd;
	(void)name;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
}

static void sk_trd_end_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}

static void sk_trd_insert_debug_label(sk_render_device_t dev, sk_command_buffer_t cmd, const_chr_t name, f32 r, f32 g, f32 b, f32 a) {
	(void)dev;
	(void)cmd;
	(void)name;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
}

/* ------------------------------------------------------------------ */
/* Query pool                                                          */
/* ------------------------------------------------------------------ */

static sk_query_pool_t sk_trd_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_query_pool_t* pool = (test_query_pool_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_query_pool_t));
	if (pool == NULL) {
		return sk_query_pool_t_zero();
	}
	pool->device = device;
	pool->desc = *desc;
	test_array_push(device->allocator, &device->query_pools, pool);
	return sk_query_pool_t_from_ptr(pool);
}

static void sk_trd_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool) {
	test_device_t* device = test_device(dev);
	test_query_pool_t* pool_obj = (test_query_pool_t*)sk_query_pool_t_to_ptr(pool);
	test_array_remove(&device->query_pools, pool_obj);
	device->allocator->free(device->allocator->instance, pool_obj);
}

static void sk_trd_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)first_query;
	(void)query_count;
}

static void sk_trd_begin_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}

static void sk_trd_end_query(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}

static void sk_trd_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}

static void sk_trd_copy_query_pool_results(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count, sk_buffer_t dst_buffer,
										   u64 dst_offset, u64 stride) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)first_query;
	(void)query_count;
	(void)dst_buffer;
	(void)dst_offset;
	(void)stride;
}

static i32 sk_trd_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait) {
	(void)dev;
	(void)pool;
	(void)first_query;
	(void)query_count;
	(void)data;
	(void)data_size;
	(void)stride;
	(void)wait;
	return -1;
}

/* ------------------------------------------------------------------ */
/* Acceleration structures                                             */
/* ------------------------------------------------------------------ */

static sk_blas_t sk_trd_create_bottom_level_as(sk_render_device_t dev, const sk_blas_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_blas_t* blas = (test_blas_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_blas_t));
	if (blas == NULL) {
		return sk_blas_t_zero();
	}
	blas->device = device;
	blas->desc = *desc;
	test_array_push(device->allocator, &device->blas_list, blas);
	return sk_blas_t_from_ptr(blas);
}

static void sk_trd_destroy_bottom_level_as(sk_render_device_t dev, sk_blas_t blas) {
	test_device_t* device = test_device(dev);
	test_blas_t* blas_obj = (test_blas_t*)sk_blas_t_to_ptr(blas);
	test_array_remove(&device->blas_list, blas_obj);
	device->allocator->free(device->allocator->instance, blas_obj);
}

static sk_tlas_t sk_trd_create_top_level_as(sk_render_device_t dev, const sk_tlas_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_tlas_t* tlas = (test_tlas_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_tlas_t));
	if (tlas == NULL) {
		return sk_tlas_t_zero();
	}
	tlas->device = device;
	tlas->desc = *desc;
	tlas->instance_count = desc->instance_count;
	test_array_push(device->allocator, &device->tlas_list, tlas);
	return sk_tlas_t_from_ptr(tlas);
}

static void sk_trd_destroy_top_level_as(sk_render_device_t dev, sk_tlas_t tlas) {
	test_device_t* device = test_device(dev);
	test_tlas_t* tlas_obj = (test_tlas_t*)sk_tlas_t_to_ptr(tlas);
	test_array_remove(&device->tlas_list, tlas_obj);
	device->allocator->free(device->allocator->instance, tlas_obj);
}

static sk_as_build_sizes_t sk_trd_get_blas_build_sizes(sk_render_device_t dev, const sk_blas_desc_t* desc) {
	(void)dev;
	(void)desc;
	sk_as_build_sizes_t sizes = {1024u, 1024u, 1024u};
	return sizes;
}

static sk_as_build_sizes_t sk_trd_get_tlas_build_sizes(sk_render_device_t dev, const sk_tlas_desc_t* desc) {
	(void)dev;
	(void)desc;
	sk_as_build_sizes_t sizes = {1024u, 1024u, 1024u};
	return sizes;
}

static void sk_trd_build_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t blas, const sk_blas_desc_t* desc, const sk_as_build_info_t* build_info) {
	(void)dev;
	(void)cmd;
	(void)blas;
	(void)desc;
	(void)build_info;
}

static void sk_trd_build_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t tlas, const sk_tlas_desc_t* desc, const sk_as_build_info_t* build_info) {
	(void)dev;
	(void)cmd;
	(void)tlas;
	(void)desc;
	(void)build_info;
}

static bool sk_trd_is_bottom_level_as_compacted(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	(void)blas;
	return false;
}

static u64 sk_trd_get_bottom_level_as_compacted_size(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	(void)blas;
	return 0ull;
}

static sk_blas_desc_t sk_trd_get_blas_desc(sk_render_device_t dev, sk_blas_t blas) {
	(void)dev;
	return ((test_blas_t*)sk_blas_t_to_ptr(blas))->desc;
}

static sk_tlas_desc_t sk_trd_get_tlas_desc(sk_render_device_t dev, sk_tlas_t tlas) {
	(void)dev;
	return ((test_tlas_t*)sk_tlas_t_to_ptr(tlas))->desc;
}

static void sk_trd_copy_bottom_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_blas_t src, sk_blas_t dst, bool compress) {
	(void)dev;
	(void)cmd;
	(void)src;
	(void)dst;
	(void)compress;
}

static void sk_trd_copy_top_level_as(sk_render_device_t dev, sk_command_buffer_t cmd, sk_tlas_t src, sk_tlas_t dst, bool compress) {
	(void)dev;
	(void)cmd;
	(void)src;
	(void)dst;
	(void)compress;
}

static bool sk_trd_update_top_level_as_instances(sk_render_device_t dev, sk_tlas_t tlas, const sk_as_instance_desc_t* instances, u32 instance_count) {
	(void)dev;
	(void)instances;
	((test_tlas_t*)sk_tlas_t_to_ptr(tlas))->instance_count = instance_count;
	return true;
}

static void sk_trd_update_top_level_as_instance(sk_render_device_t dev, sk_tlas_t tlas, u32 index, const sk_as_instance_desc_t* instance) {
	(void)dev;
	(void)tlas;
	(void)index;
	(void)instance;
}

static void sk_trd_set_top_level_as_instance_count(sk_render_device_t dev, sk_tlas_t tlas, u32 count) {
	(void)dev;
	((test_tlas_t*)sk_tlas_t_to_ptr(tlas))->instance_count = count;
}

/* ------------------------------------------------------------------ */
/* Queue                                                               */
/* ------------------------------------------------------------------ */

static sk_queue_t sk_trd_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc) {
	test_device_t* device = test_device(dev);
	test_queue_t* queue = (test_queue_t*)device->allocator->alloc(device->allocator->instance, sizeof(test_queue_t));
	if (queue == NULL) {
		return sk_queue_t_zero();
	}
	queue->device = device;
	queue->desc = *desc;
	test_array_push(device->allocator, &device->queues, queue);
	return sk_queue_t_from_ptr(queue);
}

static void sk_trd_destroy_queue(sk_render_device_t dev, sk_queue_t queue) {
	test_device_t* device = test_device(dev);
	test_queue_t* queue_obj = (test_queue_t*)sk_queue_t_to_ptr(queue);
	test_array_remove(&device->queues, queue_obj);
	device->allocator->free(device->allocator->instance, queue_obj);
}

static void sk_trd_submit_command_buffer(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
	++((test_queue_t*)sk_queue_t_to_ptr(queue))->submit_count;
}

static i32 sk_trd_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* info) {
	(void)dev;
	(void)info;
	++((test_queue_t*)sk_queue_t_to_ptr(queue))->submit_count;
	return 0;
}

static i32 sk_trd_submit_and_wait(sk_render_device_t dev, sk_queue_t queue, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
	++((test_queue_t*)sk_queue_t_to_ptr(queue))->submit_count;
	return 0;
}

static i32 sk_trd_queue_wait_idle(sk_render_device_t dev, sk_queue_t queue) {
	(void)dev;
	(void)queue;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Device query                                                        */
/* ------------------------------------------------------------------ */

static sk_texture_memory_requirements_t sk_trd_get_texture_memory_requirements(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	(void)dev;
	sk_texture_memory_requirements_t r = {0, 0, 0};
	u64 elements = (u64)desc->extent.width * desc->extent.height * desc->extent.depth;
	elements *= desc->array_layers > 0u ? desc->array_layers : 1u;
	r.size = elements * test_format_size(desc->format);
	r.alignment = 256u;
	r.memory_type_bits = 0xFFFFFFFFu;
	return r;
}

static sk_buffer_memory_requirements_t sk_trd_get_buffer_memory_requirements(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	(void)dev;
	sk_buffer_memory_requirements_t r = {0, 0, 0};
	r.size = desc->size;
	r.alignment = 256u;
	r.memory_type_bits = 0xFFFFFFFFu;
	return r;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS
#include "test.h"

SK_TEST(test_render_device_api_table_is_complete) {
	/* Device */
	TEST_ASSERT_NOT_NULL(test_render_device_api.init);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy);
	TEST_ASSERT_NOT_NULL(test_render_device_api.wait_idle);

	/* Adapter / device queries */
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter);
	TEST_ASSERT_NOT_NULL(test_render_device_api.select_adapter);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter_score);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_adapter_name);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_properties);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_features);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_api);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_memory_budgets);

	/* Buffer / texture / sampler / memory / shader / pipeline */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.buffer_map);
	TEST_ASSERT_NOT_NULL(test_render_device_api.buffer_unmap);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_buffer_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_buffer_mapped_data);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_texture_view);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_texture_view);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_sampler);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_sampler);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_texture_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_texture_view_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_sampler_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_memory);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_memory);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_aliased_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_memory_size);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_shader);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_shader);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_graphics_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_compute_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_ray_tracing_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_pipeline_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_pipeline_bind_point);

	/* Descriptors */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_descriptor_set_desc);

	/* Pass / FB / swapchain / sync */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_framebuffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_framebuffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_render_pass_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_framebuffer_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_framebuffer_extent);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_swapchain);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_swapchain);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resize_swapchain);
	TEST_ASSERT_NOT_NULL(test_render_device_api.acquire_next_image);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_image);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_extent);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_format);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_image_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_current_image_index);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_swapchain_textures);
	TEST_ASSERT_NOT_NULL(test_render_device_api.present);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_fence);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_fence);
	TEST_ASSERT_NOT_NULL(test_render_device_api.wait_fences);
	TEST_ASSERT_NOT_NULL(test_render_device_api.reset_fences);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_semaphore);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_semaphore);

	/* Cmd lifecycle / state / draw / transfer / barrier / debug */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.reset_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.execute_commands);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_viewport);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_scissor);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_blend_constants);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_stencil_reference);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_depth_bias);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_depth_bounds);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_pipeline);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_descriptor_set);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_vertex_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.bind_index_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.push_constants);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indexed);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indirect);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indexed_indirect);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indirect_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.draw_indexed_indirect_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.dispatch);
	TEST_ASSERT_NOT_NULL(test_render_device_api.dispatch_indirect);
	TEST_ASSERT_NOT_NULL(test_render_device_api.trace_rays);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_render_pass);
	TEST_ASSERT_NOT_NULL(test_render_device_api.clear_attachments);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_buffer_to_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_texture_to_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.blit_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resolve_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.fill_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.clear_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_texture);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.resource_barrier_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.memory_barrier);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_debug_label);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_debug_label);
	TEST_ASSERT_NOT_NULL(test_render_device_api.insert_debug_label);

	/* Queries / AS / queue / memreq */
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_query_pool);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_query_pool);
	TEST_ASSERT_NOT_NULL(test_render_device_api.reset_query_pool);
	TEST_ASSERT_NOT_NULL(test_render_device_api.begin_query);
	TEST_ASSERT_NOT_NULL(test_render_device_api.end_query);
	TEST_ASSERT_NOT_NULL(test_render_device_api.write_timestamp);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_query_pool_results);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_query_pool_results);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_blas_build_sizes);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_tlas_build_sizes);
	TEST_ASSERT_NOT_NULL(test_render_device_api.build_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.build_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.is_bottom_level_as_compacted);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_bottom_level_as_compacted_size);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_blas_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_tlas_desc);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_bottom_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.copy_top_level_as);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_top_level_as_instances);
	TEST_ASSERT_NOT_NULL(test_render_device_api.update_top_level_as_instance);
	TEST_ASSERT_NOT_NULL(test_render_device_api.set_top_level_as_instance_count);
	TEST_ASSERT_NOT_NULL(test_render_device_api.create_queue);
	TEST_ASSERT_NOT_NULL(test_render_device_api.destroy_queue);
	TEST_ASSERT_NOT_NULL(test_render_device_api.submit_command_buffer);
	TEST_ASSERT_NOT_NULL(test_render_device_api.submit);
	TEST_ASSERT_NOT_NULL(test_render_device_api.submit_and_wait);
	TEST_ASSERT_NOT_NULL(test_render_device_api.queue_wait_idle);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_texture_memory_requirements);
	TEST_ASSERT_NOT_NULL(test_render_device_api.get_buffer_memory_requirements);
}

SK_TEST(test_render_device_init_returns_valid_device) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));
	TEST_ASSERT_EQUAL_INT(0, test_render_device_api.wait_idle(dev));
	TEST_ASSERT_EQUAL_INT(SK_GRAPHICS_API_NONE, (int)test_render_device_api.get_api(dev));

	sk_device_properties_t props = test_render_device_api.get_properties(dev);
	TEST_ASSERT_EQUAL_INT(SK_DEVICE_TYPE_OTHER, (int)props.device_type);
	TEST_ASSERT_EQUAL_STRING("TestRenderDevice", props.device_name);
	TEST_ASSERT_TRUE(props.features.compute_shader);

	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_create_returns_valid_handles) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_buffer_desc_t buffer_desc = {0};
	buffer_desc.size = 64u;
	buffer_desc.usage_flags = SK_RESOURCE_USAGE_VERTEX_BUFFER;
	sk_buffer_t buffer = test_render_device_api.create_buffer(dev, &buffer_desc);
	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(buffer));
	TEST_ASSERT_EQUAL_UINT64(64u, test_render_device_api.get_buffer_desc(dev, buffer).size);

	sk_texture_desc_t texture_desc = {0};
	texture_desc.extent.width = 32u;
	texture_desc.extent.height = 32u;
	texture_desc.extent.depth = 1u;
	texture_desc.mip_levels = 2u;
	texture_desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	sk_texture_t texture = test_render_device_api.create_texture(dev, &texture_desc);
	TEST_ASSERT_TRUE(sk_texture_t_is_valid(texture));
	sk_texture_desc_t got_desc = test_render_device_api.get_texture_desc(dev, texture);
	TEST_ASSERT_EQUAL_UINT32(32u, got_desc.extent.width);
	TEST_ASSERT_EQUAL_UINT32(2u, got_desc.mip_levels);

	sk_command_buffer_t cmd = test_render_device_api.create_command_buffer(dev, NULL);
	TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));
	TEST_ASSERT_EQUAL_INT(0, test_render_device_api.begin_command_buffer(dev, cmd, NULL));
	test_render_device_api.end_command_buffer(dev, cmd);

	test_render_device_api.destroy_command_buffer(dev, cmd);
	test_render_device_api.destroy_buffer(dev, buffer);
	test_render_device_api.destroy_texture(dev, texture);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_buffer_storage_roundtrips) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_buffer_desc_t buffer_desc = {0};
	buffer_desc.size = 16u;
	sk_buffer_t buffer = test_render_device_api.create_buffer(dev, &buffer_desc);
	test_buffer_t* test_buffer = (test_buffer_t*)sk_buffer_t_to_ptr(buffer);
	TEST_ASSERT_NOT_NULL(test_buffer->storage);
	TEST_ASSERT_EQUAL_UINT64(16u, test_buffer->storage_size);

	u32 values[2] = {7u, 42u};
	sk_command_buffer_t cmd = test_render_device_api.create_command_buffer(dev, NULL);
	test_render_device_api.begin_command_buffer(dev, cmd, NULL);
	test_render_device_api.update_buffer(dev, cmd, buffer, 0u, sizeof(values), values);
	test_render_device_api.end_command_buffer(dev, cmd);

	u32* mapped = (u32*)test_render_device_api.buffer_map(dev, buffer);
	TEST_ASSERT_NOT_NULL(mapped);
	TEST_ASSERT_EQUAL_UINT32(7u, mapped[0]);
	TEST_ASSERT_EQUAL_UINT32(42u, mapped[1]);
	test_render_device_api.buffer_unmap(dev, buffer);
	TEST_ASSERT_FALSE(test_buffer->mapped);

	test_render_device_api.destroy_command_buffer(dev, cmd);
	test_render_device_api.destroy_buffer(dev, buffer);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_texture_subresources_start_undefined) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_texture_desc_t desc = {0};
	desc.extent.width = 64u;
	desc.extent.height = 64u;
	desc.extent.depth = 1u;
	desc.mip_levels = 4u;
	desc.array_layers = 2u;
	desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	sk_texture_t tex = test_render_device_api.create_texture(dev, &desc);
	test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);

	TEST_ASSERT_EQUAL_UINT32(4u, texture->mip_levels);
	TEST_ASSERT_EQUAL_UINT32(2u, texture->array_layers);
	TEST_ASSERT_EQUAL_UINT32(8u, texture->subresource_count);
	for (u32 i = 0u; i < texture->subresource_count; ++i) {
		TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_UNDEFINED, (int)texture->subresource_states[i]);
	}

	test_render_device_api.destroy_texture(dev, tex);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_texture_barrier_transitions_subresource) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_texture_desc_t desc = {0};
	desc.extent.width = 64u;
	desc.extent.height = 64u;
	desc.extent.depth = 1u;
	desc.mip_levels = 3u;
	desc.array_layers = 1u;
	desc.format = SK_PIXEL_FORMAT_RGBA16_FLOAT;
	sk_texture_t tex = test_render_device_api.create_texture(dev, &desc);
	test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);

	sk_command_buffer_t cmd = test_render_device_api.create_command_buffer(dev, NULL);
	test_render_device_api.begin_command_buffer(dev, cmd, NULL);
	test_render_device_api.resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_GENERAL, 1u, 1u, 0u, 1u, SK_BARRIER_SYNC_AUTOMATIC,
													SK_BARRIER_SYNC_AUTOMATIC);
	test_render_device_api.end_command_buffer(dev, cmd);

	u32 index = test_texture_subresource_index(texture, 1u, 0u);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_UNDEFINED, (int)texture->subresource_states[0]);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_GENERAL, (int)texture->subresource_states[index]);
	TEST_ASSERT_EQUAL_UINT32(0u, texture->mismatch_count);
	TEST_ASSERT_EQUAL_UINT32(1u, texture->barrier_count);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_UNDEFINED, (int)texture->barrier_history[0].old_state);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_GENERAL, (int)texture->barrier_history[0].new_state);

	test_render_device_api.destroy_command_buffer(dev, cmd);
	test_render_device_api.destroy_texture(dev, tex);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_texture_barrier_mip_range) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_texture_desc_t desc = {0};
	desc.extent.width = 32u;
	desc.extent.height = 32u;
	desc.extent.depth = 1u;
	desc.mip_levels = 5u;
	desc.array_layers = 1u;
	desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	sk_texture_t tex = test_render_device_api.create_texture(dev, &desc);
	test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);

	sk_command_buffer_t cmd = test_render_device_api.create_command_buffer(dev, NULL);
	test_render_device_api.begin_command_buffer(dev, cmd, NULL);
	/* Transition every mip via the "all remaining" form (UINT32_MAX). */
	test_render_device_api.resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_SHADER_READ, 0u, UINT32_MAX, 0u, UINT32_MAX,
													SK_BARRIER_SYNC_AUTOMATIC, SK_BARRIER_SYNC_AUTOMATIC);
	for (u32 i = 0u; i < texture->mip_levels; ++i) {
		TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_SHADER_READ, (int)texture->subresource_states[i]);
	}
	/* Move only mip 2 to COPY_DEST. */
	test_render_device_api.resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_SHADER_READ, SK_RESOURCE_STATE_COPY_DEST, 2u, 1u, 0u, 1u, SK_BARRIER_SYNC_AUTOMATIC,
													SK_BARRIER_SYNC_AUTOMATIC);
	test_render_device_api.end_command_buffer(dev, cmd);

	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_SHADER_READ, (int)texture->subresource_states[1]);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_COPY_DEST, (int)texture->subresource_states[2]);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_SHADER_READ, (int)texture->subresource_states[3]);
	TEST_ASSERT_EQUAL_UINT32(0u, texture->mismatch_count);

	test_render_device_api.destroy_command_buffer(dev, cmd);
	test_render_device_api.destroy_texture(dev, tex);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_texture_barrier_detects_mismatch) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_texture_desc_t desc = {0};
	desc.extent.width = 16u;
	desc.extent.height = 16u;
	desc.extent.depth = 1u;
	desc.mip_levels = 1u;
	desc.array_layers = 1u;
	desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	sk_texture_t tex = test_render_device_api.create_texture(dev, &desc);
	test_texture_t* texture = (test_texture_t*)sk_texture_t_to_ptr(tex);

	sk_command_buffer_t cmd = test_render_device_api.create_command_buffer(dev, NULL);
	test_render_device_api.begin_command_buffer(dev, cmd, NULL);
	/* Tracked state is UNDEFINED but the barrier claims it comes from SHADER_READ. */
	test_render_device_api.resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_SHADER_READ, SK_RESOURCE_STATE_GENERAL, 0u, 1u, 0u, 1u, SK_BARRIER_SYNC_AUTOMATIC,
													SK_BARRIER_SYNC_AUTOMATIC);
	test_render_device_api.end_command_buffer(dev, cmd);

	TEST_ASSERT_EQUAL_UINT32(1u, texture->mismatch_count);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_GENERAL, (int)texture->subresource_states[0]);

	test_render_device_api.destroy_command_buffer(dev, cmd);
	test_render_device_api.destroy_texture(dev, tex);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_command_buffer_stats) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_command_buffer_t cmd = test_render_device_api.create_command_buffer(dev, NULL);
	test_command_buffer_t* command = (test_command_buffer_t*)sk_command_buffer_t_to_ptr(cmd);
	test_render_device_api.begin_command_buffer(dev, cmd, NULL);
	test_render_device_api.begin_render_pass(dev, cmd, NULL);
	test_render_device_api.draw(dev, cmd, 3u, 1u, 0u, 0u);
	test_render_device_api.draw(dev, cmd, 3u, 1u, 0u, 0u);
	test_render_device_api.end_render_pass(dev, cmd);
	test_render_device_api.dispatch(dev, cmd, 8u, 8u, 1u);
	test_render_device_api.copy_buffer(dev, cmd, sk_buffer_t_zero(), sk_buffer_t_zero(), 0u, 0u, 0u);
	test_render_device_api.memory_barrier(dev, cmd);
	test_render_device_api.end_command_buffer(dev, cmd);

	TEST_ASSERT_EQUAL_UINT32(1u, command->stats.render_pass_count);
	TEST_ASSERT_EQUAL_UINT32(2u, command->stats.draw_count);
	TEST_ASSERT_EQUAL_UINT32(1u, command->stats.dispatch_count);
	TEST_ASSERT_EQUAL_UINT32(1u, command->stats.copy_count);
	TEST_ASSERT_EQUAL_UINT32(1u, command->stats.memory_barrier_count);
	TEST_ASSERT_FALSE(command->render_pass_active);
	TEST_ASSERT_FALSE(command->recording);

	test_render_device_api.destroy_command_buffer(dev, cmd);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_lifecycle_and_defaults) {
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_render_pass_desc_t pass_desc = {0};
	sk_render_pass_t pass = test_render_device_api.create_render_pass(dev, &pass_desc);
	TEST_ASSERT_TRUE(sk_render_pass_t_is_valid(pass));

	sk_framebuffer_desc_t fb_desc = {0};
	sk_framebuffer_t fb = test_render_device_api.create_framebuffer(dev, &fb_desc);
	TEST_ASSERT_TRUE(sk_framebuffer_t_is_valid(fb));
	TEST_ASSERT_EQUAL_UINT32(1u, test_render_device_api.get_framebuffer_extent(dev, fb).width);

	sk_swapchain_desc_t sc_desc = {0};
	sc_desc.width = 128u;
	sc_desc.height = 64u;
	sc_desc.format = SK_PIXEL_FORMAT_BGRA8_UNORM;
	sk_swapchain_t swapchain = test_render_device_api.create_swapchain(dev, &sc_desc);
	TEST_ASSERT_TRUE(sk_swapchain_t_is_valid(swapchain));
	TEST_ASSERT_EQUAL_UINT32(128u, test_render_device_api.get_swapchain_extent(dev, swapchain).width);
	TEST_ASSERT_EQUAL_UINT32(2u, test_render_device_api.get_swapchain_image_count(dev, swapchain));
	TEST_ASSERT_EQUAL_INT(SK_PIXEL_FORMAT_BGRA8_UNORM, (int)test_render_device_api.get_swapchain_format(dev, swapchain));
	sk_acquire_info_t acquire = {0};
	acquire.swapchain = swapchain;
	u32 image_index = 0u;
	TEST_ASSERT_EQUAL_INT(SK_DEVICE_RESULT_SUCCESS, (int)test_render_device_api.acquire_next_image(dev, &acquire, &image_index));

	sk_queue_desc_t queue_desc = {0};
	queue_desc.queue_type = SK_QUEUE_TYPE_GRAPHICS;
	sk_queue_t queue = test_render_device_api.create_queue(dev, &queue_desc);
	TEST_ASSERT_TRUE(sk_queue_t_is_valid(queue));
	test_render_device_api.submit_command_buffer(dev, queue, sk_command_buffer_t_zero());

	test_render_device_api.destroy_queue(dev, queue);
	test_render_device_api.destroy_swapchain(dev, swapchain);
	test_render_device_api.destroy_framebuffer(dev, fb);
	test_render_device_api.destroy_render_pass(dev, pass);
	test_render_device_api.destroy(dev);
}

SK_TEST(test_render_device_destroy_owns_resources) {
	/* Device teardown frees every created object; no per-resource destroy needed. */
	sk_render_device_t dev = test_render_device_api.init(NULL, NULL);

	sk_buffer_desc_t buffer_desc = {0};
	buffer_desc.size = 32u;
	test_render_device_api.create_buffer(dev, &buffer_desc);

	sk_texture_desc_t texture_desc = {0};
	texture_desc.extent.width = 8u;
	texture_desc.extent.height = 8u;
	texture_desc.extent.depth = 1u;
	texture_desc.mip_levels = 2u;
	texture_desc.array_layers = 2u;
	texture_desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	test_render_device_api.create_texture(dev, &texture_desc);

	sk_swapchain_desc_t sc_desc = {0};
	sc_desc.width = 64u;
	sc_desc.height = 64u;
	test_render_device_api.create_swapchain(dev, &sc_desc);

	sk_pipeline_desc_t pipeline_desc = {0};
	sk_graphics_pipeline_desc_t graphics_desc = {0};
	graphics_desc.pipeline = pipeline_desc;
	test_render_device_api.create_graphics_pipeline(dev, &graphics_desc);

	test_render_device_api.create_command_buffer(dev, NULL);
	sk_queue_desc_t queue_desc = {0};
	queue_desc.queue_type = SK_QUEUE_TYPE_GRAPHICS;
	test_render_device_api.create_queue(dev, &queue_desc);
	test_render_device_api.create_memory(dev, 1024u, 256u, 0xFFFFFFFFu);

	/* No per-resource destroy calls: device owns and frees everything. */
	test_render_device_api.destroy(dev);
}

#endif /* SK_TESTS */
