/**
 * @file resource_asset_builtins.c
 * @brief Concrete asset handlers/importers as file-static tables + add_impl
 *        registration (port of main Editor Resource/Handlers + Importers).
 *
 * Thumbnails / PreviewGenerator omitted. Heavy cooks (MSDF fonts, texture
 * decode/GPU mips, FBX/GLTF/OBJ mesh decode, DXC) create declared payload
 * shells only so the core ingest/cook pipeline stays exercisable without
 * vendoring those stacks.
 */

#include "resource_asset_builtins.h"

#include "allocator.h"
#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "resource_assets.h"
#include "resource_assets_types.h"
#include "resource_component_types.h"
#include "resource_serialize.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Bound repository for Create / cook writes                         */
/* ------------------------------------------------------------------ */

static const sk_filesystem_api_t* builtins_fs_api(void) {
	static const sk_filesystem_api_t* cached = NULL;
	if (cached == NULL) {
		sk_app_boot_t boot = sk_app_create();
		cached = boot.api->filesystem_api(boot.context);
		sk_app_shutdown(boot.context);
	}
	return cached;
}

static sk_rid_t builtins_create(sk_repository_t* repository, const sk_repository_api_t* repo, sk_type_id_t type_id, sk_uuid_t uuid, sk_undo_redo_scope_t* scope, const_chr_t name) {
	const sk_resource_type_t* type = repo->find_type(repository, type_id);
	if (type == NULL) {
		return SK_RID_ZERO;
	}
	sk_rid_t rid = repo->create_resource(repository, type, uuid, scope);
	if (rid.id == 0u || name == NULL || name[0] == '\0') {
		return rid;
	}
	sk_resource_object_t view = repo->write(repository, rid);
	repo->set_string(view, SK_NAMED_RESOURCE_FIELD_NAME, name);
	repo->commit(view, scope);
	return rid;
}

static void builtins_set_name(sk_repository_t* repository, const sk_repository_api_t* repo, sk_rid_t rid, const_chr_t name, sk_undo_redo_scope_t* scope) {
	if (rid.id == 0u || name == NULL) {
		return;
	}
	sk_resource_object_t view = repo->write(repository, rid);
	repo->set_string(view, SK_NAMED_RESOURCE_FIELD_NAME, name);
	repo->commit(view, scope);
}

static void builtins_set_content(sk_repository_t* repository, const sk_repository_api_t* repo, sk_rid_t rid, const_chr_t content, sk_undo_redo_scope_t* scope) {
	if (rid.id == 0u) {
		return;
	}
	sk_resource_object_t view = repo->write(repository, rid);
	repo->set_string(view, SK_NAMED_RESOURCE_FIELD_CONTENT, content != NULL ? content : "");
	repo->commit(view, scope);
}

static const_chr_t cook_source_name(sk_resource_cook_context_t* ctx, const_chr_t fallback) {
	const sk_repository_api_t* repo = ctx->repo_api;
	sk_resource_object_t wrapper = repo->read(ctx->repository, ctx->imported_asset);
	const_chr_t original = repo->get_string(wrapper, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME);
	if (original == NULL || original[0] == '\0') {
		return fallback;
	}
	return original;
}

/* ------------------------------------------------------------------ */
/*  Minimal payload type descriptors (Name [+ Content])               */
/* ------------------------------------------------------------------ */

typedef struct sk_named_resource_t {
	sk_field_string_t name;
} sk_named_resource_t;

typedef struct sk_content_resource_t {
	sk_field_string_t name;
	sk_field_string_t content;
} sk_content_resource_t;

typedef struct sk_audio_resource_t {
	sk_field_string_t name;
	sk_field_blob_t bytes;
} sk_audio_resource_t;

