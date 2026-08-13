/**
 * @file test.c
 * @brief Test registry + common.h unit tests (only active with SK_TESTS).
 *
 * Built as sk-test STATIC and linked only into sk-tests host and Debug plugins.
 * Never linked into Release plugins or production sk-foundation / sk-player.
 */

#include "test.h"

#ifdef SK_TESTS

#include "atomics.h"

/*
 * Unity (via test.h) may include <stdnoreturn.h>, which defines `noreturn` as
 * `_Noreturn`. Windows UCRT <stdlib.h> uses `__declspec(noreturn)`; under the
 * Windows clang-tidy driver that expands to `__declspec(_Noreturn)` and fails
 * with "__declspec attributes must be an identifier or string literal".
 * Same fix as core/offset_allocator.c / core/atomics.h.
 */
#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SK_TEST_MAX = 1024 };

typedef struct sk_test_entry_t {
	const_chr_t name;
	void (*fn)(void);
} sk_test_entry_t;

static sk_test_entry_t tests[SK_TEST_MAX];
static u32 test_count = 0u;

void setUp(void) {}
void tearDown(void) {}

static i32 env_flag_on(const char* name) {
	const char* value = getenv(name);
	return (value != NULL && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0')) ? 1 : 0;
}

i32 sk_test_set_env(const_chr_t key, const_chr_t value) {
#if defined(_WIN32)
	char buf[2048];
	int n = snprintf(buf, sizeof(buf), "%s=%s", key, (value != NULL) ? value : "");
	if (n < 0 || n >= (int)sizeof(buf)) {
		return -1;
	}
	return _putenv(buf);
#else
	if (value == NULL || value[0] == '\0') {
		return unsetenv(key);
	}
	return setenv(key, value, 1);
#endif
}

/**
 * Match a test name against one SK_TEST_FILTER token.
 * - exact name: "ui_widget_vision_tab"
 * - prefix wildcard (trailing '*'): "ui_widget_vision_*"
 * Exact tokens never use substring matching (avoids tab vs table collisions).
 */
static i32 sk_test_filter_token_matches(const_chr_t name, const char* token, size_t token_len) {
	size_t name_len;

	if (name == NULL || token == NULL || token_len == 0u) {
		return 0;
	}
	name_len = strlen(name);
	if (token[token_len - 1u] == '*') {
		size_t prefix_len = token_len - 1u;
		if (prefix_len == 0u) {
			return 1; /* bare "*" matches everything */
		}
		return (name_len >= prefix_len && strncmp(name, token, prefix_len) == 0) ? 1 : 0;
	}
	return (name_len == token_len && strncmp(name, token, token_len) == 0) ? 1 : 0;
}

i32 sk_test_name_matches_filter(const_chr_t name, const_chr_t filter) {
	const char* p;

	if (filter == NULL || filter[0] == '\0') {
		return 1;
	}
	p = filter;
	while (*p != '\0') {
		const char* start = p;
		size_t len;
		while (*p != '\0' && *p != ',') {
			p++;
		}
		len = (size_t)(p - start);
		if (sk_test_filter_token_matches(name, start, len) != 0) {
			return 1;
		}
		if (*p == ',') {
			p++;
		}
	}
	return 0;
}

void sk_test_register(const_chr_t name, void (*fn)(void)) {
	if (name == NULL || fn == NULL) {
		return;
	}
	for (u32 i = 0u; i < test_count; i++) {
		if (strcmp(tests[i].name, name) == 0) {
			fprintf(stderr, "sk_test_register: duplicate name '%s'\n", name);
			break;
		}
	}
	if (test_count >= (u32)SK_TEST_MAX) {
		fprintf(stderr, "sk_test_register: registry full (%d); test '%s' dropped\n", SK_TEST_MAX, name);
		return;
	}
	tests[test_count].name = name;
	tests[test_count].fn = fn;
	test_count += 1u;
}

i32 sk_test_parse_cli(int argc, char** argv, sk_test_cli_t* out) {
	out->filter = NULL;
	out->plugins_dir = NULL;
	out->unknown_opt = NULL;
	out->list_only = 0;
	out->help = 0;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if (arg == NULL || arg[0] == '\0') {
			continue;
		}
		if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
			out->help = 1;
			continue;
		}
		if (strcmp(arg, "--list") == 0) {
			out->list_only = 1;
			continue;
		}
		if (strncmp(arg, "--filter=", 9) == 0) {
			out->filter = arg + 9;
			continue;
		}
		if (strcmp(arg, "--filter") == 0) {
			if (i + 1 >= argc) {
				out->unknown_opt = arg;
				return -1;
			}
			out->filter = argv[++i];
			continue;
		}
		if (arg[0] == '-') {
			out->unknown_opt = arg;
			return -1;
		}
		out->plugins_dir = arg;
	}
	return 0;
}

