/**
 * @file render.c
 * @brief GPU render backend for UI draw lists (APX-134).
 *
 * Submits sk_ui_draw_list_t through sk_render_device_api_t only — no parallel
 * device layer. Owns: DXC-compiled shaders (HLSL string literals, temporary),
 * graphics pipelines (alpha blend + scissor), dynamic host-visible VB/IB,
 * font atlas GPU textures, a 1x1 white texture, and a shared sampler.
 *
 * Frame flow (host-driven):
 *   paint → renderer_prepare (outside pass: VB/IB + atlas uploads)
 *         → begin_render_pass (swapchain or offscreen)
 *         → renderer_encode (viewport, scissor, bind, draw_indexed batches)
 *         → end_render_pass
 */

#include "ui.internal.h"

#include "allocator.h"
#include "dxc_compiler.h"
#include "render_device.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Embedded HLSL (temporary — compile via DXC at renderer create)             */
/* -------------------------------------------------------------------------- */

/*
 * TODO(APX-134): HLSL is embedded as string literals and compiled through the
 * DXC plugin at init. This is temporary; move to cooked SPIR-V assets / the
 * shader resource pipeline when that lands. Do not expand this pattern.
 */
/*
 * Push block is one float4 (scale.xy + translate.xy) + one uint mode.
 * Packed as float4 to avoid HLSL cbuffer 16-byte alignment surprises.
 * Color is R32_UINT (packed RGBA8) — more portable than RGBA8 vertex formats.
 */
static const char ui_vs_hlsl[] = "struct VSInput {\n"
								 "    float2 pos : POSITION;\n"
								 "    float2 uv : TEXCOORD0;\n"
								 "    uint color : COLOR0;\n"
								 "};\n"
								 "struct VSOutput {\n"
								 "    float4 position : SV_Position;\n"
								 "    float2 uv : TEXCOORD0;\n"
								 "    float4 color : COLOR0;\n"
								 "};\n"
								 "struct UIPush {\n"
								 "    float4 st; /* xy=scale, zw=translate */\n"
								 "    uint mode;\n"
								 "    float pxRange;\n"
								 "    uint2 _pad;\n"
								 "};\n"
								 "[[vk::push_constant]] UIPush g_push;\n"
								 "float4 ui_unpack_color(uint c) {\n"
								 "    return float4(\n"
								 "        (float)(c & 255u) / 255.0f,\n"
								 "        (float)((c >> 8u) & 255u) / 255.0f,\n"
								 "        (float)((c >> 16u) & 255u) / 255.0f,\n"
								 "        (float)((c >> 24u) & 255u) / 255.0f);\n"
								 "}\n"
								 "VSOutput main(VSInput input) {\n"
								 "    VSOutput o;\n"
								 "    float2 ndc = input.pos * g_push.st.xy + g_push.st.zw;\n"
								 "    o.position = float4(ndc, 0.0f, 1.0f);\n"
								 "    o.uv = input.uv;\n"
								 "    o.color = ui_unpack_color(input.color);\n"
								 "    return o;\n"
								 "}\n";

static const char ui_ps_hlsl[] = "struct PSInput {\n"
								 "    float4 position : SV_Position;\n"
								 "    float2 uv : TEXCOORD0;\n"
								 "    float4 color : COLOR0;\n"
								 "};\n"
								 "struct UIPush {\n"
								 "    float4 st;\n"
								 "    uint mode;\n"
								 "    float pxRange;\n"
								 "    uint2 _pad;\n"
								 "};\n"
								 "[[vk::binding(0, 0)]] Texture2D g_texture : register(t0);\n"
								 "[[vk::binding(1, 0)]] SamplerState g_sampler : register(s0);\n"
								 "[[vk::push_constant]] UIPush g_push;\n"
								 "float ui_msdf_median(float r, float g, float b) {\n"
								 "    return max(min(r, g), min(max(r, g), b));\n"
								 "}\n"
								 "float ui_msdf_screen_px_range(float2 uv) {\n"
								 "    uint tw = 0u;\n"
								 "    uint th = 0u;\n"
								 "    g_texture.GetDimensions(tw, th);\n"
								 "    float2 texSize = float2((float)tw, (float)th);\n"
								 "    float2 unitRange = (texSize.x > 0.0f && texSize.y > 0.0f)\n"
								 "        ? (g_push.pxRange / texSize) : float2(0.0f, 0.0f);\n"
								 "    float2 fw = fwidth(uv);\n"
								 "    float2 screenTexSize = float2(\n"
								 "        fw.x > 1.0e-8f ? 1.0f / fw.x : 0.0f,\n"
								 "        fw.y > 1.0e-8f ? 1.0f / fw.y : 0.0f);\n"
								 "    /* Clamp to 1 px so very small text does not vanish or shimmer. */\n"
								 "    return max(0.5f * dot(unitRange, screenTexSize), 1.0f);\n"
								 "}\n"
								 "float4 main(PSInput input) : SV_Target {\n"
								 "    float4 tex = g_texture.Sample(g_sampler, input.uv);\n"
								 "    if (g_push.mode == 1u) {\n"
								 "        float med = ui_msdf_median(tex.r, tex.g, tex.b);\n"
								 "        float spr = ui_msdf_screen_px_range(input.uv);\n"
								 "        float sd = spr * (med - 0.5f);\n"
								 "        float coverage = saturate(smoothstep(-0.5f, 0.5f, sd));\n"
								 "        return float4(input.color.rgb, input.color.a * coverage);\n"
								 "    }\n"
								 "    return tex * input.color;\n"
								 "}\n";

/* Push constant layout (must match HLSL UIPush; 32 bytes). */
typedef struct ui_push_t {
	f32 scale_x;
	f32 scale_y;
	f32 translate_x;
	f32 translate_y;
	u32 mode; /* 0 = solid/image, 1 = MSDF */
	f32 px_range;
	u32 pad1;
	u32 pad2;
} ui_push_t;

enum {
	UI_PUSH_SIZE = 32,
	UI_SPIRV_CAP = 16384u,
	UI_MODE_COLOR = 0u,
	UI_MODE_MSDF = 1u,
	UI_VB_MIN_BYTES = 64u * 1024u,
	UI_IB_MIN_BYTES = 32u * 1024u,
	UI_MSDF_ATLAS_CAP = 8u,
	UI_HOST_IMAGE_CAP = 16u, /**< texture_id indexes this (out of range → white). */
};

