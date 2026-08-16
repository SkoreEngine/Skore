/**
 * @file scene_view_window.c
 * @brief Scene Viewport window (APX-372): v2 migration.
 *
 * Port of main-branch Skore::SceneViewWindow (migration manifest §3.9) onto
 * the v2 editor shell. Per the manifest, real scene rendering is **out of
 * scope** (§2.3 / §4): this window ports the shell only.
 *
 *  - Dockable window: real impl registers before the APX-330 scaffold so
 *    `window_open` picks it (Center dock slot, Scene workspace mask); the
 *    chrome hosts a toolbar row + a viewport area that fills the dock
 *    content (flex-grow), so the window sizes with the dock like the C++
 *    content-region-avail draw.
 *  - Toolbar chrome mirroring the C++ Draw() top bar: gizmo tool selection
 *    (select/translate/rotate/scale), world/local mode, snap toggle (right
 *    click opens the snap-size popup), grid toggle, scene-options "…",
 *    Play / Stop simulation, 2D/3D view type, sound toggle (MOCK — v2 has
 *    no AudioEngine), camera options and viewport-options popups.
 *  - Options popups: camera (Field of View slider, Speed slider, Smooth
 *    Camera checkbox, camera position — MOCK free camera state),
 *    viewport settings (Selection Outline / Draw Icons / Draw Gizmos /
 *    Debug Physics / All Physics Shapes / Mesh AABB / NavMesh / Lock
 *    Camera Frustum checkboxes + Forced LOD slider — MOCK RenderDebug),
 *    grid snap (uniform snap size slider), scene options (empty, matching
 *    the C++ where PathTracer is commented out).
 *  - Viewport: a placeholder texture (CPU-generated RGBA8 checkerboard +
 *    crosshair "Scene Viewport" mock, 512x288) drawn letterboxed into the
 *    viewport area, preserving aspect (C++ `aspectRatio = width / height`).
 *    The texture follows the editor_icons host-binding pattern (APX-367):
 *    the host creates the registry with a render device, registers it on
 *    the app context, and binds its view at
 *    SK_EDITOR_SCENE_VIEW_TEX_SLOT in the images array passed to the UI
 *    renderer. Without a registered texture the image node draws its
 *    bordered fallback (texture id 0) and the chrome still works.
 *  - Camera / gizmo / picking are mocks: ViewEntity records the requested
 *    RID (no camera exists), gizmo ops are session state only, simulation
 *    is a session flag, interaction-disabled mirrors the C++ simulation /
 *    read-only combination.
 *
 * Public entry points follow the APX-365 pattern (window-table-pattern.md):
 * the window publishes one process-lifetime `sk_editor_scene_view_ops_t`
 * table registered with `app_api->add_impl` under
 * SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID — never exported as free symbols. Every
 * viewport state change (toolbar clicks, popup edits, ops setters, play/
 * stop) announces through the notify.h VIEWPORT_STATE observer
 * (sk_editor_notify_viewport_state).
 */

#include "scene_view_window.h"

#include "allocator.h"
#include "editor_api.h"
#include "editor_shell.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SK_EDITOR_SCENE_VIEW_STATE_TYPE_ID SK_TYPE_ID("sk.editor.scene_view.state", 0x3d6a91c2e84bf057ULL, 0x7e0c25b9d4a8f163ULL)

#define SV_TOOLBAR_H 30.0f
#define SV_BTN_W 30.0f
#define SV_BTN_H 22.0f
#define SV_BTN_GAP 2.0f
#define SV_MENU_CAP 32u
#define SV_TEX_W 512u
#define SV_TEX_H 288u
#define SV_TEX_BORDER 2u
#define SV_TEX_CELL 32u

/* C++ FreeViewCamera defaults (session-only camera state, MOCK). */
#define SV_CAM_DEFAULT_POS_X 0.0f
#define SV_CAM_DEFAULT_POS_Y 10.0f
#define SV_CAM_DEFAULT_POS_Z 0.0f

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct sv_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	sk_editor_menu_item_desc_t menus[SV_MENU_CAP];
	u32 menu_count;
} sv_class_state_t;

typedef struct sv_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	u32 workspace_id;

	/* Viewport / gizmo preferences (C++ EditorSerialize fields, §3.9). */
	sk_editor_viewport_state_t state;
	f32 camera_speed; /* C++ freeViewCamera.cameraSpeed (session-only) */
	i32 smooth_camera;
	f32 camera_pos[3]; /* C++ freeViewCamera position (MOCK) */
	f32 forced_lod;	   /* MOCK RenderDebug::ForcedLod (-1..) */
	i32 sound_enabled; /* MOCK AudioEngine::IsSoundEnabled */
	i32 read_only;	   /* C++ sceneEditor->IsReadOnly (drives interaction) */

	/* Last entity requested through ViewEntity (C++ double-click frame). */
	sk_rid_t last_viewed_entity;

	/* Placeholder texture binding (resolved from the app context registry). */
	i32 tex_slot;
	u32 tex_width;
	u32 tex_height;

	/* Viewport geometry (previous frame, after layout). */
	f32 viewport_w;
	f32 viewport_h;
	f32 image_w;
	f32 image_h;
	f32 aspect_ratio;

	/* UI handles (live only while a ui context hosts the dock chrome). */
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
	sk_ui_node_t root;
	sk_ui_node_t toolbar;
	sk_ui_node_t viewport;
	sk_ui_node_t image;
	sk_ui_node_t tool_sel;
	sk_ui_node_t tool_move;
	sk_ui_node_t tool_rot;
	sk_ui_node_t tool_scale;
	sk_ui_node_t tool_mode;
	sk_ui_node_t tool_snap;
	sk_ui_node_t tool_grid;
	sk_ui_node_t btn_play;
	sk_ui_node_t btn_stop;
	sk_ui_node_t btn_2d;
	sk_ui_node_t btn_3d;
	sk_ui_node_t btn_vol;
	sk_ui_node_t btn_cam;
	sk_ui_node_t btn_opts;
	sk_ui_node_t btn_scene_opts;
	sk_ui_node_t scene_popup;
	sk_ui_node_t camera_popup;
	sk_ui_node_t viewport_popup;
	sk_ui_node_t snap_popup;
	sk_ui_node_t fov_slider;
	sk_ui_node_t speed_slider;
	sk_ui_node_t smooth_check;
	sk_ui_node_t snap_slider;
	sk_ui_node_t lod_slider;
	sk_ui_node_t opt_outline;
	sk_ui_node_t opt_icons;
	sk_ui_node_t opt_gizmos;
	sk_ui_node_t opt_physics;
	sk_ui_node_t opt_all_shapes;
	sk_ui_node_t opt_aabb;
	sk_ui_node_t opt_navmesh;
	sk_ui_node_t opt_lock;
	u32 ui_built;
} sv_state_t;

/* Placeholder texture registry (host-owned GPU resources, like icons). */
struct sk_editor_scene_view_tex_registry_t {
	const sk_render_device_api_t* rd;
	sk_render_device_t device;
	sk_texture_t texture;
	sk_texture_view_t view;
	sk_buffer_t staging;
	sk_queue_t queue;
	sk_command_buffer_t cmd;
	sk_fence_t fence;
	u32 width;
	u32 height;
	/* Registered on the app context (NULL when created without one). */
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
};

static void sv_init(sk_editor_window_t* window);
static void sv_draw(sk_editor_window_t* window, i32* open);
static void sv_destroy(sk_editor_window_t* window);
static i32 sv_state_interaction_disabled(const sv_state_t* state);

static sk_editor_window_t sv_window = {
	.title = "Scene Viewport",
	.dock_id = "sk.editor_window.scene_view",
	.dock_position = SK_EDITOR_DOCK_FILL,
	.order = 0,
	.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	.init = sv_init,
	.draw = sv_draw,
	.render = NULL,
	.destroy = sv_destroy,
};

