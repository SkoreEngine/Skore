#include "logger.h"

#include "allocator.h"
#include "internal/app_context.h"
#include "internal/tables.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

enum { SK_LOGGER_NAME_MAX = 64, SK_LOGGER_MAX_SINKS = 16, SK_LOG_MESSAGE_MAX = 2048, SK_LOG_FILE_PATH_MAX = 1024, SK_LOG_FILE_ROTATED_PATH_MAX = SK_LOG_FILE_PATH_MAX + 16 };

struct sk_logger_t {
	char name[SK_LOGGER_NAME_MAX];
};

/* Stub: still wraps the process-wide sink list in this TU. */
struct sk_logger_context_t {
	const sk_allocator_t* allocator;
};

/* Process-global sink list (main-thread ownership). Per static-linked module
 * instance (exe vs each plugin DLL) unless sk_logger_bind_api redirects. */
static sk_log_sink_t sinks[SK_LOGGER_MAX_SINKS];
static u32 sink_count = 0;
static i32 module_ready = 0;

/* When non-NULL, sk_logger_api() returns this (host table bound into a plugin). */
static const sk_logger_api_t* bound_api = NULL;

/* ---- shared line format (stdout + file) ---- */

static void format_log_timestamp(char* timestamp, size_t cap) {
	struct tm tm_local;
	time_t now = time(NULL);

#if defined(_WIN32)
	localtime_s(&tm_local, &now);
#else
	localtime_r(&now, &tm_local);
#endif
	if (strftime(timestamp, cap, "%Y-%m-%d %H:%M:%S", &tm_local) == 0) {
		timestamp[0] = '\0';
	}
}

/* ---- default stdout sink ---- */

static void stdout_sink_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	char timestamp[32];

	(void)user_data;
	format_log_timestamp(timestamp, sizeof(timestamp));
	fprintf(stdout, "[%s] [%s] [%s] %s\n", timestamp, sk_logger_type_name(level), logger_name, message);
	fflush(stdout);
}

static const sk_log_sink_t stdout_sink = {
	NULL,
	stdout_sink_print,
};

static void ensure_module_ready(void) {
	if (module_ready) {
		return;
	}
	sinks[0] = stdout_sink;
	sink_count = 1;
	module_ready = 1;
}

/* ---- rotating file sink ---- */

struct sk_log_file_sink_t {
	sk_log_sink_t sink;
	FILE* file;
	char path[SK_LOG_FILE_PATH_MAX];
	u64 max_bytes;
	u32 max_files;
	u64 current_size;
};

static void file_sink_build_rotated_path(const char* base, u32 index, char* out, size_t out_cap) {
	/* path.N for archives (index >= 1); active file is index 0 / base only. */
	if (index == 0u) {
		snprintf(out, out_cap, "%s", base);
	} else {
		snprintf(out, out_cap, "%s.%u", base, index);
	}
	out[out_cap - 1u] = '\0';
}

static i32 file_sink_measure_existing_size(const char* path, u64* out_size) {
	FILE* measure;
	long pos;

	/* Measure on a read stream. Seeking a FILE opened with "a" is a no-op on
	 * POSIX (cppcheck seekOnAppendedFile) and leaves Windows at position 0
	 * until the first write. */
	measure = fopen(path, "rb");
	if (measure == NULL) {
		*out_size = 0ull;
		return 0;
	}
	if (fseek(measure, 0, SEEK_END) != 0) {
		fclose(measure);
		return -1;
	}
	pos = ftell(measure);
	fclose(measure);
	*out_size = (pos > 0) ? (u64)pos : 0ull;
	return 0;
}

static i32 file_sink_open_append(sk_log_file_sink_t* fs) {
	if (file_sink_measure_existing_size(fs->path, &fs->current_size) != 0) {
		return -1;
	}
	fs->file = fopen(fs->path, "a");
	if (fs->file == NULL) {
		return -1;
	}
	return 0;
}

