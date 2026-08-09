#include "mutex.h"

#include "allocator.h"

#if defined(_WIN32)
/* SRWLOCK: kernel slim lock, no cleanup step, non-recursive (matches POSIX default). */
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#endif

struct sk_mutex_t {
#if defined(_WIN32)
	SRWLOCK srwlock;
#else
	pthread_mutex_t mutex;
#endif
};

sk_mutex_t* sk_mutex_create(void) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_mutex_t* mutex = (sk_mutex_t*)alloc->alloc(alloc->instance, sizeof(sk_mutex_t));
	if (mutex == NULL) {
		return NULL;
	}

#if defined(_WIN32)
	InitializeSRWLock(&mutex->srwlock);
#else
	int rc = pthread_mutex_init(&mutex->mutex, NULL);
	if (rc != 0) {
		alloc->free(alloc->instance, mutex);
		return NULL;
	}
#endif
	return mutex;
}

void sk_mutex_destroy(sk_mutex_t* mutex) {
	if (mutex == NULL) {
		return;
	}
#if !defined(_WIN32)
	pthread_mutex_destroy(&mutex->mutex);
#endif
	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, mutex);
}

void sk_mutex_lock(sk_mutex_t* mutex) {
#if defined(_WIN32)
	AcquireSRWLockExclusive(&mutex->srwlock);
#else
	pthread_mutex_lock(&mutex->mutex);
#endif
}

i32 sk_mutex_try_lock(sk_mutex_t* mutex) {
#if defined(_WIN32)
	return TryAcquireSRWLockExclusive(&mutex->srwlock) ? 0 : 1;
#else
	int rc = pthread_mutex_trylock(&mutex->mutex);
	if (rc == 0) {
		return 0;
	}
	return rc == EBUSY ? 1 : -1;
#endif
}

void sk_mutex_unlock(sk_mutex_t* mutex) {
#if defined(_WIN32)
	ReleaseSRWLockExclusive(&mutex->srwlock);
#else
	pthread_mutex_unlock(&mutex->mutex);
#endif
}

#ifdef SK_TESTS
#include "test.h"
#include "mutex.h"

/* Test constants (plain names; local to this TU). */
#define MUTEX_TEST_THREADS 4u
#define MUTEX_TEST_ITERS 10000u

typedef struct mutex_counter_state_t {
	sk_mutex_t* mutex;
	u64 count;
} mutex_counter_state_t;

