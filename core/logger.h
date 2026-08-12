#pragma once

/**
 * @file logger.h
 * @brief Process-wide logger module API (named loggers, sinks, formatted helpers).
 *
 * Obtain the global table via sk_logger_api(). Create named loggers, emit
 * messages through the table, and register sinks that receive every message.
 * A default stdout sink (timestamp, level, logger name, message) is registered
 * when the module is first used.
 *
 * Pure engine utility: implemented in sk-core (not sk-app).
 */

#include "common.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque named logger instance (forward declaration). */
typedef struct sk_logger_t sk_logger_t;

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

/**
 * Output destination for log lines.
 * Registered on the logger API; every message is forwarded to all sinks.
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
 * Global logger module API (one process-wide table).
 *
 * Example:
 *   const sk_logger_api_t* logger_api = sk_logger_api();
 *   sk_logger_t* log = logger_api->create_logger("log-name");
 *   logger_api->message(SK_LOGGER_TYPE_ERROR, log, "blahblah");
 *   sk_log_error(logger_api, log, "blablah %d", 1);
 *   logger_api->destroy_logger(log);
 */
typedef struct sk_logger_api_t {
	/**
     * Create a named logger.
     * @param name UTF-8 display name (copied; NULL becomes empty string).
     * @return New logger, or NULL on allocation failure.
     */
	sk_logger_t* (*create_logger)(const_chr_t name);

	/**
     * Destroy a logger from create_logger.
     * Passing NULL is a no-op.
     * @param logger Logger to free.
     */
	void (*destroy_logger)(sk_logger_t* logger);

	/**
     * Emit a pre-formatted message to all registered sinks.
     * @param type    Severity level.
     * @param logger  Emitting logger (must not be NULL).
     * @param message Message text (must not be NULL).
     */
	void (*message)(sk_logger_type_t type, sk_logger_t* logger, const_chr_t message);

	/**
     * Register a sink. Copies the sink struct into internal storage.
     * @param sink Sink descriptor (must not be NULL; print must not be NULL).
     * @return 0 on success, non-zero if capacity exceeded or bad args.
     */
	i32 (*add_sink)(const sk_log_sink_t* sink);

	/**
     * Unregister the first sink matching print + user_data.
     * @param sink Sink to match (must not be NULL).
     * @return 0 if a sink was removed, non-zero if not found / bad args.
     */
	i32 (*remove_sink)(const sk_log_sink_t* sink);
} sk_logger_api_t;

/** Type id for app-context registration of sk_logger_api_t (optional). */
#define SK_LOGGER_API_TYPE_ID SK_TYPE_ID("sk.logger_api", 0xd920710f8a86861fULL, 0xf95f424d6e26e084ULL)

/**
 * Logger API function table for this module instance.
 *
 * Plugins statically link sk-core, so each DLL has its own sink list by
 * default. Hosts call sk_logger_bind_api (via lib_symbol after LoadLibrary)
 * so plugin code that uses sk_logger_api() shares the host sink list (stdout,
 * file sinks, editor console, etc.).
 *
 * @return Non-NULL pointer to the bound host table if set, else this module's table.
 */
const sk_logger_api_t* sk_logger_api(void);

/**
 * Point this module's sk_logger_api() at another table (usually the host's).
 * Pass NULL to clear the binding and use this module's own table again.
 * sk-app load_plugin resolves this symbol in each plugin DLL and binds the
 * host table so host-registered sinks receive plugin sk_log_* traffic.
 *
 * @param api Host logger table (must outlive the plugin), or NULL to unbind.
 */
SK_API void sk_logger_bind_api(const sk_logger_api_t* api);

/**
 * Fill @p out with the active logger API table (bound host table if any).
 * @param out Destination table; must not be NULL.
 */
void sk_logger_get_api(sk_logger_api_t* out);

/**
 * Default sink that prints to stdout:
 *   [YYYY-MM-DD HH:MM:SS] [LEVEL] [logger-name] message
 *
 * Registered automatically on first use of the logger module.
 * @return Non-NULL pointer to a static sk_log_sink_t.
 */
const sk_log_sink_t* sk_logger_stdout_sink(void);

/**
 * Opaque rotating file log sink (stdio-backed).
 * Owns an open FILE and rotation state. Register with add_sink via
 * sk_log_file_sink_sink(), then remove_sink + destroy on shutdown.
 */
typedef struct sk_log_file_sink_t sk_log_file_sink_t;

/**
 * Default max bytes per log file when create is passed 0 (5 MiB).
 */
enum { SK_LOG_FILE_SINK_DEFAULT_MAX_BYTES = 5u * 1024u * 1024u };

/**
 * Default number of files kept when create is passed 0 (active + 4 archives).
 */
enum { SK_LOG_FILE_SINK_DEFAULT_MAX_FILES = 5u };

/**
 * Create a rotating file sink writing to @p path.
 * Opens/creates the file for append. When the active file reaches @p max_bytes,
 * it is rotated: path → path.1 → path.2 → … → path.(max_files-1); oldest is
 * deleted. Parent directory must already exist.
 *
 * @param path      UTF-8 base log path (e.g. "logs/player.log"). Copied.
 * @param max_bytes Bytes per file before rotation; 0 → SK_LOG_FILE_SINK_DEFAULT_MAX_BYTES.
 * @param max_files Total files to keep including the active file; 0 →
 *                  SK_LOG_FILE_SINK_DEFAULT_MAX_FILES. Clamped to at least 1.
 * @return New sink, or NULL if path is empty/too long, allocation fails, or
 *         the file cannot be opened.
 */
sk_log_file_sink_t* sk_log_file_sink_create(const_chr_t path, u64 max_bytes, u32 max_files);

/**
 * Close the file and free a sink from sk_log_file_sink_create.
 * Unregister from the logger first if still added. Passing NULL is a no-op.
 * @param sink Sink to destroy.
 */
void sk_log_file_sink_destroy(sk_log_file_sink_t* sink);

/**
 * sk_log_sink_t descriptor for registration with api->add_sink.
 * Valid until sk_log_file_sink_destroy. Same format as the stdout sink:
 *   [YYYY-MM-DD HH:MM:SS] [LEVEL] [logger-name] message
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
 * Format a message with printf-style args and emit via api->message.
 * @param api    Logger API (must not be NULL).
 * @param type   Severity level.
 * @param logger Emitting logger (must not be NULL).
 * @param fmt    printf format string (must not be NULL).
 */
void sk_log_message(const sk_logger_api_t* api, sk_logger_type_t type, sk_logger_t* logger, const_chr_t fmt, ...);

/**
 * va_list form of sk_log_message.
 */
void sk_log_messagev(const sk_logger_api_t* api, sk_logger_type_t type, sk_logger_t* logger, const_chr_t fmt, va_list args);

/** Level-specific printf helpers (see sk_log_message). */
void sk_log_trace(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_debug(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_info(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_warn(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_error(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);
void sk_log_fatal(const sk_logger_api_t* api, sk_logger_t* logger, const_chr_t fmt, ...);

#ifdef __cplusplus
}
#endif