static const sk_resource_field_t named_resource_fields[] = {
	{"Name", SK_NAMED_RESOURCE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_named_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
};

static const sk_resource_field_t content_resource_fields[] = {
	{"Name", SK_NAMED_RESOURCE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_content_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Content", SK_NAMED_RESOURCE_FIELD_CONTENT, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_content_resource_t, content), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
};

static const sk_resource_field_t audio_resource_fields[] = {
	{"Name", SK_NAMED_RESOURCE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_audio_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Bytes", SK_NAMED_RESOURCE_FIELD_BYTES, SK_RESOURCE_FIELD_TYPE_BLOB, (u32)offsetof(sk_audio_resource_t, bytes), (u32)sizeof(sk_field_blob_t), {0ull, 0ull}},
};

#define SK_BUILTIN_NAMED_TYPE(_id_lo, _id_hi, _name, _c_name)                                                                                                            \
	static const sk_resource_type_desc_t _c_name##_type_desc = {                                                                                                         \
		{_id_lo, _id_hi}, _name, (u32)sizeof(sk_named_resource_t), named_resource_fields, (u32)(sizeof(named_resource_fields) / sizeof(named_resource_fields[0])), NULL, \
	}

#define SK_BUILTIN_CONTENT_TYPE(_id_lo, _id_hi, _name, _c_name)                                                                                                                  \
	static const sk_resource_type_desc_t _c_name##_type_desc = {                                                                                                                 \
		{_id_lo, _id_hi}, _name, (u32)sizeof(sk_content_resource_t), content_resource_fields, (u32)(sizeof(content_resource_fields) / sizeof(content_resource_fields[0])), NULL, \
	}

SK_BUILTIN_NAMED_TYPE(SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_LO, SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_HI, "AnimationClipResource", animation_clip);
SK_BUILTIN_NAMED_TYPE(SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_LO, SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_HI, "AnimationControllerResource", animation_controller);
static const sk_resource_type_desc_t audio_resource_type_desc = {
	{SK_AUDIO_RESOURCE_TYPE_ID_LO, SK_AUDIO_RESOURCE_TYPE_ID_HI},
	"AudioResource",
	(u32)sizeof(sk_audio_resource_t),
	audio_resource_fields,
	(u32)(sizeof(audio_resource_fields) / sizeof(audio_resource_fields[0])),
	NULL,
};
SK_BUILTIN_NAMED_TYPE(SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_LO, SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_HI, "CSharpScriptResource", csharp_script);
SK_BUILTIN_NAMED_TYPE(SK_DCC_ASSET_TYPE_ID_LO, SK_DCC_ASSET_TYPE_ID_HI, "DCCAsset", dcc_asset);

/* EntityResource (APX-296): Name stays at index 0 (existing JSON envelopes
 * stay valid); Components / Children are owned SubObjectLists — component
 * resources and recursively nested child entity_resource payloads. Type id
 * and name are unchanged, so existing handlers / serialized documents keep
 * working (see docs/resource-to-ecs-mapping-contract.md §3.1). */
typedef struct sk_entity_resource_t {
	sk_field_string_t name;				  /* SK_ENTITY_RESOURCE_FIELD_NAME */
	sk_field_subobject_list_t components; /* SK_ENTITY_RESOURCE_FIELD_COMPONENTS */
	sk_field_subobject_list_t children;	  /* SK_ENTITY_RESOURCE_FIELD_CHILDREN */
} sk_entity_resource_t;

static const sk_resource_field_t entity_resource_fields[] = {
	{"Name", SK_ENTITY_RESOURCE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_entity_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Components",
	 SK_ENTITY_RESOURCE_FIELD_COMPONENTS,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_entity_resource_t, components),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
	{"Children",
	 SK_ENTITY_RESOURCE_FIELD_CHILDREN,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_entity_resource_t, children),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
};

static const sk_resource_type_desc_t entity_resource_type_desc = {
	{SK_ENTITY_RESOURCE_TYPE_ID_LO, SK_ENTITY_RESOURCE_TYPE_ID_HI},
	"EntityResource",
	(u32)sizeof(sk_entity_resource_t),
	entity_resource_fields,
	(u32)(sizeof(entity_resource_fields) / sizeof(entity_resource_fields[0])),
	NULL,
};

SK_BUILTIN_NAMED_TYPE(SK_FONT_RESOURCE_TYPE_ID_LO, SK_FONT_RESOURCE_TYPE_ID_HI, "FontResource", font_resource);
SK_BUILTIN_NAMED_TYPE(SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_LO, SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_HI, "MaterialGraphResource", material_graph);
SK_BUILTIN_NAMED_TYPE(SK_MESH_RESOURCE_TYPE_ID_LO, SK_MESH_RESOURCE_TYPE_ID_HI, "MeshResource", mesh_resource);
SK_BUILTIN_CONTENT_TYPE(SK_UI_DOCUMENT_RESOURCE_TYPE_ID_LO, SK_UI_DOCUMENT_RESOURCE_TYPE_ID_HI, "UIDocumentResource", ui_document);
SK_BUILTIN_CONTENT_TYPE(SK_UI_STYLE_RESOURCE_TYPE_ID_LO, SK_UI_STYLE_RESOURCE_TYPE_ID_HI, "UIStyleResource", ui_style);
/* SceneResource (APX-297): Name stays at index 0 (existing JSON envelopes
 * stay valid); Roots is an owned SubObjectList of root entity_resource
 * payloads (resource-to-ECS mapping contract §4.1). Each root entity_resource
 * already carries Components + Children, so the scene needs no implicit scene
 * entity. Type id and name are unchanged. */
typedef struct sk_scene_resource_t {
	sk_field_string_t name;			 /* SK_SCENE_RESOURCE_FIELD_NAME */
	sk_field_subobject_list_t roots; /* SK_SCENE_RESOURCE_FIELD_ROOTS */
} sk_scene_resource_t;

static const sk_resource_field_t scene_resource_fields[] = {
	{"Name", SK_SCENE_RESOURCE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_scene_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Roots",
	 SK_SCENE_RESOURCE_FIELD_ROOTS,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_scene_resource_t, roots),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
};

static const sk_resource_type_desc_t scene_resource_type_desc = {
	{SK_SCENE_RESOURCE_TYPE_ID_LO, SK_SCENE_RESOURCE_TYPE_ID_HI},
	"SceneResource",
	(u32)sizeof(sk_scene_resource_t),
	scene_resource_fields,
	(u32)(sizeof(scene_resource_fields) / sizeof(scene_resource_fields[0])),
	NULL,
};

SK_BUILTIN_CONTENT_TYPE(SK_SHADER_RESOURCE_TYPE_ID_LO, SK_SHADER_RESOURCE_TYPE_ID_HI, "ShaderResource", shader_resource);
SK_BUILTIN_NAMED_TYPE(SK_TEXTURE_RESOURCE_TYPE_ID_LO, SK_TEXTURE_RESOURCE_TYPE_ID_HI, "TextureResource", texture_resource);
SK_BUILTIN_NAMED_TYPE(SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_LO, SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_HI, "TextureImportSettings", texture_import_settings);
SK_BUILTIN_NAMED_TYPE(SK_FBX_IMPORT_SETTINGS_TYPE_ID_LO, SK_FBX_IMPORT_SETTINGS_TYPE_ID_HI, "FBXImportSettings", fbx_import_settings);
SK_BUILTIN_NAMED_TYPE(SK_GLTF_IMPORT_SETTINGS_TYPE_ID_LO, SK_GLTF_IMPORT_SETTINGS_TYPE_ID_HI, "GLTFImportSettings", gltf_import_settings);
SK_BUILTIN_NAMED_TYPE(SK_OBJ_IMPORT_SETTINGS_TYPE_ID_LO, SK_OBJ_IMPORT_SETTINGS_TYPE_ID_HI, "ObjImportSettings", obj_import_settings);

static const sk_resource_type_desc_t* const builtin_type_descs[] = {
	&animation_clip_type_desc,
	&animation_controller_type_desc,
	&audio_resource_type_desc,
	&csharp_script_type_desc,
	&dcc_asset_type_desc,
	&entity_resource_type_desc,
	&font_resource_type_desc,
	&material_graph_type_desc,
	&mesh_resource_type_desc,
	&ui_document_type_desc,
	&ui_style_type_desc,
	&scene_resource_type_desc,
	&shader_resource_type_desc,
	&texture_resource_type_desc,
	&texture_import_settings_type_desc,
	&fbx_import_settings_type_desc,
	&gltf_import_settings_type_desc,
	&obj_import_settings_type_desc,
};

i32 sk_resource_asset_builtins_register_types(sk_repository_t* repository, const sk_repository_api_t* repo_api) {
	const sk_repository_api_t* api = repo_api;
	u32 count = (u32)(sizeof(builtin_type_descs) / sizeof(builtin_type_descs[0]));
	for (u32 i = 0u; i < count; ++i) {
		i32 result = api->register_type(repository, builtin_type_descs[i]);
		if (result != 0) {
			return result;
		}
	}
	/* Built-in ECS component payload types (APX-300) — their registered
	 * repository type id doubles as the ECS component type id (mapping
	 * contract §3). Registered from a dedicated, app-free core module so the
	 * sk-entities plugin can also register them into its own repositories. */
	return sk_resource_component_types_register(repository, repo_api);
}

/* ------------------------------------------------------------------ */
/*  Shared handler helpers                                            */
/* ------------------------------------------------------------------ */

/* Font Awesome 6 codepoint strings (main Icons.h); UTF-8 bytes. */
#define SK_ICON_FA_FILE "\xef\x85\x9b"
#define SK_ICON_FA_FILE_AUDIO "\xef\x87\x87"
#define SK_ICON_FA_FILE_CODE "\xef\x87\x89"
#define SK_ICON_FA_FONT "\xef\x80\xb1"
#define SK_ICON_FA_CUBES "\xef\x86\xb3"
#define SK_ICON_FA_PALETTE "\xef\x94\xbf"
#define SK_ICON_FA_PERSON_RUNNING "\xef\x9c\x8c"
#define SK_ICON_FA_DIAGRAM_PROJECT "\xef\x95\x82"

static const_chr_t handler_icon_file(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_FILE;
}

/* ------------------------------------------------------------------ */
/*  Handlers                                                          */
/* ------------------------------------------------------------------ */

static const_chr_t animation_clip_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".animation";
}
static sk_type_id_t animation_clip_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_ANIMATION_CLIP_RESOURCE_TYPE_ID;
}
static const_chr_t animation_clip_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Animation Clip";
}
static sk_rid_t animation_clip_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_ANIMATION_CLIP_RESOURCE_TYPE_ID, uuid, scope, NULL);
}
static const_chr_t animation_clip_icon_fn(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_PERSON_RUNNING;
}
static sk_resource_asset_handler_t animation_clip_handler = {
	.user_data = NULL,
	.extension = animation_clip_extension,
	.open_asset = NULL,
	.get_resource_type_id = animation_clip_type,
	.get_desc = animation_clip_desc,
	.load = NULL,
	.save = NULL,
	.create = animation_clip_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = animation_clip_icon_fn,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t anim_controller_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".animcontroller";
}
static sk_type_id_t anim_controller_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID;
}
static const_chr_t anim_controller_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Animation Controller";
}
static sk_rid_t anim_controller_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID, uuid, scope, "AnimationController");
}
static sk_resource_asset_handler_t animation_controller_handler = {
	.user_data = NULL,
	.extension = anim_controller_extension,
	.open_asset = NULL,
	.get_resource_type_id = anim_controller_type,
	.get_desc = anim_controller_desc,
	.load = NULL,
	.save = NULL,
	.create = anim_controller_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t audio_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".audio";
}
static sk_type_id_t audio_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_AUDIO_RESOURCE_TYPE_ID;
}
static const_chr_t audio_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Audio Clip";
}
static sk_rid_t audio_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_AUDIO_RESOURCE_TYPE_ID, uuid, scope, NULL);
}
static const_chr_t audio_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_FILE_AUDIO;
}
static sk_resource_asset_handler_t audio_handler = {
	.user_data = NULL,
	.extension = audio_extension,
	.open_asset = NULL,
	.get_resource_type_id = audio_type,
	.get_desc = audio_desc,
	.load = NULL,
	.save = NULL,
	.create = audio_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = audio_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

/* C# script: Load sets Name; Save writes skeleton if missing. Dotnet rebuild omitted. */
static const_chr_t csharp_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".cs";
}
static sk_type_id_t csharp_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID;
}
static const_chr_t csharp_desc(void_ptr_t user_data) {
	(void)user_data;
	return "C# Component";
}
static sk_rid_t csharp_load(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t absolute_path) {
	(void)user_data;
	(void)asset;
	char name[256];
	if (sk_path_name(sk_str_view_cstr(absolute_path), name, (u32)sizeof(name)) < 0) {
		name[0] = '\0';
	}
	return builtins_create(repository, repo_api, SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID, SK_UUID_ZERO, NULL, name);
}
static void csharp_save(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t object, const_chr_t absolute_path) {
	(void)user_data;
	(void)repository;
	(void)repo_api;
	(void)object;
	const sk_filesystem_api_t* fs = builtins_fs_api();
	if (fs->get_file_status(absolute_path) == SK_FILE_STATUS_FILE) {
		return;
	}
	char name[256];
	if (sk_path_name(sk_str_view_cstr(absolute_path), name, (u32)sizeof(name)) < 0) {
		name[0] = '\0';
	}
	char body[512];
	/* Minimal class skeleton (namespace uses literal Project; editor path omitted). */
	i32 n = 0;
	n += snprintf(body + n, sizeof(body) - (size_t)n, "namespace Project;\n\npublic class %s\n{\n    \n}\n", name);
	(void)n;
	sk_file_handle_t file = fs->open_file(absolute_path, SK_FILE_ACCESS_WRITE);
	if (file == NULL) {
		return;
	}
	fs->write_file(file, body, strlen(body));
	fs->close_file(file);
}
static sk_rid_t csharp_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID, uuid, scope, "Script");
}
static const_chr_t csharp_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_FILE_CODE;
}
static sk_resource_asset_handler_t csharp_script_handler = {
	.user_data = NULL,
	.extension = csharp_extension,
	.open_asset = NULL,
	.get_resource_type_id = csharp_type,
	.get_desc = csharp_desc,
	.load = csharp_load,
	.save = csharp_save,
	.create = csharp_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = csharp_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t dcc_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".dcc_asset";
}
static sk_type_id_t dcc_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_DCC_ASSET_TYPE_ID;
}
static const_chr_t dcc_desc(void_ptr_t user_data) {
	(void)user_data;
	return "DCC Asset";
}
static sk_rid_t dcc_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_DCC_ASSET_TYPE_ID, uuid, scope, NULL);
}
static const_chr_t dcc_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_CUBES;
}
static sk_resource_asset_handler_t dcc_asset_handler = {
	.user_data = NULL,
	.extension = dcc_extension,
	.open_asset = NULL,
	.get_resource_type_id = dcc_type,
	.get_desc = dcc_desc,
	.load = NULL,
	.save = NULL,
	.create = dcc_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = dcc_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t entity_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".entity";
}
static sk_type_id_t entity_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_ENTITY_RESOURCE_TYPE_ID;
}
static const_chr_t entity_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Entity";
}
static sk_rid_t entity_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_ENTITY_RESOURCE_TYPE_ID, uuid, scope, "Entity");
}
static sk_resource_asset_handler_t entity_handler = {
	.user_data = NULL,
	.extension = entity_extension,
	.open_asset = NULL,
	.get_resource_type_id = entity_type,
	.get_desc = entity_desc,
	.load = NULL,
	.save = NULL,
	.create = entity_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t font_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".font";
}
static sk_type_id_t font_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_FONT_RESOURCE_TYPE_ID;
}
static const_chr_t font_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Font";
}
static sk_rid_t font_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_FONT_RESOURCE_TYPE_ID, uuid, scope, NULL);
}
static const_chr_t font_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_FONT;
}
static sk_resource_asset_handler_t font_handler = {
	.user_data = NULL,
	.extension = font_extension,
	.open_asset = NULL,
	.get_resource_type_id = font_type,
	.get_desc = font_desc,
	.load = NULL,
	.save = NULL,
	.create = font_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = font_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t matgraph_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".matgraph";
}
static sk_type_id_t matgraph_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID;
}
static const_chr_t matgraph_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Material Graph";
}
static sk_rid_t matgraph_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	/* Full node graph seed needs MaterialGraphNode types (later). Name only. */
	return builtins_create(repository, repo_api, SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID, uuid, scope, "MaterialGraph");
}
static const_chr_t matgraph_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_DIAGRAM_PROJECT;
}
static sk_resource_asset_handler_t material_graph_handler = {
	.user_data = NULL,
	.extension = matgraph_extension,
	.open_asset = NULL,
	.get_resource_type_id = matgraph_type,
	.get_desc = matgraph_desc,
	.load = NULL,
	.save = NULL,
	.create = matgraph_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = matgraph_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t mesh_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".mesh";
}
static sk_type_id_t mesh_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_MESH_RESOURCE_TYPE_ID;
}
static const_chr_t mesh_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Mesh";
}
static sk_rid_t mesh_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_MESH_RESOURCE_TYPE_ID, uuid, scope, "Mesh");
}
static sk_resource_asset_handler_t mesh_handler = {
	.user_data = NULL,
	.extension = mesh_extension,
	.open_asset = NULL,
	.get_resource_type_id = mesh_type,
	.get_desc = mesh_desc,
	.load = NULL,
	.save = NULL,
	.create = mesh_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

/* Rml UI document / style: content load/save. */
static sk_rid_t content_load(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_type_id_t type_id, const_chr_t absolute_path) {
	const sk_filesystem_api_t* fs = builtins_fs_api();
	char name[256];
	if (sk_path_name(sk_str_view_cstr(absolute_path), name, (u32)sizeof(name)) < 0) {
		name[0] = '\0';
	}
	sk_rid_t rid = builtins_create(repository, repo_api, type_id, SK_UUID_ZERO, NULL, name);
	if (rid.id == 0u) {
		return rid;
	}
	sk_file_handle_t file = fs->open_file(absolute_path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		return rid;
	}
	u64 size = fs->get_file_size(file);
	if (size > 0u && size < 0x100000ull) {
		char* buf = (char*)sk_allocator_default()->alloc(sk_allocator_default()->instance, size + 1u);
		if (buf != NULL) {
			fs->read_file(file, buf, size);
			buf[size] = '\0';
			builtins_set_content(repository, repo_api, rid, buf, NULL);
			sk_allocator_default()->free(sk_allocator_default()->instance, buf);
		}
	}
	fs->close_file(file);
	return rid;
}

static void content_save(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t object, const_chr_t absolute_path) {
	const sk_repository_api_t* repo = repo_api;
	const sk_filesystem_api_t* fs = builtins_fs_api();
	sk_resource_object_t view = repo->read(repository, object);
	const_chr_t content = repo->get_string(view, SK_NAMED_RESOURCE_FIELD_CONTENT);
	if (content == NULL) {
		content = "";
	}
	sk_file_handle_t file = fs->open_file(absolute_path, SK_FILE_ACCESS_WRITE);
	if (file == NULL) {
		return;
	}
	fs->write_file(file, content, strlen(content));
	fs->close_file(file);
}

static const_chr_t rml_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".rml";
}
static sk_type_id_t rml_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_UI_DOCUMENT_RESOURCE_TYPE_ID;
}
static const_chr_t rml_desc(void_ptr_t user_data) {
	(void)user_data;
	return "UI Document";
}
static sk_rid_t rml_load(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t absolute_path) {
	(void)user_data;
	(void)asset;
	return content_load(repository, repo_api, SK_UI_DOCUMENT_RESOURCE_TYPE_ID, absolute_path);
}
static void rml_save(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t object, const_chr_t absolute_path) {
	(void)user_data;
	content_save(repository, repo_api, object, absolute_path);
}
static sk_rid_t rml_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	sk_rid_t rid = builtins_create(repository, repo_api, SK_UI_DOCUMENT_RESOURCE_TYPE_ID, uuid, scope, "Document");
	builtins_set_content(repository, repo_api, rid, "<rml>\n<head>\n</head>\n<body>\n</body>\n</rml>\n", scope);
	return rid;
}
static const_chr_t rml_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_FILE_CODE;
}
static sk_resource_asset_handler_t rml_document_handler = {
	.user_data = NULL,
	.extension = rml_extension,
	.open_asset = NULL,
	.get_resource_type_id = rml_type,
	.get_desc = rml_desc,
	.load = rml_load,
	.save = rml_save,
	.create = rml_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = rml_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t rcss_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".rcss";
}
static sk_type_id_t rcss_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_UI_STYLE_RESOURCE_TYPE_ID;
}
static const_chr_t rcss_desc(void_ptr_t user_data) {
	(void)user_data;
	return "UI Style";
}
static sk_rid_t rcss_load(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t absolute_path) {
	(void)user_data;
	(void)asset;
	return content_load(repository, repo_api, SK_UI_STYLE_RESOURCE_TYPE_ID, absolute_path);
}
static void rcss_save(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t object, const_chr_t absolute_path) {
	(void)user_data;
	content_save(repository, repo_api, object, absolute_path);
}
static sk_rid_t rcss_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	sk_rid_t rid = builtins_create(repository, repo_api, SK_UI_STYLE_RESOURCE_TYPE_ID, uuid, scope, "Style");
	builtins_set_content(repository, repo_api, rid, "body\n{\n}\n", scope);
	return rid;
}
static const_chr_t rcss_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_PALETTE;
}
static sk_resource_asset_handler_t rml_style_handler = {
	.user_data = NULL,
	.extension = rcss_extension,
	.open_asset = NULL,
	.get_resource_type_id = rcss_type,
	.get_desc = rcss_desc,
	.load = rcss_load,
	.save = rcss_save,
	.create = rcss_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = rcss_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t scene_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".scene";
}
static sk_type_id_t scene_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_SCENE_RESOURCE_TYPE_ID;
}
static const_chr_t scene_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Scene";
}
static sk_rid_t scene_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	/* Prototype Lighting/PostProcessing entities need the package content system. */
	return builtins_create(repository, repo_api, SK_SCENE_RESOURCE_TYPE_ID, uuid, scope, "Scene");
}
static const_chr_t scene_icon(void_ptr_t user_data) {
	(void)user_data;
	return SK_ICON_FA_CUBES;
}
static sk_resource_asset_handler_t scene_handler = {
	.user_data = NULL,
	.extension = scene_extension,
	.open_asset = NULL,
	.get_resource_type_id = scene_type,
	.get_desc = scene_desc,
	.load = NULL,
	.save = NULL,
	.create = scene_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = scene_icon,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

/* Shader handlers share ShaderResource type; Load reads source as Content (no DXC). */
static sk_rid_t shader_load(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t absolute_path) {
	(void)user_data;
	(void)asset;
	return content_load(repository, repo_api, SK_SHADER_RESOURCE_TYPE_ID, absolute_path);
}
static sk_type_id_t shader_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_SHADER_RESOURCE_TYPE_ID;
}
static const_chr_t shader_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Shader";
}
static sk_rid_t shader_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_SHADER_RESOURCE_TYPE_ID, uuid, scope, "Shader");
}

