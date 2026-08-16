/**
 * @file editor_icons.c
 * @brief C++ editor icon set (Content/Images) for v2 windows (APX-367).
 *
 * Decodes the five embedded icons (editor/content/images/, mirrored in
 * editor/content/skore_editor_icons_embed.h) with stb_image, packs them into
 * one RGBA8 horizontal-strip atlas, and uploads the atlas through the
 * render-device path the UI renderer already binds (staging buffer → barrier
 * → copy_buffer_to_texture → texture view). Windows resolve the registry on
 * the app context and draw via sk_editor_icons_get + ui->widget_image_rect.
 *
 * The vendored stb_image is STBI_ONLY_PNG, so the loader embeds PNG versions
 * of every icon (the JPEG logo and the ICO are converted at authoring time;
 * see scripts/gen-editor-icons-embed.py).
 *
 * Thumbnail generation is explicitly out of scope (migration manifest §2.2):
 * this module loads the icon bitmaps only.
 */

#include "editor_icons.h"

#include "allocator.h"
#include "content/skore_editor_icons_embed.h"
#include "stb_image.h"

#include <string.h>

/* Strip-atlas metrics: margin around the packed strip, gap between icons. */
#define SK_EDITOR_ICONS_ATLAS_MARGIN 2u
#define SK_EDITOR_ICONS_ATLAS_GAP 4u

typedef struct editor_icon_source_t {
	const_chr_t name; /* stable short id used by windows / tests */
	const_chr_t file; /* C++ Content/Images file name */
	const u8* data;	  /* embedded raw bytes (PNG/JPEG) */
	u32 size;
} editor_icon_source_t;

static const editor_icon_source_t editor_icon_sources[SK_EDITOR_ICON_COUNT] = {
	{"folder", "FolderIcon.png", sk_editor_icon_folder_png, (u32)sk_editor_icon_folder_png_size},
	{"file", "FileIcon.png", sk_editor_icon_file_png, (u32)sk_editor_icon_file_png_size},
	/* The vendored stb_image is STBI_ONLY_PNG, so LogoSmall.jpeg is embedded
	 * as its RGBA PNG conversion (editor/content/images/logo_small.png). */
	{"logo_small", "LogoSmall.jpeg (logo_small.png)", sk_editor_icon_logo_small_png, (u32)sk_editor_icon_logo_small_png_size},
	{"logo_minimal", "minimalist-logo.png", sk_editor_icon_logo_minimal_png, (u32)sk_editor_icon_logo_minimal_png_size},
	{"skore", "skore.ico (skore.png)", sk_editor_icon_skore_png, (u32)sk_editor_icon_skore_png_size},
};

typedef struct editor_icon_rect_t {
	u32 x;
	u32 y;
	u32 width;
	u32 height;
} editor_icon_rect_t;

/* CPU-side decode + pack result (RGBA8 atlas + per-icon sub-rects). */
typedef struct editor_icon_atlas_t {
	u8* pixels; /* atlas_width * atlas_height * 4, tightly packed */
	u32 width;
	u32 height;
	editor_icon_rect_t rects[SK_EDITOR_ICON_COUNT];
} editor_icon_atlas_t;

struct sk_editor_icons_t {
	const sk_render_device_api_t* rd;
	sk_render_device_t device;
	sk_texture_t texture;
	sk_texture_view_t view;
	sk_buffer_t staging;
	sk_queue_t queue;
	sk_command_buffer_t cmd;
	sk_fence_t fence;
	/* Host image bindings for the UI renderer. Slot 0 stays zero (the paint
	 * pass treats texture_id 0 as "no texture" — no quad), each icon id
	 * owns slot (id + 1) pointing at the shared atlas view, so the texture
	 * id handed to widget_image_rect is always non-zero. */
	sk_texture_view_t views[SK_EDITOR_ICON_COUNT + 1];
	sk_editor_icon_t icons[SK_EDITOR_ICON_COUNT];
};

/* ------------------------------------------------------------------ */
/*  Decode + atlas (CPU)                                               */
/* ------------------------------------------------------------------ */

typedef struct editor_icon_decoded_t {
	u8* pixels; /* RGBA8, width*height*4 (stbi-owned; stbi_image_free) */
	u32 width;
	u32 height;
} editor_icon_decoded_t;

