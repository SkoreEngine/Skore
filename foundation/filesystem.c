#include "filesystem.h"

#include "allocator.h"

#include <string.h>

struct sk_filesystem_context_t {
	const sk_allocator_t* allocator;
};

sk_filesystem_context_t* sk_filesystem_context_create(const sk_allocator_t* allocator) {
	sk_filesystem_context_t* fs;

	if (allocator == NULL) {
		return NULL;
	}
	fs = (sk_filesystem_context_t*)allocator->alloc(allocator->instance, sizeof(sk_filesystem_context_t));
	if (fs == NULL) {
		return NULL;
	}
	memset(fs, 0, sizeof(*fs));
	fs->allocator = allocator;
	return fs;
}

void sk_filesystem_context_destroy(sk_filesystem_context_t* fs) {
	const sk_allocator_t* alloc;

	if (fs == NULL) {
		return;
	}
	alloc = fs->allocator;
	alloc->free(alloc->instance, fs);
}
