#pragma once

/**
 * @file mutex.h
 * @brief Portable mutex (opaque handle; OS headers live only in mutex.c).
 *
 * Platform-abstracted mutual exclusion for the rare worker/thread paths.
 * The public surface is a single opaque handle plus free functions; the
 * Win32 (SRWLOCK) and POSIX (pthread) backends are confined to core/mutex.c
 * so no OS header leaks into engine/game code.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque mutex handle. Non-recursive: a thread must not lock the same mutex
 * twice before unlocking, and only the thread that locked it may unlock it.
 * Create with sk_mutex_create, release with sk_mutex_destroy.
 */
typedef struct sk_mutex_t sk_mutex_t;

/**
 * Create a mutex in the unlocked state (uses the default allocator).
 *
 * @return New mutex handle, or NULL on allocation / OS failure.
 */
sk_mutex_t* sk_mutex_create(void);

/**
 * Destroy a mutex created with sk_mutex_create.
 * The mutex must be unlocked (no thread holds it) at destroy time.
 * Passing NULL is a no-op.
 *
 * @param mutex Handle to destroy.
 */
void sk_mutex_destroy(sk_mutex_t* mutex);

/**
 * Lock @p mutex, blocking until it is acquired.
 *
 * @param mutex Handle from sk_mutex_create; must not be NULL.
 */
void sk_mutex_lock(sk_mutex_t* mutex);

/**
 * Try to lock @p mutex without blocking.
 *
 * @param mutex Handle from sk_mutex_create; must not be NULL.
 * @return 0 if the lock was acquired, 1 if it is already held
 *         (non-zero means not acquired), -1 on an OS failure.
 */
i32 sk_mutex_try_lock(sk_mutex_t* mutex);

/**
 * Unlock @p mutex previously locked by the calling thread.
 *
 * @param mutex Handle from sk_mutex_create; must not be NULL.
 */
void sk_mutex_unlock(sk_mutex_t* mutex);

#ifdef __cplusplus
}
#endif
