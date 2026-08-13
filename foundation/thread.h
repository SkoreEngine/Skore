#pragma once

/**
 * @file thread.h
 * @brief Portable worker threads (opaque handle; OS headers live only in thread.c).
 *
 * Platform-abstracted threads for the job/parallel paths. The public surface
 * is a single opaque handle plus free functions; the Win32 (CreateThread) and
 * POSIX (pthread) backends are confined to core/thread.c so no OS header
 * leaks into engine/game code.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start routine run on a new thread. Returning from this routine ends the
 * thread.
 *
 * @param arg Caller-supplied pointer passed through from sk_thread_create.
 * @return Thread exit status (kept for parity with the Win32/pthread shapes;
 *         sk_thread_join does not surface it yet).
 */
typedef i32 (*sk_thread_routine_t)(void_ptr_t arg);

/**
 * Opaque thread handle. Represents a started thread; the handle stays valid
 * until sk_thread_join returns (or sk_thread_destroy detaches it).
 * Create with sk_thread_create, wait with sk_thread_join, then release the
 * handle with sk_thread_destroy.
 */
typedef struct sk_thread_t sk_thread_t;

/**
 * Start a new thread running @p routine with @p arg (uses the default
 * allocator).
 *
 * @param routine Entry point called on the new thread; must not be NULL.
 * @param arg     Pointer passed through to @p routine.
 * @return New thread handle, or NULL on allocation / OS failure.
 */
sk_thread_t* sk_thread_create(sk_thread_routine_t routine, void_ptr_t arg);

/**
 * Block until @p thread finishes, releasing its OS resources. Must be called
 * from a different thread than the one running @p thread.
 *
 * @param thread Handle from sk_thread_create; must not be NULL.
 * @return 0 on success, non-zero on an OS failure.
 */
i32 sk_thread_join(sk_thread_t* thread);

/**
 * Release a handle created with sk_thread_create. If the thread has not been
 * joined yet it is detached: it keeps running until its routine returns and
 * the handle is cleaned up then. Passing NULL is a no-op.
 *
 * @param thread Handle to release.
 */
void sk_thread_destroy(sk_thread_t* thread);

#ifdef __cplusplus
}
#endif