static sv_class_state_t* sv_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (sv_class_state_t*)app_api->get_api(app_context, SK_EDITOR_SCENE_VIEW_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  State changes → VIEWPORT_STATE observer                            */
/* ------------------------------------------------------------------ */

static void sv_publish(sv_state_t* state) {
	sk_editor_viewport_state_t snap;
	if (state == NULL) {
		return;
	}
	snap = state->state;
	snap.simulating = state->state.simulating;
	snap.interaction_disabled = sv_state_interaction_disabled(state);
	sk_editor_notify_viewport_state(state->app_context, state->app_api, state->workspace_id, &snap);
}

static i32 sv_state_interaction_disabled(const sv_state_t* state) {
	/* C++ IsSceneInteractionDisabled: windowStartedSimulation ||
	 * (sceneEditor && sceneEditor->HasSelectedUIDocument()). v2 has no
	 * SceneEditor / UI-doc selection, so read_only stands in for the doc
	 * path (MOCK, documented in the header). */
	return (state->state.simulating != 0) || (state->read_only != 0);
}

/* ------------------------------------------------------------------ */
/*  Placeholder texture generation (CPU RGBA8, 512x288)                */
/* ------------------------------------------------------------------ */

/* Fill @p px (SV_TEX_W x SV_TEX_H RGBA8) with a mock viewport: dark
 * vertical gradient, subtle grid, a light crosshair and a border frame. */
static void sv_tex_generate(u8* px, u32 width, u32 height) {
	u32 y;
	u32 x;
	for (y = 0u; y < height; ++y) {
		/* Gradient: lighter at the top (sky), darker at the bottom. */
		f32 t = (f32)y / (f32)(height - 1u);
		u8 r = (u8)((f32)34 + (f32)22 * t);
		u8 g = (u8)((f32)36 + (f32)18 * t);
		u8 b = (u8)((f32)42 + (f32)16 * t);
		for (x = 0u; x < width; ++x) {
			u8 pr = r;
			u8 pg = g;
			u8 pb = b;
			/* Grid lines every SV_TEX_CELL px (subtle lighter lines). */
			if (x % SV_TEX_CELL == 0u || y % SV_TEX_CELL == 0u) {
				pr = (u8)(pr + 14u);
				pg = (u8)(pg + 14u);
				pb = (u8)(pb + 14u);
			}
			/* Crosshair: horizontal + vertical light lines through the center. */
			if (y == height / 2u) {
				pr = (u8)(pr + 30u);
				pg = (u8)(pg + 30u);
				pb = (u8)(pb + 30u);
			}
			if (x == width / 2u) {
				pr = (u8)(pr + 30u);
				pg = (u8)(pg + 30u);
				pb = (u8)(pb + 30u);
			}
			/* Outer frame. */
			if (x < SV_TEX_BORDER || y < SV_TEX_BORDER || x >= width - SV_TEX_BORDER || y >= height - SV_TEX_BORDER) {
				pr = 120u;
				pg = 128u;
				pb = 140u;
			}
			px[(y * width + x) * 4u + 0u] = pr;
			px[(y * width + x) * 4u + 1u] = pg;
			px[(y * width + x) * 4u + 2u] = pb;
			px[(y * width + x) * 4u + 3u] = 255u;
		}
	}
}

sk_editor_scene_view_tex_registry_t* sk_editor_scene_view_texture_create(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_render_device_api_t* rd,
																		 sk_render_device_t device) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_scene_view_tex_registry_t* reg;
	sk_texture_desc_t tdesc;
	sk_texture_view_desc_t vdesc;
	sk_buffer_desc_t bdesc;
	sk_queue_desc_t qdesc;
	sk_command_buffer_desc_t cbdesc;
	sk_fence_desc_t fdesc;
	sk_command_buffer_begin_info_t begin_info;
	sk_buffer_texture_copy_t copy;
	sk_submit_info_t submit;
	u8* pixels;
	void* mapped;

	if (rd == NULL || !sk_render_device_t_is_valid(device)) {
		return NULL;
	}
	reg = (sk_editor_scene_view_tex_registry_t*)alloc->alloc(alloc->instance, sizeof(*reg));
	if (reg == NULL) {
		return NULL;
	}
	memset(reg, 0, sizeof(*reg));
	reg->rd = rd;
	reg->device = device;
	reg->width = SV_TEX_W;
	reg->height = SV_TEX_H;
	reg->app_context = app_context;
	reg->app_api = app_api;

	pixels = (u8*)alloc->alloc(alloc->instance, (size_t)SV_TEX_W * (size_t)SV_TEX_H * 4u);
	if (pixels == NULL) {
		goto fail;
	}
	sv_tex_generate(pixels, SV_TEX_W, SV_TEX_H);

	memset(&tdesc, 0, sizeof(tdesc));
	tdesc.extent.width = SV_TEX_W;
	tdesc.extent.height = SV_TEX_H;
	tdesc.extent.depth = 1u;
	tdesc.mip_levels = 1u;
	tdesc.array_layers = 1u;
	tdesc.sample_count = 1u;
	tdesc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_SHADER_RESOURCE | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	tdesc.debug_name = "sk-editor-scene-view-placeholder";
	reg->texture = rd->create_texture(device, &tdesc);
	if (!sk_texture_t_is_valid(reg->texture)) {
		goto fail;
	}

	memset(&vdesc, 0, sizeof(vdesc));
	vdesc.texture = reg->texture;
	vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
	vdesc.mip_level_count = 1u;
	vdesc.array_layer_count = 1u;
	vdesc.debug_name = "sk-editor-scene-view-placeholder-view";
	reg->view = rd->create_texture_view(device, &vdesc);
	if (!sk_texture_view_t_is_valid(reg->view)) {
		goto fail;
	}

	memset(&bdesc, 0, sizeof(bdesc));
	bdesc.size = (u64)SV_TEX_W * (u64)SV_TEX_H * 4u;
	bdesc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	bdesc.host_visible = true;
	bdesc.debug_name = "sk-editor-scene-view-placeholder-staging";
	reg->staging = rd->create_buffer(device, &bdesc);
	if (!sk_buffer_t_is_valid(reg->staging)) {
		goto fail;
	}
	mapped = rd->buffer_map(device, reg->staging);
	if (mapped == NULL) {
		goto fail;
	}
	memcpy(mapped, pixels, bdesc.size);
	rd->buffer_unmap(device, reg->staging);

	memset(&qdesc, 0, sizeof(qdesc));
	qdesc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	reg->queue = rd->create_queue(device, &qdesc);
	if (!sk_queue_t_is_valid(reg->queue)) {
		goto fail;
	}
	memset(&cbdesc, 0, sizeof(cbdesc));
	cbdesc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbdesc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	cbdesc.debug_name = "sk-editor-scene-view-placeholder-copy";
	reg->cmd = rd->create_command_buffer(device, &cbdesc);
	if (!sk_command_buffer_t_is_valid(reg->cmd)) {
		goto fail;
	}
	memset(&fdesc, 0, sizeof(fdesc));
	fdesc.debug_name = "sk-editor-scene-view-placeholder-copy-fence";
	reg->fence = rd->create_fence(device, &fdesc);
	if (!sk_fence_t_is_valid(reg->fence)) {
		goto fail;
	}

	/* One-shot upload: UNDEFINED → COPY_DEST, copy, COPY_DEST → SHADER_READ. */
	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	if (rd->begin_command_buffer(device, reg->cmd, &begin_info) != 0) {
		goto fail;
	}
	rd->resource_barrier_texture(device, reg->cmd, reg->texture, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_COPY_DEST, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_AUTOMATIC,
								 (u32)SK_BARRIER_SYNC_TRANSFER);
	memset(&copy, 0, sizeof(copy));
	copy.buffer = reg->staging;
	copy.buffer_offset = 0u;
	copy.texture = reg->texture;
	copy.mip_level = 0u;
	copy.array_layer = 0u;
	copy.texture_extent.width = SV_TEX_W;
	copy.texture_extent.height = SV_TEX_H;
	copy.texture_extent.depth = 1u;
	rd->copy_buffer_to_texture(device, reg->cmd, &copy);
	rd->resource_barrier_texture(device, reg->cmd, reg->texture, SK_RESOURCE_STATE_COPY_DEST, SK_RESOURCE_STATE_SHADER_READ, 0u, 1u, 0u, 1u, (u32)SK_BARRIER_SYNC_TRANSFER,
								 (u32)SK_BARRIER_SYNC_GRAPHICS);
	rd->end_command_buffer(device, reg->cmd);

	memset(&submit, 0, sizeof(submit));
	submit.command_buffers = &reg->cmd;
	submit.command_buffer_count = 1u;
	submit.signal_fence = reg->fence;
	rd->submit(device, reg->queue, &submit);
	rd->wait_fences(device, &reg->fence, 1u, true, UINT64_MAX);

	alloc->free(alloc->instance, pixels);
	return reg;

fail:
	alloc->free(alloc->instance, pixels);
	sk_editor_scene_view_texture_destroy(reg);
	return NULL;
}

void sk_editor_scene_view_texture_destroy(sk_editor_scene_view_tex_registry_t* reg) {
	const sk_allocator_t* alloc;
	const sk_render_device_api_t* rd;
	sk_render_device_t dev;
	if (reg == NULL) {
		return;
	}
	rd = reg->rd;
	dev = reg->device;
	if (rd != NULL && sk_render_device_t_is_valid(dev)) {
		if (sk_fence_t_is_valid(reg->fence)) {
			rd->destroy_fence(dev, reg->fence);
			reg->fence = sk_fence_t_zero();
		}
		if (sk_command_buffer_t_is_valid(reg->cmd)) {
			rd->destroy_command_buffer(dev, reg->cmd);
			reg->cmd = sk_command_buffer_t_zero();
		}
		if (sk_queue_t_is_valid(reg->queue)) {
			rd->destroy_queue(dev, reg->queue);
			reg->queue = sk_queue_t_zero();
		}
		if (sk_buffer_t_is_valid(reg->staging)) {
			rd->destroy_buffer(dev, reg->staging);
			reg->staging = sk_buffer_t_zero();
		}
		if (sk_texture_view_t_is_valid(reg->view)) {
			rd->destroy_texture_view(dev, reg->view);
			reg->view = sk_texture_view_t_zero();
		}
		if (sk_texture_t_is_valid(reg->texture)) {
			rd->destroy_texture(dev, reg->texture);
			reg->texture = sk_texture_t_zero();
		}
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, reg);
}

const sk_texture_view_t* sk_editor_scene_view_texture_view(const sk_editor_scene_view_tex_registry_t* reg) {
	if (reg == NULL) {
		return NULL;
	}
	return &reg->view;
}

void sk_editor_scene_view_texture_register(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_scene_view_tex_registry_t* reg) {
	if (app_context == NULL || app_api == NULL || reg == NULL) {
		return;
	}
	reg->app_context = app_context;
	reg->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_SCENE_VIEW_TEX_TYPE_ID, reg);
}

sk_editor_scene_view_tex_registry_t* sk_editor_scene_view_texture_resolve(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	if (app_context == NULL || app_api == NULL) {
		return NULL;
	}
	return (sk_editor_scene_view_tex_registry_t*)app_api->get_api(app_context, SK_EDITOR_SCENE_VIEW_TEX_TYPE_ID);
}