/* Compile-time layout check: push block size. */
typedef char ui_push_size_check[(sizeof(ui_push_t) == UI_PUSH_SIZE) ? 1 : -1];

/* -------------------------------------------------------------------------- */
/* GPU atlas slot                                                             */
/* -------------------------------------------------------------------------- */

typedef struct ui_gpu_msdf_atlas_t {
	sk_texture_t texture;
	sk_texture_view_t view;
	sk_descriptor_set_t desc_set;
	u32 font_id;
	u32 width;
	u32 height;
	u32 generation;
	u32 valid;
	u32 desc_valid;
	f32 px_range;
} ui_gpu_msdf_atlas_t;

/** Host-image binding cache (sk_ui_renderer_images_t from encode info). */
typedef struct ui_gpu_host_image_t {
	sk_texture_view_t view;
	sk_descriptor_set_t desc_set;
	u32 valid;
} ui_gpu_host_image_t;

/* -------------------------------------------------------------------------- */
/* Renderer                                                                   */
/* -------------------------------------------------------------------------- */

struct sk_ui_renderer_t {
	const sk_allocator_t* allocator;
	const sk_render_device_api_t* api;
	sk_render_device_t device;
	sk_render_pass_t render_pass;

	sk_shader_t vs;
	sk_shader_t ps;
	sk_pipeline_t pipeline;
	sk_descriptor_set_t desc_set_white; /**< Solid / image fallback (linear). */
	sk_descriptor_set_t desc_set_image; /**< IMAGE draws: white tex + nearest/clamp sampler. */
	sk_sampler_t sampler;
	sk_sampler_t sampler_nearest; /**< Nearest min/mag + clamp (IMAGE, §16 viewport). */

	sk_texture_t white_tex;
	sk_texture_view_t white_view;
	u32 white_uploaded; /* 1 after first prepare that uploaded white texels */

	sk_buffer_t vb;
	sk_buffer_t ib;
	u64 vb_capacity;
	u64 ib_capacity;

	sk_buffer_t staging; /* host-visible staging for atlas / white / geometry uploads */
	u64 staging_capacity;

	ui_gpu_msdf_atlas_t msdf_atlases[UI_MSDF_ATLAS_CAP];
	u32 msdf_atlas_count;

	/* Host image views bound per frame via renderer_encode info->images;
	 * texture_id on IMAGE mesh commands indexes this array. */
	ui_gpu_host_image_t host_images[UI_HOST_IMAGE_CAP];
	const sk_ui_renderer_images_t* active_images;

	/* Cached pipeline layout pieces (rebuilt with pipeline). */
	sk_descriptor_set_layout_binding_t set_bindings[2];
	sk_descriptor_set_layout_t set_layout;
	sk_push_constant_range_t push_range;
	sk_interface_variable_t vertex_inputs[3];
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static u64 ui_render_max_u64(u64 a, u64 b) {
	return a > b ? a : b;
}

static u64 ui_render_align_u64(u64 value, u64 align) {
	u64 mask;
	if (align == 0u) {
		return value;
	}
	mask = align - 1u;
	return (value + mask) & ~mask;
}

static void ui_render_destroy_buffers(sk_ui_renderer_t* r) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	if (sk_buffer_t_is_valid(r->vb)) {
		api->destroy_buffer(dev, r->vb);
		r->vb = sk_buffer_t_zero();
	}
	if (sk_buffer_t_is_valid(r->ib)) {
		api->destroy_buffer(dev, r->ib);
		r->ib = sk_buffer_t_zero();
	}
	if (sk_buffer_t_is_valid(r->staging)) {
		api->destroy_buffer(dev, r->staging);
		r->staging = sk_buffer_t_zero();
	}
	r->vb_capacity = 0u;
	r->ib_capacity = 0u;
	r->staging_capacity = 0u;
}

static void ui_render_destroy_atlas(sk_ui_renderer_t* r) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	u32 i;

	for (i = 0u; i < UI_MSDF_ATLAS_CAP; ++i) {
		ui_gpu_msdf_atlas_t* msdf = &r->msdf_atlases[i];
		if (msdf->desc_valid != 0u && sk_descriptor_set_t_is_valid(msdf->desc_set)) {
			api->destroy_descriptor_set(dev, msdf->desc_set);
		}
		if (msdf->valid != 0u) {
			if (sk_texture_view_t_is_valid(msdf->view)) {
				api->destroy_texture_view(dev, msdf->view);
			}
			if (sk_texture_t_is_valid(msdf->texture)) {
				api->destroy_texture(dev, msdf->texture);
			}
		}
		memset(msdf, 0, sizeof(*msdf));
	}
	r->msdf_atlas_count = 0u;
}

static void ui_render_destroy_desc_sets(sk_ui_renderer_t* r) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	u32 i;
	if (sk_descriptor_set_t_is_valid(r->desc_set_white)) {
		api->destroy_descriptor_set(dev, r->desc_set_white);
		r->desc_set_white = sk_descriptor_set_t_zero();
	}
	if (sk_descriptor_set_t_is_valid(r->desc_set_image)) {
		api->destroy_descriptor_set(dev, r->desc_set_image);
		r->desc_set_image = sk_descriptor_set_t_zero();
	}
	for (i = 0u; i < UI_HOST_IMAGE_CAP; ++i) {
		if (r->host_images[i].valid != 0u && sk_descriptor_set_t_is_valid(r->host_images[i].desc_set)) {
			api->destroy_descriptor_set(dev, r->host_images[i].desc_set);
		}
		memset(&r->host_images[i], 0, sizeof(r->host_images[i]));
	}
	r->active_images = NULL;
}

static void ui_render_destroy_pipeline(sk_ui_renderer_t* r) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	if (sk_pipeline_t_is_valid(r->pipeline)) {
		api->destroy_pipeline(dev, r->pipeline);
		r->pipeline = sk_pipeline_t_zero();
	}
	ui_render_destroy_desc_sets(r);
}

