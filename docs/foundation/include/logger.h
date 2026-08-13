#pragma once

/**
 * @file logger.h
 * @brief Context-owned logger (named loggers, sinks, formatted helpers).
 *
 * There is no process-wide sink list and no `sk_logger_api()` /
 * `sk_logger_bind_api` / `sk_logger_get_api`. Mutable sink state lives on an
 * explicit `sk_logger_context_t` owned by the host `sk_app_context_t`
 * (created in `sk_app_startup` / `sk_app_init`). Plugins obtain both the
 * context and the immutable function table from `sk_app_api_t`:
 *
 * @code
 * const sk_logger_api_t* logger_api = app_api->logger_api(app_ctx);
 * sk_logger_context_t* log_ctx = app_api->logger_context(app_ctx);
 * sk_logger_t* log = logger_api->create_logger(log_ctx, "ui");
 * sk_log_error(logger_api, log, "failed: %d", rc);
 * logger_api->destroy_logger(log_ctx, log);
 * @endcode
 *
 * Tests that need a logger without a full app boot may call
 * `sk_logger_context_create` / `sk_logger_context_destroy` directly.
 *
 * Main-thread ownership. Not worker-safe.
 */

#include "allocator.h"
#include "common.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque named logger instance. Stores a back-pointer to its creating context. */
typedef struct sk_logger_t sk_logger_t;

/**
 * Opaque logger module state: sink array + count + allocator.
 * Replaces the former file-scope `sinks[]`, `sink_count`, `module_ready`,
 * and `bound_api` statics. Layout is internal (`foundation/internal/logger_context.h`).
 */
typedef struct sk_logger_context_t sk_logger_context_t;

/**
 * Log severity level.
 * Higher values are more severe; sinks may filter by level later.
 */
typedef enum sk_logger_type_t {
	SK_LOGGER_TYPE_TRACE = 0,
	SK_LOGGER_TYPE_DEBUG = 1,
	SK_LOGGER_TYPE_INFO = 2,
	SK_LOGGER_TYPE_WARN = 3,
	SK_LOGGER_TYPE_ERROR = 4,
	SK_LOGGER_TYPE_FATAL = 5
} sk_logger_type_t;

/** Maximum registered sinks per logger context (copied descriptors). */
enum { SK_LOGGER_MAX_SINKS = 16 };

/** Copied name capacity for a named logger (including NUL). */
enum { SK_LOGGER_NAME_MAX = 64 };

/** Formatted message capacity for `sk_log_*` helpers (including NUL). */
enum { SK_LOG_MESSAGE_MAX = 2048 };

/**
 * Output destination for log lines.
 * Registered on a logger context; every message is forwarded to all sinks.
 */
typedef struct sk_log_sink_t {
	/** Opaque per-sink state (passed to print). May be NULL. */
	void_ptr_t user_data;

	/**
	 * Emit one formatted log line.
	 * @param user_data   Sink user_data field.
	 * @param level       Severity of the message.
	 * @param logger_name Name of the emitting logger (never NULL; may be empty).
	 * @param message     Message text (never NULL; may be empty).
	 */
	void (*print)(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message);
} sk_log_sink_t;

/**
 * Logger module API (one immutable function table).
 * Obtain via `app_api->logger_api(app_ctx)`, never via a free-function accessor.
 * Every entry that touches sink state takes the logger context as the first argument.
 */
typedef struct sk_logger_api_t {
	/**
	 * Create a named logger bound to @p log_ctx.
	 * @param log_ctx Logger context (must not be NULL).
	 * @param name    UTF-8 display name (copied; NULL becomes empty string).
	 * @return New logger, or NULL on allocation failure.
	 */
	sk_logger_t* (*create_logger)(sk_logger_context_t* log_ctx, const_chr_t name);

	/**
	 * Destroy a logger from create_logger.
	 * Passing a NULL @p logger is a no-op. Does not destroy @p log_ctx.
	 * @param log_ctx Logger context that created @p logger (must not be NULL).
	 * @param logger  Logger to free.
	 */
	void (*destroy_logger)(sk_logger_context_t* log_ctx, sk_logger_t* logger);

	/**
	 * Emit a pre-formatted message to all sinks registered on @p log_ctx.
	 * @param log_ctx Logger context (must not be NULL).
	 * @param type    Severity level.
	 * @param logger  Emitting logger (must not be NULL).
	 * @param message Message text (must not be NULL).
	 */
	void (*message)(sk_logger_context_t* log_ctx, sk_logger_type_t type, sk_logger_t* logger, const_chr_t message);

	/**
	 * Register a sink. Copies the sink struct into @p log_ctx storage.
	 * Does not take ownership of any FILE* inside user_data.
	 * @param log_ctx Logger context (must not be NULL).
	 * @param sink    Sink descriptor (must not be NULL; print must not be NULL).
	 * @return 0 on success, non-zero if capacity exceeded (`SK_LOGGER_MAX_SINKS`).
	 */
	i32 (*add_sink)(sk_logger_context_t* log_ctx, const sk_log_sink_t* sink);

	/**
	 * Unregister the first sink matching print + user_data.
	 * Does not destroy user_data and does not close FILE*s.
	 * @param log_ctx Logger context (must not be NULL).
	 * @param sink    Sink to match (must not be NULL).
	 * @return 0 if a sink was removed, non-zero if not found.
	 */
	i32 (*remove_sink)(sk_logger_context_t* log_ctx, const sk_log_sink_t* sink);
} sk_logger_api_t;

/** Type id for app-context registration of `sk_logger_api_t`. */
#define SK_LOGGER_API_TYPE_ID SK_TYPE_ID("sk.logger_api", 0xd920710f8a86861fULL, 0xf95f424d6e26e084ULL)