sk_editor_scene_view_tex_t sk_editor_scene_view_texture_get(const sk_editor_scene_view_tex_registry_t* reg) {
	sk_editor_scene_view_tex_t out;
	memset(&out, 0, sizeof(out));
	if (reg == NULL) {
		return out;
	}
	out.texture_id = SK_EDITOR_SCENE_VIEW_TEX_SLOT;
	out.width = reg->width;
	out.height = reg->height;
	return out;
}

/* ------------------------------------------------------------------ */
/*  UI chrome (toolbar + viewport + options popups)                    */
/* ------------------------------------------------------------------ */

static void sv_style_button(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_SHRINK;
	p.layout.width = sk_ui_pt(SV_BTN_W);
	p.layout.height = sk_ui_pt(SV_BTN_H);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void sv_apply_toolbar_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_PADDING |
			 SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = SV_BTN_GAP;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt(SV_TOOLBAR_H);
	p.layout.flex_shrink = 0.0f;
	p.layout.padding.left = 4.0f;
	p.layout.padding.right = 4.0f;
	p.background_color = sk_ui_rgba(0.16f, 0.17f, 0.20f, 1.0f);
	p.border_color = sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f);
	p.layout.border.bottom = 1.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void sv_apply_viewport_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_BACKGROUND_COLOR |
			 SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.min_height = sk_ui_pt(48.0f);
	p.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.background_color = sk_ui_rgba(0.05f, 0.06f, 0.08f, 1.0f);
	p.border_color = sk_ui_rgba(0.18f, 0.19f, 0.22f, 1.0f);
	p.layout.border.left = 1.0f;
	p.layout.border.top = 1.0f;
	p.layout.border.right = 1.0f;
	p.layout.border.bottom = 1.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void sv_apply_popup_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR |
			 SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_pt(280.0f);
	p.layout.padding.left = 10.0f;
	p.layout.padding.right = 10.0f;
	p.layout.padding.top = 10.0f;
	p.layout.padding.bottom = 10.0f;
	p.layout.row_gap = 8.0f;
	p.background_color = sk_ui_rgba(0.11f, 0.12f, 0.15f, 0.98f);
	p.border_color = sk_ui_rgba(0.30f, 0.32f, 0.38f, 1.0f);
	p.layout.border.left = 1.0f;
	p.layout.border.top = 1.0f;
	p.layout.border.right = 1.0f;
	p.layout.border.bottom = 1.0f;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(8.0f);
	p.layout.top = sk_ui_pt(SV_TOOLBAR_H + 4.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* Slider row: label + slider under it, so popup tables read as two columns
 * like the C++ BeginTable. */
static sk_ui_node_t sv_popup_slider_row(sv_state_t* state, sk_ui_node_t parent, const_chr_t label, const_chr_t id, f32 min_v, f32 max_v, f32 value) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t slider;
	char slider_id[96];
	(void)ui->widget_label(ctx, parent, label, id);
	(void)snprintf(slider_id, sizeof(slider_id), "%s.slider", id);
	slider = ui->widget_slider(ctx, parent, min_v, max_v, value, slider_id);
	return slider;
}

static void sv_on_fov_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	(void)ctx;
	(void)node;
	if (state == NULL || value < 4.0f || value > 120.0f) {
		return;
	}
	state->state.camera_fov = value;
	sv_publish(state);
}

static void sv_on_speed_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	(void)ctx;
	(void)node;
	if (state != NULL && value >= 1.0f && value <= 100.0f) {
		state->camera_speed = value;
	}
}

static void sv_on_smooth_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	(void)ctx;
	(void)node;
	if (state != NULL) {
		state->smooth_camera = value;
	}
}

static void sv_on_snap_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	u32 i;
	(void)ctx;
	(void)node;
	if (state == NULL || value < 0.1f) {
		return;
	}
	for (i = 0u; i < 3u; ++i) {
		state->state.snap[i] = value;
	}
	sv_publish(state);
}

static void sv_on_lod_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	(void)ctx;
	(void)node;
	if (state != NULL) {
		state->forced_lod = value;
	}
}

static void sv_on_bool_option_change(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	(void)ctx;
	if (state == NULL) {
		return;
	}
	if (sk_ui_node_eq(node, state->opt_outline)) {
		state->state.draw_selection_outline = value;
	} else if (sk_ui_node_eq(node, state->opt_icons)) {
		state->state.draw_icons = value;
	} else if (sk_ui_node_eq(node, state->opt_gizmos)) {
		state->state.draw_component_gizmos = value;
	} else if (sk_ui_node_eq(node, state->opt_physics)) {
		state->state.draw_debug_physics = value;
	} else if (sk_ui_node_eq(node, state->opt_all_shapes)) {
		state->state.show_all_physics_shapes = value;
	} else if (sk_ui_node_eq(node, state->opt_aabb)) {
		state->state.draw_mesh_aabb = value;
	} else if (sk_ui_node_eq(node, state->opt_navmesh)) {
		state->state.draw_nav_mesh = value;
	} else if (sk_ui_node_eq(node, state->opt_lock)) {
		state->state.lock_camera_frustum = value;
	} else {
		return;
	}
	sv_publish(state);
}

static void sv_on_toolbar_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	sv_state_t* state = (sv_state_t*)user;
	(void)ctx;
	if (state == NULL || event == NULL) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN && event->button == SK_UI_POINTER_BUTTON_RIGHT && sk_ui_node_eq(node, state->tool_snap)) {
		/* C++ right-click on the snap button opens the grid-snap popup. */
		if (sk_ui_node_is_valid(state->snap_popup)) {
			(void)state->ui->menu_set_open(state->ui_ctx, state->snap_popup, 1);
		}
		event->consumed = 1;
		return;
	}
	if (event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	if (sk_ui_node_eq(node, state->tool_sel)) {
		state->state.gizmo_operation = SK_EDITOR_GIZMO_OP_SELECT;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->tool_move)) {
		state->state.gizmo_operation = SK_EDITOR_GIZMO_OP_TRANSLATE;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->tool_rot)) {
		state->state.gizmo_operation = SK_EDITOR_GIZMO_OP_ROTATE;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->tool_scale)) {
		state->state.gizmo_operation = SK_EDITOR_GIZMO_OP_SCALE;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->tool_mode)) {
		state->state.gizmo_mode = (state->state.gizmo_mode == SK_EDITOR_GIZMO_MODE_WORLD) ? SK_EDITOR_GIZMO_MODE_LOCAL : SK_EDITOR_GIZMO_MODE_WORLD;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->tool_snap)) {
		state->state.gizmo_snap_enabled = !state->state.gizmo_snap_enabled;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->tool_grid)) {
		state->state.draw_grid = !state->state.draw_grid;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->btn_play)) {
		state->state.simulating = 1;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->btn_stop)) {
		state->state.simulating = 0;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->btn_2d)) {
		state->state.view_type = SK_EDITOR_SCENE_VIEW_TYPE_2D;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->btn_3d)) {
		state->state.view_type = SK_EDITOR_SCENE_VIEW_TYPE_3D;
		sv_publish(state);
	} else if (sk_ui_node_eq(node, state->btn_vol)) {
		state->sound_enabled = !state->sound_enabled;
	} else if (sk_ui_node_eq(node, state->btn_cam)) {
		if (sk_ui_node_is_valid(state->camera_popup)) {
			(void)state->ui->menu_set_open(state->ui_ctx, state->camera_popup, 1);
		}
	} else if (sk_ui_node_eq(node, state->btn_opts)) {
		if (sk_ui_node_is_valid(state->viewport_popup)) {
			(void)state->ui->menu_set_open(state->ui_ctx, state->viewport_popup, 1);
		}
	} else if (sk_ui_node_eq(node, state->btn_scene_opts)) {
		if (sk_ui_node_is_valid(state->scene_popup)) {
			(void)state->ui->menu_set_open(state->ui_ctx, state->scene_popup, 1);
		}
	}
	event->consumed = 1;
}