static const_chr_t raster_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".raster";
}
static sk_resource_asset_handler_t raster_shader_handler = {
	.user_data = NULL,
	.extension = raster_extension,
	.open_asset = NULL,
	.get_resource_type_id = shader_type,
	.get_desc = shader_desc,
	.load = shader_load,
	.save = NULL,
	.create = shader_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t comp_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".comp";
}
static sk_resource_asset_handler_t compute_shader_handler = {
	.user_data = NULL,
	.extension = comp_extension,
	.open_asset = NULL,
	.get_resource_type_id = shader_type,
	.get_desc = shader_desc,
	.load = shader_load,
	.save = NULL,
	.create = shader_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t rt_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".rt";
}
static sk_resource_asset_handler_t raytrace_shader_handler = {
	.user_data = NULL,
	.extension = rt_extension,
	.open_asset = NULL,
	.get_resource_type_id = shader_type,
	.get_desc = shader_desc,
	.load = shader_load,
	.save = NULL,
	.create = shader_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t shader_cfg_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".shader";
}
static sk_resource_asset_handler_t config_shader_handler = {
	.user_data = NULL,
	.extension = shader_cfg_extension,
	.open_asset = NULL,
	.get_resource_type_id = shader_type,
	.get_desc = shader_desc,
	.load = shader_load,
	.save = NULL,
	.create = shader_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

static const_chr_t texture_extension(void_ptr_t user_data) {
	(void)user_data;
	return ".texture";
}
static sk_type_id_t texture_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_TEXTURE_RESOURCE_TYPE_ID;
}
static const_chr_t texture_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Texture";
}
static sk_rid_t texture_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	return builtins_create(repository, repo_api, SK_TEXTURE_RESOURCE_TYPE_ID, uuid, scope, NULL);
}
static sk_resource_asset_handler_t texture_handler = {
	.user_data = NULL,
	.extension = texture_extension,
	.open_asset = NULL,
	.get_resource_type_id = texture_type,
	.get_desc = texture_desc,
	.load = NULL,
	.save = NULL,
	.create = texture_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

/* Imported asset handler: empty extension; maps by resource type + output exts. */
static const_chr_t imported_extension(void_ptr_t user_data) {
	(void)user_data;
	return "";
}
static sk_type_id_t imported_type(void_ptr_t user_data) {
	(void)user_data;
	return SK_RESOURCE_IMPORTED_ASSET_TYPE_ID;
}
static const_chr_t imported_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Imported Asset";
}
static sk_rid_t imported_create(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	const sk_resource_type_t* type = repo_api->find_type(repository, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID);
	if (type == NULL) {
		return SK_RID_ZERO;
	}
	return repo_api->create_resource(repository, type, uuid, scope);
}
static sk_resource_asset_handler_t imported_asset_handler = {
	.user_data = NULL,
	.extension = imported_extension,
	.open_asset = NULL,
	.get_resource_type_id = imported_type,
	.get_desc = imported_desc,
	.load = NULL,
	.save = NULL,
	.create = imported_create,
	.reloaded = NULL,
	.after_move = NULL,
	.export_object = NULL,
	.get_icon = handler_icon_file,
	.get_load_order = NULL,
	.get_asset_name = NULL,
};