static i32 editor_icons_decode_source(const editor_icon_source_t* src, editor_icon_decoded_t* out) {
	i32 w = 0;
	i32 h = 0;
	i32 channels = 0;
	u8* px = stbi_load_from_memory(src->data, (int)src->size, &w, &h, &channels, 4);
	if (px == NULL || w <= 0 || h <= 0) {
		if (px != NULL) {
			stbi_image_free(px);
		}
		return -1;
	}
	out->pixels = px;
	out->width = (u32)w;
	out->height = (u32)h;
	return 0;
}

static i32 editor_icons_decode_atlas(editor_icon_atlas_t* out) {
	const sk_allocator_t* alloc = sk_allocator_default();
	editor_icon_decoded_t dec[SK_EDITOR_ICON_COUNT];
	u32 i;
	u32 cursor;
	u32 max_h = 0u;
	u32 row_bytes;
	u8* dst;

	memset(out, 0, sizeof(*out));
	memset(dec, 0, sizeof(dec));

	/* Pass 1: decode every icon; remember dims; compute strip size. */
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		if (editor_icons_decode_source(&editor_icon_sources[i], &dec[i]) != 0) {
			goto fail;
		}
		if (dec[i].height > max_h) {
			max_h = dec[i].height;
		}
	}
	cursor = SK_EDITOR_ICONS_ATLAS_MARGIN;
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		out->rects[i].x = cursor;
		out->rects[i].y = SK_EDITOR_ICONS_ATLAS_MARGIN;
		out->rects[i].width = dec[i].width;
		out->rects[i].height = dec[i].height;
		cursor += dec[i].width + SK_EDITOR_ICONS_ATLAS_GAP;
	}
	out->width = cursor - SK_EDITOR_ICONS_ATLAS_GAP + SK_EDITOR_ICONS_ATLAS_MARGIN;
	out->height = max_h + 2u * SK_EDITOR_ICONS_ATLAS_MARGIN;

	row_bytes = out->width * 4u;
	out->pixels = (u8*)alloc->alloc(alloc->instance, row_bytes * out->height);
	if (out->pixels == NULL) {
		goto fail;
	}
	memset(out->pixels, 0, row_bytes * out->height);

	/* Pass 2: blit each decoded icon into its sub-rect (top-left aligned). */
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		const editor_icon_rect_t* r = &out->rects[i];
		u32 y;
		dst = out->pixels + (u32)r->y * row_bytes + (u32)r->x * 4u;
		for (y = 0u; y < dec[i].height; ++y) {
			memcpy(dst, dec[i].pixels + (size_t)y * dec[i].width * 4u, (size_t)dec[i].width * 4u);
			dst += row_bytes;
		}
		stbi_image_free(dec[i].pixels);
		dec[i].pixels = NULL;
	}
	return 0;