static void file_sink_rotate(sk_log_file_sink_t* fs) {
	char from[SK_LOG_FILE_ROTATED_PATH_MAX];
	char to[SK_LOG_FILE_ROTATED_PATH_MAX];
	u32 i;

	if (fs->file != NULL) {
		fclose(fs->file);
		fs->file = NULL;
	}

	if (fs->max_files <= 1u) {
		/* Single-file mode: truncate by remove + reopen. */
		(void)remove(fs->path);
		(void)file_sink_open_append(fs);
		return;
	}

	/* Drop oldest archive, then shift path.(n-2) → path.(n-1) … path → path.1 */
	file_sink_build_rotated_path(fs->path, fs->max_files - 1u, to, sizeof(to));
	(void)remove(to);
	for (i = fs->max_files - 1u; i > 1u; --i) {
		file_sink_build_rotated_path(fs->path, i - 1u, from, sizeof(from));
		file_sink_build_rotated_path(fs->path, i, to, sizeof(to));
		(void)remove(to);
		(void)rename(from, to);
	}
	file_sink_build_rotated_path(fs->path, 1u, to, sizeof(to));
	(void)remove(to);
	(void)rename(fs->path, to);

	(void)file_sink_open_append(fs);
}

static void file_sink_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	sk_log_file_sink_t* fs = (sk_log_file_sink_t*)user_data;
	char timestamp[32];
	char line[SK_LOG_MESSAGE_MAX + 96];
	int n;
	size_t written;

	if (fs->file == NULL) {
		return;
	}

	format_log_timestamp(timestamp, sizeof(timestamp));
	n = snprintf(line, sizeof(line), "[%s] [%s] [%s] %s\n", timestamp, sk_logger_type_name(level), logger_name, message);
	if (n < 0) {
		return;
	}
	if ((size_t)n >= sizeof(line)) {
		n = (int)(sizeof(line) - 1u);
		line[sizeof(line) - 1u] = '\0';
	}

	if (fs->current_size > 0ull && fs->current_size + (u64)n > fs->max_bytes) {
		file_sink_rotate(fs);
		if (fs->file == NULL) {
			return;
		}
	}

	written = fwrite(line, 1u, (size_t)n, fs->file);
	fs->current_size += written;
	fflush(fs->file);
}

sk_log_file_sink_t* sk_log_file_sink_create(const_chr_t path, u64 max_bytes, u32 max_files) {
	const sk_allocator_t* alloc;
	sk_log_file_sink_t* fs;
	size_t len;

	if (path == NULL || path[0] == '\0') {
		return NULL;
	}
	len = strlen(path);
	if (len >= (size_t)SK_LOG_FILE_PATH_MAX) {
		return NULL;
	}

	alloc = sk_allocator_default();
	fs = (sk_log_file_sink_t*)alloc->alloc(alloc->instance, sizeof(sk_log_file_sink_t));
	if (fs == NULL) {
		return NULL;
	}
	memset(fs, 0, sizeof(*fs));
	memcpy(fs->path, path, len);
	fs->path[len] = '\0';
	fs->max_bytes = (max_bytes == 0ull) ? (u64)SK_LOG_FILE_SINK_DEFAULT_MAX_BYTES : max_bytes;
	fs->max_files = (max_files == 0u) ? (u32)SK_LOG_FILE_SINK_DEFAULT_MAX_FILES : max_files;
	if (fs->max_files < 1u) {
		fs->max_files = 1u;
	}

	fs->sink.user_data = fs;
	fs->sink.print = file_sink_print;

	if (file_sink_open_append(fs) != 0) {
		alloc->free(alloc->instance, fs);
		return NULL;
	}
	return fs;
}

void sk_log_file_sink_destroy(sk_log_file_sink_t* sink) {
	const sk_allocator_t* alloc;

	if (sink == NULL) {
		return;
	}
	if (sink->file != NULL) {
		fclose(sink->file);
		sink->file = NULL;
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, sink);
}

const sk_log_sink_t* sk_log_file_sink_sink(sk_log_file_sink_t* sink) {
	return &sink->sink;
}