static sk_descriptor_set_t ui_render_make_image_set(sk_ui_renderer_t* r, sk_texture_view_t view, sk_sampler_t sampler) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	sk_descriptor_set_desc_t set_desc;
	sk_descriptor_set_t set;
	sk_descriptor_write_t writes[2];

	memset(&set_desc, 0, sizeof(set_desc));
	set_desc.bindings = r->set_bindings;
	set_desc.binding_count = 2u;
	set_desc.debug_name = "ui-desc-set";
	set = api->create_descriptor_set(dev, &set_desc);
	if (!sk_descriptor_set_t_is_valid(set)) {
		return sk_descriptor_set_t_zero();
	}
	memset(writes, 0, sizeof(writes));
	writes[0].binding = 0u;
	writes[0].type = SK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	writes[0].texture_view = view;
	writes[1].binding = 1u;
	writes[1].type = SK_DESCRIPTOR_TYPE_SAMPLER;
	writes[1].sampler = sampler;
	api->update_descriptor_set(dev, set, writes, 2u);
	return set;
}

/**
 * Refresh the per-frame host-image descriptor cache from encode info. Each
 * IMAGE mesh texture_id indexes @p images->views (out of range → white
 * fallback). Sets are recreated only when the bound view changes.
 */
static i32 ui_render_sync_host_images(sk_ui_renderer_t* r, const sk_ui_renderer_images_t* images) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	u32 i;
	for (i = 0u; i < UI_HOST_IMAGE_CAP; ++i) {
		ui_gpu_host_image_t* slot = &r->host_images[i];
		sk_texture_view_t want = sk_texture_view_t_zero();
		if (images != NULL && i < images->count) {
			want = images->views[i];
		}
		if (slot->valid != 0u && !sk_texture_view_t_eq(slot->view, want)) {
			if (sk_descriptor_set_t_is_valid(slot->desc_set)) {
				api->destroy_descriptor_set(dev, slot->desc_set);
			}
			memset(slot, 0, sizeof(*slot));
		}
		if (sk_texture_view_t_is_valid(want) && slot->valid == 0u) {
			slot->desc_set = ui_render_make_image_set(r, want, r->sampler_nearest);
			if (!sk_descriptor_set_t_is_valid(slot->desc_set)) {
				return -1;
			}
			slot->view = want;
			slot->valid = 1u;
		}
	}
	return 0;
}

/** Device-local VB/IB (uploaded via update_buffer each frame — matches triangle test). */
static i32 ui_render_ensure_dynamic_buffer(sk_ui_renderer_t* r, sk_buffer_t* out_buf, u64* out_cap, u64 need, u32 usage_flags, const_chr_t debug_name) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	sk_buffer_desc_t desc;
	sk_buffer_t buf;
	u64 cap;

	if (need == 0u) {
		need = 4u;
	}
	if (*out_cap >= need && sk_buffer_t_is_valid(*out_buf)) {
		return 0;
	}

	cap = ui_render_max_u64(need, *out_cap > 0u ? (*out_cap * 2u) : need);
	cap = ui_render_align_u64(cap, 256u);

	if (sk_buffer_t_is_valid(*out_buf)) {
		api->destroy_buffer(dev, *out_buf);
		*out_buf = sk_buffer_t_zero();
		*out_cap = 0u;
	}

	memset(&desc, 0, sizeof(desc));
	desc.size = cap;
	desc.usage_flags = usage_flags | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	desc.host_visible = false;
	desc.persistent_mapped = false;
	desc.debug_name = debug_name;
	buf = api->create_buffer(dev, &desc);
	if (!sk_buffer_t_is_valid(buf)) {
		return -1;
	}
	*out_buf = buf;
	*out_cap = cap;
	return 0;
}

static i32 ui_render_ensure_staging(sk_ui_renderer_t* r, u64 need) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	sk_buffer_desc_t desc;
	sk_buffer_t buf;
	u64 cap;

	if (need == 0u) {
		return 0;
	}
	if (r->staging_capacity >= need && sk_buffer_t_is_valid(r->staging)) {
		return 0;
	}

	cap = ui_render_max_u64(need, r->staging_capacity > 0u ? (r->staging_capacity * 2u) : need);
	cap = ui_render_align_u64(cap, 256u);

	if (sk_buffer_t_is_valid(r->staging)) {
		api->destroy_buffer(dev, r->staging);
		r->staging = sk_buffer_t_zero();
		r->staging_capacity = 0u;
	}

	memset(&desc, 0, sizeof(desc));
	desc.size = cap;
	desc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	desc.host_visible = true;
	desc.persistent_mapped = true;
	desc.debug_name = "ui-staging";
	buf = api->create_buffer(dev, &desc);
	if (!sk_buffer_t_is_valid(buf)) {
		return -1;
	}
	r->staging = buf;
	r->staging_capacity = cap;
	return 0;
}

static void* ui_render_staging_ptr(sk_ui_renderer_t* r) {
	const sk_render_device_api_t* api = r->api;
	void* p = api->get_buffer_mapped_data(r->device, r->staging);
	if (p == NULL) {
		p = api->buffer_map(r->device, r->staging);
	}
	return p;
}

/**
 * Upload CPU texels into a GPU texture via staging buffer + copy.
 *
 * Uses a full memory_barrier around the host write and image copies so lavapipe
 * and discrete GPUs both see coherent host→device visibility without relying on
 * fine-grained buffer state tracking for HOST_VISIBLE heaps.
 */
static i32 ui_render_upload_texture_bytes(sk_ui_renderer_t* r, sk_command_buffer_t cmd, sk_texture_t tex, u32 width, u32 height, const u8* pixels, u32 bytes_per_pixel) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	const u64 nbytes = (u64)width * (u64)height * (u64)bytes_per_pixel;
	void* staging;
	sk_buffer_texture_copy_t copy;

	if (pixels == NULL || width == 0u || height == 0u || bytes_per_pixel == 0u) {
		return -1;
	}
	if (ui_render_ensure_staging(r, nbytes) != 0) {
		return -1;
	}
	staging = ui_render_staging_ptr(r);
	if (staging == NULL) {
		return -1;
	}
	memcpy(staging, pixels, (size_t)nbytes);

	/* Host write complete before GPU reads staging. */
	api->memory_barrier(dev, cmd);

	api->resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_COPY_DEST, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_AUTOMATIC,
								  (u32)SK_BARRIER_SYNC_TRANSFER);

	memset(&copy, 0, sizeof(copy));
	copy.buffer = r->staging;
	copy.buffer_offset = 0u;
	copy.texture = tex;
	copy.mip_level = 0u;
	copy.array_layer = 0u;
	copy.texture_extent.width = width;
	copy.texture_extent.height = height;
	copy.texture_extent.depth = 1u;
	api->copy_buffer_to_texture(dev, cmd, &copy);

	api->resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_COPY_DEST, SK_RESOURCE_STATE_SHADER_READ, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_TRANSFER,
								  (u32)SK_BARRIER_SYNC_GRAPHICS);
	api->memory_barrier(dev, cmd);
	return 0;
}

