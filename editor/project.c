/**
 * @file project.c
 * @brief Editor project open/import as a pure consumer of core repository-assets.
 *
 * Registration entry point: sk_resource_asset_builtins_register_impls (add_impl
 * of every static handler/importer table). No editor-local handler class tree,
 * no duplicated import pipeline, no thumbnail cache.
 */

#include "project.h"

#include "allocator.h"
#include "filesystem.h"
#include "path.h"
#include "resource_asset_builtins.h"
#include "resource_assets_types.h"

#include <string.h>

struct sk_editor_project_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	sk_repository_t* repository;
	sk_resource_assets_context_t* assets;
	sk_rid_t package;
	sk_rid_t root_directory;
	char package_name[256];
	char package_path[SK_FS_PATH_MAX];
};

sk_editor_project_t* sk_editor_project_open(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t package_name, const_chr_t package_path) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char assets_dir[SK_FS_PATH_MAX];
	if (sk_path_join(sk_str_view_cstr(package_path), sk_str_view_cstr("Assets"), assets_dir, (u32)sizeof(assets_dir)) < 0) {
		return NULL;
	}
	/* Expected failure: package root without Assets/ is not a project. */
	if (fs->get_file_status(assets_dir) != SK_FILE_STATUS_DIRECTORY) {
		return NULL;
	}

	const sk_repository_api_t* repo_api = app_api->repository_api(app_context);
	sk_repository_t* repository = repo_api->create(sk_allocator_default());
	if (repository == NULL) {
		return NULL;
	}

	if (sk_resource_assets_register_types(repository, repo_api) != 0 || sk_resource_asset_builtins_register_types(repository, repo_api) != 0) {
		repo_api->destroy(repository);
		return NULL;
	}

	/* New registration entry point: static tables via add_impl (core builtins).
	 * Skip when this app context already has handlers (re-open / multi-call). */
	if (app_api->impl_count(app_context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID) == 0u) {
		sk_resource_asset_builtins_register_impls(app_context, app_api);
	}

	const sk_resource_assets_api_t* assets_api = app_api->resource_assets_api(app_context);
	sk_resource_assets_context_t* assets = assets_api->create(repository, app_context, app_api, sk_allocator_default());
	if (assets == NULL) {
		repo_api->destroy(repository);
		return NULL;
	}

	sk_rid_t package = assets_api->scan_package_from_directory(assets, package_name, package_path);
	if (package.id == 0u) {
		assets_api->destroy(assets);
		repo_api->destroy(repository);
		return NULL;
	}

	sk_editor_project_t* project = (sk_editor_project_t*)sk_allocator_default()->alloc(sk_allocator_default()->instance, sizeof(sk_editor_project_t));
	if (project == NULL) {
		assets_api->destroy(assets);
		repo_api->destroy(repository);
		return NULL;
	}
	memset(project, 0, sizeof(*project));
	project->app_context = app_context;
	project->app_api = app_api;
	project->repository = repository;
	project->assets = assets;
	project->package = package;
	project->root_directory = assets_api->get_root_directory(assets);
	strncpy(project->package_name, package_name, sizeof(project->package_name) - 1u);
	project->package_name[sizeof(project->package_name) - 1u] = '\0';
	strncpy(project->package_path, package_path, sizeof(project->package_path) - 1u);
	project->package_path[sizeof(project->package_path) - 1u] = '\0';
	return project;
}

void sk_editor_project_close(sk_editor_project_t* project) {
	project->app_api->resource_assets_api(project->app_context)->destroy(project->assets);
	project->app_api->repository_api(project->app_context)->destroy(project->repository);
	sk_allocator_default()->free(sk_allocator_default()->instance, project);
}

sk_resource_assets_context_t* sk_editor_project_assets(const sk_editor_project_t* project) {
	return project->assets;
}

sk_repository_t* sk_editor_project_repository(const sk_editor_project_t* project) {
	return project->repository;
}

sk_rid_t sk_editor_project_root_directory(const sk_editor_project_t* project) {
	return project->root_directory;
}

i32 sk_editor_project_import(sk_editor_project_t* project, const_chr_t path) {
	return project->app_api->resource_assets_api(project->app_context)->import_asset(project->assets, project->root_directory, path, NULL);
}

i32 sk_editor_project_import_into(sk_editor_project_t* project, sk_rid_t parent, const_chr_t path) {
	return project->app_api->resource_assets_api(project->app_context)->import_asset(project->assets, parent, path, NULL);
}

void sk_editor_project_open_asset(sk_editor_project_t* project, sk_rid_t rid) {
	project->app_api->resource_assets_api(project->app_context)->open_asset(project->assets, rid);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "test.h"
#include "unity.h"

#include <stdio.h>

static void ed_path(const_chr_t a, const_chr_t b, char* out, u32 out_cap) {
	TEST_ASSERT_TRUE(sk_path_join(sk_str_view_cstr(a), sk_str_view_cstr(b), out, out_cap) >= 0);
}

static void ed_write(const_chr_t path, const_chr_t text) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_TRUE(fs->write_file(file, text, strlen(text)) == strlen(text));
	fs->close_file(file);
}