/* ---- API impl ---- */

static sk_logger_t* create_logger_impl(const_chr_t name) {
	ensure_module_ready();

	const sk_allocator_t* alloc = sk_allocator_default();
	sk_logger_t* logger = (sk_logger_t*)alloc->alloc(alloc->instance, sizeof(sk_logger_t));
	if (logger == NULL) {
		return NULL;
	}

	memset(logger, 0, sizeof(*logger));
	if (name != NULL) {
		size_t len = strlen(name);
		if (len >= (size_t)SK_LOGGER_NAME_MAX) {
			len = (size_t)SK_LOGGER_NAME_MAX - 1u;
		}
		memcpy(logger->name, name, len);
		logger->name[len] = '\0';
	}

	return logger;
}

static void destroy_logger_impl(sk_logger_t* logger) {
	if (logger == NULL) {
		return;
	}
	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, logger);
}

static void message_impl(sk_logger_type_t type, sk_logger_t* logger, const_chr_t message) {
	ensure_module_ready();

	for (u32 i = 0; i < sink_count; ++i) {
		sinks[i].print(sinks[i].user_data, type, logger->name, message);
	}
}

static i32 add_sink_impl(const sk_log_sink_t* sink) {
	ensure_module_ready();

	if (sink_count >= (u32)SK_LOGGER_MAX_SINKS) {
		return -1;
	}

	sinks[sink_count] = *sink;
	sink_count += 1u;
	return 0;
}

static i32 remove_sink_impl(const sk_log_sink_t* sink) {
	ensure_module_ready();

	for (u32 i = 0; i < sink_count; ++i) {
		if (sinks[i].print == sink->print && sinks[i].user_data == sink->user_data) {
			for (u32 j = i + 1u; j < sink_count; ++j) {
				sinks[j - 1u] = sinks[j];
			}
			sink_count -= 1u;
			memset(&sinks[sink_count], 0, sizeof(sinks[0]));
			return 0;
		}
	}
	return -1;
}

static const sk_logger_api_t logger_api = {
	create_logger_impl, destroy_logger_impl, message_impl, add_sink_impl, remove_sink_impl,
};

/* ---- public free functions ---- */

void sk_logger_bind_api(const sk_logger_api_t* api) {
	bound_api = api;
}

const sk_logger_api_t* sk_logger_api(void) {
	if (bound_api != NULL) {
		return bound_api;
	}
	ensure_module_ready();
	return &logger_api;
}

void sk_logger_get_api(sk_logger_api_t* out) {
	*out = *sk_logger_api();
}

sk_logger_context_t* sk_logger_context_create(const sk_allocator_t* allocator) {
	sk_logger_context_t* ctx;

	if (allocator == NULL) {
		return NULL;
	}
	ctx = (sk_logger_context_t*)allocator->alloc(allocator->instance, sizeof(sk_logger_context_t));
	if (ctx == NULL) {
		return NULL;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->allocator = allocator;
	ensure_module_ready();
	return ctx;
}

void sk_logger_context_destroy(sk_logger_context_t* log_ctx) {
	const sk_allocator_t* alloc;

	if (log_ctx == NULL) {
		return;
	}
	alloc = log_ctx->allocator;
	alloc->free(alloc->instance, log_ctx);
}

void sk_logger_install(sk_app_context_t* ctx) {
	ctx->logger_api = &logger_api;
}

const sk_log_sink_t* sk_logger_stdout_sink(void) {
	return &stdout_sink;
}

const_chr_t sk_logger_type_name(sk_logger_type_t type) {
	switch (type) {
	case SK_LOGGER_TYPE_TRACE:
		return "TRACE";
	case SK_LOGGER_TYPE_DEBUG:
		return "DEBUG";
	case SK_LOGGER_TYPE_INFO:
		return "INFO";
	case SK_LOGGER_TYPE_WARN:
		return "WARN";
	case SK_LOGGER_TYPE_ERROR:
		return "ERROR";
	case SK_LOGGER_TYPE_FATAL:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

const_chr_t sk_logger_name(const sk_logger_t* logger) {
	return logger->name;
}

void sk_log_messagev(const sk_logger_api_t* api, sk_logger_type_t type, sk_logger_t* logger, const_chr_t fmt, va_list args) {
	char buffer[SK_LOG_MESSAGE_MAX];

	/* fmt is a runtime printf format from the public logging API (callers pass literals). */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
	vsnprintf(buffer, sizeof(buffer), fmt, args);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
	buffer[sizeof(buffer) - 1u] = '\0';
	api->message(type, logger, buffer);
}

void sk_log_message(const sk_logger_api_t* api, sk_logger_type_t type, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, type, logger, fmt, args);
	va_end(args);
}

void sk_log_trace(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_TRACE, logger, fmt, args);
	va_end(args);
}

void sk_log_debug(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_DEBUG, logger, fmt, args);
	va_end(args);
}

