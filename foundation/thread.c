#include "thread.h"

#include "allocator.h"

#if defined(_WIN32)
/* Win32 threads: CreateThread / WaitForSingleObject / CloseHandle. */
#include <windows.h>
#else
#include <pthread.h>
#endif

struct sk_thread_t {
#if defined(_WIN32)
	HANDLE handle;
#else
	pthread_t handle;
#endif
};

/* Start-routine + argument packed for the single void* platform entry param.
 * Freed by the entry function after unpacking so it can live on the caller's
 * stack or in the default heap without an ownership race at spawn time. */
typedef struct sk_thread_start_t {
	sk_thread_routine_t routine;
	void_ptr_t arg;
} sk_thread_start_t;

#if defined(_WIN32)
static DWORD WINAPI sk_thread_win32_entry(LPVOID param) {
	sk_thread_start_t* start = (sk_thread_start_t*)param;
	sk_thread_routine_t routine = start->routine;
	void_ptr_t arg = start->arg;
	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, start);
	return (DWORD)routine(arg);
}
#else
static void* sk_thread_posix_entry(void* param) {
	sk_thread_start_t* start = (sk_thread_start_t*)param;
	sk_thread_routine_t routine = start->routine;
	void_ptr_t arg = start->arg;
	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, start);
	routine(arg);
	return NULL;
}
#endif

sk_thread_t* sk_thread_create(sk_thread_routine_t routine, void_ptr_t arg) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_thread_t* thread = (sk_thread_t*)alloc->alloc(alloc->instance, sizeof(sk_thread_t));
	if (thread == NULL) {
		return NULL;
	}

	sk_thread_start_t* start = (sk_thread_start_t*)alloc->alloc(alloc->instance, sizeof(sk_thread_start_t));
	if (start == NULL) {
		alloc->free(alloc->instance, thread);
		return NULL;
	}
	start->routine = routine;
	start->arg = arg;

#if defined(_WIN32)
	thread->handle = CreateThread(NULL, 0, sk_thread_win32_entry, start, 0, NULL);
	if (thread->handle == NULL) {
		alloc->free(alloc->instance, start);
		alloc->free(alloc->instance, thread);
		return NULL;
	}
#else
	int rc = pthread_create(&thread->handle, NULL, sk_thread_posix_entry, start);
	if (rc != 0) {
		alloc->free(alloc->instance, start);
		alloc->free(alloc->instance, thread);
		return NULL;
	}
#endif
	return thread;
}

i32 sk_thread_join(sk_thread_t* thread) {
#if defined(_WIN32)
	if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0) {
		return -1;
	}
	CloseHandle(thread->handle);
	thread->handle = NULL;
	return 0;
#else
	return pthread_join(thread->handle, NULL);
#endif
}

void sk_thread_destroy(sk_thread_t* thread) {
	if (thread == NULL) {
		return;
	}
#if defined(_WIN32)
	if (thread->handle != NULL) {
		CloseHandle(thread->handle);
	}
#endif
	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, thread);
}

#ifdef SK_TESTS
#include "test.h"
#include "mutex.h"

/* Test constants (plain names; local to this TU). */
#define THREAD_TEST_COUNT 4u
#define THREAD_TEST_ITERS 10000u

typedef struct thread_value_state_t {
	u64 value;
} thread_value_state_t;

static i32 thread_write_value(void_ptr_t param) {
	thread_value_state_t* s = (thread_value_state_t*)param;
	s->value = 42u;
	return 0;
}

SK_TEST(thread_create_join_destroy) {
	thread_value_state_t state = {.value = 0u};
	sk_thread_t* thread = sk_thread_create(thread_write_value, &state);
	TEST_ASSERT_NOT_NULL(thread);

	TEST_ASSERT_EQUAL_INT(0, sk_thread_join(thread));
	sk_thread_destroy(thread);
	sk_thread_destroy(NULL);

	TEST_ASSERT_EQUAL_UINT64(42u, state.value);
}

typedef struct thread_counter_state_t {
	sk_mutex_t* mutex;
	u64 count;
} thread_counter_state_t;

static i32 thread_counter_worker(void_ptr_t param) {
	thread_counter_state_t* s = (thread_counter_state_t*)param;
	for (u32 i = 0u; i < THREAD_TEST_ITERS; ++i) {
		sk_mutex_lock(s->mutex);
		s->count += 1u;
		sk_mutex_unlock(s->mutex);
	}
	return 0;
}

SK_TEST(thread_workers_serialize_increments) {
	sk_mutex_t* mutex = sk_mutex_create();
	TEST_ASSERT_NOT_NULL(mutex);

	thread_counter_state_t state = {.mutex = mutex, .count = 0u};
	sk_thread_t* threads[THREAD_TEST_COUNT];
	u32 spawned = 0u;
	for (u32 i = 0u; i < THREAD_TEST_COUNT; ++i) {
		threads[i] = sk_thread_create(thread_counter_worker, &state);
		if (threads[i] == NULL) {
			break;
		}
		spawned += 1u;
	}
	TEST_ASSERT_EQUAL_UINT32(THREAD_TEST_COUNT, spawned);
	for (u32 i = 0u; i < spawned; ++i) {
		TEST_ASSERT_EQUAL_INT(0, sk_thread_join(threads[i]));
		sk_thread_destroy(threads[i]);
	}

	TEST_ASSERT_EQUAL_UINT64((u64)THREAD_TEST_COUNT * (u64)THREAD_TEST_ITERS, state.count);

	sk_mutex_destroy(mutex);
}
#endif /* SK_TESTS */