/* ------------------------------------------------------------------ */
/*  Importers                                                         */
/* ------------------------------------------------------------------ */

static u32 fill_exts(const_chr_t* list, u32 list_count, const_chr_t* out, u32 out_cap) {
	u32 n = list_count < out_cap ? list_count : out_cap;
	if (out != NULL) {
		for (u32 i = 0u; i < n; ++i) {
			out[i] = list[i];
		}
	}
	return list_count;
}

static u32 audio_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const_chr_t exts[] = {".wav", ".mp3", ".ogg", ".flac"};
	return fill_exts(exts, 4u, out, out_cap);
}
static const_chr_t audio_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".audio";
}
static u32 audio_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}
static sk_type_id_t audio_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_TYPE_ID_ZERO;
}
static void audio_importer_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	ctx->declare_sub_resource(ctx, "main", SK_AUDIO_RESOURCE_TYPE_ID);
}
static void audio_importer_cook(void_ptr_t user_data, sk_resource_cook_context_t* ctx) {
	(void)user_data;
	const_chr_t name = cook_source_name(ctx, "audio");
	sk_rid_t audio = ctx->sub_resource(ctx, "main", SK_AUDIO_RESOURCE_TYPE_ID);
	builtins_set_name(ctx->repository, ctx->repo_api, audio, name, ctx->scope);
	/* Bytes blob: no public set_blob accessor yet; name + sub-resource match main shape. */
	(void)ctx->source_bytes;
	(void)ctx->source_size;
}
static sk_resource_asset_importer_t audio_importer = {
	.user_data = NULL,
	.imported_extensions = audio_importer_exts,
	.output_extension = audio_importer_out,
	.cooker_version = audio_importer_cooker,
	.get_settings_type = audio_importer_settings,
	.ingest = audio_importer_ingest,
	.cook = audio_importer_cook,
	.import_asset = NULL,
};

static u32 font_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const_chr_t exts[] = {".ttf", ".otf"};
	return fill_exts(exts, 2u, out, out_cap);
}
static const_chr_t font_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".font";
}
static u32 font_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}
static sk_type_id_t font_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_TYPE_ID_ZERO;
}
static void font_importer_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	ctx->declare_sub_resource(ctx, "main", SK_FONT_RESOURCE_TYPE_ID);
}
static void font_importer_cook(void_ptr_t user_data, sk_resource_cook_context_t* ctx) {
	(void)user_data;
	/* MSDF atlas gen deferred; create named FontResource shell. */
	const_chr_t name = cook_source_name(ctx, "font");
	sk_rid_t font = ctx->sub_resource(ctx, "main", SK_FONT_RESOURCE_TYPE_ID);
	builtins_set_name(ctx->repository, ctx->repo_api, font, name, ctx->scope);
}
static sk_resource_asset_importer_t font_importer = {
	.user_data = NULL,
	.imported_extensions = font_importer_exts,
	.output_extension = font_importer_out,
	.cooker_version = font_importer_cooker,
	.get_settings_type = font_importer_settings,
	.ingest = font_importer_ingest,
	.cook = font_importer_cook,
	.import_asset = NULL,
};

static u32 texture_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const_chr_t exts[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"};
	return fill_exts(exts, 6u, out, out_cap);
}
static const_chr_t texture_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".texture";
}
static u32 texture_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}
static sk_type_id_t texture_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID;
}
static void texture_importer_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	ctx->declare_sub_resource(ctx, "main", SK_TEXTURE_RESOURCE_TYPE_ID);
}
static void texture_importer_cook(void_ptr_t user_data, sk_resource_cook_context_t* ctx) {
	(void)user_data;
	/* stb_image + GPU mip path deferred; named TextureResource shell. */
	const_chr_t name = cook_source_name(ctx, "texture");
	sk_rid_t tex = ctx->sub_resource(ctx, "main", SK_TEXTURE_RESOURCE_TYPE_ID);
	builtins_set_name(ctx->repository, ctx->repo_api, tex, name, ctx->scope);
}
static sk_resource_asset_importer_t texture_importer = {
	.user_data = NULL,
	.imported_extensions = texture_importer_exts,
	.output_extension = texture_importer_out,
	.cooker_version = texture_importer_cooker,
	.get_settings_type = texture_importer_settings,
	.ingest = texture_importer_ingest,
	.cook = texture_importer_cook,
	.import_asset = NULL,
};

static void dcc_root_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	ctx->declare_sub_resource(ctx, "root", SK_DCC_ASSET_TYPE_ID);
}
static void dcc_root_cook(void_ptr_t user_data, sk_resource_cook_context_t* ctx) {
	(void)user_data;
	const_chr_t name = cook_source_name(ctx, "model");
	sk_rid_t dcc = ctx->sub_resource(ctx, "root", SK_DCC_ASSET_TYPE_ID);
	builtins_set_name(ctx->repository, ctx->repo_api, dcc, name, ctx->scope);
}