static void sv_build_popups(sv_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	char buf[64];

	/* Scene options (C++ "scene-options-modal": empty popup). */
	state->scene_popup = ui->widget_menu_popup(ctx, state->root, 0, "sv.scene_popup");
	sv_apply_popup_style(ui, ctx, state->scene_popup);
	(void)ui->widget_label(ctx, state->scene_popup, "Scene options (MOCK: no render scene)", "sv.scene_popup.note");

	/* Camera options (C++ "camera-options-modal"). */
	state->camera_popup = ui->widget_menu_popup(ctx, state->root, 0, "sv.camera_popup");
	sv_apply_popup_style(ui, ctx, state->camera_popup);
	state->fov_slider = sv_popup_slider_row(state, state->camera_popup, "Field of View", "sv.camera.fov", 4.0f, 120.0f, state->state.camera_fov);
	(void)ui->slider_set_on_change(ctx, state->fov_slider, sv_on_fov_change, state);
	state->speed_slider = sv_popup_slider_row(state, state->camera_popup, "Speed", "sv.camera.speed", 1.0f, 100.0f, state->camera_speed);
	(void)ui->slider_set_on_change(ctx, state->speed_slider, sv_on_speed_change, state);
	state->smooth_check = ui->widget_checkbox(ctx, state->camera_popup, state->smooth_camera, "sv.camera.smooth");
	(void)ui->checkbox_set_label(ctx, state->smooth_check, "Smooth Camera");
	(void)ui->checkbox_set_on_change(ctx, state->smooth_check, sv_on_smooth_change, state);
	(void)snprintf(buf, sizeof(buf), "Camera Position (MOCK) %.0f %.0f %.0f", (double)state->camera_pos[0], (double)state->camera_pos[1], (double)state->camera_pos[2]);
	(void)ui->widget_label(ctx, state->camera_popup, buf, "sv.camera.pos");

	/* Viewport settings (C++ "viewport-options-modal"). */
	state->viewport_popup = ui->widget_menu_popup(ctx, state->root, 0, "sv.viewport_popup");
	sv_apply_popup_style(ui, ctx, state->viewport_popup);
	state->opt_outline = ui->widget_checkbox(ctx, state->viewport_popup, state->state.draw_selection_outline, "sv.viewport.outline");
	(void)ui->checkbox_set_label(ctx, state->opt_outline, "Selection Outline");
	state->opt_icons = ui->widget_checkbox(ctx, state->viewport_popup, state->state.draw_icons, "sv.viewport.icons");
	(void)ui->checkbox_set_label(ctx, state->opt_icons, "Draw Icons");
	state->opt_gizmos = ui->widget_checkbox(ctx, state->viewport_popup, state->state.draw_component_gizmos, "sv.viewport.gizmos");
	(void)ui->checkbox_set_label(ctx, state->opt_gizmos, "Draw Gizmos");
	state->opt_physics = ui->widget_checkbox(ctx, state->viewport_popup, state->state.draw_debug_physics, "sv.viewport.physics");
	(void)ui->checkbox_set_label(ctx, state->opt_physics, "Draw Debug Physics");
	state->opt_all_shapes = ui->widget_checkbox(ctx, state->viewport_popup, state->state.show_all_physics_shapes, "sv.viewport.all_shapes");
	(void)ui->checkbox_set_label(ctx, state->opt_all_shapes, "Show All Physics Shapes");
	state->opt_aabb = ui->widget_checkbox(ctx, state->viewport_popup, state->state.draw_mesh_aabb, "sv.viewport.aabb");
	(void)ui->checkbox_set_label(ctx, state->opt_aabb, "Draw Mesh AABB");
	state->opt_navmesh = ui->widget_checkbox(ctx, state->viewport_popup, state->state.draw_nav_mesh, "sv.viewport.navmesh");
	(void)ui->checkbox_set_label(ctx, state->opt_navmesh, "Draw NavMesh");
	state->opt_lock = ui->widget_checkbox(ctx, state->viewport_popup, state->state.lock_camera_frustum, "sv.viewport.lock");
	(void)ui->checkbox_set_label(ctx, state->opt_lock, "Lock Camera Frustum");
	{
		sk_ui_node_t* opts[8] = {&state->opt_outline,	 &state->opt_icons, &state->opt_gizmos,	 &state->opt_physics,
								 &state->opt_all_shapes, &state->opt_aabb,	&state->opt_navmesh, &state->opt_lock};
		u32 i;
		for (i = 0u; i < 8u; ++i) {
			(void)ui->checkbox_set_on_change(ctx, *opts[i], sv_on_bool_option_change, state);
		}
	}
	state->lod_slider = sv_popup_slider_row(state, state->viewport_popup, "Forced LOD (MOCK)", "sv.viewport.lod", -1.0f, 4.0f, state->forced_lod);
	(void)ui->slider_set_on_change(ctx, state->lod_slider, sv_on_lod_change, state);

	/* Grid snap settings (C++ "grid-snap-options-modal"). */
	state->snap_popup = ui->widget_menu_popup(ctx, state->root, 0, "sv.snap_popup");
	sv_apply_popup_style(ui, ctx, state->snap_popup);
	state->snap_slider = sv_popup_slider_row(state, state->snap_popup, "Snap Size (uniform)", "sv.snap.size", 0.1f, 10.0f, state->state.snap[0]);
	(void)ui->slider_set_on_change(ctx, state->snap_slider, sv_on_snap_change, state);

	(void)ui->menu_set_open(ctx, state->scene_popup, 0);
	(void)ui->menu_set_open(ctx, state->camera_popup, 0);
	(void)ui->menu_set_open(ctx, state->viewport_popup, 0);
	(void)ui->menu_set_open(ctx, state->snap_popup, 0);
}

static void sv_build_ui(sv_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_node_callbacks_t cbs;
	sk_ui_style_props_t p;
	sk_ui_node_t spacer;
	sk_ui_color_t border;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "sv.root");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.background_color = sk_ui_rgba(0.08f, 0.09f, 0.11f, 1.0f);
	(void)ui->node_set_inline_style(ctx, state->root, &p);

	/* Toolbar row (mirrors the C++ top bar). */
	state->toolbar = ui->widget_view(ctx, state->root, "sv.toolbar");
	sv_apply_toolbar_style(ui, ctx, state->toolbar);

	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = sv_on_toolbar_click;
	cbs.user = state;

	state->tool_sel = ui->widget_button(ctx, state->toolbar, "Sel", "sv.tool.sel");
	sv_style_button(ui, ctx, state->tool_sel);
	state->tool_move = ui->widget_button(ctx, state->toolbar, "Move", "sv.tool.move");
	sv_style_button(ui, ctx, state->tool_move);
	state->tool_rot = ui->widget_button(ctx, state->toolbar, "Rot", "sv.tool.rot");
	sv_style_button(ui, ctx, state->tool_rot);
	state->tool_scale = ui->widget_button(ctx, state->toolbar, "Scl", "sv.tool.scl");
	sv_style_button(ui, ctx, state->tool_scale);
	state->tool_mode = ui->widget_button(ctx, state->toolbar, "Glo", "sv.tool.mode");
	sv_style_button(ui, ctx, state->tool_mode);
	state->tool_snap = ui->widget_button(ctx, state->toolbar, "Snap", "sv.tool.snap");
	sv_style_button(ui, ctx, state->tool_snap);
	state->tool_grid = ui->widget_button(ctx, state->toolbar, "Grid", "sv.tool.grid");
	sv_style_button(ui, ctx, state->tool_grid);
	state->btn_scene_opts = ui->widget_button(ctx, state->toolbar, "...", "sv.tool.scene_opts");
	sv_style_button(ui, ctx, state->btn_scene_opts);

	spacer = ui->widget_view(ctx, state->toolbar, "sv.toolbar.spacer1");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_pt(1.0f);
	(void)ui->node_merge_inline_style(ctx, spacer, &p);

	state->btn_play = ui->widget_button(ctx, state->toolbar, "Play", "sv.tool.play");
	sv_style_button(ui, ctx, state->btn_play);
	state->btn_stop = ui->widget_button(ctx, state->toolbar, "Stop", "sv.tool.stop");
	sv_style_button(ui, ctx, state->btn_stop);

	spacer = ui->widget_view(ctx, state->toolbar, "sv.toolbar.spacer2");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_pt(1.0f);
	(void)ui->node_merge_inline_style(ctx, spacer, &p);

	state->btn_2d = ui->widget_button(ctx, state->toolbar, "2D", "sv.tool.2d");
	sv_style_button(ui, ctx, state->btn_2d);
	state->btn_3d = ui->widget_button(ctx, state->toolbar, "3D", "sv.tool.3d");
	sv_style_button(ui, ctx, state->btn_3d);
	state->btn_vol = ui->widget_button(ctx, state->toolbar, "Vol", "sv.tool.vol");
	sv_style_button(ui, ctx, state->btn_vol);
	state->btn_cam = ui->widget_button(ctx, state->toolbar, "Cam", "sv.tool.cam");
	sv_style_button(ui, ctx, state->btn_cam);
	state->btn_opts = ui->widget_button(ctx, state->toolbar, "Opts", "sv.tool.opts");
	sv_style_button(ui, ctx, state->btn_opts);

	{
		sk_ui_node_t* btns[16];
		u32 i;
		u32 n = 0u;
		btns[n++] = &state->tool_sel;
		btns[n++] = &state->tool_move;
		btns[n++] = &state->tool_rot;
		btns[n++] = &state->tool_scale;
		btns[n++] = &state->tool_mode;
		btns[n++] = &state->tool_snap;
		btns[n++] = &state->tool_grid;
		btns[n++] = &state->btn_scene_opts;
		btns[n++] = &state->btn_play;
		btns[n++] = &state->btn_stop;
		btns[n++] = &state->btn_2d;
		btns[n++] = &state->btn_3d;
		btns[n++] = &state->btn_vol;
		btns[n++] = &state->btn_cam;
		btns[n++] = &state->btn_opts;
		for (i = 0u; i < n; ++i) {
			(void)ui->node_set_callbacks(ctx, *btns[i], &cbs);
		}
	}

	/* Viewport area (fills the remaining content; placeholder texture drawn
	 * letterboxed and centered inside it). */
	state->viewport = ui->widget_view(ctx, state->root, "sv.viewport");
	sv_apply_viewport_style(ui, ctx, state->viewport);

	border.r = 0.55f;
	border.g = 0.58f;
	border.b = 0.64f;
	border.a = 1.0f;
	state->image = ui->widget_image_rect(ctx, state->viewport, state->tex_slot > 0 ? state->tex_slot : 0, 16.0f, 16.0f, 0.0f, 0.0f, 1.0f, 1.0f, NULL, &border, "sv.viewport.image");
	(void)state->image;

	sv_build_popups(state);
	state->ui_built = 1;
}

/* Recompute the letterboxed image size from the viewport content rect (last
 * frame's layout) and the placeholder texture aspect; mirror the C++
 * aspectRatio = width / height tracking. */