void sk_log_info(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_INFO, logger, fmt, args);
	va_end(args);
}

void sk_log_warn(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_WARN, logger, fmt, args);
	va_end(args);
}

void sk_log_error(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_ERROR, logger, fmt, args);
	va_end(args);
}

void sk_log_fatal(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...) {
	va_list args;

	va_start(args, fmt);
	sk_log_messagev(api, SK_LOGGER_TYPE_FATAL, logger, fmt, args);
	va_end(args);
}

#ifdef SK_TESTS
#include "test.h"
#include "logger.h"
#include <string.h>

typedef struct capture_sink_state_t {
	i32 call_count;
	sk_logger_type_t last_level;
	char last_name[64];
	char last_message[256];
} capture_sink_state_t;

static void capture_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	capture_sink_state_t* state = (capture_sink_state_t*)user_data;

	state->call_count += 1;
	state->last_level = level;
	strncpy(state->last_name, logger_name, sizeof(state->last_name) - 1u);
	state->last_name[sizeof(state->last_name) - 1u] = '\0';
	strncpy(state->last_message, message, sizeof(state->last_message) - 1u);
	state->last_message[sizeof(state->last_message) - 1u] = '\0';
}

/* Suppress default stdout during capture tests; restore afterward. */
static void mute_stdout_sink(void) {
	(void)sk_logger_api()->remove_sink(sk_logger_stdout_sink());
}

static void restore_stdout_sink(void) {
	const sk_logger_api_t* api = sk_logger_api();
	/* Idempotent: ignore if already present / full. */
	(void)api->remove_sink(sk_logger_stdout_sink());
	(void)api->add_sink(sk_logger_stdout_sink());
}

SK_TEST(logger_api_table_is_complete) {
	const sk_logger_api_t* api = sk_logger_api();
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_NOT_NULL(api->create_logger);
	TEST_ASSERT_NOT_NULL(api->destroy_logger);
	TEST_ASSERT_NOT_NULL(api->message);
	TEST_ASSERT_NOT_NULL(api->add_sink);
	TEST_ASSERT_NOT_NULL(api->remove_sink);
}

SK_TEST(logger_get_api_matches_static_table) {
	sk_logger_api_t out;

	memset(&out, 0, sizeof(out));
	sk_logger_get_api(&out);
	const sk_logger_api_t* api = sk_logger_api();

	TEST_ASSERT_EQUAL_PTR(api->create_logger, out.create_logger);
	TEST_ASSERT_EQUAL_PTR(api->destroy_logger, out.destroy_logger);
	TEST_ASSERT_EQUAL_PTR(api->message, out.message);
	TEST_ASSERT_EQUAL_PTR(api->add_sink, out.add_sink);
	TEST_ASSERT_EQUAL_PTR(api->remove_sink, out.remove_sink);
}

SK_TEST(logger_create_destroy_and_name) {
	const sk_logger_api_t* api = sk_logger_api();
	sk_logger_t* log = api->create_logger("log-name");

	TEST_ASSERT_NOT_NULL(log);
	TEST_ASSERT_EQUAL_STRING("log-name", sk_logger_name(log));
	api->destroy_logger(log);
}

