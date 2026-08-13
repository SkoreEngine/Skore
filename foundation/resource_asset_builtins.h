#pragma once

/**
 * @file resource_asset_builtins.h
 * @brief Concrete asset handlers/importers ported from main as file-static
 *        sk_resource_asset_handler_t / sk_resource_asset_importer_t tables,
 *        registered via sk_app_api_t::add_impl.
 *
 * Thumbnail / PreviewGenerator surface is intentionally omitted (see
 * docs/repository-assets-inventory.md §3). Editor-only OpenAsset actions are
 * no-ops. Full FBX/GLTF/OBJ mesh decode, font MSDF atlas generation, texture
 * GPU processing, and DXC shader compile are deferred: importers still claim
 * the same extensions, declare the same ingest sub-resources / dependencies
 * where path-local, and cook creates the declared payload shell so the core
 * import pipeline matches main's wrapper/sub-resource shape.
 */

#include "app.h"
#include "common.h"
#include "repository.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Payload resource type ids (stable MD5("sk.*") halves)             */
/* ------------------------------------------------------------------ */

#define SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_LO 0x26caf0838c2a74ffULL
#define SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_HI 0xa2bb9ac112b9b41dULL
#define SK_ANIMATION_CLIP_RESOURCE_TYPE_ID SK_TYPE_ID("sk.animation_clip_resource", SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_LO, SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_HI)

#define SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_LO 0x14efcda2644cbf4dULL
#define SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_HI 0x1c9840c09364f6eeULL
#define SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID \
	SK_TYPE_ID("sk.animation_controller_resource", SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_LO, SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_HI)

#define SK_AUDIO_RESOURCE_TYPE_ID_LO 0xb13f137870875927ULL
#define SK_AUDIO_RESOURCE_TYPE_ID_HI 0x08aea7d81aebe197ULL
#define SK_AUDIO_RESOURCE_TYPE_ID SK_TYPE_ID("sk.audio_resource", SK_AUDIO_RESOURCE_TYPE_ID_LO, SK_AUDIO_RESOURCE_TYPE_ID_HI)

#define SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_LO 0xf412fb44a006cee2ULL
#define SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_HI 0xfb74857d7c3f3478ULL
#define SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID SK_TYPE_ID("sk.csharp_script_resource", SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_LO, SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_HI)

#define SK_DCC_ASSET_TYPE_ID_LO 0xec8a5fbd0cf401b5ULL
#define SK_DCC_ASSET_TYPE_ID_HI 0x64e97f4f6d0940e2ULL
#define SK_DCC_ASSET_TYPE_ID SK_TYPE_ID("sk.dcc_asset", SK_DCC_ASSET_TYPE_ID_LO, SK_DCC_ASSET_TYPE_ID_HI)

#define SK_ENTITY_RESOURCE_TYPE_ID_LO 0x6f4a38b3b59f4a23ULL
#define SK_ENTITY_RESOURCE_TYPE_ID_HI 0x49ae107dbbe36a85ULL
#define SK_ENTITY_RESOURCE_TYPE_ID SK_TYPE_ID("sk.entity_resource", SK_ENTITY_RESOURCE_TYPE_ID_LO, SK_ENTITY_RESOURCE_TYPE_ID_HI)

#define SK_FONT_RESOURCE_TYPE_ID_LO 0xd1d92b52fbeba0d6ULL
#define SK_FONT_RESOURCE_TYPE_ID_HI 0x4550d0fafade16e0ULL
#define SK_FONT_RESOURCE_TYPE_ID SK_TYPE_ID("sk.font_resource", SK_FONT_RESOURCE_TYPE_ID_LO, SK_FONT_RESOURCE_TYPE_ID_HI)

#define SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_LO 0x6adff297f06b74c9ULL
#define SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_HI 0xc67bb91b849afb12ULL
#define SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID SK_TYPE_ID("sk.material_graph_resource", SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_LO, SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_HI)

#define SK_MESH_RESOURCE_TYPE_ID_LO 0xe459a55f60295b36ULL
#define SK_MESH_RESOURCE_TYPE_ID_HI 0xe96f4ffc74770c64ULL
#define SK_MESH_RESOURCE_TYPE_ID SK_TYPE_ID("sk.mesh_resource", SK_MESH_RESOURCE_TYPE_ID_LO, SK_MESH_RESOURCE_TYPE_ID_HI)

#define SK_UI_DOCUMENT_RESOURCE_TYPE_ID_LO 0xa1908d2c3131d0f1ULL
#define SK_UI_DOCUMENT_RESOURCE_TYPE_ID_HI 0xe1e224ae957e6a4eULL
#define SK_UI_DOCUMENT_RESOURCE_TYPE_ID SK_TYPE_ID("sk.ui_document_resource", SK_UI_DOCUMENT_RESOURCE_TYPE_ID_LO, SK_UI_DOCUMENT_RESOURCE_TYPE_ID_HI)