fail:
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		if (dec[i].pixels != NULL) {
			stbi_image_free(dec[i].pixels);
			dec[i].pixels = NULL;
		}
	}
	if (out->pixels != NULL) {
		alloc->free(alloc->instance, out->pixels);
		out->pixels = NULL;
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/*  Registry (GPU upload + lookup)                                     */
/* ------------------------------------------------------------------ */

void sk_editor_icons_destroy(sk_editor_icons_t* icons) {
	const sk_allocator_t* alloc;
	const sk_render_device_api_t* rd;
	sk_render_device_t dev;
	if (icons == NULL) {
		return;
	}
	rd = icons->rd;
	dev = icons->device;
	if (rd != NULL && sk_render_device_t_is_valid(dev)) {
		if (sk_fence_t_is_valid(icons->fence)) {
			rd->destroy_fence(dev, icons->fence);
			icons->fence = sk_fence_t_zero();
		}
		if (sk_command_buffer_t_is_valid(icons->cmd)) {
			rd->destroy_command_buffer(dev, icons->cmd);
			icons->cmd = sk_command_buffer_t_zero();
		}
		if (sk_queue_t_is_valid(icons->queue)) {
			rd->destroy_queue(dev, icons->queue);
			icons->queue = sk_queue_t_zero();
		}
		if (sk_buffer_t_is_valid(icons->staging)) {
			rd->destroy_buffer(dev, icons->staging);
			icons->staging = sk_buffer_t_zero();
		}
		if (sk_texture_view_t_is_valid(icons->view)) {
			rd->destroy_texture_view(dev, icons->view);
			icons->view = sk_texture_view_t_zero();
		}
		if (sk_texture_t_is_valid(icons->texture)) {
			rd->destroy_texture(dev, icons->texture);
			icons->texture = sk_texture_t_zero();
		}
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, icons);
}

sk_editor_icons_t* sk_editor_icons_create(const sk_render_device_api_t* rd, sk_render_device_t device) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_icons_t* icons;
	editor_icon_atlas_t atlas;
	sk_texture_desc_t tdesc;
	sk_texture_view_desc_t vdesc;
	sk_buffer_desc_t bdesc;
	sk_queue_desc_t qdesc;
	sk_command_buffer_desc_t cbdesc;
	sk_fence_desc_t fdesc;
	sk_command_buffer_begin_info_t begin_info;
	sk_buffer_texture_copy_t copy;
	sk_submit_info_t submit;
	void* mapped;
	u32 i;

	if (rd == NULL || !sk_render_device_t_is_valid(device)) {
		return NULL;
	}
	icons = (sk_editor_icons_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_icons_t));
	if (icons == NULL) {
		return NULL;
	}
	memset(icons, 0, sizeof(*icons));
	icons->rd = rd;
	icons->device = device;

	if (editor_icons_decode_atlas(&atlas) != 0) {
		alloc->free(alloc->instance, icons);
		return NULL;
	}

	/* Atlas texture (the single host image binding for every icon). */
	memset(&tdesc, 0, sizeof(tdesc));
	tdesc.extent.width = atlas.width;
	tdesc.extent.height = atlas.height;
	tdesc.extent.depth = 1u;
	tdesc.mip_levels = 1u;
	tdesc.array_layers = 1u;
	tdesc.sample_count = 1u;
	tdesc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_SHADER_RESOURCE | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	tdesc.debug_name = "sk-editor-icons-atlas";
	icons->texture = rd->create_texture(device, &tdesc);
	if (!sk_texture_t_is_valid(icons->texture)) {
		goto fail;
	}

	memset(&vdesc, 0, sizeof(vdesc));
	vdesc.texture = icons->texture;
	vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
	vdesc.mip_level_count = 1u;
	vdesc.array_layer_count = 1u;
	vdesc.debug_name = "sk-editor-icons-atlas-view";
	icons->view = rd->create_texture_view(device, &vdesc);
	if (!sk_texture_view_t_is_valid(icons->view)) {
		goto fail;
	}

	/* Staging buffer holds the packed atlas bytes. */
	memset(&bdesc, 0, sizeof(bdesc));
	bdesc.size = (u64)atlas.width * (u64)atlas.height * 4u;
	bdesc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	bdesc.host_visible = true;
	bdesc.debug_name = "sk-editor-icons-staging";
	icons->staging = rd->create_buffer(device, &bdesc);
	if (!sk_buffer_t_is_valid(icons->staging)) {
		goto fail;
	}
	mapped = rd->buffer_map(device, icons->staging);
	if (mapped == NULL) {
		goto fail;
	}
	memcpy(mapped, atlas.pixels, bdesc.size);
	rd->buffer_unmap(device, icons->staging);

	memset(&qdesc, 0, sizeof(qdesc));
	qdesc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	icons->queue = rd->create_queue(device, &qdesc);
	if (!sk_queue_t_is_valid(icons->queue)) {
		goto fail;
	}
	memset(&cbdesc, 0, sizeof(cbdesc));
	cbdesc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbdesc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	cbdesc.debug_name = "sk-editor-icons-copy";
	icons->cmd = rd->create_command_buffer(device, &cbdesc);
	if (!sk_command_buffer_t_is_valid(icons->cmd)) {
		goto fail;
	}
	memset(&fdesc, 0, sizeof(fdesc));
	fdesc.debug_name = "sk-editor-icons-copy-fence";
	icons->fence = rd->create_fence(device, &fdesc);
	if (!sk_fence_t_is_valid(icons->fence)) {
		goto fail;
	}

	/* One-shot upload: UNDEFINED → COPY_DEST, copy, COPY_DEST → SHADER_READ. */
	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	if (rd->begin_command_buffer(device, icons->cmd, &begin_info) != 0) {
		goto fail;
	}
	rd->resource_barrier_texture(device, icons->cmd, icons->texture, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_COPY_DEST, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_AUTOMATIC,
								 (u32)SK_BARRIER_SYNC_TRANSFER);
	memset(&copy, 0, sizeof(copy));
	copy.buffer = icons->staging;
	copy.buffer_offset = 0u;
	copy.texture = icons->texture;
	copy.mip_level = 0u;
	copy.array_layer = 0u;
	copy.texture_extent.width = atlas.width;
	copy.texture_extent.height = atlas.height;
	copy.texture_extent.depth = 1u;
	rd->copy_buffer_to_texture(device, icons->cmd, &copy);
	rd->resource_barrier_texture(device, icons->cmd, icons->texture, SK_RESOURCE_STATE_COPY_DEST, SK_RESOURCE_STATE_SHADER_READ, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_TRANSFER,
								 (u32)SK_BARRIER_SYNC_GRAPHICS);
	rd->end_command_buffer(device, icons->cmd);

	memset(&submit, 0, sizeof(submit));
	submit.command_buffers = &icons->cmd;
	submit.command_buffer_count = 1u;
	submit.signal_fence = icons->fence;
	if (rd->submit(device, icons->queue, &submit) != 0 || rd->wait_fences(device, &icons->fence, 1u, true, UINT64_MAX) != 0) {
		goto fail;
	}
	rd->reset_fences(device, &icons->fence, 1u);

	/* The UI renderer binds one host image per slot: every icon owns its own
	 * non-zero slot (id + 1) pointing at the shared atlas view, because the
	 * paint pass treats texture_id 0 as "no texture" (no quad emitted). */
	memset(icons->views, 0, sizeof(icons->views));
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		icons->views[i + 1u] = icons->view;
	}

	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		const editor_icon_rect_t* r = &atlas.rects[i];
		sk_editor_icon_t* ic = &icons->icons[i];
		ic->view_index = i + 1u;
		ic->uv0x = (f32)r->x / (f32)atlas.width;
		ic->uv0y = (f32)r->y / (f32)atlas.height;
		ic->uv1x = (f32)(r->x + r->width) / (f32)atlas.width;
		ic->uv1y = (f32)(r->y + r->height) / (f32)atlas.height;
		ic->width = r->width;
		ic->height = r->height;
		ic->name = editor_icon_sources[i].name;
		ic->file = editor_icon_sources[i].file;
	}

	alloc->free(alloc->instance, atlas.pixels);
	return icons;

