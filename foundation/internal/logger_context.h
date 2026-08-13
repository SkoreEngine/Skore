#pragma once

/**
 * @file internal/logger_context.h
 * @brief One definition of `sk_logger_context_t` for foundation TUs only.
 *
 * Not a public header. Plugins and hosts must not include this.
 */

#include "logger.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sk_logger_context_t {
	sk_log_sink_t sinks[SK_LOGGER_MAX_SINKS];
	u32 sink_count;
	const sk_allocator_t* allocator;
};

#ifdef __cplusplus
}
#endif