#define SK_UI_STYLE_RESOURCE_TYPE_ID_LO 0x9df07911592bb997ULL
#define SK_UI_STYLE_RESOURCE_TYPE_ID_HI 0x8682aca9865aed82ULL
#define SK_UI_STYLE_RESOURCE_TYPE_ID SK_TYPE_ID("sk.ui_style_resource", SK_UI_STYLE_RESOURCE_TYPE_ID_LO, SK_UI_STYLE_RESOURCE_TYPE_ID_HI)

#define SK_SCENE_RESOURCE_TYPE_ID_LO 0x337e27ed4a123a48ULL
#define SK_SCENE_RESOURCE_TYPE_ID_HI 0x52738ae7965e7018ULL
#define SK_SCENE_RESOURCE_TYPE_ID SK_TYPE_ID("sk.scene_resource", SK_SCENE_RESOURCE_TYPE_ID_LO, SK_SCENE_RESOURCE_TYPE_ID_HI)

#define SK_SHADER_RESOURCE_TYPE_ID_LO 0x79e422ab62d334bfULL
#define SK_SHADER_RESOURCE_TYPE_ID_HI 0xea94206eeaefb353ULL
#define SK_SHADER_RESOURCE_TYPE_ID SK_TYPE_ID("sk.shader_resource", SK_SHADER_RESOURCE_TYPE_ID_LO, SK_SHADER_RESOURCE_TYPE_ID_HI)

#define SK_TEXTURE_RESOURCE_TYPE_ID_LO 0xd3ae4dd3f773f74fULL
#define SK_TEXTURE_RESOURCE_TYPE_ID_HI 0x40523ed4f617ae50ULL
#define SK_TEXTURE_RESOURCE_TYPE_ID SK_TYPE_ID("sk.texture_resource", SK_TEXTURE_RESOURCE_TYPE_ID_LO, SK_TEXTURE_RESOURCE_TYPE_ID_HI)

#define SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_LO 0xec7b515cf530cc8bULL
#define SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_HI 0x0852a3e7e7b31836ULL
#define SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID SK_TYPE_ID("sk.texture_import_settings", SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_LO, SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_HI)

#define SK_FBX_IMPORT_SETTINGS_TYPE_ID_LO 0x794ede262f1a869aULL
#define SK_FBX_IMPORT_SETTINGS_TYPE_ID_HI 0x29fd979a884485d2ULL
#define SK_FBX_IMPORT_SETTINGS_TYPE_ID SK_TYPE_ID("sk.fbx_import_settings", SK_FBX_IMPORT_SETTINGS_TYPE_ID_LO, SK_FBX_IMPORT_SETTINGS_TYPE_ID_HI)

#define SK_GLTF_IMPORT_SETTINGS_TYPE_ID_LO 0x040bebfc3caf0436ULL
#define SK_GLTF_IMPORT_SETTINGS_TYPE_ID_HI 0xafb34c86fa8ad9aeULL
#define SK_GLTF_IMPORT_SETTINGS_TYPE_ID SK_TYPE_ID("sk.gltf_import_settings", SK_GLTF_IMPORT_SETTINGS_TYPE_ID_LO, SK_GLTF_IMPORT_SETTINGS_TYPE_ID_HI)

#define SK_OBJ_IMPORT_SETTINGS_TYPE_ID_LO 0x2f4ecd8317b94286ULL
#define SK_OBJ_IMPORT_SETTINGS_TYPE_ID_HI 0x51f9dfec58a15bd6ULL
#define SK_OBJ_IMPORT_SETTINGS_TYPE_ID SK_TYPE_ID("sk.obj_import_settings", SK_OBJ_IMPORT_SETTINGS_TYPE_ID_LO, SK_OBJ_IMPORT_SETTINGS_TYPE_ID_HI)

/* Named resource field indices (Name always 0; Content 1 where present). */
enum sk_named_resource_field_t {
	SK_NAMED_RESOURCE_FIELD_NAME = 0,
	SK_NAMED_RESOURCE_FIELD_CONTENT = 1,
	SK_NAMED_RESOURCE_FIELD_BYTES = 1,
};

/**
 * Register minimal payload resource types (Name [+ Content/Bytes]) used by the
 * built-in handlers/importers into @p repository.
 * @param repository Target repository (must not be NULL).
 * @param repo_api   Repository function table (must not be NULL).
 * @return 0 on success, first register_type error otherwise.
 */
i32 sk_resource_asset_builtins_register_types(sk_repository_t* repository, const sk_repository_api_t* repo_api);

/**
 * Register every concrete handler and importer via add_impl.
 * Safe to call once per app context after plugins/app bootstrap.
 */
void sk_resource_asset_builtins_register_impls(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