fail:
	alloc->free(alloc->instance, atlas.pixels);
	sk_editor_icons_destroy(icons);
	return NULL;
}

const sk_editor_icon_t* sk_editor_icons_get(const sk_editor_icons_t* icons, sk_editor_icon_id_t id) {
	if (icons == NULL || (u32)id >= (u32)SK_EDITOR_ICON_COUNT) {
		return NULL;
	}
	return &icons->icons[id];
}

const sk_texture_view_t* sk_editor_icons_views(const sk_editor_icons_t* icons, u32* out_count) {
	if (out_count != NULL) {
		*out_count = 0u;
	}
	if (icons == NULL) {
		return NULL;
	}
	if (out_count != NULL) {
		*out_count = SK_EDITOR_ICON_COUNT + 1u;
	}
	return icons->views;
}

void sk_editor_icons_register(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_icons_t* icons) {
	if (app_context == NULL || app_api == NULL) {
		return;
	}
	app_api->set_api(app_context, SK_EDITOR_ICONS_TYPE_ID, icons);
}

sk_editor_icons_t* sk_editor_icons_resolve(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	if (app_context == NULL || app_api == NULL) {
		return NULL;
	}
	return (sk_editor_icons_t*)app_api->get_api(app_context, SK_EDITOR_ICONS_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "platform.h"
#include "test.h"
#include "ui.h"

#include <stdlib.h>

/* CPU-only decode hook: same decode+pack path as create(), without a device.
 * Exposes per-icon dimensions and non-zero-alpha texel counts so tests can
 * prove the embedded bitmaps actually decoded (and are not blank). */
typedef struct sk_editor_icons_test_decode_t {
	u32 atlas_width;
	u32 atlas_height;
	u32 icon_width[SK_EDITOR_ICON_COUNT];
	u32 icon_height[SK_EDITOR_ICON_COUNT];
	u32 alpha_texels[SK_EDITOR_ICON_COUNT];
} sk_editor_icons_test_decode_t;

static i32 editor_icons_test_decode(sk_editor_icons_test_decode_t* out) {
	editor_icon_atlas_t atlas;
	u32 i;
	memset(out, 0, sizeof(*out));
	if (editor_icons_decode_atlas(&atlas) != 0) {
		return -1;
	}
	out->atlas_width = atlas.width;
	out->atlas_height = atlas.height;
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		const editor_icon_rect_t* r = &atlas.rects[i];
		u32 y;
		out->icon_width[i] = r->width;
		out->icon_height[i] = r->height;
		for (y = 0u; y < r->height; ++y) {
			const u8* row = atlas.pixels + (size_t)(r->y + y) * atlas.width * 4u + (size_t)r->x * 4u;
			u32 x;
			for (x = 0u; x < r->width; ++x) {
				if (row[x * 4u + 3u] != 0u) {
					out->alpha_texels[i]++;
				}
			}
		}
	}
	{
		const sk_allocator_t* alloc = sk_allocator_default();
		alloc->free(alloc->instance, atlas.pixels);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Mock RHI: records the upload call sequence (no GPU needed)         */
/* ------------------------------------------------------------------ */

typedef struct icons_mock_dev_t {
	const sk_allocator_t* allocator;
	sk_texture_desc_t tex_desc;
	sk_buffer_desc_t buf_desc;
	u8* buf_storage;
	u32 texture_count;
	u32 view_count;
	u32 buffer_count;
	u32 destroyed_textures;
	u32 destroyed_views;
	u32 destroyed_buffers;
	u32 copy_count;
	u32 barrier_count;
	u32 submit_count;
	u32 wait_count;
} icons_mock_dev_t;

static sk_render_device_t icons_mock_init(void_ptr_t user, const sk_device_init_desc_t* desc) {
	/* The device handle IS the mock state pointer (same as the render_graph
	 * mock), so every call site can recover the counters via to_ptr. */
	(void)desc;
	return sk_render_device_t_from_ptr(user);
}

static void icons_mock_destroy(sk_render_device_t dev) {
	(void)dev;
}

static sk_texture_t icons_mock_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	d->tex_desc = *desc;
	d->texture_count++;
	return sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)(0x100u + (uintptr_t)d->texture_count));
}