static i32 ui_render_upload_texture_rgba8(sk_ui_renderer_t* r, sk_command_buffer_t cmd, sk_texture_t tex, u32 width, u32 height, const u8* pixels) {
	/* Fast path: solid white 1x1 via clear (avoids staging on some ICDs). */
	if (width == 1u && height == 1u && pixels[0] == 255u && pixels[1] == 255u && pixels[2] == 255u && pixels[3] == 255u) {
		sk_clear_values_t clear;
		const sk_render_device_api_t* api = r->api;
		sk_render_device_t dev = r->device;
		memset(&clear, 0, sizeof(clear));
		clear.color.r = 1.0f;
		clear.color.g = 1.0f;
		clear.color.b = 1.0f;
		clear.color.a = 1.0f;
		api->resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_COPY_DEST, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_AUTOMATIC,
									  (u32)SK_BARRIER_SYNC_TRANSFER);
		api->clear_texture(dev, cmd, tex, &clear, 0u, 1u, 0u, 1u);
		api->resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_COPY_DEST, SK_RESOURCE_STATE_SHADER_READ, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_TRANSFER,
									  (u32)SK_BARRIER_SYNC_GRAPHICS);
		return 0;
	}
	return ui_render_upload_texture_bytes(r, cmd, tex, width, height, pixels, 4u);
}

static ui_gpu_msdf_atlas_t* ui_render_msdf_slot(sk_ui_renderer_t* r, u32 font_id) {
	u32 i;
	ui_gpu_msdf_atlas_t* empty = NULL;
	for (i = 0u; i < UI_MSDF_ATLAS_CAP; ++i) {
		ui_gpu_msdf_atlas_t* slot = &r->msdf_atlases[i];
		if (slot->valid != 0u && slot->font_id == font_id) {
			return slot;
		}
		if (empty == NULL && slot->valid == 0u) {
			empty = slot;
		}
	}
	return empty;
}

static i32 ui_render_ensure_msdf_atlas(sk_ui_renderer_t* r, sk_command_buffer_t cmd, u32 font_id, const sk_ui_msdf_atlas_t* cpu) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	ui_gpu_msdf_atlas_t* gpu;
	sk_texture_desc_t tdesc;
	sk_texture_view_desc_t vdesc;
	u8* rgba;
	u32 i;
	u32 px_count;
	const sk_allocator_t* a;

	if (font_id == 0u || cpu == NULL || cpu->pixels == NULL || cpu->width == 0u || cpu->height == 0u || cpu->channels < 3u) {
		return -1;
	}
	gpu = ui_render_msdf_slot(r, font_id);
	if (gpu == NULL) {
		return -1;
	}

	if (gpu->valid != 0u && gpu->width == cpu->width && gpu->height == cpu->height && gpu->generation == cpu->generation) {
		gpu->px_range = cpu->px_range > 0.0f ? cpu->px_range : 2.0f;
		return 0;
	}

	if (gpu->valid != 0u && (gpu->width != cpu->width || gpu->height != cpu->height)) {
		if (gpu->desc_valid != 0u && sk_descriptor_set_t_is_valid(gpu->desc_set)) {
			api->destroy_descriptor_set(dev, gpu->desc_set);
		}
		if (sk_texture_view_t_is_valid(gpu->view)) {
			api->destroy_texture_view(dev, gpu->view);
		}
		if (sk_texture_t_is_valid(gpu->texture)) {
			api->destroy_texture(dev, gpu->texture);
		}
		memset(gpu, 0, sizeof(*gpu));
	}

	if (gpu->valid == 0u) {
		memset(&tdesc, 0, sizeof(tdesc));
		tdesc.extent.width = cpu->width;
		tdesc.extent.height = cpu->height;
		tdesc.extent.depth = 1u;
		tdesc.mip_levels = 1u;
		tdesc.array_layers = 1u;
		tdesc.sample_count = 1u;
		tdesc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
		tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_SHADER_RESOURCE | (u32)SK_RESOURCE_USAGE_COPY_DEST;
		tdesc.debug_name = "ui-msdf-atlas";
		gpu->texture = api->create_texture(dev, &tdesc);
		if (!sk_texture_t_is_valid(gpu->texture)) {
			return -1;
		}

		memset(&vdesc, 0, sizeof(vdesc));
		vdesc.texture = gpu->texture;
		vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
		vdesc.base_mip_level = 0u;
		vdesc.mip_level_count = 1u;
		vdesc.base_array_layer = 0u;
		vdesc.array_layer_count = 1u;
		vdesc.debug_name = "ui-msdf-atlas-view";
		gpu->view = api->create_texture_view(dev, &vdesc);
		if (!sk_texture_view_t_is_valid(gpu->view)) {
			api->destroy_texture(dev, gpu->texture);
			gpu->texture = sk_texture_t_zero();
			return -1;
		}
		gpu->width = cpu->width;
		gpu->height = cpu->height;
		gpu->font_id = font_id;
		gpu->valid = 1u;
		if (r->msdf_atlas_count < UI_MSDF_ATLAS_CAP) {
			r->msdf_atlas_count += 1u;
		}
	}

	/* Expand RGB8 → RGBA8 (A=255) for the RGBA8_UNORM texture. */
	a = r->allocator;
	px_count = cpu->width * cpu->height;
	rgba = (u8*)a->alloc(a->instance, (size_t)px_count * 4u);
	if (rgba == NULL) {
		return -1;
	}
	for (i = 0u; i < px_count; ++i) {
		rgba[i * 4u + 0u] = cpu->pixels[i * cpu->channels + 0u];
		rgba[i * 4u + 1u] = cpu->pixels[i * cpu->channels + 1u];
		rgba[i * 4u + 2u] = cpu->pixels[i * cpu->channels + 2u];
		rgba[i * 4u + 3u] = 255u;
	}
	if (ui_render_upload_texture_rgba8(r, cmd, gpu->texture, cpu->width, cpu->height, rgba) != 0) {
		a->free(a->instance, rgba);
		return -1;
	}
	a->free(a->instance, rgba);

	gpu->generation = cpu->generation;
	gpu->px_range = cpu->px_range > 0.0f ? cpu->px_range : 2.0f;

	if (gpu->desc_valid != 0u && sk_descriptor_set_t_is_valid(gpu->desc_set)) {
		api->destroy_descriptor_set(dev, gpu->desc_set);
		gpu->desc_set = sk_descriptor_set_t_zero();
		gpu->desc_valid = 0u;
	}
	gpu->desc_set = ui_render_make_image_set(r, gpu->view, r->sampler);
	if (!sk_descriptor_set_t_is_valid(gpu->desc_set)) {
		return -1;
	}
	gpu->desc_valid = 1u;
	return 0;
}

