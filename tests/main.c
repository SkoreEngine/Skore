/**
 * @file main.c
 * @brief Test host bootstrap only.
 *
 * 1. Parse --list / --filter / optional plugins_dir.
 * 2. Install the fatal-fault handler (stacktrace on signal / SEH).
 * 3. Run foundation in-process registry (linked sk-foundation-tests).
 * 4. Scan {exe_dir}/plugins (or plugins_dir override), load each shared library,
 *    call sk_plugin_run_tests (plugin-local Unity).
 */

#include "app.h"
#include "crash.h"
#include "filesystem.h"
#include "path.h"
#include "platform.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

typedef int (*sk_plugin_entry_point_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

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

/* Cap on concurrently held plugin handles for one test pass. Production keeps
 * every plugin loaded for the process lifetime; the host mirrors that so API
 * tables registered via set_api stay valid for later plugins (e.g. entities
 * resolving sk-profiler under SK_ENABLE_PROFILER=ON). Closing mid-scan left
 * dangling table pointers and segfaulted instrumented call sites. */
enum { SK_TEST_HOST_MAX_OPEN_PLUGINS = 64 };

static i32 run_plugin_tests_in_dir(const_chr_t plugins_dir, sk_test_report_t* total) {
	char name[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	i32 any_fail = 0;
	sk_shared_lib_t open_libs[SK_TEST_HOST_MAX_OPEN_PLUGINS];
	u32 open_count = 0u;
	const sk_platform_api_t* plat;
	const sk_filesystem_api_t* fs;
	sk_directory_iterator_t it;

	/* Bootstrapped context (platform + logger) handed to each plugin entry
	 * point, mirroring production loading (sk_app_load_plugin). Plugin-local
	 * tests then see the host-registered APIs they need (e.g. the platform API
	 * used by sk-dxc-compiler to load its DXC runtime). */
	sk_app_boot_t boot = sk_app_startup();
	sk_app_context_t* context = boot.context;
	if (context == NULL) {
		printf("app startup failed for plugin tests (skip plugin tests)\n");
		return 0;
	}

	plat = boot.api->platform_api(context);
	fs = boot.api->filesystem_api(context);

	it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		printf("plugins dir not openable: %s (skip plugin tests)\n", plugins_dir);
		sk_app_shutdown(context);
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

		void_ptr_t entry_raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
		if (entry_raw == NULL) {
			printf("missing sk_plugin_entry_point — skip\n");
			plat->lib_close(lib);
			continue;
		}
		/* Keep the module mapped for the rest of the pass (see open_libs). */
		if (open_count < (u32)SK_TEST_HOST_MAX_OPEN_PLUGINS) {
			open_libs[open_count++] = lib;
		} else {
			printf("warning: open-plugin cap (%d) exceeded; closing %s early (API tables may dangle)\n", SK_TEST_HOST_MAX_OPEN_PLUGINS, name);
		}

		sk_plugin_entry_point_fn entry = SK_PTR_TO_FN(sk_plugin_entry_point_fn, entry_raw);
		(void)entry(context, boot.api);

		void_ptr_t raw = plat->lib_symbol(lib, SK_PLUGIN_RUN_TESTS_NAME);
		if (raw == NULL) {
			printf("missing %s — skip\n", SK_PLUGIN_RUN_TESTS_NAME);
			/* Still held in open_libs until teardown so any set_api tables remain live. */
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
	}

	fs->close_directory(it);
	/* Destroy the context first so no code walks registered tables after unmap. */
	sk_app_shutdown(context);
	for (u32 i = 0u; i < open_count; i++) {
		plat->lib_close(open_libs[i]);
	}
	return any_fail;
}

static i32 resolve_plugins_dir(const_chr_t override_dir, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char base[SK_FS_PATH_MAX];

	if (override_dir != NULL && override_dir[0] != '\0') {
		size_t len = strlen(override_dir);
		if (len + 1u > out_cap) {
			return -1;
		}
		memcpy(out, override_dir, len + 1u);
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
	sk_test_cli_t cli = {0};
	sk_test_report_t total;
	char plugins_dir[SK_FS_PATH_MAX];
	i32 status = 0;

	if (sk_test_parse_cli(argc, argv, &cli) != 0) {
		printf("unknown option: %s\n", (cli.unknown_opt != NULL) ? cli.unknown_opt : "");
		sk_test_print_runner_help(argv[0], 1);
		return 2;
	}
	if (cli.help != 0) {
		sk_test_print_runner_help(argv[0], 1);
		return 0;
	}
	sk_test_apply_cli(&cli);

	memset(&total, 0, sizeof(total));

	/* Process-wide crash reporting for host + plugin tests (same path as sk_app_init).
	 * Refcounted so sk_app_destroy during individual tests does not strip it. */
	if (sk_crash_install() != 0) {
		printf("crash handler install failed (continuing without stacktraces on fault)\n");
	}

	/* Host registry (core + app objects compiled with SK_TESTS). */
	if (run_host_tests(&total) != 0) {
		status = 1;
	}

	if (resolve_plugins_dir(cli.plugins_dir, plugins_dir, (u32)sizeof(plugins_dir)) != 0) {
		printf("could not resolve plugins directory\n");
		status = 1;
	} else {
		printf("\nscanning plugins: %s\n", plugins_dir);
		if (run_plugin_tests_in_dir(plugins_dir, &total) != 0) {
			status = 1;
		}
	}

	if (cli.list_only == 0) {
		printf("\n======== TOTAL: ran=%d failed=%d ========\n", total.ran, total.failed);
	}
	if (total.failed != 0) {
		status = 1;
	}
	return status;
}