static void sv_apply_viewport_geometry(sv_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_rect_t border_box;
	sk_ui_rect_t content_box;
	f32 avail_w;
	f32 avail_h;
	f32 scale;
	sk_ui_style_props_t p;
	if (state == NULL || ui == NULL || !sk_ui_node_is_valid(state->viewport) || !sk_ui_node_is_valid(state->image)) {
		return;
	}
	memset(&border_box, 0, sizeof(border_box));
	memset(&content_box, 0, sizeof(content_box));
	if (ui->node_get_abs_rect(ctx, state->viewport, &border_box, &content_box) != 0) {
		return;
	}
	avail_w = content_box.width > 1.0f ? content_box.width : 0.0f;
	avail_h = content_box.height > 1.0f ? content_box.height : 0.0f;
	if (avail_w > 0.0f && avail_h > 0.0f) {
		state->viewport_w = avail_w;
		state->viewport_h = avail_h;
		state->aspect_ratio = avail_w / avail_h;
	}
	if (state->tex_width > 0u && state->tex_height > 0u && avail_w > 0.0f && avail_h > 0.0f) {
		f32 sx = avail_w / (f32)state->tex_width;
		f32 sy = avail_h / (f32)state->tex_height;
		scale = sx < sy ? sx : sy;
		if (scale > 0.0f) {
			state->image_w = (f32)state->tex_width * scale;
			state->image_h = (f32)state->tex_height * scale;
		}
	}
	if (state->image_w <= 1.0f || state->image_h <= 1.0f) {
		/* First frame(s) before layout: draw at the texture's native size. */
		state->image_w = (f32)(state->tex_width > 0u ? state->tex_width : 16u);
		state->image_h = (f32)(state->tex_height > 0u ? state->tex_height : 16u);
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(state->image_w);
	p.layout.height = sk_ui_pt(state->image_h);
	(void)ui->node_merge_inline_style(ctx, state->image, &p);
}

/* Toolbar active-state highlight (C++ ImGuiSelectionButton) + label sync
 * (world/local mode, sound toggle). */
static void sv_sync_toolbar(sv_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t active = SK_UI_NODE_INVALID;
	sk_ui_color_t on_color = sk_ui_rgba(0.24f, 0.42f, 0.66f, 1.0f);
	sk_ui_color_t off_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 0.0f);
	sk_ui_style_props_t p;
	switch (state->state.gizmo_operation) {
	case SK_EDITOR_GIZMO_OP_SELECT:
		active = state->tool_sel;
		break;
	case SK_EDITOR_GIZMO_OP_TRANSLATE:
		active = state->tool_move;
		break;
	case SK_EDITOR_GIZMO_OP_ROTATE:
		active = state->tool_rot;
		break;
	default:
		active = state->tool_scale;
		break;
	}
	if (sk_ui_node_is_valid(state->tool_mode)) {
		const_chr_t label = state->state.gizmo_mode == SK_EDITOR_GIZMO_MODE_LOCAL ? "Loc" : "Glo";
		(void)ui->button_set_label(ctx, state->tool_mode, label);
	}
	if (sk_ui_node_is_valid(state->btn_vol)) {
		(void)ui->button_set_label(ctx, state->btn_vol, state->sound_enabled ? "Vol" : "Mute");
	}

#define SV_BTN_ON(_btn)                                                                                                                       \
	(sk_ui_node_eq((_btn), active) || ((sk_ui_node_eq((_btn), state->tool_snap) && state->state.gizmo_snap_enabled) ||                        \
									   ((sk_ui_node_eq((_btn), state->tool_grid) && state->state.draw_grid) ||                                \
										((sk_ui_node_eq((_btn), state->btn_2d) && state->state.view_type == SK_EDITOR_SCENE_VIEW_TYPE_2D) ||  \
										 ((sk_ui_node_eq((_btn), state->btn_3d) && state->state.view_type == SK_EDITOR_SCENE_VIEW_TYPE_3D) || \
										  (sk_ui_node_eq((_btn), state->tool_mode) && state->state.gizmo_mode == SK_EDITOR_GIZMO_MODE_LOCAL))))))

#define SV_HIGHLIGHT(_btn)                                               \
	do {                                                                 \
		if (sk_ui_node_is_valid(_btn)) {                                 \
			memset(&p, 0, sizeof(p));                                    \
			p.mask = SK_UI_SP_BACKGROUND_COLOR;                          \
			p.background_color = SV_BTN_ON(_btn) ? on_color : off_color; \
			(void)ui->node_merge_inline_style(ctx, _btn, &p);            \
		}                                                                \
	} while (0)

	SV_HIGHLIGHT(state->tool_sel);
	SV_HIGHLIGHT(state->tool_move);
	SV_HIGHLIGHT(state->tool_rot);
	SV_HIGHLIGHT(state->tool_scale);
	SV_HIGHLIGHT(state->tool_mode);
	SV_HIGHLIGHT(state->tool_snap);
	SV_HIGHLIGHT(state->tool_grid);
	SV_HIGHLIGHT(state->btn_2d);
	SV_HIGHLIGHT(state->btn_3d);
#undef SV_HIGHLIGHT
}

static void sv_sync(sv_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_editor_scene_view_tex_registry_t* reg;
	sk_editor_scene_view_tex_t tex;

	/* Resolve the placeholder texture binding (host-registered, like icons). */
	reg = sk_editor_scene_view_texture_resolve(state->app_context, state->app_api);
	tex = sk_editor_scene_view_texture_get(reg);
	state->tex_slot = (i32)tex.texture_id;
	state->tex_width = tex.width;
	state->tex_height = tex.height;

	/* Keep the image node's texture id in sync with the resolved slot. */
	if (sk_ui_node_is_valid(state->image) && state->tex_slot > 0) {
		(void)ui->node_set_prop_i32(ctx, state->image, "texture_id", state->tex_slot);
	}

	sv_apply_viewport_geometry(state);
	sv_sync_toolbar(state);

	/* Slider / checkbox widgets own interaction; sync their props from the
	 * state when an ops-table setter changed them outside the widgets. */
	if (sk_ui_node_is_valid(state->fov_slider)) {
		(void)ui->slider_set_value(ctx, state->fov_slider, state->state.camera_fov);
	}
	if (sk_ui_node_is_valid(state->speed_slider)) {
		(void)ui->slider_set_value(ctx, state->speed_slider, state->camera_speed);
	}
	if (sk_ui_node_is_valid(state->smooth_check)) {
		(void)ui->checkbox_set_checked(ctx, state->smooth_check, state->smooth_camera);
	}
	if (sk_ui_node_is_valid(state->snap_slider)) {
		(void)ui->slider_set_value(ctx, state->snap_slider, state->state.snap[0]);
	}
	if (sk_ui_node_is_valid(state->lod_slider)) {
		(void)ui->slider_set_value(ctx, state->lod_slider, state->forced_lod);
	}
	if (sk_ui_node_is_valid(state->opt_outline)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_outline, state->state.draw_selection_outline);
	}
	if (sk_ui_node_is_valid(state->opt_icons)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_icons, state->state.draw_icons);
	}
	if (sk_ui_node_is_valid(state->opt_gizmos)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_gizmos, state->state.draw_component_gizmos);
	}
	if (sk_ui_node_is_valid(state->opt_physics)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_physics, state->state.draw_debug_physics);
	}
	if (sk_ui_node_is_valid(state->opt_all_shapes)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_all_shapes, state->state.show_all_physics_shapes);
	}
	if (sk_ui_node_is_valid(state->opt_aabb)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_aabb, state->state.draw_mesh_aabb);
	}
	if (sk_ui_node_is_valid(state->opt_navmesh)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_navmesh, state->state.draw_nav_mesh);
	}
	if (sk_ui_node_is_valid(state->opt_lock)) {
		(void)ui->checkbox_set_checked(ctx, state->opt_lock, state->state.lock_camera_frustum);
	}
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void sv_init(sk_editor_window_t* window) {
	sv_state_t* state;
	const sk_allocator_t* alloc = sk_allocator_default();
	sv_class_state_t* cls;
	sk_editor_workspace_t* ws;
	if (window == NULL) {
		return;
	}
	/* The impl's user_data holds the class state until init replaces it
	 * with the per-open instance state (window_open copies the table). */
	cls = (sv_class_state_t*)window->user_data;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (sv_state_t*)alloc->alloc(alloc->instance, sizeof(sv_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	/* C++ Init: freeViewCamera.SetPosition({0,10,0}); guizmoOperation =
	 * ImGuizmo::TRANSLATE. */
	state->state.gizmo_operation = SK_EDITOR_GIZMO_OP_TRANSLATE;
	state->state.gizmo_mode = SK_EDITOR_GIZMO_MODE_WORLD;
	state->state.view_type = SK_EDITOR_SCENE_VIEW_TYPE_3D;
	state->state.draw_icons = 1;
	state->state.camera_fov = 60.0f;
	state->state.draw_grid = 1;
	state->state.draw_selection_outline = 1;
	state->state.draw_debug_physics = 1;
	state->state.draw_component_gizmos = 1;
	state->state.snap[0] = 1.0f;
	state->state.snap[1] = 1.0f;
	state->state.snap[2] = 1.0f;
	state->camera_speed = 8.0f;
	state->smooth_camera = 1;
	state->camera_pos[0] = SV_CAM_DEFAULT_POS_X;
	state->camera_pos[1] = SV_CAM_DEFAULT_POS_Y;
	state->camera_pos[2] = SV_CAM_DEFAULT_POS_Z;
	state->forced_lod = -1.0f;
	state->sound_enabled = 1;
	state->tex_slot = 0;
	state->tex_width = SV_TEX_W;
	state->tex_height = SV_TEX_H;
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	state->workspace_id = ws != NULL ? sk_editor_workspace_type_id(ws) : 0u;
	window->user_data = state;
	sv_publish(state);
}

static void sv_draw(sk_editor_window_t* window, i32* open) {
	sv_state_t* state;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_ui_node_t chrome;
	sk_ui_node_t content;
	*open = 1;
	if (window == NULL) {
		*open = 0;
		return;
	}
	state = (sv_state_t*)window->user_data;
	if (state == NULL) {
		*open = 0;
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (ctx == NULL) {
		return; /* no ui host (plain unit tests keep the ops/logic path only) */
	}
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		return;
	}
	chrome = ui->find_by_id(ctx, window->dock_id);
	if (!sk_ui_node_is_valid(chrome)) {
		return; /* not docked in the active workspace yet */
	}
	content = ui->editor_window_content(ctx, chrome);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	/* Rebuild the chrome when the dock teardown destroyed it (workspace
	 * switch / tab close) — stable ids survive node recycling. */
	if (!sk_ui_node_is_valid(state->root)) {
		sv_build_ui(state, ui, ctx, content);
	}
	if (state->ui_built == 0) {
		return;
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	sv_sync(state);
}

static void sv_destroy(sk_editor_window_t* window) {
	sv_state_t* state = window != NULL ? (sv_state_t*)window->user_data : NULL;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	/* Tear down the chrome with the shell's ui context (valid while the
	 * shell owns the frame; dock teardown may have destroyed the nodes
	 * already — node_alive guards that case). */
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ctx != NULL && ui != NULL && sk_ui_node_is_valid(state->root) && ui->node_alive(ctx, state->root)) {
		(void)ui->node_destroy(ctx, state->root);
	}
	state->root = SK_UI_NODE_INVALID;
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

/* ------------------------------------------------------------------ */
/*  Ops table (add_impl; never direct symbols)                         */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* sv_ops_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_SCENE_VIEW);
}

static void sv_ops_view_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid) {
	sv_state_t* state;
	(void)app_context;
	(void)app_api;
	if (window == NULL) {
		return;
	}
	state = (sv_state_t*)window->user_data;
	if (state != NULL) {
		/* C++ ViewEntity moves the free camera to frame the entity. v2 has
		 * no camera; record the request (MOCK, header). */
		state->last_viewed_entity = rid;
	}
}

