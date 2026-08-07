#include "logger.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

enum { SK_LOGGER_NAME_MAX = 64, SK_LOGGER_MAX_SINKS = 16, SK_LOG_MESSAGE_MAX = 2048 };

struct sk_logger_t {
	char name[SK_LOGGER_NAME_MAX];
};

/* Process-global sink list (main-thread ownership). */
static sk_log_sink_t sinks[SK_LOGGER_MAX_SINKS];
static u32 sink_count = 0;
static i32 module_ready = 0;

/* ---- default stdout sink ---- */

static void stdout_sink_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	struct tm tm_local;
	char timestamp[32];

	(void)user_data;

	time_t now = time(NULL);
#if defined(_WIN32)
	localtime_s(&tm_local, &now);
#else
	localtime_r(&now, &tm_local);
#endif
	if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_local) == 0) {
		timestamp[0] = '\0';
	}

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

const sk_logger_api_t* sk_logger_api(void) {
	ensure_module_ready();
	return &logger_api;
}

void sk_logger_get_api(sk_logger_api_t* out) {
	ensure_module_ready();
	*out = logger_api;
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
#endif /* SK_TESTS */
