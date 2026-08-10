/**
 * @file main.c
 * @brief sk-editor host entry: open a package via core repository-assets.
 *
 * Usage:
 *   sk-editor <package_path> [--name <package_name>] [--import <path>]...
 *
 * package_path must contain an Assets/ directory. All import/scan work is
 * performed by sk-core (sk_resource_assets_api_t); this binary only registers
 * built-in handlers through sk_resource_asset_builtins_register_impls and
 * drives open/import. Thumbnail generation is not available (dropped with the
 * main-branch editor asset implementation).
 */

#include "app.h"
#include "logger.h"
#include "project.h"
#include "repository.h"
#include "resource_assets_types.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const_chr_t argv0) {
	fprintf(stderr,
			"Usage: %s <package_path> [--name <package_name>] [--import <path>]...\n"
			"  package_path   Directory that contains Assets/\n"
			"  --name         Package name for path ids (default: Game)\n"
			"  --import       Import a source file or directory via core importers\n"
			"\n"
			"Asset handlers/importers come from core (add_impl registration).\n"
			"No editor-side ResourceAssetHandler hierarchy; no thumbnails.\n",
			argv0 != NULL ? argv0 : "sk-editor");
}

static i32 count_root_children(sk_editor_project_t* project) {
	sk_repository_t* repository = sk_editor_project_repository(project);
	sk_rid_t root = sk_editor_project_root_directory(project);
	if (repository == NULL || root.id == 0u) {
		return -1;
	}
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(repository, root);
	u32 count = 0u;
	(void)repo->get_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	return (i32)count;
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		print_usage(argc > 0 ? argv[0] : "sk-editor");
		return 1;
	}

	const_chr_t package_path = NULL;
	const_chr_t package_name = "Game";
	const_chr_t import_paths[64];
	u32 import_count = 0u;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--name") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "sk-editor: --name requires a value\n");
				return 1;
			}
			package_name = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--import") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "sk-editor: --import requires a path\n");
				return 1;
			}
			if (import_count >= (u32)(sizeof(import_paths) / sizeof(import_paths[0]))) {
				fprintf(stderr, "sk-editor: too many --import paths\n");
				return 1;
			}
			import_paths[import_count++] = argv[++i];
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "sk-editor: unknown option %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
		if (package_path != NULL) {
			fprintf(stderr, "sk-editor: unexpected argument %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
		package_path = argv[i];
	}

	if (package_path == NULL) {
		print_usage(argv[0]);
		return 1;
	}

	sk_app_context_t* app = sk_app_init(argc, argv);
	if (app == NULL) {
		fprintf(stderr, "sk-editor: sk_app_init failed\n");
		return 1;
	}

	const sk_app_api_t* app_api = sk_app_api();
	const sk_logger_api_t* logger_api = sk_logger_api();
	sk_logger_t* log = logger_api->create_logger("editor");

	sk_editor_project_t* project = sk_editor_project_open(app, app_api, package_name, package_path);
	if (project == NULL) {
		sk_log_error(logger_api, log, "failed to open project at %s (need Assets/ under package root)", package_path);
		logger_api->destroy_logger(log);
		sk_app_destroy(app);
		return 1;
	}

	sk_log_info(logger_api, log, "opened package '%s' at %s via core resource assets", package_name, package_path);
	sk_log_info(logger_api, log, "handlers/importers registered through sk_resource_asset_builtins_register_impls");

	for (u32 i = 0u; i < import_count; ++i) {
		i32 rc = sk_editor_project_import(project, import_paths[i]);
		if (rc != 0) {
			sk_log_error(logger_api, log, "import failed (%d): %s", rc, import_paths[i]);
			sk_editor_project_close(project);
			logger_api->destroy_logger(log);
			sk_app_destroy(app);
			return 1;
		}
		sk_log_info(logger_api, log, "imported via core: %s", import_paths[i]);
	}

	i32 child_count = count_root_children(project);
	sk_log_info(logger_api, log, "package root has %d child asset node(s); thumbnails not generated (dropped)", child_count);

	sk_editor_project_close(project);
	logger_api->destroy_logger(log);
	sk_app_destroy(app);
	return 0;
}