static i32 ui_render_create_pipeline(sk_ui_renderer_t* r) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	sk_blend_state_desc_t blend;
	sk_rasterizer_state_desc_t rasterizer;
	sk_depth_stencil_state_desc_t depth_stencil;
	sk_graphics_pipeline_desc_t pipe_desc;

	ui_render_destroy_pipeline(r);

	/* Descriptor layout: binding 0 = sampled image, binding 1 = sampler. */
	memset(r->set_bindings, 0, sizeof(r->set_bindings));
	r->set_bindings[0].binding = 0u;
	r->set_bindings[0].descriptor_count = 1u;
	r->set_bindings[0].name = "g_texture";
	r->set_bindings[0].descriptor_type = SK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	r->set_bindings[0].shader_stages = (u32)SK_SHADER_STAGE_PIXEL;
	r->set_bindings[0].view_type = SK_TEXTURE_VIEW_TYPE_2D;

	r->set_bindings[1].binding = 1u;
	r->set_bindings[1].descriptor_count = 1u;
	r->set_bindings[1].name = "g_sampler";
	r->set_bindings[1].descriptor_type = SK_DESCRIPTOR_TYPE_SAMPLER;
	r->set_bindings[1].shader_stages = (u32)SK_SHADER_STAGE_PIXEL;

	memset(&r->set_layout, 0, sizeof(r->set_layout));
	r->set_layout.set = 0u;
	r->set_layout.bindings = r->set_bindings;
	r->set_layout.binding_count = 2u;
	r->set_layout.debug_name = "ui-desc-layout";

	memset(&r->push_range, 0, sizeof(r->push_range));
	r->push_range.name = "UIPush";
	r->push_range.offset = 0u;
	r->push_range.size = (u32)UI_PUSH_SIZE;
	r->push_range.shader_stages = (u32)SK_SHADER_STAGE_VERTEX | (u32)SK_SHADER_STAGE_PIXEL;

	memset(r->vertex_inputs, 0, sizeof(r->vertex_inputs));
	r->vertex_inputs[0].location = 0u;
	r->vertex_inputs[0].offset = 0u;
	r->vertex_inputs[0].name = "POSITION";
	r->vertex_inputs[0].format = SK_PIXEL_FORMAT_RG32_FLOAT;
	r->vertex_inputs[0].size = 8u;
	r->vertex_inputs[1].location = 1u;
	r->vertex_inputs[1].offset = 8u;
	r->vertex_inputs[1].name = "TEXCOORD";
	r->vertex_inputs[1].format = SK_PIXEL_FORMAT_RG32_FLOAT;
	r->vertex_inputs[1].size = 8u;
	r->vertex_inputs[2].location = 2u;
	r->vertex_inputs[2].offset = 16u;
	r->vertex_inputs[2].name = "COLOR";
	r->vertex_inputs[2].format = SK_PIXEL_FORMAT_R32_UINT;
	r->vertex_inputs[2].size = 4u;

	memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.polygon_mode = SK_POLYGON_MODE_FILL;
	rasterizer.cull_mode = SK_CULL_MODE_NONE;
	rasterizer.front_face = SK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.line_width = 1.0f;

	memset(&depth_stencil, 0, sizeof(depth_stencil));
	depth_stencil.depth_test_enable = false;
	depth_stencil.depth_write_enable = false;
	depth_stencil.depth_compare_op = SK_COMPARE_OP_ALWAYS;

	memset(&blend, 0, sizeof(blend));
	blend.blend_enable = true;
	blend.src_color_blend_factor = SK_BLEND_FACTOR_SRC_ALPHA;
	blend.dst_color_blend_factor = SK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.color_blend_op = SK_BLEND_OP_ADD;
	blend.src_alpha_blend_factor = SK_BLEND_FACTOR_ONE;
	blend.dst_alpha_blend_factor = SK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alpha_blend_op = SK_BLEND_OP_ADD;
	blend.color_write_mask = (u32)SK_COLOR_MASK_COMPONENT_ALL;

	memset(&pipe_desc, 0, sizeof(pipe_desc));
	pipe_desc.vertex_input_stride = UINT32_MAX;
	pipe_desc.pipeline.input_variables = r->vertex_inputs;
	pipe_desc.pipeline.input_variable_count = 3u;
	pipe_desc.pipeline.stride = (u32)sizeof(sk_ui_draw_vertex_t);
	pipe_desc.pipeline.descriptors = &r->set_layout;
	pipe_desc.pipeline.descriptor_count = 1u;
	pipe_desc.pipeline.push_constants = &r->push_range;
	pipe_desc.pipeline.push_constant_count = 1u;
	pipe_desc.vertex_shader = r->vs;
	pipe_desc.fragment_shader = r->ps;
	pipe_desc.topology = SK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pipe_desc.rasterizer_state = rasterizer;
	pipe_desc.depth_stencil_state = depth_stencil;
	pipe_desc.blend_states = &blend;
	pipe_desc.blend_state_count = 1u;
	pipe_desc.render_pass = r->render_pass;
	pipe_desc.debug_name = "ui-pipeline";

	r->pipeline = api->create_graphics_pipeline(dev, &pipe_desc);
	if (!sk_pipeline_t_is_valid(r->pipeline)) {
		return -1;
	}

	/* One descriptor set per texture; updated only outside draw recording. */
	r->desc_set_white = ui_render_make_image_set(r, r->white_view, r->sampler);
	if (!sk_descriptor_set_t_is_valid(r->desc_set_white)) {
		ui_render_destroy_pipeline(r);
		return -1;
	}

	/* IMAGE draws (host texture quads, viewport ImGuiDrawTextureView) use a
	 * nearest min/mag + clamp-to-edge sampler per manifest §16 — no bilinear
	 * blur on the viewport texture and no repeat wrapping. This set binds the
	 * 1x1 white texel and is the fallback when no host view is bound. */
	r->desc_set_image = ui_render_make_image_set(r, r->white_view, r->sampler_nearest);
	if (!sk_descriptor_set_t_is_valid(r->desc_set_image)) {
		ui_render_destroy_pipeline(r);
		return -1;
	}
	return 0;
}