SK_TEST(logger_message_reaches_custom_sink) {
	const sk_logger_api_t* api = sk_logger_api();
	capture_sink_state_t capture;
	sk_log_sink_t sink;

	memset(&capture, 0, sizeof(capture));
	sink.user_data = &capture;
	sink.print = capture_print;

	mute_stdout_sink();
	TEST_ASSERT_EQUAL_INT(0, api->add_sink(&sink));

	sk_logger_t* log = api->create_logger("log-name");
	TEST_ASSERT_NOT_NULL(log);

	api->message(SK_LOGGER_TYPE_ERROR, log, "blahblah");

	TEST_ASSERT_EQUAL_INT(1, capture.call_count);
	TEST_ASSERT_EQUAL_INT(SK_LOGGER_TYPE_ERROR, (int)capture.last_level);
	TEST_ASSERT_EQUAL_STRING("log-name", capture.last_name);
	TEST_ASSERT_EQUAL_STRING("blahblah", capture.last_message);

	api->destroy_logger(log);
	TEST_ASSERT_EQUAL_INT(0, api->remove_sink(&sink));
	restore_stdout_sink();
}

SK_TEST(log_error_formats_with_va_args) {
	const sk_logger_api_t* api = sk_logger_api();
	capture_sink_state_t capture;
	sk_log_sink_t sink;

	memset(&capture, 0, sizeof(capture));
	sink.user_data = &capture;
	sink.print = capture_print;

	mute_stdout_sink();
	TEST_ASSERT_EQUAL_INT(0, api->add_sink(&sink));

	sk_logger_t* log = api->create_logger("fmt");
	sk_log_error(api, log, "blablah %d", 1);

	TEST_ASSERT_EQUAL_INT(1, capture.call_count);
	TEST_ASSERT_EQUAL_INT(SK_LOGGER_TYPE_ERROR, (int)capture.last_level);
	TEST_ASSERT_EQUAL_STRING("blablah 1", capture.last_message);

	sk_log_info(api, log, "hello %s", "world");
	TEST_ASSERT_EQUAL_INT(2, capture.call_count);
	TEST_ASSERT_EQUAL_INT(SK_LOGGER_TYPE_INFO, (int)capture.last_level);
	TEST_ASSERT_EQUAL_STRING("hello world", capture.last_message);

	api->destroy_logger(log);
	TEST_ASSERT_EQUAL_INT(0, api->remove_sink(&sink));
	restore_stdout_sink();
}

SK_TEST(logger_type_names) {
	TEST_ASSERT_EQUAL_STRING("TRACE", sk_logger_type_name(SK_LOGGER_TYPE_TRACE));
	TEST_ASSERT_EQUAL_STRING("DEBUG", sk_logger_type_name(SK_LOGGER_TYPE_DEBUG));
	TEST_ASSERT_EQUAL_STRING("INFO", sk_logger_type_name(SK_LOGGER_TYPE_INFO));
	TEST_ASSERT_EQUAL_STRING("WARN", sk_logger_type_name(SK_LOGGER_TYPE_WARN));
	TEST_ASSERT_EQUAL_STRING("ERROR", sk_logger_type_name(SK_LOGGER_TYPE_ERROR));
	TEST_ASSERT_EQUAL_STRING("FATAL", sk_logger_type_name(SK_LOGGER_TYPE_FATAL));
	TEST_ASSERT_EQUAL_STRING("UNKNOWN", sk_logger_type_name((sk_logger_type_t)99));
}

SK_TEST(logger_stdout_sink_is_valid) {
	const sk_log_sink_t* sink = sk_logger_stdout_sink();
	TEST_ASSERT_NOT_NULL(sink);
	TEST_ASSERT_NOT_NULL(sink->print);
}

SK_TEST(logger_api_type_id_nonzero) {
	sk_type_id_t id = SK_LOGGER_API_TYPE_ID;
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
}