#if defined(_WIN32)
static DWORD WINAPI mutex_counter_thread(LPVOID param) {
#else
static void* mutex_counter_thread(void* param) {
#endif
	mutex_counter_state_t* s = (mutex_counter_state_t*)param;
	for (u32 i = 0u; i < MUTEX_TEST_ITERS; ++i) {
		sk_mutex_lock(s->mutex);
		s->count += 1u;
		sk_mutex_unlock(s->mutex);
	}
#if defined(_WIN32)
	return 0;
#else
	return NULL;
#endif
}

typedef struct mutex_busy_state_t {
	sk_mutex_t* state_mutex;
	sk_mutex_t* target;
	u32 held;
	u32 release;
} mutex_busy_state_t;

#if defined(_WIN32)
static DWORD WINAPI mutex_busy_worker(LPVOID param) {
#else
static void* mutex_busy_worker(void* param) {
#endif
	mutex_busy_state_t* s = (mutex_busy_state_t*)param;
	sk_mutex_lock(s->target);
	sk_mutex_lock(s->state_mutex);
	s->held = 1u;
	sk_mutex_unlock(s->state_mutex);
	for (;;) {
		sk_mutex_lock(s->state_mutex);
		u32 go = s->release;
		sk_mutex_unlock(s->state_mutex);
		if (go != 0u) {
			break;
		}
	}
	sk_mutex_unlock(s->target);
#if defined(_WIN32)
	return 0;
#else
	return NULL;
#endif
}

SK_TEST(mutex_create_lock_unlock_destroy) {
	sk_mutex_t* mutex = sk_mutex_create();
	TEST_ASSERT_NOT_NULL(mutex);

	sk_mutex_lock(mutex);
	sk_mutex_unlock(mutex);

	sk_mutex_destroy(mutex);
	sk_mutex_destroy(NULL);
}

SK_TEST(mutex_try_lock_single_thread) {
	sk_mutex_t* mutex = sk_mutex_create();
	TEST_ASSERT_NOT_NULL(mutex);

	TEST_ASSERT_EQUAL_INT(0, sk_mutex_try_lock(mutex));
	sk_mutex_unlock(mutex);
	TEST_ASSERT_EQUAL_INT(0, sk_mutex_try_lock(mutex));
	sk_mutex_unlock(mutex);

	sk_mutex_destroy(mutex);
}

SK_TEST(mutex_try_lock_busy_when_held) {
	sk_mutex_t* target = sk_mutex_create();
	sk_mutex_t* state_mutex = sk_mutex_create();
	TEST_ASSERT_NOT_NULL(target);
	TEST_ASSERT_NOT_NULL(state_mutex);

	mutex_busy_state_t state = {.state_mutex = state_mutex, .target = target, .held = 0u, .release = 0u};

#if defined(_WIN32)
	HANDLE h = CreateThread(NULL, 0, mutex_busy_worker, &state, 0, NULL);
	TEST_ASSERT_NOT_NULL(h);
#else
	pthread_t tid;
	TEST_ASSERT_EQUAL_INT(0, pthread_create(&tid, NULL, mutex_busy_worker, &state));
#endif

	u32 worker_ready = 0u;
	while (worker_ready == 0u) {
		sk_mutex_lock(state_mutex);
		worker_ready = state.held;
		sk_mutex_unlock(state_mutex);
	}

	TEST_ASSERT_EQUAL_INT(1, sk_mutex_try_lock(target));

	sk_mutex_lock(state_mutex);
	state.release = 1u;
	sk_mutex_unlock(state_mutex);

#if defined(_WIN32)
	WaitForSingleObject(h, INFINITE);
	CloseHandle(h);
#else
	pthread_join(tid, NULL);
#endif

	sk_mutex_destroy(target);
	sk_mutex_destroy(state_mutex);
}

SK_TEST(mutex_serializes_concurrent_increments) {
	sk_mutex_t* mutex = sk_mutex_create();
	TEST_ASSERT_NOT_NULL(mutex);

	mutex_counter_state_t state = {.mutex = mutex, .count = 0u};

#if defined(_WIN32)
	HANDLE handles[MUTEX_TEST_THREADS];
	u32 spawned = 0u;
	for (u32 i = 0u; i < MUTEX_TEST_THREADS; ++i) {
		handles[i] = CreateThread(NULL, 0, mutex_counter_thread, &state, 0, NULL);
		if (handles[i] == NULL) {
			break;
		}
		spawned += 1u;
	}
	TEST_ASSERT_EQUAL_UINT32(MUTEX_TEST_THREADS, spawned);
	if (spawned > 0u) {
		WaitForMultipleObjects(spawned, handles, TRUE, INFINITE);
	}
	for (u32 i = 0u; i < spawned; ++i) {
		CloseHandle(handles[i]);
	}
#else
	pthread_t threads[MUTEX_TEST_THREADS];
	u32 spawned = 0u;
	for (u32 i = 0u; i < MUTEX_TEST_THREADS; ++i) {
		if (pthread_create(&threads[i], NULL, mutex_counter_thread, &state) != 0) {
			break;
		}
		spawned += 1u;
	}
	TEST_ASSERT_EQUAL_UINT32(MUTEX_TEST_THREADS, spawned);
	for (u32 i = 0u; i < spawned; ++i) {
		pthread_join(threads[i], NULL);
	}
#endif

	TEST_ASSERT_EQUAL_UINT64((u64)MUTEX_TEST_THREADS * (u64)MUTEX_TEST_ITERS, state.count);

	sk_mutex_destroy(mutex);
}
#endif /* SK_TESTS */