static void icons_mock_destroy_texture(sk_render_device_t dev, sk_texture_t tex) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)tex;
	d->destroyed_textures++;
}

static sk_texture_view_t icons_mock_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)desc;
	d->view_count++;
	return sk_texture_view_t_from_ptr((void_ptr_t)(uintptr_t)(0x200u + (uintptr_t)d->view_count));
}

static void icons_mock_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)view;
	d->destroyed_views++;
}

static sk_buffer_t icons_mock_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	d->buf_desc = *desc;
	d->buf_storage = (u8*)d->allocator->alloc(d->allocator->instance, (size_t)desc->size);
	if (d->buf_storage != NULL) {
		memset(d->buf_storage, 0, (size_t)desc->size);
	}
	d->buffer_count++;
	return sk_buffer_t_from_ptr((void_ptr_t)(uintptr_t)(0x300u + (uintptr_t)d->buffer_count));
}

static void icons_mock_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)buf;
	if (d->buf_storage != NULL) {
		d->allocator->free(d->allocator->instance, d->buf_storage);
		d->buf_storage = NULL;
	}
	d->destroyed_buffers++;
}

static void* icons_mock_buffer_map(sk_render_device_t dev, sk_buffer_t buf) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)buf;
	return d->buf_storage;
}

static void icons_mock_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
}

static sk_queue_t icons_mock_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_queue_t_from_ptr((void*)0x400);
}

static void icons_mock_destroy_queue(sk_render_device_t dev, sk_queue_t q) {
	(void)dev;
	(void)q;
}

static sk_command_buffer_t icons_mock_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_command_buffer_t_from_ptr((void*)0x500);
}

static void icons_mock_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}

static i32 icons_mock_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info) {
	(void)dev;
	(void)cmd;
	(void)info;
	return 0;
}

static void icons_mock_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}

static void icons_mock_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
												u32 base_mip, u32 level_count, u32 base_layer, u32 layer_count, u32 src_sync, u32 dst_sync) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)cmd;
	(void)tex;
	(void)old_state;
	(void)new_state;
	(void)base_mip;
	(void)level_count;
	(void)base_layer;
	(void)layer_count;
	(void)src_sync;
	(void)dst_sync;
	d->barrier_count++;
}

static void icons_mock_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)cmd;
	(void)copy_info;
	d->copy_count++;
}

static i32 icons_mock_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* submit) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)queue;
	(void)submit;
	d->submit_count++;
	return 0;
}

static i32 icons_mock_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns) {
	icons_mock_dev_t* d = (icons_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)fences;
	(void)fence_count;
	(void)wait_all;
	(void)timeout_ns;
	d->wait_count++;
	return 0;
}

static void icons_mock_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count) {
	(void)dev;
	(void)fences;
	(void)fence_count;
}

static sk_fence_t icons_mock_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_fence_t_from_ptr((void*)0x600);
}

static void icons_mock_destroy_fence(sk_render_device_t dev, sk_fence_t fence) {
	(void)dev;
	(void)fence;
}