SK_TEST(logger_remove_missing_sink_fails) {
	const sk_logger_api_t* api = sk_logger_api();
	sk_log_sink_t phantom;

	phantom.user_data = (void_ptr_t)0x1;
	phantom.print = capture_print;
	TEST_ASSERT_NOT_EQUAL(0, api->remove_sink(&phantom));
}

static void file_sink_test_cleanup(const_chr_t base, u32 max_files) {
	char path[SK_LOG_FILE_ROTATED_PATH_MAX];
	u32 i;

	(void)remove(base);
	for (i = 1u; i < max_files; ++i) {
		file_sink_build_rotated_path(base, i, path, sizeof(path));
		(void)remove(path);
	}
}

static i32 file_sink_test_file_size(const_chr_t path) {
	FILE* f = fopen(path, "rb");
	long pos;

	if (f == NULL) {
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	pos = ftell(f);
	fclose(f);
	return (pos >= 0) ? (i32)pos : -1;
}

SK_TEST(log_file_sink_create_writes_and_destroy) {
	const sk_logger_api_t* api = sk_logger_api();
	static const char* base = "sk_log_file_sink_unit.log";
	sk_log_file_sink_t* file_sink;
	sk_logger_t* log;
	i32 size;

	file_sink_test_cleanup(base, 3u);
	file_sink = sk_log_file_sink_create(base, 64ull * 1024ull, 3u);
	TEST_ASSERT_NOT_NULL(file_sink);
	TEST_ASSERT_NOT_NULL(sk_log_file_sink_sink(file_sink));
	TEST_ASSERT_NOT_NULL(sk_log_file_sink_sink(file_sink)->print);

	mute_stdout_sink();
	TEST_ASSERT_EQUAL_INT(0, api->add_sink(sk_log_file_sink_sink(file_sink)));

	log = api->create_logger("file-sink");
	TEST_ASSERT_NOT_NULL(log);
	api->message(SK_LOGGER_TYPE_INFO, log, "hello-file-sink");
	api->destroy_logger(log);

	TEST_ASSERT_EQUAL_INT(0, api->remove_sink(sk_log_file_sink_sink(file_sink)));
	sk_log_file_sink_destroy(file_sink);
	restore_stdout_sink();

	size = file_sink_test_file_size(base);
	TEST_ASSERT_TRUE(size > 0);
	{
		FILE* f = fopen(base, "rb");
		char buf[256];
		size_t n;

		TEST_ASSERT_NOT_NULL(f);
		n = fread(buf, 1u, sizeof(buf) - 1u, f);
		fclose(f);
		buf[n] = '\0';
		TEST_ASSERT_NOT_NULL(strstr(buf, "hello-file-sink"));
		TEST_ASSERT_NOT_NULL(strstr(buf, "[INFO]"));
		TEST_ASSERT_NOT_NULL(strstr(buf, "[file-sink]"));
	}

	file_sink_test_cleanup(base, 3u);
}

SK_TEST(log_file_sink_rotates_when_over_max_bytes) {
	const sk_logger_api_t* api = sk_logger_api();
	static const char* base = "sk_log_file_sink_rotate.log";
	sk_log_file_sink_t* file_sink;
	sk_logger_t* log;
	char rotated[SK_LOG_FILE_ROTATED_PATH_MAX];
	u32 i;

	file_sink_test_cleanup(base, 3u);
	/* Tiny limit forces rotation after a few lines. */
	file_sink = sk_log_file_sink_create(base, 120ull, 3u);
	TEST_ASSERT_NOT_NULL(file_sink);

	mute_stdout_sink();
	TEST_ASSERT_EQUAL_INT(0, api->add_sink(sk_log_file_sink_sink(file_sink)));

	log = api->create_logger("rotate");
	TEST_ASSERT_NOT_NULL(log);
	for (i = 0u; i < 20u; ++i) {
		sk_log_info(api, log, "rotate-line-%02u-xxxxxxxxxxxxxxxxxxxx", i);
	}
	api->destroy_logger(log);

	TEST_ASSERT_EQUAL_INT(0, api->remove_sink(sk_log_file_sink_sink(file_sink)));
	sk_log_file_sink_destroy(file_sink);
	restore_stdout_sink();

	file_sink_build_rotated_path(base, 1u, rotated, sizeof(rotated));
	TEST_ASSERT_TRUE(file_sink_test_file_size(base) >= 0);
	TEST_ASSERT_TRUE(file_sink_test_file_size(rotated) > 0);

	file_sink_test_cleanup(base, 3u);
}

SK_TEST(log_file_sink_resumes_size_from_existing_file) {
	const sk_logger_api_t* api = sk_logger_api();
	static const char* base = "sk_log_file_sink_resume.log";
	sk_log_file_sink_t* file_sink;
	sk_logger_t* log;
	char rotated[SK_LOG_FILE_ROTATED_PATH_MAX];

	file_sink_test_cleanup(base, 3u);
	/* One formatted line is ~70 bytes; 100 forces a rotate on the second write
	 * only if create() picks up the existing file size. */
	file_sink = sk_log_file_sink_create(base, 100ull, 3u);
	TEST_ASSERT_NOT_NULL(file_sink);

	mute_stdout_sink();
	TEST_ASSERT_EQUAL_INT(0, api->add_sink(sk_log_file_sink_sink(file_sink)));
	log = api->create_logger("resume");
	TEST_ASSERT_NOT_NULL(log);
	sk_log_info(api, log, "resume-line-aaaaaaaaaaaaaaaaaaaa");
	api->destroy_logger(log);
	TEST_ASSERT_EQUAL_INT(0, api->remove_sink(sk_log_file_sink_sink(file_sink)));
	sk_log_file_sink_destroy(file_sink);
	TEST_ASSERT_TRUE(file_sink_test_file_size(base) > 0);

	file_sink = sk_log_file_sink_create(base, 100ull, 3u);
	TEST_ASSERT_NOT_NULL(file_sink);
	TEST_ASSERT_EQUAL_INT(0, api->add_sink(sk_log_file_sink_sink(file_sink)));
	log = api->create_logger("resume");
	TEST_ASSERT_NOT_NULL(log);
	sk_log_info(api, log, "resume-line-bbbbbbbbbbbbbbbbbbbb");
	api->destroy_logger(log);
	TEST_ASSERT_EQUAL_INT(0, api->remove_sink(sk_log_file_sink_sink(file_sink)));
	sk_log_file_sink_destroy(file_sink);
	restore_stdout_sink();

	file_sink_build_rotated_path(base, 1u, rotated, sizeof(rotated));
	TEST_ASSERT_TRUE(file_sink_test_file_size(rotated) > 0);

	file_sink_test_cleanup(base, 3u);
}

SK_TEST(log_file_sink_create_rejects_empty_path) {
	TEST_ASSERT_NULL(sk_log_file_sink_create(NULL, 0ull, 0u));
	TEST_ASSERT_NULL(sk_log_file_sink_create("", 0ull, 0u));
}

SK_TEST(log_file_sink_destroy_null_is_noop) {
	sk_log_file_sink_destroy(NULL);
}

SK_TEST(logger_bind_api_redirects_and_clears) {
	const sk_logger_api_t* local = sk_logger_api();
	sk_logger_api_t fake;

	TEST_ASSERT_NOT_NULL(local);
	memset(&fake, 0, sizeof(fake));
	fake.create_logger = local->create_logger;
	fake.destroy_logger = local->destroy_logger;
	fake.message = local->message;
	fake.add_sink = local->add_sink;
	fake.remove_sink = local->remove_sink;

	sk_logger_bind_api(&fake);
	TEST_ASSERT_EQUAL_PTR(&fake, sk_logger_api());

	sk_logger_bind_api(NULL);
	TEST_ASSERT_EQUAL_PTR(local, sk_logger_api());
}
#endif /* SK_TESTS */