static sk_rid_t sv_ops_get_last_viewed_entity(const sk_editor_window_t* window) {
	const sv_state_t* state = window != NULL ? (const sv_state_t*)window->user_data : NULL;
	return state != NULL ? state->last_viewed_entity : SK_RID_ZERO;
}

static i32 sv_ops_is_scene_interaction_disabled(const sk_editor_window_t* window) {
	const sv_state_t* state = window != NULL ? (const sv_state_t*)window->user_data : NULL;
	return state != NULL ? sv_state_interaction_disabled(state) : 0;
}

static void sv_ops_add_menu_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item) {
	sv_class_state_t* cls = sv_class(app_context, app_api);
	if (cls == NULL || item == NULL || item->item_name == NULL) {
		return;
	}
	if (cls->menu_count < SV_MENU_CAP) {
		cls->menus[cls->menu_count++] = *item;
	}
}

static void* sv_ops_get_scene_editor(const sk_editor_window_t* window) {
	/* C++ GetSceneEditor: v2 has no SceneEditor — MOCK NULL. */
	(void)window;
	return NULL;
}

static void sv_ops_get_state(const sk_editor_window_t* window, sk_editor_viewport_state_t* out) {
	const sv_state_t* state = window != NULL ? (const sv_state_t*)window->user_data : NULL;
	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	if (state != NULL) {
		*out = state->state;
		out->simulating = state->state.simulating;
		out->interaction_disabled = sv_state_interaction_disabled(state);
	}
}

static void sv_ops_set_state(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_editor_viewport_state_t* state_in) {
	sv_state_t* state;
	(void)app_context;
	(void)app_api;
	if (window == NULL || state_in == NULL) {
		return;
	}
	state = (sv_state_t*)window->user_data;
	if (state == NULL) {
		return;
	}
	state->state = *state_in;
	sv_publish(state);
}

static void sv_ops_start_simulation(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	sv_state_t* state;
	(void)app_context;
	(void)app_api;
	if (window == NULL) {
		return;
	}
	state = (sv_state_t*)window->user_data;
	if (state != NULL && state->state.simulating == 0) {
		state->state.simulating = 1;
		sv_publish(state);
	}
}

static void sv_ops_stop_simulation(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	sv_state_t* state;
	(void)app_context;
	(void)app_api;
	if (window == NULL) {
		return;
	}
	state = (sv_state_t*)window->user_data;
	if (state != NULL && state->state.simulating != 0) {
		state->state.simulating = 0;
		sv_publish(state);
	}
}

static void sv_ops_set_read_only(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 ro) {
	sv_state_t* state;
	(void)app_context;
	(void)app_api;
	if (window == NULL) {
		return;
	}
	state = (sv_state_t*)window->user_data;
	if (state != NULL && state->read_only != (ro != 0)) {
		state->read_only = (ro != 0);
		sv_publish(state);
	}
}

static void sv_ops_get_viewport_size(const sk_editor_window_t* window, f32* out_w, f32* out_h) {
	const sv_state_t* state = window != NULL ? (const sv_state_t*)window->user_data : NULL;
	if (out_w != NULL) {
		*out_w = state != NULL ? state->viewport_w : 0.0f;
	}
	if (out_h != NULL) {
		*out_h = state != NULL ? state->viewport_h : 0.0f;
	}
}

static f32 sv_ops_get_aspect_ratio(const sk_editor_window_t* window) {
	const sv_state_t* state = window != NULL ? (const sv_state_t*)window->user_data : NULL;
	return state != NULL ? state->aspect_ratio : 0.0f;
}

static const sk_editor_scene_view_ops_t sv_ops = {
	.open = sv_ops_open,
	.view_entity = sv_ops_view_entity,
	.get_last_viewed_entity = sv_ops_get_last_viewed_entity,
	.is_scene_interaction_disabled = sv_ops_is_scene_interaction_disabled,
	.add_menu_item = sv_ops_add_menu_item,
	.get_scene_editor = sv_ops_get_scene_editor,
	.get_state = sv_ops_get_state,
	.set_state = sv_ops_set_state,
	.start_simulation = sv_ops_start_simulation,
	.stop_simulation = sv_ops_stop_simulation,
	.set_read_only = sv_ops_set_read_only,
	.get_viewport_size = sv_ops_get_viewport_size,
	.get_aspect_ratio = sv_ops_get_aspect_ratio,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_scene_view_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sv_class_state_t* cls = sv_class(app_context, app_api);
	if (cls != NULL) {
		sv_window.user_data = cls;
		return;
	}
	cls = (sv_class_state_t*)alloc->alloc(alloc->instance, sizeof(sv_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_SCENE_VIEW_STATE_TYPE_ID, cls);

	sv_window.type_id = SK_EDITOR_WINDOW_SCENE_VIEW;
	sv_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &sv_window);
	app_api->add_impl(app_context, SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID, &sv_ops);
}

void sk_editor_scene_view_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sv_class_state_t* cls = sv_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &sv_window);
	app_api->remove_impl(app_context, SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID, &sv_ops);
	app_api->set_api(app_context, SK_EDITOR_SCENE_VIEW_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (sv_window.user_data == cls) {
		sv_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"

/* --- Mock RHI: records the placeholder upload (no GPU needed) ------ */

typedef struct sv_mock_dev_t {
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
} sv_mock_dev_t;

static sk_render_device_t sv_mock_init(void_ptr_t user, const sk_device_init_desc_t* desc) {
	(void)desc;
	return sk_render_device_t_from_ptr(user);
}

static void sv_mock_destroy(sk_render_device_t dev) {
	(void)dev;
}

static sk_texture_t sv_mock_create_texture(sk_render_device_t dev, const sk_texture_desc_t* desc) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	d->tex_desc = *desc;
	d->texture_count++;
	return sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)(0x100u + (uintptr_t)d->texture_count));
}

static void sv_mock_destroy_texture(sk_render_device_t dev, sk_texture_t tex) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)tex;
	d->destroyed_textures++;
}

static sk_texture_view_t sv_mock_create_texture_view(sk_render_device_t dev, const sk_texture_view_desc_t* desc) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)desc;
	d->view_count++;
	return sk_texture_view_t_from_ptr((void_ptr_t)(uintptr_t)(0x200u + (uintptr_t)d->view_count));
}

static void sv_mock_destroy_texture_view(sk_render_device_t dev, sk_texture_view_t view) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)view;
	d->destroyed_views++;
}

static sk_buffer_t sv_mock_create_buffer(sk_render_device_t dev, const sk_buffer_desc_t* desc) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	d->buf_desc = *desc;
	d->buf_storage = (u8*)d->allocator->alloc(d->allocator->instance, (size_t)desc->size);
	if (d->buf_storage != NULL) {
		memset(d->buf_storage, 0, (size_t)desc->size);
	}
	d->buffer_count++;
	return sk_buffer_t_from_ptr((void_ptr_t)(uintptr_t)(0x300u + (uintptr_t)d->buffer_count));
}

static void sv_mock_destroy_buffer(sk_render_device_t dev, sk_buffer_t buf) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)buf;
	if (d->buf_storage != NULL) {
		d->allocator->free(d->allocator->instance, d->buf_storage);
		d->buf_storage = NULL;
	}
	d->destroyed_buffers++;
}

static void* sv_mock_buffer_map(sk_render_device_t dev, sk_buffer_t buf) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)buf;
	return d->buf_storage;
}

static void sv_mock_buffer_unmap(sk_render_device_t dev, sk_buffer_t buf) {
	(void)dev;
	(void)buf;
}