static const sk_render_device_api_t* icons_mock_rhi(icons_mock_dev_t* d) {
	static sk_render_device_api_t api; /* const table built once; guard allows */
	(void)d;
	memset(&api, 0, sizeof(api));
	api.init = icons_mock_init;
	api.destroy = icons_mock_destroy;
	api.create_texture = icons_mock_create_texture;
	api.destroy_texture = icons_mock_destroy_texture;
	api.create_texture_view = icons_mock_create_texture_view;
	api.destroy_texture_view = icons_mock_destroy_texture_view;
	api.create_buffer = icons_mock_create_buffer;
	api.destroy_buffer = icons_mock_destroy_buffer;
	api.buffer_map = icons_mock_buffer_map;
	api.buffer_unmap = icons_mock_buffer_unmap;
	api.create_queue = icons_mock_create_queue;
	api.destroy_queue = icons_mock_destroy_queue;
	api.create_command_buffer = icons_mock_create_command_buffer;
	api.destroy_command_buffer = icons_mock_destroy_command_buffer;
	api.begin_command_buffer = icons_mock_begin_command_buffer;
	api.end_command_buffer = icons_mock_end_command_buffer;
	api.resource_barrier_texture = icons_mock_resource_barrier_texture;
	api.copy_buffer_to_texture = icons_mock_copy_buffer_to_texture;
	api.submit = icons_mock_submit;
	api.wait_fences = icons_mock_wait_fences;
	api.reset_fences = icons_mock_reset_fences;
	api.create_fence = icons_mock_create_fence;
	api.destroy_fence = icons_mock_destroy_fence;
	return &api;
}

SK_TEST(editor_icons_decode_atlas_and_lookup) {
	sk_editor_icons_test_decode_t dec = {0};
	const sk_editor_icon_t* ic;
	u32 i;

	/* Decode the embedded bitmaps + pack the strip atlas (CPU, no device). */
	TEST_ASSERT_EQUAL_INT(0, editor_icons_test_decode(&dec));

	/* Known source dimensions: folder/file 512x512, logo small 128x128,
	 * minimalist 200x200, skore 128x128 (see editor/content/images/). */
	TEST_ASSERT_EQUAL_UINT(512u, dec.icon_width[SK_EDITOR_ICON_FOLDER]);
	TEST_ASSERT_EQUAL_UINT(512u, dec.icon_height[SK_EDITOR_ICON_FOLDER]);
	TEST_ASSERT_EQUAL_UINT(512u, dec.icon_width[SK_EDITOR_ICON_FILE]);
	TEST_ASSERT_EQUAL_UINT(512u, dec.icon_height[SK_EDITOR_ICON_FILE]);
	TEST_ASSERT_EQUAL_UINT(128u, dec.icon_width[SK_EDITOR_ICON_LOGO_SMALL]);
	TEST_ASSERT_EQUAL_UINT(128u, dec.icon_height[SK_EDITOR_ICON_LOGO_SMALL]);
	TEST_ASSERT_EQUAL_UINT(200u, dec.icon_width[SK_EDITOR_ICON_LOGO_MINIMAL]);
	TEST_ASSERT_EQUAL_UINT(200u, dec.icon_height[SK_EDITOR_ICON_LOGO_MINIMAL]);
	TEST_ASSERT_EQUAL_UINT(128u, dec.icon_width[SK_EDITOR_ICON_SKORE]);
	TEST_ASSERT_EQUAL_UINT(128u, dec.icon_height[SK_EDITOR_ICON_SKORE]);

	/* Strip layout: 2 + (512+4) + (512+4) + (128+4) + (200+4) + 128 + 2. */
	TEST_ASSERT_EQUAL_UINT(2u + 512u + 4u + 512u + 4u + 128u + 4u + 200u + 4u + 128u + 2u, dec.atlas_width);
	TEST_ASSERT_EQUAL_UINT(2u + 512u + 2u, dec.atlas_height);

	/* None of the icons is blank: every one contributes opaque texels. */
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		TEST_ASSERT_TRUE_MESSAGE(dec.alpha_texels[i] > 0u, "icon decoded to an all-transparent image");
	}

	/* Lookup shape: one shared view + per-icon UVs that stay inside [0,1]. */
	ic = sk_editor_icons_get(NULL, SK_EDITOR_ICON_FOLDER);
	TEST_ASSERT_NULL(ic);
	ic = sk_editor_icons_get(NULL, (sk_editor_icon_id_t)SK_EDITOR_ICON_COUNT);
	TEST_ASSERT_NULL(ic);
}