static u32 fbx_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const_chr_t exts[] = {".fbx"};
	return fill_exts(exts, 1u, out, out_cap);
}
static const_chr_t fbx_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".dcc_asset";
}
static u32 fbx_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}
static sk_type_id_t fbx_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_FBX_IMPORT_SETTINGS_TYPE_ID;
}
static sk_resource_asset_importer_t fbx_importer = {
	.user_data = NULL,
	.imported_extensions = fbx_importer_exts,
	.output_extension = fbx_importer_out,
	.cooker_version = fbx_importer_cooker,
	.get_settings_type = fbx_importer_settings,
	.ingest = dcc_root_ingest,
	.cook = dcc_root_cook,
	.import_asset = NULL,
};

static u32 gltf_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const_chr_t exts[] = {".gltf", ".glb"};
	return fill_exts(exts, 2u, out, out_cap);
}
static const_chr_t gltf_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".dcc_asset";
}
static u32 gltf_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}
static sk_type_id_t gltf_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_GLTF_IMPORT_SETTINGS_TYPE_ID;
}
static sk_resource_asset_importer_t gltf_importer = {
	.user_data = NULL,
	.imported_extensions = gltf_importer_exts,
	.output_extension = gltf_importer_out,
	.cooker_version = gltf_importer_cooker,
	.get_settings_type = gltf_importer_settings,
	.ingest = dcc_root_ingest,
	.cook = dcc_root_cook,
	.import_asset = NULL,
};

static u32 obj_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const_chr_t exts[] = {".obj"};
	return fill_exts(exts, 1u, out, out_cap);
}
static const_chr_t obj_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".dcc_asset";
}
static u32 obj_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}
static sk_type_id_t obj_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_OBJ_IMPORT_SETTINGS_TYPE_ID;
}
static void obj_importer_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	ctx->declare_sub_resource(ctx, "root", SK_DCC_ASSET_TYPE_ID);
	/* Full mtl/texture dependency walk needs tinyobj; scan mtllib lines only. */
	if (ctx->source_bytes == NULL || ctx->source_size == 0u || ctx->source_path == NULL) {
		return;
	}
	const sk_filesystem_api_t* fs = builtins_fs_api();
	char parent[SK_FS_PATH_MAX];
	if (sk_path_parent(sk_str_view_cstr(ctx->source_path), parent, (u32)sizeof(parent)) < 0) {
		return;
	}
	const char* text = (const char*)ctx->source_bytes;
	const char* end = text + ctx->source_size;
	const char* line = text;
	while (line < end) {
		const char* nl = line;
		while (nl < end && *nl != '\n') {
			++nl;
		}
		size_t line_len = (size_t)(nl - line);
		if (line_len > 7u && strncmp(line, "mtllib ", 7) == 0) {
			const char* mtl = line + 7;
			while (mtl < nl && (*mtl == ' ' || *mtl == '\t')) {
				++mtl;
			}
			const char* mtl_end = nl;
			while (mtl_end > mtl && (mtl_end[-1] == '\r' || mtl_end[-1] == ' ' || mtl_end[-1] == '\t')) {
				--mtl_end;
			}
			if (mtl_end > mtl) {
				char rel[SK_FS_PATH_MAX];
				size_t rel_len = (size_t)(mtl_end - mtl);
				if (rel_len >= sizeof(rel)) {
					rel_len = sizeof(rel) - 1u;
				}
				memcpy(rel, mtl, rel_len);
				rel[rel_len] = '\0';
				if (!ctx->has_dependency(ctx, rel)) {
					char abs[SK_FS_PATH_MAX];
					if (sk_path_join(sk_str_view_cstr(parent), sk_str_view_cstr(rel), abs, (u32)sizeof(abs)) >= 0 && fs->get_file_status(abs) == SK_FILE_STATUS_FILE) {
						sk_file_handle_t f = fs->open_file(abs, SK_FILE_ACCESS_READ);
						if (f != NULL) {
							u64 sz = fs->get_file_size(f);
							if (sz > 0u && sz < 0x100000ull) {
								u8* bytes = (u8*)sk_allocator_default()->alloc(sk_allocator_default()->instance, sz);
								if (bytes != NULL) {
									fs->read_file(f, bytes, sz);
									ctx->add_dependency(ctx, rel, bytes, (u32)sz);
									sk_allocator_default()->free(sk_allocator_default()->instance, bytes);
								}
							}
							fs->close_file(f);
						}
					}
				}
			}
		}
		line = (nl < end) ? nl + 1 : end;
	}
}
static sk_resource_asset_importer_t obj_importer = {
	.user_data = NULL,
	.imported_extensions = obj_importer_exts,
	.output_extension = obj_importer_out,
	.cooker_version = obj_importer_cooker,
	.get_settings_type = obj_importer_settings,
	.ingest = obj_importer_ingest,
	.cook = dcc_root_cook,
	.import_asset = NULL,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                      */
/* ------------------------------------------------------------------ */

static sk_resource_asset_handler_t* const builtin_handlers[] = {
	&animation_clip_handler,
	&animation_controller_handler,
	&audio_handler,
	&csharp_script_handler,
	&dcc_asset_handler,
	&entity_handler,
	&font_handler,
	&material_graph_handler,
	&mesh_handler,
	&rml_document_handler,
	&rml_style_handler,
	&scene_handler,
	&raster_shader_handler,
	&compute_shader_handler,
	&raytrace_shader_handler,
	&config_shader_handler,
	&texture_handler,
	&imported_asset_handler,
};

static sk_resource_asset_importer_t* const builtin_importers[] = {
	&audio_importer, &font_importer, &texture_importer, &fbx_importer, &gltf_importer, &obj_importer,
};

void sk_resource_asset_builtins_register_impls(sk_app_context_t* context, const sk_app_api_t* app_api) {
	u32 handler_count = (u32)(sizeof(builtin_handlers) / sizeof(builtin_handlers[0]));
	u32 importer_count = (u32)(sizeof(builtin_importers) / sizeof(builtin_importers[0]));
	for (u32 i = 0u; i < handler_count; ++i) {
		app_api->add_impl(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, builtin_handlers[i]);
	}
	for (u32 i = 0u; i < importer_count; ++i) {
		app_api->add_impl(context, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID, builtin_importers[i]);
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "test.h"
#include "unity.h"

static void bi_path(const_chr_t a, const_chr_t b, char* out, u32 out_cap) {
	TEST_ASSERT_TRUE(sk_path_join(sk_str_view_cstr(a), sk_str_view_cstr(b), out, out_cap) >= 0);
}

static void bi_write(const_chr_t path, const_chr_t text) {
	const sk_filesystem_api_t* fs = builtins_fs_api();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_TRUE(fs->write_file(file, text, strlen(text)) == strlen(text));
	fs->close_file(file);
}

static sk_rid_t bi_find_asset(sk_repository_t* repository, const sk_repository_api_t* repo, sk_rid_t node, const_chr_t name, const_chr_t extension) {
	sk_resource_object_t view = repo->read(repository, node);
	u32 count = 0u;
	const sk_rid_t* children = repo->get_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	for (u32 i = 0u; i < count; ++i) {
		sk_resource_object_t child = repo->read(repository, children[i]);
		const_chr_t child_name = repo->get_string(child, SK_RESOURCE_ASSET_FIELD_NAME);
		const_chr_t child_ext = repo->get_string(child, SK_RESOURCE_ASSET_FIELD_EXTENSION);
		if (child_name != NULL && strcmp(child_name, name) == 0 && child_ext != NULL && strcmp(child_ext, extension) == 0) {
			return children[i];
		}
	}
	return SK_RID_ZERO;
}

static const sk_repository_api_t* bi_repo_api(void) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	return api;
}

static const sk_resource_assets_api_t* bi_assets_api(void) {
	sk_app_boot_t boot = sk_app_create();
	const sk_resource_assets_api_t* api = boot.api->resource_assets_api(boot.context);
	sk_app_shutdown(boot.context);
	return api;
}

static void bi_setup(sk_repository_t** out_repo, sk_app_context_t** out_app, sk_resource_assets_context_t** out_ctx) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* repo_api = boot.api->repository_api(boot.context);
	sk_repository_t* repository = repo_api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repository);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repository, repo_api));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repository, repo_api));

	sk_app_context_t* app = boot.context;
	TEST_ASSERT_NOT_NULL(app);
	sk_resource_asset_builtins_register_impls(app, boot.api);

	sk_resource_assets_context_t* ctx = boot.api->resource_assets_api(boot.context)->create(repository, app, boot.api, sk_allocator_default());
	TEST_ASSERT_NOT_NULL(ctx);

	*out_repo = repository;
	*out_app = app;
	*out_ctx = ctx;
}