static sk_queue_t sv_mock_create_queue(sk_render_device_t dev, const sk_queue_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_queue_t_from_ptr((void*)0x400);
}

static void sv_mock_destroy_queue(sk_render_device_t dev, sk_queue_t q) {
	(void)dev;
	(void)q;
}

static sk_command_buffer_t sv_mock_create_command_buffer(sk_render_device_t dev, const sk_command_buffer_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_command_buffer_t_from_ptr((void*)0x500);
}

static void sv_mock_destroy_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}

static i32 sv_mock_begin_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_command_buffer_begin_info_t* info) {
	(void)dev;
	(void)cmd;
	(void)info;
	return 0;
}

static void sv_mock_end_command_buffer(sk_render_device_t dev, sk_command_buffer_t cmd) {
	(void)dev;
	(void)cmd;
}

static void sv_mock_resource_barrier_texture(sk_render_device_t dev, sk_command_buffer_t cmd, sk_texture_t tex, sk_resource_state_t old_state, sk_resource_state_t new_state,
											 u32 base_mip, u32 level_count, u32 base_layer, u32 layer_count, u32 src_sync, u32 dst_sync) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
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

static void sv_mock_copy_buffer_to_texture(sk_render_device_t dev, sk_command_buffer_t cmd, const sk_buffer_texture_copy_t* copy_info) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)cmd;
	(void)copy_info;
	d->copy_count++;
}

static i32 sv_mock_submit(sk_render_device_t dev, sk_queue_t queue, const sk_submit_info_t* submit) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)queue;
	(void)submit;
	d->submit_count++;
	return 0;
}

static i32 sv_mock_wait_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count, bool wait_all, u64 timeout_ns) {
	sv_mock_dev_t* d = (sv_mock_dev_t*)sk_render_device_t_to_ptr(dev);
	(void)fences;
	(void)fence_count;
	(void)wait_all;
	(void)timeout_ns;
	d->wait_count++;
	return 0;
}

static void sv_mock_reset_fences(sk_render_device_t dev, const sk_fence_t* fences, u32 fence_count) {
	(void)dev;
	(void)fences;
	(void)fence_count;
}

static sk_fence_t sv_mock_create_fence(sk_render_device_t dev, const sk_fence_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_fence_t_from_ptr((void*)0x600);
}

static void sv_mock_destroy_fence(sk_render_device_t dev, sk_fence_t fence) {
	(void)dev;
	(void)fence;
}

static const sk_render_device_api_t* sv_mock_rhi(sv_mock_dev_t* d) {
	static sk_render_device_api_t api; /* const table built once; guard allows */
	(void)d;
	memset(&api, 0, sizeof(api));
	api.init = sv_mock_init;
	api.destroy = sv_mock_destroy;
	api.create_texture = sv_mock_create_texture;
	api.destroy_texture = sv_mock_destroy_texture;
	api.create_texture_view = sv_mock_create_texture_view;
	api.destroy_texture_view = sv_mock_destroy_texture_view;
	api.create_buffer = sv_mock_create_buffer;
	api.destroy_buffer = sv_mock_destroy_buffer;
	api.buffer_map = sv_mock_buffer_map;
	api.buffer_unmap = sv_mock_buffer_unmap;
	api.create_queue = sv_mock_create_queue;
	api.destroy_queue = sv_mock_destroy_queue;
	api.create_command_buffer = sv_mock_create_command_buffer;
	api.destroy_command_buffer = sv_mock_destroy_command_buffer;
	api.begin_command_buffer = sv_mock_begin_command_buffer;
	api.end_command_buffer = sv_mock_end_command_buffer;
	api.resource_barrier_texture = sv_mock_resource_barrier_texture;
	api.copy_buffer_to_texture = sv_mock_copy_buffer_to_texture;
	api.submit = sv_mock_submit;
	api.wait_fences = sv_mock_wait_fences;
	api.reset_fences = sv_mock_reset_fences;
	api.create_fence = sv_mock_create_fence;
	api.destroy_fence = sv_mock_destroy_fence;
	return &api;
}

/* Placeholder texture: generated 512x288 RGBA8, uploaded through the RHI,
 * registered on the app context, and resolvable by the window. */
SK_TEST(editor_scene_view_placeholder_texture_upload) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sv_mock_dev_t dev;
	const sk_render_device_api_t* rhi;
	sk_render_device_t device;
	sk_editor_scene_view_tex_registry_t* reg;
	sk_editor_scene_view_tex_t tex;
	sk_app_boot_t boot;
	const u8* px;
	u32 i;
	u32 opaque = 0u;

	memset(&dev, 0, sizeof(dev));
	dev.allocator = alloc;
	rhi = sv_mock_rhi(&dev);
	device = rhi->init(&dev, NULL);
	boot = sk_app_create();
	TEST_ASSERT_NOT_NULL(boot.context);

	TEST_ASSERT_NULL(sk_editor_scene_view_texture_resolve(boot.context, boot.api));
	reg = sk_editor_scene_view_texture_create(boot.context, boot.api, rhi, device);
	TEST_ASSERT_NOT_NULL(reg);
	TEST_ASSERT_EQUAL_UINT(1u, dev.texture_count);
	TEST_ASSERT_EQUAL_UINT(1u, dev.view_count);
	TEST_ASSERT_EQUAL_UINT(1u, dev.buffer_count);
	TEST_ASSERT_EQUAL_UINT(2u, dev.barrier_count);
	TEST_ASSERT_EQUAL_UINT(1u, dev.copy_count);
	TEST_ASSERT_EQUAL_UINT(1u, dev.submit_count);
	TEST_ASSERT_EQUAL_UINT(1u, dev.wait_count);
	TEST_ASSERT_EQUAL_UINT(512u, dev.tex_desc.extent.width);
	TEST_ASSERT_EQUAL_UINT(288u, dev.tex_desc.extent.height);
	TEST_ASSERT_EQUAL_INT(SK_PIXEL_FORMAT_RGBA8_UNORM, dev.tex_desc.format);
	TEST_ASSERT_EQUAL_UINT(512u * 288u * 4u, (u32)dev.buf_desc.size);

	/* Staging bytes hold the generated mock viewport (opaque + frame). */
	px = dev.buf_storage;
	TEST_ASSERT_NOT_NULL(px);
	for (i = 0u; i < 512u * 288u; ++i) {
		if (px[i * 4u + 3u] == 255u) {
			opaque++;
		}
	}
	TEST_ASSERT_EQUAL_UINT(512u * 288u, opaque);
	/* First texel: the 2px border frame (120), interior texels opaque. */
	TEST_ASSERT_EQUAL_UINT(120u, px[0u]);
	TEST_ASSERT_EQUAL_UINT(255u, px[(16u * 512u + 32u) * 4u + 3u]);

	/* Registered on the app context; window lookup returns the slot + size. */
	sk_editor_scene_view_texture_register(boot.context, boot.api, reg);
	TEST_ASSERT_EQUAL_PTR(reg, sk_editor_scene_view_texture_resolve(boot.context, boot.api));
	tex = sk_editor_scene_view_texture_get(reg);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_SCENE_VIEW_TEX_SLOT, tex.texture_id);
	TEST_ASSERT_EQUAL_UINT(512u, tex.width);
	TEST_ASSERT_EQUAL_UINT(288u, tex.height);
	TEST_ASSERT_NOT_NULL(sk_editor_scene_view_texture_view(reg));

	sk_editor_scene_view_texture_destroy(reg);
	TEST_ASSERT_EQUAL_UINT(1u, dev.destroyed_textures);
	TEST_ASSERT_EQUAL_UINT(1u, dev.destroyed_views);
	TEST_ASSERT_EQUAL_UINT(1u, dev.destroyed_buffers);
	TEST_ASSERT_NULL(dev.buf_storage);
	sk_editor_scene_view_texture_destroy(NULL); /* safe on NULL */
	sk_app_shutdown(boot.context);
}

/* Observer harness (notify.h). */
typedef struct sv_obs_t {
	u32 viewport_changes;
	u32 last_workspace;
	f32 last_fov;
	u32 last_op;
	i32 last_simulating;
} sv_obs_t;

static void sv_on_viewport_state(void* user, u32 workspace_id, const sk_editor_viewport_state_t* state) {
	sv_obs_t* o = (sv_obs_t*)user;
	o->viewport_changes++;
	o->last_workspace = workspace_id;
	if (state != NULL) {
		o->last_fov = state->camera_fov;
		o->last_op = state->gizmo_operation;
		o->last_simulating = state->simulating;
	}
}

/* Ops-table path (no ui): the window opens, the mock viewport state surface
 * works, ViewEntity records, and state changes announce through the
 * VIEWPORT_STATE observer. */