SK_TEST(editor_icons_upload_path_and_views) {
	icons_mock_dev_t d;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	sk_editor_icons_t* icons;
	const sk_texture_view_t* views;
	u32 view_count = 0u;
	const sk_editor_icon_t* ic;
	u32 i;

	memset(&d, 0, sizeof(d));
	d.allocator = sk_allocator_default();
	api = icons_mock_rhi(&d);
	dev = api->init(&d, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));

	icons = sk_editor_icons_create(api, dev);
	TEST_ASSERT_NOT_NULL_MESSAGE(icons, "create failed (decode or mock RHI)");

	/* Atlas uploaded as one RGBA8 texture at the strip dimensions. */
	TEST_ASSERT_EQUAL_UINT(1u, d.texture_count);
	TEST_ASSERT_EQUAL_UINT(1500u, d.tex_desc.extent.width);
	TEST_ASSERT_EQUAL_UINT(516u, d.tex_desc.extent.height);
	TEST_ASSERT_EQUAL_INT((int)SK_PIXEL_FORMAT_RGBA8_UNORM, (int)d.tex_desc.format);
	TEST_ASSERT_EQUAL_UINT(1u, d.view_count);

	/* Staging buffer got the full atlas bytes; one copy + two barriers ran. */
	TEST_ASSERT_EQUAL_UINT(1u, d.buffer_count);
	TEST_ASSERT_EQUAL_UINT(1500u * 516u * 4u, (u32)d.buf_desc.size);
	TEST_ASSERT_NOT_NULL(d.buf_storage);
	TEST_ASSERT_EQUAL_UINT(1u, d.copy_count);
	TEST_ASSERT_EQUAL_UINT(2u, d.barrier_count);
	TEST_ASSERT_TRUE(d.submit_count >= 1u);
	TEST_ASSERT_EQUAL_UINT(1u, d.wait_count);

	/* The registry exposes one host view slot per icon id (slot 0 stays zero:
	 * texture_id 0 means "no texture" in the paint pass). Every slot points
	 * at the shared atlas view. */
	views = sk_editor_icons_views(icons, &view_count);
	TEST_ASSERT_NOT_NULL(views);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_ICON_COUNT + 1u, view_count);
	TEST_ASSERT_FALSE(sk_texture_view_t_is_valid(views[0]));
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		TEST_ASSERT_TRUE(sk_texture_view_t_is_valid(views[i + 1u]));
	}

	/* Lookup: every icon resolves to its own non-zero view slot with an
	 * in-range UV sub-rect and the manifest's file names. */
	for (i = 0u; i < SK_EDITOR_ICON_COUNT; ++i) {
		ic = sk_editor_icons_get(icons, (sk_editor_icon_id_t)i);
		TEST_ASSERT_NOT_NULL(ic);
		TEST_ASSERT_EQUAL_UINT(i + 1u, ic->view_index);
		TEST_ASSERT_TRUE(ic->uv0x >= 0.0f && ic->uv0x < ic->uv1x && ic->uv1x <= 1.0f);
		TEST_ASSERT_TRUE(ic->uv0y >= 0.0f && ic->uv0y < ic->uv1y && ic->uv1y <= 1.0f);
		TEST_ASSERT_TRUE(ic->width > 0u && ic->height > 0u);
		TEST_ASSERT_NOT_NULL(ic->name);
		TEST_ASSERT_NOT_NULL(ic->file);
	}
	ic = sk_editor_icons_get(icons, SK_EDITOR_ICON_FOLDER);
	TEST_ASSERT_EQUAL_STRING("folder", ic->name);
	TEST_ASSERT_EQUAL_STRING("FolderIcon.png", ic->file);
	TEST_ASSERT_EQUAL_STRING("skore.ico (skore.png)", sk_editor_icons_get(icons, SK_EDITOR_ICON_SKORE)->file);

	/* Cleanup releases every resource (idempotent destroy is NULL-safe). */
	sk_editor_icons_destroy(icons);
	TEST_ASSERT_EQUAL_UINT(1u, d.destroyed_textures);
	TEST_ASSERT_EQUAL_UINT(1u, d.destroyed_views);
	TEST_ASSERT_EQUAL_UINT(1u, d.destroyed_buffers);
	TEST_ASSERT_NULL(d.buf_storage);
	sk_editor_icons_destroy(NULL);

	api->destroy(dev);
}

/* ------------------------------------------------------------------ */
/*  Draw path: icon handle → widget_image_rect → IMAGE mesh command    */
/* ------------------------------------------------------------------ */

/* Load the sk-ui shared plugin on a started app context (same plugins-dir
 * resolution as the editor_window.c / editor_shell.c ui fixtures). */