void sk_test_print_runner_help(const_chr_t prog, i32 has_plugins_dir) {
	const_chr_t name = (prog != NULL && prog[0] != '\0') ? prog : "sk-tests";
	printf("usage: %s [--list] [--filter=<tokens>]", name);
	if (has_plugins_dir != 0) {
		printf(" [plugins_dir]");
	}
	printf("\n");
	printf("  --list              print registered test names and exit\n");
	printf("  --filter=<tokens>   comma-separated exact names and/or prefix* (also SK_TEST_FILTER)\n");
	if (has_plugins_dir != 0) {
		printf("  plugins_dir         override {app_folder}/plugins\n");
	}
	printf("\n");
	printf("Default ctest runs unit tests only. Integration binaries skip unless\n");
	printf("SK_RUN_INTEGRATION=1 (or you invoke the binary directly).\n");
	printf("  cmake -E env SK_RUN_INTEGRATION=1 ctest --test-dir build -L integration\n");
}

void sk_test_apply_cli(const sk_test_cli_t* cli) {
	if (cli->filter != NULL) {
		(void)sk_test_set_env("SK_TEST_FILTER", cli->filter);
	}
	if (cli->list_only != 0) {
		(void)sk_test_set_env("SK_TEST_LIST", "1");
	}
}

i32 sk_test_should_skip_integration(void) {
	if (env_flag_on("SK_CTEST_GATE") == 0) {
		return 0;
	}
	if (env_flag_on("SK_RUN_INTEGRATION") != 0) {
		return 0;
	}
	printf("SKIP: integration tests (set SK_RUN_INTEGRATION=1, or run this binary directly)\n");
	return 1;
}

i32 sk_test_ctest_map_skip(i32 process_rc) {
	if (process_rc == 2 && env_flag_on("SK_CTEST_GATE") != 0) {
		return SK_TEST_SKIP_CODE;
	}
	return process_rc;
}

void sk_test_run_all(sk_test_report_t* out) {
	const char* filter = getenv("SK_TEST_FILTER");
	i32 list_only = env_flag_on("SK_TEST_LIST");
	i32 ran = 0;
	i32 skipped = 0;

	if (list_only != 0) {
		for (u32 i = 0u; i < test_count; ++i) {
			if (sk_test_name_matches_filter(tests[i].name, filter) == 0) {
				continue;
			}
			printf("%s\n", tests[i].name);
		}
		if (out != NULL) {
			out->ran = 0;
			out->failed = 0;
		}
		return;
	}

	UNITY_BEGIN();
	for (u32 i = 0u; i < test_count; ++i) {
		if (sk_test_name_matches_filter(tests[i].name, filter) == 0) {
			skipped += 1;
			continue;
		}
		UnityDefaultTestRun(tests[i].fn, tests[i].name, (int)i);
		ran += 1;
	}
	i32 failed = (i32)UNITY_END();
	if (filter != NULL && filter[0] != '\0') {
		printf("SK_TEST_FILTER: ran=%d skipped=%d\n", ran, skipped);
	}

	if (out != NULL) {
		out->ran = ran;
		out->failed = failed;
	}
}

i32 sk_test_run_all_status(sk_test_report_t* out) {
	sk_test_report_t local;
	sk_test_report_t* report = (out != NULL) ? out : &local;

	memset(report, 0, sizeof(*report));
	sk_test_run_all(report);
	return (report->failed != 0) ? 1 : 0;
}

/* Host-only SK_TESTs (common.h, atomics, harness) live in test_cases.c so
 * plugins that statically link this registry do not re-run them. */

#endif /* SK_TESTS */