static void bi_teardown(sk_repository_t* repository, sk_app_context_t* app, sk_resource_assets_context_t* ctx) {
	bi_assets_api()->destroy(ctx);
	sk_app_shutdown(app);
	bi_repo_api()->destroy(repository);
}

SK_TEST(resource_asset_builtins_register_handlers_and_importers) {
	sk_repository_t* repository = NULL;
	sk_app_context_t* app = NULL;
	sk_resource_assets_context_t* ctx = NULL;
	bi_setup(&repository, &app, &ctx);
	const sk_resource_assets_api_t* api = bi_assets_api();

	/* Handlers by extension (inventory §1.3). Output extensions are remapped
	 * to ImportedAssetHandler (main ReloadAssetHandlers). */
	TEST_ASSERT_EQUAL_PTR(&animation_clip_handler, api->get_asset_handler_for_extension(ctx, ".animation"));
	TEST_ASSERT_EQUAL_PTR(&animation_controller_handler, api->get_asset_handler_for_extension(ctx, ".animcontroller"));
	TEST_ASSERT_EQUAL_PTR(&csharp_script_handler, api->get_asset_handler_for_extension(ctx, ".cs"));
	TEST_ASSERT_EQUAL_PTR(&entity_handler, api->get_asset_handler_for_extension(ctx, ".entity"));
	TEST_ASSERT_EQUAL_PTR(&material_graph_handler, api->get_asset_handler_for_extension(ctx, ".matgraph"));
	TEST_ASSERT_EQUAL_PTR(&mesh_handler, api->get_asset_handler_for_extension(ctx, ".mesh"));
	TEST_ASSERT_EQUAL_PTR(&rml_document_handler, api->get_asset_handler_for_extension(ctx, ".rml"));
	TEST_ASSERT_EQUAL_PTR(&rml_style_handler, api->get_asset_handler_for_extension(ctx, ".rcss"));
	TEST_ASSERT_EQUAL_PTR(&scene_handler, api->get_asset_handler_for_extension(ctx, ".scene"));
	TEST_ASSERT_EQUAL_PTR(&raster_shader_handler, api->get_asset_handler_for_extension(ctx, ".raster"));
	TEST_ASSERT_EQUAL_PTR(&compute_shader_handler, api->get_asset_handler_for_extension(ctx, ".comp"));
	TEST_ASSERT_EQUAL_PTR(&raytrace_shader_handler, api->get_asset_handler_for_extension(ctx, ".rt"));
	TEST_ASSERT_EQUAL_PTR(&config_shader_handler, api->get_asset_handler_for_extension(ctx, ".shader"));

	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".audio"));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".font"));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".texture"));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".dcc_asset"));

	/* Payload handlers remain discoverable by resource type. */
	TEST_ASSERT_EQUAL_PTR(&audio_handler, api->get_asset_handler_for_type(ctx, SK_AUDIO_RESOURCE_TYPE_ID));
	TEST_ASSERT_EQUAL_PTR(&font_handler, api->get_asset_handler_for_type(ctx, SK_FONT_RESOURCE_TYPE_ID));
	TEST_ASSERT_EQUAL_PTR(&texture_handler, api->get_asset_handler_for_type(ctx, SK_TEXTURE_RESOURCE_TYPE_ID));
	TEST_ASSERT_EQUAL_PTR(&dcc_asset_handler, api->get_asset_handler_for_type(ctx, SK_DCC_ASSET_TYPE_ID));
	TEST_ASSERT_EQUAL_PTR(&mesh_handler, api->get_asset_handler_for_type(ctx, SK_MESH_RESOURCE_TYPE_ID));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_type(ctx, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID));

	TEST_ASSERT_EQUAL_PTR(&audio_importer, api->get_importer(ctx, ".wav"));
	TEST_ASSERT_EQUAL_PTR(&audio_importer, api->get_importer(ctx, ".mp3"));
	TEST_ASSERT_EQUAL_PTR(&audio_importer, api->get_importer(ctx, ".ogg"));
	TEST_ASSERT_EQUAL_PTR(&audio_importer, api->get_importer(ctx, ".flac"));
	TEST_ASSERT_EQUAL_PTR(&font_importer, api->get_importer(ctx, ".ttf"));
	TEST_ASSERT_EQUAL_PTR(&font_importer, api->get_importer(ctx, ".otf"));
	TEST_ASSERT_EQUAL_PTR(&texture_importer, api->get_importer(ctx, ".png"));
	TEST_ASSERT_EQUAL_PTR(&texture_importer, api->get_importer(ctx, ".jpg"));
	TEST_ASSERT_EQUAL_PTR(&texture_importer, api->get_importer(ctx, ".jpeg"));
	TEST_ASSERT_EQUAL_PTR(&texture_importer, api->get_importer(ctx, ".tga"));
	TEST_ASSERT_EQUAL_PTR(&texture_importer, api->get_importer(ctx, ".bmp"));
	TEST_ASSERT_EQUAL_PTR(&texture_importer, api->get_importer(ctx, ".hdr"));
	TEST_ASSERT_EQUAL_PTR(&fbx_importer, api->get_importer(ctx, ".fbx"));
	TEST_ASSERT_EQUAL_PTR(&gltf_importer, api->get_importer(ctx, ".gltf"));
	TEST_ASSERT_EQUAL_PTR(&gltf_importer, api->get_importer(ctx, ".glb"));
	TEST_ASSERT_EQUAL_PTR(&obj_importer, api->get_importer(ctx, ".obj"));

	TEST_ASSERT_EQUAL_STRING("Animation Clip", sk_resource_asset_handler_get_desc(&animation_clip_handler));
	TEST_ASSERT_EQUAL_STRING("DCC Asset", sk_resource_asset_handler_get_desc(&dcc_asset_handler));
	TEST_ASSERT_EQUAL_STRING(".dcc_asset", sk_resource_asset_handler_extension(&dcc_asset_handler));
	TEST_ASSERT_EQUAL_STRING(".audio", audio_importer.output_extension(NULL));
	TEST_ASSERT_EQUAL_STRING(".texture", texture_importer.output_extension(NULL));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(texture_importer.get_settings_type(NULL), SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID));

	bi_teardown(repository, app, ctx);
}

SK_TEST(resource_asset_builtins_create_via_handlers) {
	sk_repository_t* repository = NULL;
	sk_app_context_t* app = NULL;
	sk_resource_assets_context_t* ctx = NULL;
	bi_setup(&repository, &app, &ctx);
	const sk_resource_assets_api_t* api = bi_assets_api();
	const sk_repository_api_t* repo = bi_repo_api();

	char root[SK_FS_PATH_MAX];
	char assets[SK_FS_PATH_MAX];
	const sk_filesystem_api_t* fs = builtins_fs_api();
	char temp[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	bi_path(temp, "skore_bi_create", root, (u32)sizeof(root));
	bi_path(root, "Assets", assets, (u32)sizeof(assets));
	(void)fs->create_directory(root);
	(void)fs->create_directory(assets);

	sk_rid_t package = api->scan_package_from_directory(ctx, "Game", root);
	TEST_ASSERT_TRUE(package.id != 0u);
	sk_rid_t root_node = api->get_root_directory(ctx);
	TEST_ASSERT_TRUE(root_node.id != 0u);

	sk_rid_t mesh_asset = api->create_asset(ctx, root_node, SK_MESH_RESOURCE_TYPE_ID, "Cube", NULL);
	TEST_ASSERT_TRUE(mesh_asset.id != 0u);
	{
		sk_resource_object_t view = repo->read(repository, mesh_asset);
		TEST_ASSERT_EQUAL_STRING(".mesh", repo->get_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION));
		sk_rid_t payload = repo->get_subobject(view, SK_RESOURCE_ASSET_FIELD_OBJECT);
		TEST_ASSERT_TRUE(payload.id != 0u);
		sk_resource_object_t payload_view = repo->read(repository, payload);
		TEST_ASSERT_EQUAL_STRING("Mesh", repo->get_string(payload_view, SK_NAMED_RESOURCE_FIELD_NAME));
	}

	sk_rid_t entity_asset = api->create_asset(ctx, root_node, SK_ENTITY_RESOURCE_TYPE_ID, NULL, NULL);
	TEST_ASSERT_TRUE(entity_asset.id != 0u);

	sk_rid_t rml_asset = api->create_asset(ctx, root_node, SK_UI_DOCUMENT_RESOURCE_TYPE_ID, "Hud", NULL);
	TEST_ASSERT_TRUE(rml_asset.id != 0u);
	{
		sk_resource_object_t view = repo->read(repository, rml_asset);
		sk_rid_t payload = repo->get_subobject(view, SK_RESOURCE_ASSET_FIELD_OBJECT);
		sk_resource_object_t payload_view = repo->read(repository, payload);
		const_chr_t content = repo->get_string(payload_view, SK_NAMED_RESOURCE_FIELD_CONTENT);
		TEST_ASSERT_NOT_NULL(content);
		TEST_ASSERT_TRUE(strstr(content, "<rml>") != NULL);
	}

	api->destroy(ctx);
	sk_app_shutdown(app);
	bi_repo_api()->destroy(repository);
	(void)fs->remove(assets);
	(void)fs->remove(root);
}