static sk_rid_t ed_find_asset(sk_repository_t* repository, const sk_repository_api_t* repo, sk_rid_t node, const_chr_t name, const_chr_t extension) {
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

SK_TEST(editor_project_open_scan_and_import_via_core) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char temp[SK_FS_PATH_MAX];
	char root[SK_FS_PATH_MAX];
	char assets[SK_FS_PATH_MAX];
	char samples[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];

	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	ed_path(temp, "skore_editor_project", root, (u32)sizeof(root));
	ed_path(root, "Assets", assets, (u32)sizeof(assets));
	ed_path(temp, "skore_editor_samples", samples, (u32)sizeof(samples));
	(void)fs->create_directory(root);
	(void)fs->create_directory(assets);
	(void)fs->create_directory(samples);

	/* Pre-seed an on-disk cooked-looking asset so scan exercises core. */
	ed_path(assets, "seed.mesh", path, (u32)sizeof(path));
	ed_write(path, "mesh-placeholder");

	ed_path(samples, "tone.wav", path, (u32)sizeof(path));
	ed_write(path, "RIFF....WAVEfmt ");
	ed_path(samples, "wood.png", path, (u32)sizeof(path));
	ed_write(path, "png-bytes");
	ed_path(samples, "hero.fbx", path, (u32)sizeof(path));
	ed_write(path, "fbx-bytes");

	sk_app_boot_t boot = sk_app_create();

	sk_app_context_t* app = boot.context;
	TEST_ASSERT_NOT_NULL(app);

	sk_editor_project_t* project = sk_editor_project_open(app, boot.api, "Game", root);
	TEST_ASSERT_NOT_NULL(project);
	TEST_ASSERT_NOT_NULL(sk_editor_project_assets(project));
	TEST_ASSERT_NOT_NULL(sk_editor_project_repository(project));
	TEST_ASSERT_TRUE(sk_editor_project_root_directory(project).id != 0u);

	/* Seeded mesh discovered by core scan (not by an editor-side registry). */
	sk_rid_t seed = ed_find_asset(sk_editor_project_repository(project), boot.api->repository_api(app), sk_editor_project_root_directory(project), "seed", ".mesh");
	TEST_ASSERT_TRUE(seed.id != 0u);

	ed_path(samples, "tone.wav", path, (u32)sizeof(path));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_project_import(project, path));
	ed_path(samples, "wood.png", path, (u32)sizeof(path));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_project_import(project, path));
	ed_path(samples, "hero.fbx", path, (u32)sizeof(path));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_project_import(project, path));

	sk_rid_t audio = ed_find_asset(sk_editor_project_repository(project), boot.api->repository_api(app), sk_editor_project_root_directory(project), "tone", ".audio");
	sk_rid_t texture = ed_find_asset(sk_editor_project_repository(project), boot.api->repository_api(app), sk_editor_project_root_directory(project), "wood", ".texture");
	sk_rid_t dcc = ed_find_asset(sk_editor_project_repository(project), boot.api->repository_api(app), sk_editor_project_root_directory(project), "hero", ".dcc_asset");
	TEST_ASSERT_TRUE(audio.id != 0u);
	TEST_ASSERT_TRUE(texture.id != 0u);
	TEST_ASSERT_TRUE(dcc.id != 0u);

	/* open_asset is routed to core (builtins are no-ops; must not crash). */
	sk_editor_project_open_asset(project, audio);

	/* Importers are resolved only through core engine maps. */
	const sk_resource_assets_api_t* assets_api = boot.api->resource_assets_api(app);
	TEST_ASSERT_NOT_NULL(assets_api->get_importer(sk_editor_project_assets(project), ".wav"));
	TEST_ASSERT_NOT_NULL(assets_api->get_importer(sk_editor_project_assets(project), ".png"));
	TEST_ASSERT_NOT_NULL(assets_api->get_importer(sk_editor_project_assets(project), ".fbx"));
	TEST_ASSERT_NOT_NULL(assets_api->get_asset_handler_for_extension(sk_editor_project_assets(project), ".mesh"));

	sk_editor_project_close(project);
	sk_app_shutdown(app);

	ed_path(samples, "tone.wav", path, (u32)sizeof(path));
	(void)fs->remove(path);
	ed_path(samples, "wood.png", path, (u32)sizeof(path));
	(void)fs->remove(path);
	ed_path(samples, "hero.fbx", path, (u32)sizeof(path));
	(void)fs->remove(path);
	(void)fs->remove(samples);
	ed_path(assets, "seed.mesh", path, (u32)sizeof(path));
	(void)fs->remove(path);
	(void)fs->remove(assets);
	(void)fs->remove(root);
}

SK_TEST(editor_project_rejects_missing_assets_dir) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char temp[SK_FS_PATH_MAX];
	char root[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	ed_path(temp, "skore_editor_missing_assets", root, (u32)sizeof(root));
	(void)fs->create_directory(root);

	sk_app_boot_t boot = sk_app_create();

	sk_app_context_t* app = boot.context;
	TEST_ASSERT_NOT_NULL(app);
	TEST_ASSERT_NULL(sk_editor_project_open(app, boot.api, "Game", root));
	sk_app_shutdown(app);
	(void)fs->remove(root);
}

#endif /* SK_TESTS */
