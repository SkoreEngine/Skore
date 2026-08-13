/*
 * main.c — integration test host (APX-61).
 *
 * Runs only the integration-test registry (vulkan_device.c) in-process.
 * These tests bootstrap a real app context via sk_app_init, which auto-loads
 * the plugin DLLs from {app_folder}/plugins, then drive the registered
 * sk_render_device_api_t through the device/buffer lifecycle.
 */

#include "test.h"

#include <stdio.h>

int main(int argc, char** argv) {
	sk_test_cli_t cli = {0};

	if (sk_test_parse_cli(argc, argv, &cli) != 0) {
		printf("unknown option: %s\n", (cli.unknown_opt != NULL) ? cli.unknown_opt : "");
		sk_test_print_runner_help(argv[0], 0);
		return 2;
	}
	if (cli.help != 0) {
		sk_test_print_runner_help(argv[0], 0);
		return 0;
	}
	sk_test_apply_cli(&cli);

	/* --list always runs. CTest-gated runs skip unless SK_RUN_INTEGRATION=1. */
	if (cli.list_only == 0 && sk_test_should_skip_integration() != 0) {
		return SK_TEST_SKIP_CODE;
	}
	return sk_test_run_all_status(NULL);
}