static void bi_assert_import(sk_repository_t* repository, sk_resource_assets_context_t* ctx, sk_rid_t root_node, const_chr_t sample_path, const_chr_t asset_name,
							 const_chr_t wrapper_ext, const_chr_t sub_id, sk_type_id_t sub_type) {
	const sk_resource_assets_api_t* api = bi_assets_api();
	const sk_repository_api_t* repo = bi_repo_api();

	TEST_ASSERT_EQUAL_INT(0, api->import_asset(ctx, root_node, sample_path, NULL));
	sk_rid_t asset = bi_find_asset(repository, repo, root_node, asset_name, wrapper_ext);
	TEST_ASSERT_TRUE(asset.id != 0u);

	sk_resource_object_t asset_view = repo->read(repository, asset);
	TEST_ASSERT_EQUAL_STRING(wrapper_ext, repo->get_string(asset_view, SK_RESOURCE_ASSET_FIELD_EXTENSION));
	sk_rid_t wrapper = repo->get_subobject(asset_view, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET);
	TEST_ASSERT_TRUE(wrapper.id != 0u);

	sk_resource_object_t wrapper_view = repo->read(repository, wrapper);
	TEST_ASSERT_EQUAL_STRING(asset_name, repo->get_string(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME));
	TEST_ASSERT_EQUAL_STRING(wrapper_ext, repo->get_string(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION));
	TEST_ASSERT_EQUAL_UINT64(1u, repo->get_uint(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION));
	TEST_ASSERT_TRUE(repo->get_uint(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE) > 0u);

	u32 sub_count = 0u;
	const sk_rid_t* subs = repo->get_subobject_list(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, &sub_count);
	TEST_ASSERT_TRUE(sub_count >= 1u);
	i32 found_sub = 0;
	for (u32 i = 0u; i < sub_count; ++i) {
		sk_resource_object_t entry = repo->read(repository, subs[i]);
		const_chr_t id = repo->get_string(entry, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID);
		if (id != NULL && strcmp(id, sub_id) == 0) {
			found_sub = 1;
			break;
		}
	}
	TEST_ASSERT_EQUAL_INT(1, found_sub);
	(void)sub_type;
}

SK_TEST(resource_asset_builtins_import_samples_per_importer) {
	sk_repository_t* repository = NULL;
	sk_app_context_t* app = NULL;
	sk_resource_assets_context_t* ctx = NULL;
	bi_setup(&repository, &app, &ctx);
	const sk_resource_assets_api_t* api = bi_assets_api();
	const sk_filesystem_api_t* fs = builtins_fs_api();

	char temp[SK_FS_PATH_MAX];
	char root[SK_FS_PATH_MAX];
	char assets[SK_FS_PATH_MAX];
	char samples[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	bi_path(temp, "skore_bi_import", root, (u32)sizeof(root));
	bi_path(root, "Assets", assets, (u32)sizeof(assets));
	bi_path(temp, "skore_bi_samples", samples, (u32)sizeof(samples));
	(void)fs->create_directory(root);
	(void)fs->create_directory(assets);
	(void)fs->create_directory(samples);

	char path[SK_FS_PATH_MAX];
	bi_path(samples, "tone.wav", path, (u32)sizeof(path));
	bi_write(path, "RIFF....WAVEfmt "); /* minimal bytes for import pipeline */
	bi_path(samples, "face.ttf", path, (u32)sizeof(path));
	bi_write(path, "otfont-bytes");
	bi_path(samples, "wood.png", path, (u32)sizeof(path));
	bi_write(path, "png-bytes");
	bi_path(samples, "hero.fbx", path, (u32)sizeof(path));
	bi_write(path, "fbx-bytes");
	bi_path(samples, "prop.gltf", path, (u32)sizeof(path));
	bi_write(path, "gltf-bytes");
	bi_path(samples, "crate.obj", path, (u32)sizeof(path));
	bi_write(path, "mtllib crate.mtl\nv 0 0 0\n");
	bi_path(samples, "crate.mtl", path, (u32)sizeof(path));
	bi_write(path, "newmtl default\n");

	sk_rid_t package = api->scan_package_from_directory(ctx, "Game", root);
	TEST_ASSERT_TRUE(package.id != 0u);
	sk_rid_t root_node = api->get_root_directory(ctx);
	TEST_ASSERT_TRUE(root_node.id != 0u);

	bi_path(samples, "tone.wav", path, (u32)sizeof(path));
	bi_assert_import(repository, ctx, root_node, path, "tone", ".audio", "main", SK_AUDIO_RESOURCE_TYPE_ID);

	bi_path(samples, "face.ttf", path, (u32)sizeof(path));
	bi_assert_import(repository, ctx, root_node, path, "face", ".font", "main", SK_FONT_RESOURCE_TYPE_ID);

	bi_path(samples, "wood.png", path, (u32)sizeof(path));
	bi_assert_import(repository, ctx, root_node, path, "wood", ".texture", "main", SK_TEXTURE_RESOURCE_TYPE_ID);

	bi_path(samples, "hero.fbx", path, (u32)sizeof(path));
	bi_assert_import(repository, ctx, root_node, path, "hero", ".dcc_asset", "root", SK_DCC_ASSET_TYPE_ID);

	bi_path(samples, "prop.gltf", path, (u32)sizeof(path));
	bi_assert_import(repository, ctx, root_node, path, "prop", ".dcc_asset", "root", SK_DCC_ASSET_TYPE_ID);

	bi_path(samples, "crate.obj", path, (u32)sizeof(path));
	bi_assert_import(repository, ctx, root_node, path, "crate", ".dcc_asset", "root", SK_DCC_ASSET_TYPE_ID);
	/* Obj mtllib dependency recorded. */
	{
		const sk_repository_api_t* repo = bi_repo_api();
		sk_rid_t asset = bi_find_asset(repository, repo, root_node, "crate", ".dcc_asset");
		sk_resource_object_t av = repo->read(repository, asset);
		sk_rid_t wrapper = repo->get_subobject(av, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET);
		sk_resource_object_t wv = repo->read(repository, wrapper);
		u32 dep_count = 0u;
		const sk_rid_t* deps = repo->get_subobject_list(wv, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, &dep_count);
		TEST_ASSERT_TRUE(dep_count >= 1u);
		i32 found_mtl = 0;
		for (u32 i = 0u; i < dep_count; ++i) {
			sk_resource_object_t dep = repo->read(repository, deps[i]);
			const_chr_t rel = repo->get_string(dep, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH);
			if (rel != NULL && strcmp(rel, "crate.mtl") == 0) {
				found_mtl = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(1, found_mtl);
	}

	/* Output-extension handler is ImportedAssetHandler (main ReloadAssetHandlers). */
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".audio"));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".font"));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".texture"));
	TEST_ASSERT_EQUAL_PTR(&imported_asset_handler, api->get_asset_handler_for_extension(ctx, ".dcc_asset"));

	/* Cleanup samples. */
	const_chr_t files[] = {"tone.wav", "face.ttf", "wood.png", "hero.fbx", "prop.gltf", "crate.obj", "crate.mtl"};
	for (u32 i = 0u; i < sizeof(files) / sizeof(files[0]); ++i) {
		bi_path(samples, files[i], path, (u32)sizeof(path));
		(void)fs->remove(path);
	}
	(void)fs->remove(samples);
	(void)fs->remove(assets);
	(void)fs->remove(root);

	bi_teardown(repository, app, ctx);
}

