/*
 * main.c — integration test host (APX-61).
 *
 * Runs only the integration-test registry (vulkan_device.c) in-process.
 * These tests bootstrap a real app context via sk_app_init, which auto-loads
 * the plugin DLLs from {app_folder}/plugins, then drive the registered
 * sk_render_device_api_t through the device/buffer lifecycle.
 */

#include "test.h"

int main(void) {
	return sk_test_run_all_status(NULL);
}