SK_TEST(editor_scene_view_ops_mock) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_scene_view_ops_t* ops;
	sk_editor_window_t* window;
	sk_editor_viewport_state_t state;
	sv_obs_t obs;
	sk_editor_on_viewport_state_t observer;
	sk_rid_t rid;
	f32 w;
	f32 h;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_scene_view_ops(app, boot.api));

	sk_editor_scene_view_register(app, boot.api);
	sk_editor_scene_view_register(app, boot.api); /* idempotent */
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID));
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID));

	ops = sk_editor_scene_view_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);
	TEST_ASSERT_EQUAL_PTR(&sv_ops, ops);
	TEST_ASSERT_NOT_NULL(ops->open);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(window->type_id, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_EQUAL_STRING("Scene Viewport", window->title);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.scene_view", window->dock_id);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_FILL, window->dock_position);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));
	/* One open instance per type. */
	TEST_ASSERT_EQUAL_PTR(window, ops->open(app, boot.api));

	/* C++ Init defaults. */
	ops->get_state(window, &state);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_GIZMO_OP_TRANSLATE, state.gizmo_operation);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_GIZMO_MODE_WORLD, state.gizmo_mode);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_SCENE_VIEW_TYPE_3D, state.view_type);
	TEST_ASSERT_EQUAL_FLOAT(60.0f, state.camera_fov);
	TEST_ASSERT_EQUAL_INT(1, state.draw_grid);
	TEST_ASSERT_EQUAL_INT(0, state.simulating);
	TEST_ASSERT_EQUAL_INT(0, ops->is_scene_interaction_disabled(window));

	/* Observer fires on state changes (viewport state announced). */
	memset(&obs, 0, sizeof(obs));
	memset(&observer, 0, sizeof(observer));
	observer.user = &obs;
	observer.on_viewport_state = sv_on_viewport_state;
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_VIEWPORT_STATE, &observer);

	ops->set_state(app, boot.api, window,
				   &(sk_editor_viewport_state_t){
					   .gizmo_operation = SK_EDITOR_GIZMO_OP_ROTATE,
					   .camera_fov = 75.0f,
					   .draw_grid = 0,
				   });
	TEST_ASSERT_EQUAL_UINT(1u, obs.viewport_changes);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_GIZMO_OP_ROTATE, obs.last_op);
	TEST_ASSERT_EQUAL_FLOAT(75.0f, obs.last_fov);

	/* Simulation mocks drive the C++ IsSceneInteractionDisabled. */
	ops->start_simulation(app, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(2u, obs.viewport_changes);
	TEST_ASSERT_EQUAL_INT(1, obs.last_simulating);
	TEST_ASSERT_EQUAL_INT(1, ops->is_scene_interaction_disabled(window));
	ops->stop_simulation(app, boot.api, window);
	TEST_ASSERT_EQUAL_INT(0, ops->is_scene_interaction_disabled(window));
	ops->set_read_only(app, boot.api, window, 1);
	TEST_ASSERT_EQUAL_INT(1, ops->is_scene_interaction_disabled(window));
	ops->set_read_only(app, boot.api, window, 0);

	/* ViewEntity records (C++ double-click frame; no camera in v2). */
	rid.id = 77ull;
	ops->view_entity(app, boot.api, window, rid);
	TEST_ASSERT_EQUAL_UINT64(77ull, ops->get_last_viewed_entity(window).id);

	/* No live scene editor in v2 (C++ GetSceneEditor → NULL mock). */
	TEST_ASSERT_NULL(ops->get_scene_editor(window));

	/* Viewport size stays 0 before any layout. */
	ops->get_viewport_size(window, &w, &h);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, w);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, h);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, ops->get_aspect_ratio(window));

	boot.api->remove_impl(app, SK_EDITOR_NOTIFY_VIEWPORT_STATE, &observer);
	sk_editor_scene_view_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_scene_view_ops(app, boot.api));
	TEST_ASSERT_EQUAL_UINT(0u, boot.api->impl_count(app, SK_EDITOR_SCENE_VIEW_OPS_TYPE_ID));
	sk_app_shutdown(boot.context);
}

/* --- UI test: load the sk-ui plugin on the app, host the window chrome
 *     directly on a context (same recipe as the entity tree UI tests) --- */

static void sv_test_click(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_rect_t border_box;
	sk_ui_input_event_t ev;
	f32 cx;
	f32 cy;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, node, &border_box, NULL));
	cx = border_box.x + border_box.width * 0.5f;
	cy = border_box.y + border_box.height * 0.5f;
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = cx;
	ev.y = cy;
	(void)ui->input_dispatch(ctx, &ev);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = cx;
	ev.y = cy;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	(void)ui->input_dispatch(ctx, &ev);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = cx;
	ev.y = cy;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 0;
	(void)ui->input_dispatch(ctx, &ev);
}

/* Shell path: the Scene workspace docks the Scene Viewport window; its
 * toolbar chrome exists, toolbar clicks mutate the viewport state through
 * the observer, and the placeholder image letterboxes to the content area
 * (aspect handling). */
SK_TEST(editor_scene_view_chrome_and_aspect) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_scene_view_ops_t* ops;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_editor_window_t* window;
	sv_obs_t obs;
	sk_editor_on_viewport_state_t observer;
	sk_editor_viewport_state_t state;
	sk_ui_node_t chrome;
	sk_ui_node_t tool_rot;
	sk_ui_node_t image;
	sk_ui_node_t fov_slider;
	sk_ui_style_props_t style;
	sk_ui_style_props_t p;
	f32 w;
	f32 h;
	f32 aspect;
	const_chr_t plugin_name;
	i32 open = 1;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	/* Load the ui plugin (skipped when not built next to the tests). */
	{
		const sk_filesystem_api_t* fs = sk_test_filesystem_table();
		char base[SK_FS_PATH_MAX];
		char plugin[SK_FS_PATH_MAX];
		char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
		plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
		plugin_name = "sk-ui.dylib";
#else
		plugin_name = "sk-ui.so";
#endif
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			base[0] = '\0';
		}
		if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugin, (u32)sizeof(plugin)) < 0) {
			plugin[0] = '\0';
		}
		(void)sk_path_join(sk_str_view_cstr(plugin), sk_str_view_cstr(plugin_name), path, (u32)sizeof(path));
		(void)boot.api->load_plugin(app, path);
	}
	ui = (const sk_ui_api_t*)boot.api->get_api(app, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		sk_app_shutdown(app);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, ui->init());
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	sk_editor_workspace_register_impls(app, boot.api);
	sk_editor_scene_view_register(app, boot.api);
	ops = sk_editor_scene_view_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	/* Register a placeholder texture (mock RHI) so the window resolves the
	 * 512x288 source size and letterboxes the image to the content area. */
	{
		sv_mock_dev_t* dev;
		const sk_render_device_api_t* rhi;
		sk_render_device_t device;
		sk_editor_scene_view_tex_registry_t* reg;
		dev = (sv_mock_dev_t*)sk_allocator_default()->alloc(sk_allocator_default()->instance, sizeof(sv_mock_dev_t));
		TEST_ASSERT_NOT_NULL(dev);
		memset(dev, 0, sizeof(*dev));
		dev->allocator = sk_allocator_default();
		rhi = sv_mock_rhi(dev);
		device = rhi->init(dev, NULL);
		reg = sk_editor_scene_view_texture_create(app, boot.api, rhi, device);
		TEST_ASSERT_NOT_NULL(reg);
		sk_editor_scene_view_texture_register(app, boot.api, reg);
	}

	/* Active workspace hosting the window chrome (no dockspace model). */
	ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(ws);
	sk_editor_workspace_set_dock_context(ws, ctx);
	sk_editor_workspace_switch(ws);

	/* Attach the editor-window chrome directly to the context root and size
	 * it (wide 16:9 window), so the window's own chrome builds inside a
	 * laid-out subtree. */
	chrome = ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene Viewport", "sk.editor_window.scene_view");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(chrome));
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(10.0f);
	p.layout.top = sk_ui_pt(10.0f);
	p.layout.width = sk_ui_pt(960.0f);
	p.layout.height = sk_ui_pt(540.0f);
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_merge_inline_style(ctx, chrome, &p);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(1, open);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));

	/* Toolbar chrome exists with the C++ tool surface. */
	tool_rot = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.tool.rot");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tool_rot));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.tool.move")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.tool.snap")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.tool.play")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.tool.3d")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.tool.opts")));
	image = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.viewport.image");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(image));
	fov_slider = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "sv.camera.fov.slider");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(fov_slider));

	/* Observer harness. */
	memset(&obs, 0, sizeof(obs));
	memset(&observer, 0, sizeof(observer));
	observer.user = &obs;
	observer.on_viewport_state = sv_on_viewport_state;
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_VIEWPORT_STATE, &observer);

	/* Clicking the Rotate tool changes the state and announces it. */
	sv_test_click(ui, ctx, tool_rot);
	ops->get_state(window, &state);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_GIZMO_OP_ROTATE, state.gizmo_operation);
	TEST_ASSERT_EQUAL_UINT(1u, obs.viewport_changes);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_GIZMO_OP_ROTATE, obs.last_op);

	/* The image node carries a concrete size (aspect-handled): the chrome
	 * is wider than tall (16:9), so the letterbox fit is height-bound and
	 * the image keeps the 512x288 (16:9) ratio. */
	memset(&style, 0, sizeof(style));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_inline_style(ctx, image, &style));
	TEST_ASSERT_TRUE((style.mask & SK_UI_SP_WIDTH) != 0u);
	TEST_ASSERT_TRUE((style.mask & SK_UI_SP_HEIGHT) != 0u);
	TEST_ASSERT_TRUE(style.layout.width.value > 100.0f);
	TEST_ASSERT_TRUE(style.layout.height.value > 50.0f);
	{
		f32 ratio = style.layout.width.value / style.layout.height.value;
		TEST_ASSERT_FLOAT_WITHIN(0.02f, 512.0f / 288.0f, ratio);
	}

	/* Viewport geometry tracked for the aspect (C++ aspectRatio). */
	ops->get_viewport_size(window, &w, &h);
	aspect = ops->get_aspect_ratio(window);
	TEST_ASSERT_TRUE(w > 200.0f);
	TEST_ASSERT_TRUE(h > 100.0f);
	TEST_ASSERT_TRUE(aspect > 1.0f);

	boot.api->remove_impl(app, SK_EDITOR_NOTIFY_VIEWPORT_STATE, &observer);
	ui->context_destroy(ctx);
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
