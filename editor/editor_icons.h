#pragma once

/**
 * @file editor_icons.h
 * @brief C++ editor icon set (Content/Images) for v2 windows (APX-367).
 *
 * The C++ editor loads a small set of bitmap icons from Content/Images; every
 * other glyph comes from the Font Awesome font (not ported). The migration
 * manifest (docs/editor/migration-manifest.md §1 "Icons") lists exactly five:
 *
 *   Content/Images/FolderIcon.png   → ProjectBrowserWindow directoryTexture
 *   Content/Images/FileIcon.png     → ResourceAssets assertTexture (generic
 *                                     asset tile fallback)
 *   Content/Images/LogoSmall.jpeg   → project manager logo / window icon
 *   Content/Images/minimalist-logo.png → empty-project graphic
 *   Content/Images/skore.ico        → Win32 window icon resource
 *
 * The vendored stb_image is STBI_ONLY_PNG, so the JPEG logo and the ICO are
 * embedded as RGBA PNG conversions (logo_small.png / skore.png) committed in
 * editor/content/images/ next to the originals — same pixels, decodable by
 * the v2 texture path.
 *
 * This module makes that set available to v2 editor windows:
 *
 *   1. Decode: the raw PNG/JPEG bytes (embedded from editor/content/images/,
 *      see editor/content/skore_editor_icons_embed.h) are decoded to RGBA8
 *      with stb_image.
 *   2. Atlas: every icon is packed into ONE RGBA8 texture (a horizontal
 *      strip) so the UI renderer needs a single host image binding.
 *   3. Upload: the atlas is uploaded through the existing v2 UI texture path
 *      — a render-device staging buffer, barrier + copy_buffer_to_texture,
 *      and one texture view handed to the UI renderer via
 *      sk_ui_renderer_images_t at renderer_encode / capture_frame time.
 *   4. Lookup: windows call sk_editor_icons_resolve + sk_editor_icons_get
 *      (icon id → sk_editor_icon_t handle carrying the texture id and the
 *      sub-rect UVs) and draw with ui->widget_image_rect.
 *
 * Explicitly NOT implemented: thumbnail generation (migration manifest §2.2).
 * These are the icon assets only; asset thumbnails stay mocked in v2.
 *
 * The registry is created by the GPU host (it owns the render device), then
 * registered on the app context so windows can resolve it
 * (sk_editor_icons_register / sk_editor_icons_resolve). The host must pass
 * sk_editor_icons_views() to the UI renderer every frame the icons are drawn.
 *
 * Reference renderer: the sk-sandbox-shell debug window creates the registry,
 * draws all five icons, and captures them (sandbox/editor_shell_sandbox.c).
 */

#include "app.h"
#include "common.h"
#include "render_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id under which a live icon registry is registered on the app context. */
#define SK_EDITOR_ICONS_TYPE_ID SK_TYPE_ID("sk.editor.icons", 0x7f3a2b1c5d9e0f12ULL, 0x9c81a4b2d6e8f0aaULL)

/** Icon ids covering the migration-manifest Content/Images set (APX-367). */
typedef enum sk_editor_icon_id_t {
	SK_EDITOR_ICON_FOLDER = 0,		 /* Content/Images/FolderIcon.png */
	SK_EDITOR_ICON_FILE = 1,		 /* Content/Images/FileIcon.png */
	SK_EDITOR_ICON_LOGO_SMALL = 2,	 /* Content/Images/LogoSmall.jpeg */
	SK_EDITOR_ICON_LOGO_MINIMAL = 3, /* Content/Images/minimalist-logo.png */
	SK_EDITOR_ICON_SKORE = 4,		 /* Content/Images/skore.ico (skore.png) */
	SK_EDITOR_ICON_COUNT = 5
} sk_editor_icon_id_t;

/**
 * Icon handle a window draws with: pass @p view_index as the texture id to
 * ui->widget_image_rect(ctx, parent, view_index, w, h, uv0x, uv0y, uv1x, uv1y,
 * ...). UVs are in atlas space (0..1) and select the icon's sub-rect inside
 * the shared atlas texture (the only host image binding in the registry).
 */
typedef struct sk_editor_icon_t {
	u32 view_index; /* index into sk_editor_icons_views() → texture_id */
	f32 uv0x;
	f32 uv0y;
	f32 uv1x;
	f32 uv1y;
	u32 width;		  /* source pixel size (for aspect-ratio sizing) */
	u32 height;		  /* source pixel size (for aspect-ratio sizing) */
	const_chr_t name; /* stable short id ("folder", "file", ...) */
	const_chr_t file; /* C++ Content/Images file this icon came from */
} sk_editor_icon_t;

/** Opaque icon registry: owns the decoded + uploaded atlas. */
typedef struct sk_editor_icons_t sk_editor_icons_t;

/**
 * Create the icon registry: decode all five embedded icons, pack them into a
 * single RGBA8 atlas, and upload it through @p rd (staging buffer → copy →
 * view). The registry keeps the render device alive references for destroy.
 * @return Registry, or NULL on decode / allocation / device failure.
 */
sk_editor_icons_t* sk_editor_icons_create(const sk_render_device_api_t* rd, sk_render_device_t device);

/** Destroy the registry and all owned GPU resources. Safe on NULL. */
void sk_editor_icons_destroy(sk_editor_icons_t* icons);

/**
 * The lookup windows call: icon id → handle (view_index + UV sub-rect).
 * Returns NULL when @p icons is NULL or @p id is out of range.
 */
const sk_editor_icon_t* sk_editor_icons_get(const sk_editor_icons_t* icons, sk_editor_icon_id_t id);

/**
 * Host image bindings for the UI renderer: pass to
 * sk_ui_renderer_images_t at renderer_encode / capture_frame time. The array
 * has one slot per icon id plus a zero slot 0 (the paint pass treats
 * texture_id 0 as "no texture"): every icon handle owns its non-zero slot
 * (view_index = id + 1) pointing at the shared atlas view, and selects its
 * pixels via UVs. @p out_count receives the array length.
 */
const sk_texture_view_t* sk_editor_icons_views(const sk_editor_icons_t* icons, u32* out_count);

/** Register a live registry on the app context (windows resolve it). */
void sk_editor_icons_register(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_icons_t* icons);

/** Resolve the registered registry, or NULL when none is registered. */
sk_editor_icons_t* sk_editor_icons_resolve(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