static i32 ui_render_compile_shaders(sk_ui_renderer_t* r, const sk_dxc_compiler_api_t* dxc) {
	const sk_render_device_api_t* api = r->api;
	sk_render_device_t dev = r->device;
	u8 vs_spirv[UI_SPIRV_CAP];
	u8 ps_spirv[UI_SPIRV_CAP];
	u32 vs_size = 0u;
	u32 ps_size = 0u;
	char log[1024];

	/* Keep SPIR-V as raw bytes — never treat as text. */
	memset(vs_spirv, 0, sizeof(vs_spirv));
	memset(ps_spirv, 0, sizeof(ps_spirv));
	memset(log, 0, sizeof(log));

	if (dxc->compile("main", "vs_6_0", ui_vs_hlsl, (u32)strlen(ui_vs_hlsl), vs_spirv, (u32)sizeof(vs_spirv), &vs_size, log, (u32)sizeof(log)) != 0) {
		return -1;
	}
	if (dxc->compile("main", "ps_6_0", ui_ps_hlsl, (u32)strlen(ui_ps_hlsl), ps_spirv, (u32)sizeof(ps_spirv), &ps_size, log, (u32)sizeof(log)) != 0) {
		return -1;
	}
	if (vs_size < 4u || ps_size < 4u) {
		return -1;
	}

	r->vs = api->create_shader(dev, (const_chr_t)vs_spirv, vs_size, (u32)SK_SHADER_STAGE_VERTEX);
	r->ps = api->create_shader(dev, (const_chr_t)ps_spirv, ps_size, (u32)SK_SHADER_STAGE_PIXEL);
	if (!sk_shader_t_is_valid(r->vs) || !sk_shader_t_is_valid(r->ps)) {
		if (sk_shader_t_is_valid(r->vs)) {
			api->destroy_shader(dev, r->vs);
			r->vs = sk_shader_t_zero();
		}
		if (sk_shader_t_is_valid(r->ps)) {
			api->destroy_shader(dev, r->ps);
			r->ps = sk_shader_t_zero();
		}
		return -1;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public impl                                                                */
/* -------------------------------------------------------------------------- */

sk_ui_renderer_t* ui_renderer_create_impl(const sk_ui_renderer_desc_t* desc) {
	const sk_allocator_t* a;
	sk_ui_renderer_t* r;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	sk_sampler_desc_t sdesc;
	sk_texture_desc_t tdesc;
	sk_texture_view_desc_t vdesc;

	if (desc == NULL || desc->device_api == NULL || desc->dxc == NULL) {
		return NULL;
	}
	if (!sk_render_device_t_is_valid(desc->device) || !sk_render_pass_t_is_valid(desc->render_pass)) {
		return NULL;
	}

	a = desc->allocator != NULL ? desc->allocator : sk_allocator_default();
	r = (sk_ui_renderer_t*)a->alloc(a->instance, sizeof(sk_ui_renderer_t));
	if (r == NULL) {
		return NULL;
	}
	memset(r, 0, sizeof(*r));
	r->allocator = a;
	r->api = desc->device_api;
	r->device = desc->device;
	r->render_pass = desc->render_pass;
	api = r->api;
	dev = r->device;

	if (ui_render_compile_shaders(r, desc->dxc) != 0) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	/* Linear min/mag, no mipmaps (max_lod 0): required for MSDF atlases so
	 * bilinear filtering does not introduce mipmap-induced distance bleed. */
	memset(&sdesc, 0, sizeof(sdesc));
	sdesc.min_filter = SK_FILTER_MODE_LINEAR;
	sdesc.mag_filter = SK_FILTER_MODE_LINEAR;
	sdesc.mipmap_filter = SK_FILTER_MODE_NEAREST;
	sdesc.address_mode_u = SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE;
	sdesc.address_mode_v = SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE;
	sdesc.address_mode_w = SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE;
	sdesc.max_lod = 0.0f;
	sdesc.debug_name = "ui-sampler";
	r->sampler = api->create_sampler(dev, &sdesc);
	if (!sk_sampler_t_is_valid(r->sampler)) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	/* Nearest min/mag + clamp for IMAGE host textures (manifest §16 viewport
	 * case). Reuses the same white texel until host views are wired. */
	memset(&sdesc, 0, sizeof(sdesc));
	sdesc.min_filter = SK_FILTER_MODE_NEAREST;
	sdesc.mag_filter = SK_FILTER_MODE_NEAREST;
	sdesc.mipmap_filter = SK_FILTER_MODE_NEAREST;
	sdesc.address_mode_u = SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE;
	sdesc.address_mode_v = SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE;
	sdesc.address_mode_w = SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE;
	sdesc.max_lod = 0.0f;
	sdesc.debug_name = "ui-sampler-image-nearest";
	r->sampler_nearest = api->create_sampler(dev, &sdesc);
	if (!sk_sampler_t_is_valid(r->sampler_nearest)) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	memset(&tdesc, 0, sizeof(tdesc));
	tdesc.extent.width = 1u;
	tdesc.extent.height = 1u;
	tdesc.extent.depth = 1u;
	tdesc.mip_levels = 1u;
	tdesc.array_layers = 1u;
	tdesc.sample_count = 1u;
	tdesc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_SHADER_RESOURCE | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	tdesc.debug_name = "ui-white-tex";
	r->white_tex = api->create_texture(dev, &tdesc);
	if (!sk_texture_t_is_valid(r->white_tex)) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	memset(&vdesc, 0, sizeof(vdesc));
	vdesc.texture = r->white_tex;
	vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
	vdesc.base_mip_level = 0u;
	vdesc.mip_level_count = 1u;
	vdesc.base_array_layer = 0u;
	vdesc.array_layer_count = 1u;
	vdesc.debug_name = "ui-white-view";
	r->white_view = api->create_texture_view(dev, &vdesc);
	if (!sk_texture_view_t_is_valid(r->white_view)) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	if (ui_render_create_pipeline(r) != 0) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	/* Pre-size dynamic buffers (grow on demand). */
	if (ui_render_ensure_dynamic_buffer(r, &r->vb, &r->vb_capacity, UI_VB_MIN_BYTES, (u32)SK_RESOURCE_USAGE_VERTEX_BUFFER, "ui-vb") != 0) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}
	if (ui_render_ensure_dynamic_buffer(r, &r->ib, &r->ib_capacity, UI_IB_MIN_BYTES, (u32)SK_RESOURCE_USAGE_INDEX_BUFFER, "ui-ib") != 0) {
		ui_renderer_destroy_impl(r);
		return NULL;
	}

	return r;
}

void ui_renderer_destroy_impl(sk_ui_renderer_t* renderer) {
	const sk_allocator_t* a;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;

	if (renderer == NULL) {
		return;
	}
	a = renderer->allocator;
	api = renderer->api;
	dev = renderer->device;

	ui_render_destroy_pipeline(renderer);
	ui_render_destroy_atlas(renderer);
	ui_render_destroy_buffers(renderer);

	if (sk_texture_view_t_is_valid(renderer->white_view)) {
		api->destroy_texture_view(dev, renderer->white_view);
	}
	if (sk_texture_t_is_valid(renderer->white_tex)) {
		api->destroy_texture(dev, renderer->white_tex);
	}
	if (sk_sampler_t_is_valid(renderer->sampler)) {
		api->destroy_sampler(dev, renderer->sampler);
	}
	if (sk_sampler_t_is_valid(renderer->sampler_nearest)) {
		api->destroy_sampler(dev, renderer->sampler_nearest);
	}
	if (sk_shader_t_is_valid(renderer->vs)) {
		api->destroy_shader(dev, renderer->vs);
	}
	if (sk_shader_t_is_valid(renderer->ps)) {
		api->destroy_shader(dev, renderer->ps);
	}

	memset(renderer, 0, sizeof(*renderer));
	a->free(a->instance, renderer);
}

i32 ui_renderer_set_render_pass_impl(sk_ui_renderer_t* renderer, sk_render_pass_t render_pass) {
	if (renderer == NULL || !sk_render_pass_t_is_valid(render_pass)) {
		return -1;
	}
	renderer->render_pass = render_pass;
	return ui_render_create_pipeline(renderer);
}

i32 ui_renderer_prepare_impl(sk_ui_renderer_t* renderer, const sk_ui_renderer_prepare_info_t* info) {
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	const sk_ui_draw_list_t* dl;
	u64 vb_bytes;
	u64 ib_bytes;

	if (renderer == NULL || info == NULL || info->draw_list == NULL) {
		return -1;
	}
	if (!sk_command_buffer_t_is_valid(info->cmd)) {
		return -1;
	}

	api = renderer->api;
	dev = renderer->device;
	dl = info->draw_list;

	/*
	 * Geometry upload only via update_buffer (same path as the triangle
	 * integration test). Texture uploads for the white texel and font atlas
	 * use staging copies with layout barriers — see ui_render_upload_texture_*.
	 */
	vb_bytes = (u64)dl->vertex_count * sizeof(sk_ui_draw_vertex_t);
	ib_bytes = (u64)dl->index_count * sizeof(u32);
	if (vb_bytes > 0u) {
		if (ui_render_ensure_dynamic_buffer(renderer, &renderer->vb, &renderer->vb_capacity, vb_bytes, (u32)SK_RESOURCE_USAGE_VERTEX_BUFFER, "ui-vb") != 0) {
			return -1;
		}
		api->update_buffer(dev, info->cmd, renderer->vb, 0u, vb_bytes, dl->vertices);
	}
	if (ib_bytes > 0u) {
		if (ui_render_ensure_dynamic_buffer(renderer, &renderer->ib, &renderer->ib_capacity, ib_bytes, (u32)SK_RESOURCE_USAGE_INDEX_BUFFER, "ui-ib") != 0) {
			return -1;
		}
		api->update_buffer(dev, info->cmd, renderer->ib, 0u, ib_bytes, dl->indices);
	}

	/* White + font atlas uploads. */
	if (renderer->white_uploaded == 0u) {
		const u8 white_px[4] = {255u, 255u, 255u, 255u};
		if (ui_render_upload_texture_rgba8(renderer, info->cmd, renderer->white_tex, 1u, 1u, white_px) != 0) {
			return -1;
		}
		renderer->white_uploaded = 1u;
	}
	if (info->font_system != NULL) {
		u32 fi;
		/* Upload every baked MSDF atlas so TEX_MSDF cmds can bind by font id. */
		{
			const u32 nfonts = ui_font_system_font_count(info->font_system);
			i32 uploaded = 0;
			for (fi = 0u; fi < nfonts; ++fi) {
				sk_ui_font_t* font = ui_font_system_font_at(info->font_system, fi);
				sk_ui_msdf_atlas_t atlas;
				if (font == NULL) {
					continue;
				}
				if (ui_font_msdf_ptr(font) == NULL) {
					(void)ui_font_msdf_bake_impl(font);
				}
				if (ui_font_msdf_get_atlas_impl(font, &atlas) != 0) {
					continue;
				}
				if (ui_render_ensure_msdf_atlas(renderer, info->cmd, ui_font_id(font), &atlas) != 0) {
					return -1;
				}
				uploaded = 1;
			}
			if (uploaded == 0 && info->font != NULL) {
				sk_ui_msdf_atlas_t atlas;
				if (ui_font_msdf_get_atlas_impl(info->font, &atlas) == 0) {
					if (ui_render_ensure_msdf_atlas(renderer, info->cmd, ui_font_id(info->font), &atlas) != 0) {
						return -1;
					}
				}
			}
		}
	} else if (info->font != NULL) {
		sk_ui_msdf_atlas_t atlas;
		if (ui_font_msdf_get_atlas_impl(info->font, &atlas) == 0) {
			if (ui_render_ensure_msdf_atlas(renderer, info->cmd, ui_font_id(info->font), &atlas) != 0) {
				return -1;
			}
		}
	}

	api->memory_barrier(dev, info->cmd);
	return 0;
}

static sk_descriptor_set_t ui_render_resolve_desc_set(sk_ui_renderer_t* r, const sk_ui_draw_cmd_t* cmd) {
	if (cmd->texture_kind == SK_UI_DRAW_TEX_MSDF) {
		ui_gpu_msdf_atlas_t* slot = ui_render_msdf_slot(r, cmd->texture_id);
		if (slot != NULL && slot->desc_valid != 0u) {
			return slot->desc_set;
		}
	}
	if (cmd->texture_kind == SK_UI_DRAW_TEX_IMAGE) {
		/* IMAGE host textures: texture_id indexes the encode-time view array;
		 * out of range / unbound falls back to white. All IMAGE draws use the
		 * nearest/clamp image sampler (manifest §16 viewport case). */
		if (r->active_images != NULL && cmd->texture_id < r->active_images->count && cmd->texture_id < UI_HOST_IMAGE_CAP) {
			const ui_gpu_host_image_t* slot = &r->host_images[cmd->texture_id];
			if (slot->valid != 0u && sk_descriptor_set_t_is_valid(slot->desc_set)) {
				return slot->desc_set;
			}
		}
		return r->desc_set_image;
	}
	(void)cmd;
	return r->desc_set_white;
}

static u32 ui_render_mode_for_cmd(sk_ui_renderer_t* r, const sk_ui_draw_cmd_t* cmd, f32* out_px_range) {
	if (cmd->texture_kind == SK_UI_DRAW_TEX_MSDF) {
		ui_gpu_msdf_atlas_t* slot = ui_render_msdf_slot(r, cmd->texture_id);
		if (out_px_range != NULL) {
			*out_px_range = (slot != NULL && slot->px_range > 0.0f) ? slot->px_range : 2.0f;
		}
		return UI_MODE_MSDF;
	}
	if (out_px_range != NULL) {
		*out_px_range = 2.0f;
	}
	return UI_MODE_COLOR;
}

static sk_rect2d_t ui_render_scissor_from_clip(const sk_ui_rect_t* clip, u32 target_w, u32 target_h) {
	sk_rect2d_t sc;
	i32 x0;
	i32 y0;
	i32 x1;
	i32 y1;

	x0 = (i32)clip->x;
	y0 = (i32)clip->y;
	x1 = (i32)(clip->x + clip->width);
	y1 = (i32)(clip->y + clip->height);

	if (x0 < 0) {
		x0 = 0;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (x1 > (i32)target_w) {
		x1 = (i32)target_w;
	}
	if (y1 > (i32)target_h) {
		y1 = (i32)target_h;
	}
	if (x1 < x0) {
		x1 = x0;
	}
	if (y1 < y0) {
		y1 = y0;
	}

	sc.x = x0;
	sc.y = y0;
	sc.width = (u32)(x1 - x0);
	sc.height = (u32)(y1 - y0);
	if (sc.width == 0u && target_w > 0u) {
		sc.width = 1u;
	}
	if (sc.height == 0u && target_h > 0u) {
		sc.height = 1u;
	}
	return sc;
}

i32 ui_renderer_encode_impl(sk_ui_renderer_t* renderer, const sk_ui_renderer_encode_info_t* info) {
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	const sk_ui_draw_list_t* dl;
	sk_viewport_t viewport;
	sk_buffer_t vbs[1];
	u64 vb_off[1];
	ui_push_t push;
	u32 i;
	u32 last_mode;
	i32 bound;

	if (renderer == NULL || info == NULL || info->draw_list == NULL) {
		return -1;
	}
	if (!sk_command_buffer_t_is_valid(info->cmd)) {
		return -1;
	}
	if (info->target_width == 0u || info->target_height == 0u) {
		return -1;
	}
	if (!sk_pipeline_t_is_valid(renderer->pipeline)) {
		return -1;
	}

	api = renderer->api;
	dev = renderer->device;
	dl = info->draw_list;

	if (dl->vertex_count == 0u || dl->index_count == 0u || dl->command_count == 0u) {
		return 0;
	}

	/* Refresh host-image descriptor sets (IMAGE texture_id → view). */
	if (ui_render_sync_host_images(renderer, &info->images) != 0) {
		return -1;
	}
	renderer->active_images = &info->images;

	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (f32)info->target_width;
	viewport.height = (f32)info->target_height;
	viewport.min_depth = 0.0f;
	viewport.max_depth = 1.0f;
	api->set_viewport(dev, info->cmd, 0u, &viewport, 1u);

	/* Pixel → NDC (top-left origin, y-down matches Vulkan viewport). */
	memset(&push, 0, sizeof(push));
	push.scale_x = 2.0f / (f32)info->target_width;
	push.scale_y = 2.0f / (f32)info->target_height;
	push.translate_x = -1.0f;
	push.translate_y = -1.0f;
	push.mode = UI_MODE_COLOR;
	push.px_range = 2.0f;

	api->bind_pipeline(dev, info->cmd, SK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline);
	vbs[0] = renderer->vb;
	vb_off[0] = 0u;
	api->bind_vertex_buffer(dev, info->cmd, 0u, vbs, vb_off, 1u);
	api->bind_index_buffer(dev, info->cmd, renderer->ib, 0u, SK_INDEX_TYPE_UINT32);

	{
		sk_descriptor_set_t last_set = sk_descriptor_set_t_zero();
		last_mode = 0xffffffffu;
		bound = 0;

		for (i = 0u; i < dl->command_count; ++i) {
			const sk_ui_draw_cmd_t* cmd = &dl->commands[i];
			sk_descriptor_set_t set;
			u32 mode;
			f32 px_range = 2.0f;
			sk_rect2d_t scissor;

			if (cmd->kind != SK_UI_DRAW_CMD_MESH) {
				/* PUSH/POP_CLIP: scissor is already baked into MESH.clip. */
				continue;
			}
			if (cmd->index_count == 0u) {
				continue;
			}

			set = ui_render_resolve_desc_set(renderer, cmd);
			mode = ui_render_mode_for_cmd(renderer, cmd, &px_range);

			if (bound == 0 || !sk_descriptor_set_t_eq(set, last_set)) {
				api->bind_descriptor_set(dev, info->cmd, SK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline, 0u, set, NULL, 0u);
				last_set = set;
				bound = 1;
			}

			if (mode != last_mode) {
				push.mode = mode;
				push.px_range = px_range;
				api->push_constants(dev, info->cmd, renderer->pipeline, (u32)SK_SHADER_STAGE_VERTEX | (u32)SK_SHADER_STAGE_PIXEL, 0u, (u32)UI_PUSH_SIZE, &push);
				last_mode = mode;
			}

			scissor = ui_render_scissor_from_clip(&cmd->clip, info->target_width, info->target_height);
			api->set_scissor(dev, info->cmd, 0u, &scissor, 1u);

			api->draw_indexed(dev, info->cmd, cmd->index_count, 1u, cmd->index_offset, 0, 0u);
		}
	}

	renderer->active_images = NULL;
	return 0;
}
