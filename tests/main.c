/**
 * @file main.c
 * @brief Test host bootstrap only.
 *
 * 1. Run core + app in-process registry (linked sk-core-tests / sk-app-tests).
 * 2. Scan {exe_dir}/plugins (or argv[1] override), load each shared library,
 *    call sk_plugin_run_tests (plugin-local Unity).
 */

#include "filesystem.h"
#include "path.h"
#include "platform.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

static i32 run_host_tests(sk_test_report_t* total) {
	sk_test_report_t report;

	printf("\n======== host (core + app) ========\n");
	memset(&report, 0, sizeof(report));
	sk_test_run_all(&report);
	printf("host: ran=%d failed=%d\n", report.ran, report.failed);

	total->ran += report.ran;
	total->failed += report.failed;
	return (report.failed != 0) ? 1 : 0;
}

static i32 run_plugin_tests_in_dir(const_chr_t plugins_dir, sk_test_report_t* total) {
	const sk_platform_api_t* plat = sk_platform_api();
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char name[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	i32 any_fail = 0;

	sk_directory_iterator_t it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		printf("plugins dir not openable: %s (skip plugin tests)\n", plugins_dir);
		return 0;
	}

	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		sk_test_report_t report;

		if (!sk_is_shared_library_filename(name)) {
			continue;
		}

		i32 n = sk_path_join(sk_str_view_cstr(plugins_dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path));
		if (n < 0) {
			continue;
		}

		printf("\n======== plugin: %s ========\n", name);
		sk_shared_lib_t lib = plat->lib_open(full_path);
		if (lib == NULL) {
			printf("lib_open failed: %s (%s)\n", full_path, plat->lib_error());
			total->failed += 1;
			any_fail = 1;
			continue;
		}

		void_ptr_t raw = plat->lib_symbol(lib, SK_PLUGIN_RUN_TESTS_NAME);
		if (raw == NULL) {
			printf("missing %s — skip\n", SK_PLUGIN_RUN_TESTS_NAME);
			plat->lib_close(lib);
			continue;
		}

		sk_plugin_run_tests_fn run_tests = SK_PTR_TO_FN(sk_plugin_run_tests_fn, raw);
		memset(&report, 0, sizeof(report));
		if (run_tests(&report) != 0) {
			any_fail = 1;
		}
		printf("plugin %s: ran=%d failed=%d\n", name, report.ran, report.failed);
		total->ran += report.ran;
		total->failed += report.failed;
		plat->lib_close(lib);
	}

	fs->close_directory(it);
	return any_fail;
}

static i32 resolve_plugins_dir(int argc, char* argv[], char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];

	if (argc >= 2 && argv[1] != NULL && argv[1][0] != '\0') {
		size_t len = strlen(argv[1]);
		if (len + 1u > out_cap) {
			return -1;
		}
		memcpy(out, argv[1], len + 1u);
		return 0;
	}

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}

	i32 n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), out, out_cap);
	return (n < 0) ? -1 : 0;
}

int main(int argc, char* argv[]) {
	sk_test_report_t total;
	char plugins_dir[SK_FS_PATH_MAX];
	i32 status = 0;

	memset(&total, 0, sizeof(total));

	/* Host registry (core + app objects compiled with SK_TESTS). */
	if (run_host_tests(&total) != 0) {
		status = 1;
	}

	if (resolve_plugins_dir(argc, argv, plugins_dir, (u32)sizeof(plugins_dir)) != 0) {
		printf("could not resolve plugins directory\n");
		status = 1;
	} else {
		printf("\nscanning plugins: %s\n", plugins_dir);
		if (run_plugin_tests_in_dir(plugins_dir, &total) != 0) {
			status = 1;
		}
	}

	printf("\n======== TOTAL: ran=%d failed=%d ========\n", total.ran, total.failed);
	if (total.failed != 0) {
		status = 1;
	}
	return status;
}