SK_TEST(resource_asset_builtins_entity_resource_nested_roundtrip) {
	const sk_repository_api_t* api = bi_repo_api();
	const sk_allocator_t* a = sk_allocator_default();

	/* EntityResource carries Name(0) + two owned SubObjectLists: Components(1)
	 * (component resources; their registered repository type id IS the ECS
	 * component type id per the mapping contract) and Children(2) (recursive
	 * child entity_resource payloads). */
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repo, api));

	const sk_resource_type_t* entity_type = api->find_type(repo, SK_ENTITY_RESOURCE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(entity_type);
	TEST_ASSERT_EQUAL_STRING("EntityResource", api->type_name(entity_type));
	TEST_ASSERT_EQUAL_UINT32(3u, api->type_field_count(entity_type));
	const sk_resource_field_t* f_components = api->type_field_at(entity_type, 1u);
	const sk_resource_field_t* f_children = api->type_field_at(entity_type, 2u);
	TEST_ASSERT_NOT_NULL(f_components);
	TEST_ASSERT_NOT_NULL(f_children);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST, f_components->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST, f_children->type);

	/* Parent entity: two components of distinct registered types + two child
	 * entity_resources (recursive nesting). Package serialization encodes
	 * cross-resource edges as UUID strings, so every node carries a UUID. */
	sk_rid_t parent = api->create_resource(repo, entity_type, (sk_uuid_t){0x5101u, 0x5102u}, NULL);
	sk_rid_t comp_mesh = api->create_resource(repo, api->find_type(repo, SK_MESH_RESOURCE_TYPE_ID), (sk_uuid_t){0x5103u, 0x5104u}, NULL);
	sk_rid_t comp_mat = api->create_resource(repo, api->find_type(repo, SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID), (sk_uuid_t){0x5105u, 0x5106u}, NULL);
	sk_rid_t child_a = api->create_resource(repo, entity_type, (sk_uuid_t){0x5107u, 0x5108u}, NULL);
	sk_rid_t child_b = api->create_resource(repo, entity_type, (sk_uuid_t){0x5109u, 0x510au}, NULL);
	TEST_ASSERT_TRUE(parent.id != 0u && comp_mesh.id != 0u && comp_mat.id != 0u && child_a.id != 0u && child_b.id != 0u);

	{
		sk_resource_object_t w = api->write(repo, child_a);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "ChildA"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, child_b);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "ChildB"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, parent);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "Parent"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, &comp_mesh, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, comp_mat));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &child_a, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_CHILDREN, child_b));
		api->commit(w, NULL);
	}

	/* Each component sub-object resolves to its registered repository type id. */
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, comp_mesh), SK_MESH_RESOURCE_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, comp_mat), SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, SK_RID_ZERO), SK_TYPE_ID_ZERO));

	/* Round-trip the nested graph through the repository (package JSON). */
	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_package_json_alloc(repo, api, parent, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_NOT_NULL(strstr(json, "sk.resource_package"));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Components\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Children\""));
	api->destroy(repo);

	repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repo, api));

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_package_json_string(repo, api, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	a->free(a->instance, json);

	sk_resource_object_t pr = api->read(repo, loaded);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(pr));
	TEST_ASSERT_EQUAL_STRING("Parent", api->get_string(pr, SK_ENTITY_RESOURCE_FIELD_NAME));
	u32 comp_count = 0u;
	const sk_rid_t* comps = api->get_subobject_list(pr, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, &comp_count);
	TEST_ASSERT_EQUAL_UINT32(2u, comp_count);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, comps[0]), SK_MESH_RESOURCE_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, comps[1]), SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID));

	u32 child_count = 0u;
	const sk_rid_t* children = api->get_subobject_list(pr, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &child_count);
	TEST_ASSERT_EQUAL_UINT32(2u, child_count);
	/* Loaded children are EntityResource payloads themselves (recursion). */
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, children[0])), SK_ENTITY_RESOURCE_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, children[1])), SK_ENTITY_RESOURCE_TYPE_ID));
	{
		sk_resource_object_t ca = api->read(repo, children[0]);
		sk_resource_object_t cb = api->read(repo, children[1]);
		TEST_ASSERT_EQUAL_STRING("ChildA", api->get_string(ca, SK_ENTITY_RESOURCE_FIELD_NAME));
		TEST_ASSERT_EQUAL_STRING("ChildB", api->get_string(cb, SK_ENTITY_RESOURCE_FIELD_NAME));
	}

	api->destroy(repo);
}

SK_TEST(resource_asset_builtins_scene_resource_roots_roundtrip) {
	const sk_repository_api_t* api = bi_repo_api();
	const sk_allocator_t* a = sk_allocator_default();

	/* SceneResource carries Name(0) + one owned SubObjectList: Roots(1) of
	 * root entity_resource payloads (mapping contract §4.1). Each root is the
	 * same EntityResource type as APX-296, so it already carries Components +
	 * Children. */
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repo, api));

	const sk_resource_type_t* scene_type = api->find_type(repo, SK_SCENE_RESOURCE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(scene_type);
	TEST_ASSERT_EQUAL_STRING("SceneResource", api->type_name(scene_type));
	TEST_ASSERT_EQUAL_UINT32(2u, api->type_field_count(scene_type));
	const sk_resource_field_t* f_roots = api->type_field_at(scene_type, 1u);
	TEST_ASSERT_NOT_NULL(f_roots);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST, f_roots->type);
	TEST_ASSERT_EQUAL_STRING("Roots", f_roots->name);

	/* Scene with two root entities; root A has a child entity (recursive
	 * EntityResource nesting), root B is a leaf. Every node gets a distinct
	 * UUID so package serialization can encode the cross-resource edges. */
	const sk_resource_type_t* entity_type = api->find_type(repo, SK_ENTITY_RESOURCE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(entity_type);
	sk_rid_t scene = api->create_resource(repo, scene_type, (sk_uuid_t){0x5201u, 0x5202u}, NULL);
	sk_rid_t root_a = api->create_resource(repo, entity_type, (sk_uuid_t){0x5203u, 0x5204u}, NULL);
	sk_rid_t root_b = api->create_resource(repo, entity_type, (sk_uuid_t){0x5205u, 0x5206u}, NULL);
	sk_rid_t child_a1 = api->create_resource(repo, entity_type, (sk_uuid_t){0x5207u, 0x5208u}, NULL);
	sk_rid_t child_a2 = api->create_resource(repo, entity_type, (sk_uuid_t){0x5209u, 0x520au}, NULL);
	TEST_ASSERT_TRUE(scene.id != 0u && root_a.id != 0u && root_b.id != 0u && child_a1.id != 0u && child_a2.id != 0u);

	{
		sk_resource_object_t w = api->write(repo, child_a1);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "ChildA1"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, child_a2);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "ChildA2"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, root_a);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "RootA"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &child_a1, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_CHILDREN, child_a2));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, root_b);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_ENTITY_RESOURCE_FIELD_NAME, "RootB"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, scene);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SCENE_RESOURCE_FIELD_NAME, "Level"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_SCENE_RESOURCE_FIELD_ROOTS, &root_a, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(w, SK_SCENE_RESOURCE_FIELD_ROOTS, root_b));
		api->commit(w, NULL);
	}

	/* Round-trip the scene graph through the repository (package JSON). */
	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_package_json_alloc(repo, api, scene, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_NOT_NULL(strstr(json, "sk.resource_package"));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Roots\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Children\""));
	api->destroy(repo);

	repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repo, api));

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_package_json_string(repo, api, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	a->free(a->instance, json);

	sk_resource_object_t sr = api->read(repo, loaded);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(sr));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, loaded)), SK_SCENE_RESOURCE_TYPE_ID));
	TEST_ASSERT_EQUAL_STRING("Level", api->get_string(sr, SK_SCENE_RESOURCE_FIELD_NAME));

	u32 root_count = 0u;
	const sk_rid_t* roots = api->get_subobject_list(sr, SK_SCENE_RESOURCE_FIELD_ROOTS, &root_count);
	TEST_ASSERT_EQUAL_UINT32(2u, root_count);
	/* Every root is an EntityResource payload (same type as APX-296). */
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, roots[0])), SK_ENTITY_RESOURCE_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, roots[1])), SK_ENTITY_RESOURCE_TYPE_ID));
	{
		sk_resource_object_t ra = api->read(repo, roots[0]);
		sk_resource_object_t rb = api->read(repo, roots[1]);
		TEST_ASSERT_EQUAL_STRING("RootA", api->get_string(ra, SK_ENTITY_RESOURCE_FIELD_NAME));
		TEST_ASSERT_EQUAL_STRING("RootB", api->get_string(rb, SK_ENTITY_RESOURCE_FIELD_NAME));

		/* Root A keeps its child tree after the round-trip. */
		u32 child_count = 0u;
		const sk_rid_t* children = api->get_subobject_list(ra, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &child_count);
		TEST_ASSERT_EQUAL_UINT32(2u, child_count);
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, children[0])), SK_ENTITY_RESOURCE_TYPE_ID));
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(api->resource_type(repo, children[1])), SK_ENTITY_RESOURCE_TYPE_ID));
		{
			sk_resource_object_t ca = api->read(repo, children[0]);
			sk_resource_object_t cb = api->read(repo, children[1]);
			TEST_ASSERT_EQUAL_STRING("ChildA1", api->get_string(ca, SK_ENTITY_RESOURCE_FIELD_NAME));
			TEST_ASSERT_EQUAL_STRING("ChildA2", api->get_string(cb, SK_ENTITY_RESOURCE_FIELD_NAME));
		}
		/* Leaf root B has no children. */
		u32 b_child_count = 0u;
		(void)api->get_subobject_list(rb, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &b_child_count);
		TEST_ASSERT_EQUAL_UINT32(0u, b_child_count);
	}

	api->destroy(repo);
}

#endif /* SK_TESTS */