/**
 * Allocate a logger context and install the default stdout sink.
 * No lazy init; the context is ready on return. Caller owns the result
 * and must call `sk_logger_context_destroy`. Hosts normally do not call
 * this — `sk_app_startup` / `sk_app_init` does.
 *
 * @param allocator Heap used for the context and for named loggers (must not be NULL).
 * @return New context, or NULL on allocation failure.
 */
sk_logger_context_t* sk_logger_context_create(const sk_allocator_t* allocator);

/**
 * Free a logger context. Does **not** close FILE*s and does **not** destroy
 * named `sk_logger_t` instances (caller must destroy those first).
 * Passing NULL is a no-op.
 *
 * @param log_ctx Context from `sk_logger_context_create`.
 */
void sk_logger_context_destroy(sk_logger_context_t* log_ctx);

/**
 * Allocator stored on @p log_ctx at create (borrowed).
 * @param log_ctx Logger context (must not be NULL).
 * @return Non-NULL allocator.
 */
const sk_allocator_t* sk_logger_context_allocator(const sk_logger_context_t* log_ctx);

/**
 * Context that created @p logger (borrowed; valid until that context is destroyed).
 * @param logger Logger from create_logger (must not be NULL).
 * @return Non-NULL logger context.
 */
sk_logger_context_t* sk_logger_get_context(const sk_logger_t* logger);

/**
 * Default sink that prints to stdout:
 *   [YYYY-MM-DD HH:MM:SS] [LEVEL] [logger-name] message
 *
 * Installed automatically by `sk_logger_context_create`. The descriptor is
 * immutable (`static const`); the logger context copies it into its sink array.
 * @return Non-NULL pointer to a static `sk_log_sink_t`.
 */
const sk_log_sink_t* sk_logger_stdout_sink(void);

/**
 * Opaque rotating file log sink (stdio-backed).
 * Owns an open FILE and rotation state. Register with add_sink via
 * `sk_log_file_sink_sink()`, then `remove_sink` + destroy on shutdown.
 * The logger context never closes this FILE*.
 */
typedef struct sk_log_file_sink_t sk_log_file_sink_t;

/** Default max bytes per log file when create is passed 0 (5 MiB). */
enum { SK_LOG_FILE_SINK_DEFAULT_MAX_BYTES = 5u * 1024u * 1024u };

/** Default number of files kept when create is passed 0 (active + 4 archives). */
enum { SK_LOG_FILE_SINK_DEFAULT_MAX_FILES = 5u };

/**
 * Create a rotating file sink writing to @p path.
 * Opens/creates the file for append. When the active file reaches @p max_bytes,
 * it is rotated: path → path.1 → path.2 → … → path.(max_files-1); oldest is
 * deleted. Parent directory must already exist. Allocates the sink object with
 * @p allocator (must not be NULL). Destroy uses the same table (stored on the sink).
 *
 * @param path      UTF-8 base log path (e.g. "logs/player.log"). Copied.
 * @param max_bytes Bytes per file before rotation; 0 → SK_LOG_FILE_SINK_DEFAULT_MAX_BYTES.
 * @param max_files Total files to keep including the active file; 0 →
 *                  SK_LOG_FILE_SINK_DEFAULT_MAX_FILES. Clamped to at least 1.
 * @param allocator Heap for the sink object (must not be NULL).
 * @return New sink, or NULL if path is empty/too long, allocation fails, or
 *         the file cannot be opened.
 */
sk_log_file_sink_t* sk_log_file_sink_create(const_chr_t path, u64 max_bytes, u32 max_files, const sk_allocator_t* allocator);

/**
 * Close the owned FILE and free a sink from `sk_log_file_sink_create`.
 * Unregister from the logger context first if still added. Passing NULL is a no-op.
 * @param sink Sink to destroy.
 */
void sk_log_file_sink_destroy(sk_log_file_sink_t* sink);

/**
 * `sk_log_sink_t` descriptor for registration with `api->add_sink`.
 * Valid until `sk_log_file_sink_destroy`. Same format as the stdout sink.
 *
 * @param sink File sink (must not be NULL).
 * @return Non-NULL pointer to the embedded sink descriptor.
 */
const sk_log_sink_t* sk_log_file_sink_sink(sk_log_file_sink_t* sink);

/**
 * Human-readable name for a log level (e.g. "ERROR").
 * Unknown levels return "UNKNOWN".
 * @param type Log level.
 * @return Non-NULL static C string.
 */
const_chr_t sk_logger_type_name(sk_logger_type_t type);

/**
 * Name of a logger instance (from create_logger).
 * @param logger Logger (must not be NULL).
 * @return Non-NULL name string owned by the logger.
 */
const_chr_t sk_logger_name(const sk_logger_t* logger);

/**
 * Format a message with printf-style args and emit via `api->message`.
 * Uses `sk_logger_get_context(logger)` as the context argument.
 * @param api    Logger API (must not be NULL).
 * @param type   Severity level.
 * @param logger Emitting logger (must not be NULL).
 * @param fmt    printf format string (must not be NULL).
 */
void sk_log_message(const sk_logger_api_t* api, sk_logger_type_t type, sk_logger_t* logger, const_chr_t fmt, ...);

/**
 * va_list form of `sk_log_message`.
 */
void sk_log_messagev(const sk_logger_api_t* api, sk_logger_type_t type, sk_logger_t* logger, const_chr_t fmt, va_list args);

/** Level-specific printf helpers (see `sk_log_message`). */
void sk_log_trace(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_debug(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_info(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_warn(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_error(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_fatal(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);

#ifdef __cplusplus
}
#endif