static const sk_ui_api_t* icons_test_load_ui(sk_app_context_t* app_ctx, const sk_app_api_t* app_api) {
	const sk_platform_api_t* plat = app_api->platform_api(app_ctx);
	const sk_filesystem_api_t* fs = app_api->filesystem_api(app_ctx);
	char base[SK_FS_PATH_MAX];
	char plugins_dir[SK_FS_PATH_MAX];
	sk_directory_iterator_t it;
	char name[SK_FS_PATH_MAX];
	typedef int (*icons_plugin_entry_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		return NULL;
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		return NULL;
	}
	it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		return NULL;
	}
	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		char full_path[SK_FS_PATH_MAX];
		sk_shared_lib_t lib;
		if (strncmp(name, "sk-ui.", 6u) != 0 || sk_is_shared_library_filename(name) == 0) {
			continue;
		}
		if (sk_path_join(sk_str_view_cstr(plugins_dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path)) < 0) {
			break;
		}
		lib = plat->lib_open(full_path);
		if (lib == NULL) {
			break;
		}
		{
			void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
			icons_plugin_entry_fn entry = SK_PTR_TO_FN(icons_plugin_entry_fn, raw);
			if (entry == NULL || entry(app_ctx, app_api) != 0) {
				plat->lib_close(lib);
				break;
			}
		}
		/* Keep the plugin loaded (set_api tables stay valid) until app shutdown. */
		return (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);
	}
	return NULL;
}

/* First IMAGE mesh command with the given texture id (or NULL). */
static const sk_ui_draw_cmd_t* icons_test_find_image_cmd(const sk_ui_draw_list_t* dl, u32 tex_id) {
	u32 c;
	if (dl == NULL) {
		return NULL;
	}
	for (c = 0u; c < dl->command_count; ++c) {
		const sk_ui_draw_cmd_t* cmd = &dl->commands[c];
		if (cmd->kind == SK_UI_DRAW_CMD_MESH && cmd->texture_kind == SK_UI_DRAW_TEX_IMAGE && cmd->texture_id == tex_id) {
			return cmd;
		}
	}
	return NULL;
}

SK_TEST(editor_icons_draw_command_from_lookup) {
	icons_mock_dev_t d;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	sk_editor_icons_t* icons;
	sk_app_boot_t boot = sk_app_startup();
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t img;
	const sk_editor_icon_t* ic;
	const sk_ui_draw_list_t* dl;
	const sk_ui_draw_cmd_t* cmd;
	u32 base;

	TEST_ASSERT_NOT_NULL(boot.context);
	ui = icons_test_load_ui(boot.context, boot.api);
	if (ui == NULL) {
		sk_app_shutdown(boot.context);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}

	memset(&d, 0, sizeof(d));
	d.allocator = sk_allocator_default();
	api = icons_mock_rhi(&d);
	dev = api->init(&d, NULL);
	TEST_ASSERT_TRUE(sk_render_device_t_is_valid(dev));
	icons = sk_editor_icons_create(api, dev);
	TEST_ASSERT_NOT_NULL(icons);

	TEST_ASSERT_EQUAL_INT(0, ui->init());
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	/* Draw the folder icon (the ProjectBrowserWindow tile) through the same
	 * lookup windows call: sk_editor_icons_get → widget_image_rect. */
	root = ui->context_root(ctx);
	ic = sk_editor_icons_get(icons, SK_EDITOR_ICON_FOLDER);
	TEST_ASSERT_NOT_NULL(ic);
	img = ui->widget_image_rect(ctx, root, (i32)ic->view_index, 32.0f, 32.0f, ic->uv0x, ic->uv0y, ic->uv1x, ic->uv1y, NULL, NULL, "icons-test-folder");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(img));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, &(sk_ui_paint_params_t){0}));

	dl = ui->get_draw_list(ctx);
	cmd = icons_test_find_image_cmd(dl, ic->view_index);
	TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "IMAGE mesh with the icon texture id missing");
	TEST_ASSERT_TRUE(cmd->index_count >= 6u);
	base = dl->indices[cmd->index_offset];
	/* Sub-rect UVs from the atlas lookup reach the quad geometry. */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, ic->uv0x, dl->vertices[base + 0u].u);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, ic->uv0y, dl->vertices[base + 0u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, ic->uv1x, dl->vertices[base + 2u].u);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, ic->uv1y, dl->vertices[base + 2u].v);

	ui->context_destroy(ctx);
	ui->shutdown();
	sk_editor_icons_destroy(icons);
	api->destroy(dev);
	sk_app_shutdown(boot.context);
}

#endif /* SK_TESTS */
