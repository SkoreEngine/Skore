/*
 * ui_text_screenshot_main.c — standalone deterministic text screenshot
 * harness CLI (APX-268).
 *
 * Runs the fixed text-sample suite through the headless offscreen renderer
 * and writes PNG captures into {out-dir}/text-screenshot/msdf/ (all UI text
 * renders through the MSDF pipeline since APX-271).
 *
 * Usage:
 *   sk-text-screenshot [--out-dir <dir>] [--verify] [--help]
 *
 *   --out-dir  Output root (default: $SK_TEST_ARTIFACT_DIR, else
 *              ./text-screenshot). Captures land in
 *              {out-dir}/text-screenshot/msdf/.
 *   --verify   Re-capture every sample and byte-compare the two runs
 *              (raw readback + PNG artifact bytes). Fails non-zero on any
 *              byte difference.
 *
 * Exit codes: 0 = complete deterministic set written; 1 = hard failure;
 * 2 = skipped (no Vulkan loader/ICD available).
 *
 * Determinism check from the shell: run the same command twice and diff the
 * output trees — PNG bytes must be identical:
 *   sk-text-screenshot --verify
 *   cp -r text-screenshot text-screenshot.run1
 *   sk-text-screenshot --verify
 *   diff -r text-screenshot.run1 text-screenshot
 */

#include "ui_text_screenshot.h"

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"

#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifdef noreturn
#undef noreturn
#endif
#include <stdlib.h>
static int ts_main_setenv(const char* k, const char* v) {
	char buf[2048];
	if (snprintf(buf, sizeof(buf), "%s=%s", k, v != NULL ? v : "") < 0) {
		return -1;
	}
	return _putenv(buf);
}
#else
#include <stdlib.h>
static int ts_main_setenv(const char* k, const char* v) {
	return setenv(k, v, 1);
}
#endif

static void ts_main_usage(const_chr_t prog) {
	fprintf(stderr,
			"usage: %s [--out-dir <dir>] [--verify] [--help]\n"
			"  --out-dir  output root (default: $SK_TEST_ARTIFACT_DIR or ./text-screenshot)\n"
			"  --verify   re-capture every sample and byte-compare (determinism check)\n"
			"  --help     this message\n",
			prog);
}

int main(int argc, char** argv) {
	const_chr_t out_dir = NULL;
	char buf[SK_FS_PATH_MAX];
	i32 verify = 0;
	i32 rc_total = 0;
	i32 a;

	if (sk_test_should_skip_integration() != 0) {
		return SK_TEST_SKIP_CODE;
	}

	for (a = 1; a < argc; ++a) {
		if (strcmp(argv[a], "--help") == 0 || strcmp(argv[a], "-h") == 0) {
			ts_main_usage(argv[0]);
			return 0;
		} else if (strcmp(argv[a], "--out-dir") == 0 && a + 1 < argc) {
			out_dir = argv[++a];
		} else if (strcmp(argv[a], "--verify") == 0) {
			verify = 1;
		} else {
			fprintf(stderr, "error: unknown argument '%s'\n", argv[a]);
			ts_main_usage(argv[0]);
			return 1;
		}
	}

	/* Resolve the artifact root: --out-dir wins, else the env/compile-time
	 * root the ui plugin would use (keeps both in sync via SK_TEST_ARTIFACT_DIR). */
	if (out_dir == NULL || out_dir[0] == '\0') {
		const char* env = getenv("SK_TEST_ARTIFACT_DIR");
		if (env != NULL && env[0] != '\0') {
			out_dir = env;
		} else {
			out_dir = "text-screenshot";
		}
	}
	if (ts_main_setenv("SK_TEST_ARTIFACT_DIR", out_dir) != 0) {
		fprintf(stderr, "error: cannot set SK_TEST_ARTIFACT_DIR=%s\n", out_dir);
		return 1;
	}
	{
		const sk_filesystem_api_t* fs = sk_test_filesystem_table();
		if (fs == NULL) {
			fprintf(stderr, "error: filesystem API unavailable\n");
			return 1;
		}
		/* The ui plugin creates subdirectories on PNG write; pre-create the
		 * out root so the artifact-root resolution (and any file writes)
		 * start from a real directory. */
		if (fs->get_file_status(out_dir) != SK_FILE_STATUS_DIRECTORY) {
			(void)fs->create_directory(out_dir);
		}
	}

	/* Sanity print of the resolved output tree. */
	(void)snprintf(buf, sizeof(buf), "sk-text-screenshot: output root = %s (mode=%s verify=%d)", out_dir, SK_UI_TEXT_SCREENSHOT_MODE_DIR, verify);
	fprintf(stderr, "%s\n", buf);

	{
		sk_ui_text_screenshot_params_t p;
		i32 rc;
		memset(&p, 0, sizeof(p));
		p.verify = verify;
		rc = sk_ui_text_screenshot_run(&p);
		if (rc == SK_UI_TEXT_SCREENSHOT_RC_SKIPPED) {
			fprintf(stderr, "sk-text-screenshot: no Vulkan ICD; skipping msdf suite\n");
			return sk_test_ctest_map_skip(2);
		}
		if (rc != SK_UI_TEXT_SCREENSHOT_RC_OK) {
			fprintf(stderr, "sk-text-screenshot: msdf suite FAILED\n");
			rc_total = 1;
		}
	}

	if (rc_total == 0) {
		fprintf(stderr, "sk-text-screenshot: complete capture set written under %s/text-screenshot/msdf/\n", out_dir);
	}
	return rc_total;
}
