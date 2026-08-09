#include "serialization.h"

#include "array.h"

#include <stdint.h> /* UINT64_MAX */
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Binary writer                                                             */
/* ------------------------------------------------------------------------- */

/** One scope on the writer stack; init = data.count before the size prefix. */
typedef struct binary_stack_frame_t {
	u64 init;
} binary_stack_frame_t;

/** Growing byte buffer (u64 sizes so the u64-prefixed format is not capped). */
typedef struct byte_buffer_t {
	u8* data;
	u64 count;
	u64 capacity;
	u8 oom;
} byte_buffer_t;

typedef struct binary_writer_ctx_t {
	byte_buffer_t data;
	SK_ARRAY(binary_stack_frame_t) stack;
	const sk_allocator_t* allocator;
} binary_writer_ctx_t;

static void byte_buffer_reserve(byte_buffer_t* buf, const sk_allocator_t* allocator, u64 extra) {
	if (buf->oom || buf->count + extra <= buf->capacity) {
		return;
	}
	u64 new_cap = buf->capacity == 0u ? 1024u : buf->capacity;
	while (new_cap < buf->count + extra) {
		new_cap *= 2u;
	}
	u8* new_data = (u8*)allocator->realloc(allocator->instance, buf->data, new_cap);
	if (new_data == NULL) {
		buf->oom = 1;
		return;
	}
	buf->data = new_data;
	buf->capacity = new_cap;
}

static void byte_buffer_append(byte_buffer_t* buf, const sk_allocator_t* allocator, const_ptr_t src, u64 size) {
	byte_buffer_reserve(buf, allocator, size);
	if (buf->oom) {
		return;
	}
	if (size > 0u) {
		memcpy(buf->data + buf->count, src, size);
	}
	buf->count += size;
}

static void binary_writer_write_name(binary_writer_ctx_t* ctx, sk_str_view_t name) {
	u32 name_size = name.size;
	byte_buffer_append(&ctx->data, ctx->allocator, &name_size, sizeof(name_size));
	byte_buffer_append(&ctx->data, ctx->allocator, name.data, name_size);
}

static void binary_writer_write_value(binary_writer_ctx_t* ctx, const_ptr_t value, u64 size) {
	u64 value_size = size;
	byte_buffer_append(&ctx->data, ctx->allocator, &value_size, sizeof(value_size));
	byte_buffer_append(&ctx->data, ctx->allocator, value, size);
}

static void binary_writer_write_map_data(binary_writer_ctx_t* ctx, sk_str_view_t name, const_ptr_t value, u64 size) {
	binary_writer_write_name(ctx, name);
	binary_writer_write_value(ctx, value, size);
}

static void binary_writer_write_bool(void_ptr_t instance, sk_str_view_t name, i32 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	u8 b = (u8)(value != 0);
	binary_writer_write_map_data(ctx, name, &b, 1u);
}

static void binary_writer_write_int(void_ptr_t instance, sk_str_view_t name, i64 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_map_data(ctx, name, &value, sizeof(value));
}

static void binary_writer_write_uint(void_ptr_t instance, sk_str_view_t name, u64 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_map_data(ctx, name, &value, sizeof(value));
}

static void binary_writer_write_float(void_ptr_t instance, sk_str_view_t name, f64 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_map_data(ctx, name, &value, sizeof(value));
}

static void binary_writer_write_string(void_ptr_t instance, sk_str_view_t name, sk_str_view_t value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_map_data(ctx, name, value.data, value.size);
}

static void binary_writer_write_blob(void_ptr_t instance, sk_str_view_t name, const_ptr_t data, u64 size) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_map_data(ctx, name, data, size);
}

static void binary_writer_add_bool(void_ptr_t instance, i32 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	u8 b = (u8)(value != 0);
	binary_writer_write_value(ctx, &b, 1u);
}

static void binary_writer_add_int(void_ptr_t instance, i64 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_value(ctx, &value, sizeof(value));
}

static void binary_writer_add_uint(void_ptr_t instance, u64 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_value(ctx, &value, sizeof(value));
}

static void binary_writer_add_float(void_ptr_t instance, f64 value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_value(ctx, &value, sizeof(value));
}

static void binary_writer_add_string(void_ptr_t instance, sk_str_view_t value) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_value(ctx, value.data, value.size);
}

static void binary_writer_add_blob(void_ptr_t instance, const_ptr_t data, u64 size) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_value(ctx, data, size);
}

static void binary_writer_begin_map(void_ptr_t instance) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	if (sk_array_push(&ctx->stack, (binary_stack_frame_t){.init = ctx->data.count}) != 0) {
		ctx->data.oom = 1;
		return;
	}
	u64 size = 0u;
	byte_buffer_append(&ctx->data, ctx->allocator, &size, sizeof(size));
}

static void binary_writer_begin_map_named(void_ptr_t instance, sk_str_view_t name) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_writer_write_name(ctx, name);
	binary_writer_begin_map(instance);
}

static void binary_writer_end_map(void_ptr_t instance) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	binary_stack_frame_t* frame = &ctx->stack.items[ctx->stack.count - 1u];
	if (!ctx->data.oom) {
		u64 size = ctx->data.count - frame->init - sizeof(u64);
		memcpy(ctx->data.data + frame->init, &size, sizeof(size));
	}
	ctx->stack.count--;
}

static void binary_writer_destroy(void_ptr_t instance) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)instance;
	const sk_allocator_t* allocator = ctx->allocator;
	if (ctx->data.data != NULL) {
		allocator->free(allocator->instance, ctx->data.data);
	}
	sk_array_free(&ctx->stack);
	allocator->free(allocator->instance, ctx);
}

i32 sk_binary_archive_writer_init(sk_archive_writer_t* out, const sk_allocator_t* allocator) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)allocator->alloc(allocator->instance, sizeof(*ctx));
	if (ctx == NULL) {
		return -1;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->allocator = allocator;
	sk_array_init(&ctx->stack, allocator);
	byte_buffer_reserve(&ctx->data, allocator, 1024u);

	memset(out, 0, sizeof(*out));
	out->instance = ctx;
	out->write_bool = binary_writer_write_bool;
	out->write_int = binary_writer_write_int;
	out->write_uint = binary_writer_write_uint;
	out->write_float = binary_writer_write_float;
	out->write_string = binary_writer_write_string;
	out->write_blob = binary_writer_write_blob;
	out->add_bool = binary_writer_add_bool;
	out->add_int = binary_writer_add_int;
	out->add_uint = binary_writer_add_uint;
	out->add_float = binary_writer_add_float;
	out->add_string = binary_writer_add_string;
	out->add_blob = binary_writer_add_blob;
	out->begin_map = binary_writer_begin_map;
	out->begin_map_named = binary_writer_begin_map_named;
	out->end_map = binary_writer_end_map;
	/* Binary seq == map: both are size-prefixed scopes in this format. */
	out->begin_seq = binary_writer_begin_map;
	out->begin_seq_named = binary_writer_begin_map_named;
	out->end_seq = binary_writer_end_map;
	out->destroy = binary_writer_destroy;
	return 0;
}

void sk_archive_writer_destroy(sk_archive_writer_t* writer) {
	writer->destroy(writer->instance);
	writer->instance = NULL;
}

sk_blob_view_t sk_binary_archive_writer_data(const sk_archive_writer_t* writer) {
	binary_writer_ctx_t* ctx = (binary_writer_ctx_t*)writer->instance;
	if (ctx->data.oom) {
		return SK_BLOB_VIEW_EMPTY;
	}
	return (sk_blob_view_t){ctx->data.data, ctx->data.count};
}

/* ------------------------------------------------------------------------- */
/* Binary reader                                                             */
/* ------------------------------------------------------------------------- */

typedef enum iter_type_t {
	ITER_TYPE_MAP = 1,
	ITER_TYPE_SEQ = 2,
} iter_type_t;

/** One scope on the reader stack (mirrors the writer's size-prefixed scopes). */
typedef struct binary_reader_frame_t {
	u64 begin;
	u64 end;
	u64 iter;
	iter_type_t type;
} binary_reader_frame_t;

typedef struct binary_reader_ctx_t {
	sk_blob_view_t data;
	SK_ARRAY(binary_reader_frame_t) stack;
} binary_reader_ctx_t;

/** A parsed map field: [u32 name size][name][u64 value size][value]. */
typedef struct binary_map_field_t {
	sk_str_view_t name;
	u64 pos;
	u64 size;
} binary_map_field_t;

/** A parsed value offset: where the value bytes start and how long they are. */
typedef struct binary_value_offset_t {
	u64 pos;
	u64 size;
} binary_value_offset_t;

static u32 read_u32_at(const u8* data, u64 offset) {
	u32 value;
	memcpy(&value, data + offset, sizeof(value));
	return value;
}

static u64 read_u64_at(const u8* data, u64 offset) {
	u64 value;
	memcpy(&value, data + offset, sizeof(value));
	return value;
}

static void read_map_field(const u8* data, u64 offset, binary_map_field_t* out) {
	u32 name_size = read_u32_at(data, offset);
	out->name = sk_str_view_make((const_chr_t)(data + offset + sizeof(u32)), name_size);
	out->size = read_u64_at(data, offset + sizeof(u32) + name_size);
	out->pos = offset + sizeof(u32) + (u64)name_size + sizeof(u64);
}

static i32 read_map_field_named(const binary_reader_ctx_t* ctx, sk_str_view_t name, binary_map_field_t* out) {
	const binary_reader_frame_t* frame = &ctx->stack.items[ctx->stack.count - 1u];
	u64 current = frame->begin;
	while (current < frame->end) {
		read_map_field(ctx->data.data, current, out);
		if (out->name.size == name.size && (name.size == 0u || memcmp(out->name.data, name.data, name.size) == 0)) {
			return 1;
		}
		current += sizeof(u32) + (u64)out->name.size + sizeof(u64) + out->size;
	}
	return 0;
}

static void read_value_offset(const u8* data, const binary_reader_frame_t* frame, binary_value_offset_t* out) {
	u64 offset = frame->iter;
	if (frame->type == ITER_TYPE_SEQ) {
		out->size = read_u64_at(data, offset);
		out->pos = offset + sizeof(u64);
	} else {
		binary_map_field_t field;
		read_map_field(data, offset, &field);
		out->size = field.size;
		out->pos = field.pos;
	}
}

static void binary_reader_read_offset(binary_reader_ctx_t* ctx, binary_value_offset_t* out) {
	binary_reader_frame_t* frame = &ctx->stack.items[ctx->stack.count - 1u];
	read_value_offset(ctx->data.data, frame, out);
}

static i32 binary_reader_read_bool(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (read_map_field_named(ctx, name, &field)) {
		return ctx->data.data[field.pos] != 0;
	}
	return 0;
}

static i64 binary_reader_read_int(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (read_map_field_named(ctx, name, &field)) {
		i64 value;
		memcpy(&value, ctx->data.data + field.pos, sizeof(value));
		return value;
	}
	return 0;
}

static u64 binary_reader_read_uint(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (read_map_field_named(ctx, name, &field)) {
		u64 value;
		memcpy(&value, ctx->data.data + field.pos, sizeof(value));
		return value;
	}
	return 0;
}

static f64 binary_reader_read_float(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (read_map_field_named(ctx, name, &field)) {
		f64 value;
		memcpy(&value, ctx->data.data + field.pos, sizeof(value));
		return value;
	}
	return 0.0;
}

static sk_str_view_t binary_reader_read_string(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (read_map_field_named(ctx, name, &field)) {
		return sk_str_view_make((const_chr_t)(ctx->data.data + field.pos), (u32)field.size);
	}
	return SK_STR_VIEW_EMPTY;
}

static i32 binary_reader_get_bool(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	return ctx->data.data[offset.pos] != 0;
}

static i64 binary_reader_get_int(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	i64 value;
	memcpy(&value, ctx->data.data + offset.pos, sizeof(value));
	return value;
}

static u64 binary_reader_get_uint(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	u64 value;
	memcpy(&value, ctx->data.data + offset.pos, sizeof(value));
	return value;
}

static f64 binary_reader_get_float(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	f64 value;
	memcpy(&value, ctx->data.data + offset.pos, sizeof(value));
	return value;
}

static sk_str_view_t binary_reader_get_string(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	return sk_str_view_make((const_chr_t)(ctx->data.data + offset.pos), (u32)offset.size);
}

static sk_blob_view_t binary_reader_get_blob(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	return (sk_blob_view_t){ctx->data.data + offset.pos, offset.size};
}

static void binary_reader_begin_seq(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	binary_reader_frame_t frame = {.begin = offset.pos, .end = offset.pos + offset.size, .iter = UINT64_MAX, .type = ITER_TYPE_SEQ};
	sk_array_push(&ctx->stack, frame);
}

static i32 binary_reader_begin_seq_named(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (!read_map_field_named(ctx, name, &field)) {
		return 0;
	}
	binary_reader_frame_t frame = {.begin = field.pos, .end = field.pos + field.size, .iter = UINT64_MAX, .type = ITER_TYPE_SEQ};
	sk_array_push(&ctx->stack, frame);
	return 1;
}

static i32 binary_reader_next_seq_entry(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_reader_frame_t* frame = &ctx->stack.items[ctx->stack.count - 1u];
	if (frame->end == 0u) {
		return 0;
	}
	if (frame->iter == UINT64_MAX) {
		frame->iter = frame->begin;
	} else {
		binary_value_offset_t offset = {0};
		read_value_offset(ctx->data.data, frame, &offset);
		frame->iter += sizeof(u64) + offset.size;
	}
	return frame->iter < frame->end;
}

static void binary_reader_begin_map(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_value_offset_t offset = {0};
	binary_reader_read_offset(ctx, &offset);
	binary_reader_frame_t frame = {.begin = offset.pos, .end = offset.pos + offset.size, .iter = UINT64_MAX, .type = ITER_TYPE_MAP};
	sk_array_push(&ctx->stack, frame);
}

static i32 binary_reader_begin_map_named(void_ptr_t instance, sk_str_view_t name) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_map_field_t field;
	if (!read_map_field_named(ctx, name, &field)) {
		return 0;
	}
	binary_reader_frame_t frame = {.begin = field.pos, .end = field.pos + field.size, .iter = UINT64_MAX, .type = ITER_TYPE_MAP};
	sk_array_push(&ctx->stack, frame);
	return 1;
}

static i32 binary_reader_next_map_entry(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_reader_frame_t* frame = &ctx->stack.items[ctx->stack.count - 1u];
	if (frame->iter == UINT64_MAX) {
		frame->iter = frame->begin;
	} else {
		binary_map_field_t field;
		read_map_field(ctx->data.data, frame->iter, &field);
		frame->iter += sizeof(u32) + (u64)field.name.size + sizeof(u64) + field.size;
	}
	return frame->iter < frame->end;
}

static sk_str_view_t binary_reader_get_current_key(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	binary_reader_frame_t* frame = &ctx->stack.items[ctx->stack.count - 1u];
	if (frame->iter == UINT64_MAX || frame->iter >= frame->end) {
		return SK_STR_VIEW_EMPTY;
	}
	binary_map_field_t field;
	read_map_field(ctx->data.data, frame->iter, &field);
	return field.name;
}

static void binary_reader_end_map(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	ctx->stack.count--;
}

static void binary_reader_destroy(void_ptr_t instance) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)instance;
	sk_array_free(&ctx->stack);
}

i32 sk_binary_archive_reader_init(sk_archive_reader_t* out, sk_blob_view_t data, const sk_allocator_t* allocator) {
	binary_reader_ctx_t* ctx = (binary_reader_ctx_t*)allocator->alloc(allocator->instance, sizeof(*ctx));
	if (ctx == NULL) {
		return -1;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->data = data;
	sk_array_init(&ctx->stack, allocator);

	binary_reader_frame_t root = {.begin = 0u, .end = data.size, .iter = UINT64_MAX, .type = ITER_TYPE_MAP};
	sk_array_push(&ctx->stack, root);

	memset(out, 0, sizeof(*out));
	out->instance = ctx;
	out->read_bool = binary_reader_read_bool;
	out->read_int = binary_reader_read_int;
	out->read_uint = binary_reader_read_uint;
	out->read_float = binary_reader_read_float;
	out->read_string = binary_reader_read_string;
	out->get_bool = binary_reader_get_bool;
	out->get_int = binary_reader_get_int;
	out->get_uint = binary_reader_get_uint;
	out->get_float = binary_reader_get_float;
	out->get_string = binary_reader_get_string;
	out->get_blob = binary_reader_get_blob;
	out->begin_seq = binary_reader_begin_seq;
	out->begin_seq_named = binary_reader_begin_seq_named;
	out->next_seq_entry = binary_reader_next_seq_entry;
	out->end_seq = binary_reader_end_map;
	out->begin_map = binary_reader_begin_map;
	out->begin_map_named = binary_reader_begin_map_named;
	out->next_map_entry = binary_reader_next_map_entry;
	out->get_current_key = binary_reader_get_current_key;
	out->end_map = binary_reader_end_map;
	out->destroy = binary_reader_destroy;
	return 0;
}

void sk_archive_reader_destroy(sk_archive_reader_t* reader) {
	reader->destroy(reader->instance);
	reader->instance = NULL;
}

/* ------------------------------------------------------------------------- */
/* Tests (port of IO::Serialization::BinaryFull / BinaryMapNavigation /      */
/* Binary + blob coverage)                                                   */
/* ------------------------------------------------------------------------- */
#ifdef SK_TESTS
#include "test.h"

static i32 sv_eq_cstr(sk_str_view_t view, const_chr_t s) {
	sk_str_view_t other = sk_str_view_cstr(s);
	return view.size == other.size && (view.size == 0u || memcmp(view.data, other.data, view.size) == 0);
}

static void binary_full_write(sk_archive_writer_t* writer) {
	writer->write_bool(writer->instance, sk_str_view_cstr("boolValue"), 1);
	writer->write_int(writer->instance, sk_str_view_cstr("intValue"), -123456789);
	writer->write_uint(writer->instance, sk_str_view_cstr("uintValue"), 987654321u);
	writer->write_float(writer->instance, sk_str_view_cstr("floatValue"), 3.14159265359);
	writer->write_string(writer->instance, sk_str_view_cstr("stringValue"), sk_str_view_cstr("Hello, Archive!"));

	writer->begin_map_named(writer->instance, sk_str_view_cstr("vector3"));
	writer->write_float(writer->instance, sk_str_view_cstr("x"), 1.5);
	writer->write_float(writer->instance, sk_str_view_cstr("y"), 2.5);
	writer->write_float(writer->instance, sk_str_view_cstr("z"), 3.5);
	writer->end_map(writer->instance);

	writer->begin_seq_named(writer->instance, sk_str_view_cstr("intArray"));
	writer->add_int(writer->instance, 1);
	writer->add_int(writer->instance, 2);
	writer->add_int(writer->instance, 3);
	writer->add_int(writer->instance, 4);
	writer->add_int(writer->instance, 5);
	writer->end_seq(writer->instance);

	writer->begin_seq_named(writer->instance, sk_str_view_cstr("entities"));
	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("name"), sk_str_view_cstr("Entity1"));
	writer->write_int(writer->instance, sk_str_view_cstr("id"), 1);
	writer->write_bool(writer->instance, sk_str_view_cstr("active"), 1);
	writer->begin_map_named(writer->instance, sk_str_view_cstr("position"));
	writer->write_float(writer->instance, sk_str_view_cstr("x"), 10.0);
	writer->write_float(writer->instance, sk_str_view_cstr("y"), 20.0);
	writer->write_float(writer->instance, sk_str_view_cstr("z"), 30.0);
	writer->end_map(writer->instance);
	writer->begin_seq_named(writer->instance, sk_str_view_cstr("tags"));
	writer->add_string(writer->instance, sk_str_view_cstr("player"));
	writer->add_string(writer->instance, sk_str_view_cstr("enemy"));
	writer->end_seq(writer->instance);
	writer->end_map(writer->instance);

	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("name"), sk_str_view_cstr("Entity2"));
	writer->write_int(writer->instance, sk_str_view_cstr("id"), 2);
	writer->write_bool(writer->instance, sk_str_view_cstr("active"), 0);
	writer->begin_map_named(writer->instance, sk_str_view_cstr("position"));
	writer->write_float(writer->instance, sk_str_view_cstr("x"), -10.0);
	writer->write_float(writer->instance, sk_str_view_cstr("y"), -20.0);
	writer->write_float(writer->instance, sk_str_view_cstr("z"), -30.0);
	writer->end_map(writer->instance);
	writer->begin_seq_named(writer->instance, sk_str_view_cstr("tags"));
	writer->add_string(writer->instance, sk_str_view_cstr("static"));
	writer->add_string(writer->instance, sk_str_view_cstr("obstacle"));
	writer->end_seq(writer->instance);
	writer->end_map(writer->instance);
	writer->end_seq(writer->instance);

	writer->begin_map_named(writer->instance, sk_str_view_cstr("gameState"));
	writer->write_string(writer->instance, sk_str_view_cstr("level"), sk_str_view_cstr("level1"));
	writer->write_int(writer->instance, sk_str_view_cstr("score"), 9000);
	writer->write_bool(writer->instance, sk_str_view_cstr("paused"), 0);
	writer->begin_seq_named(writer->instance, sk_str_view_cstr("players"));

	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("name"), sk_str_view_cstr("Player1"));
	writer->write_int(writer->instance, sk_str_view_cstr("health"), 100);
	writer->write_float(writer->instance, sk_str_view_cstr("speed"), 5.5);
	writer->begin_seq_named(writer->instance, sk_str_view_cstr("inventory"));
	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("item"), sk_str_view_cstr("Sword"));
	writer->write_int(writer->instance, sk_str_view_cstr("count"), 1);
	writer->end_map(writer->instance);
	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("item"), sk_str_view_cstr("Potion"));
	writer->write_int(writer->instance, sk_str_view_cstr("count"), 5);
	writer->end_map(writer->instance);
	writer->end_seq(writer->instance);
	writer->end_map(writer->instance);

	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("name"), sk_str_view_cstr("Player2"));
	writer->write_int(writer->instance, sk_str_view_cstr("health"), 85);
	writer->write_float(writer->instance, sk_str_view_cstr("speed"), 6.0);
	writer->begin_seq_named(writer->instance, sk_str_view_cstr("inventory"));
	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("item"), sk_str_view_cstr("Bow"));
	writer->write_int(writer->instance, sk_str_view_cstr("count"), 1);
	writer->end_map(writer->instance);
	writer->begin_map(writer->instance);
	writer->write_string(writer->instance, sk_str_view_cstr("item"), sk_str_view_cstr("Arrow"));
	writer->write_int(writer->instance, sk_str_view_cstr("count"), 30);
	writer->end_map(writer->instance);
	writer->end_seq(writer->instance);
	writer->end_map(writer->instance);

	writer->end_seq(writer->instance);
	writer->end_map(writer->instance);
}

static void binary_full_check(sk_archive_reader_t* reader) {
	TEST_ASSERT_TRUE(reader->read_bool(reader->instance, sk_str_view_cstr("boolValue")) != 0);
	TEST_ASSERT_EQUAL_INT64(-123456789, reader->read_int(reader->instance, sk_str_view_cstr("intValue")));
	TEST_ASSERT_EQUAL_UINT64(987654321u, reader->read_uint(reader->instance, sk_str_view_cstr("uintValue")));
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.14159265f, (float)reader->read_float(reader->instance, sk_str_view_cstr("floatValue")));
	TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("stringValue")), "Hello, Archive!"));

	TEST_ASSERT_TRUE(reader->begin_map_named(reader->instance, sk_str_view_cstr("vector3")));
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, (float)reader->read_float(reader->instance, sk_str_view_cstr("x")));
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.5f, (float)reader->read_float(reader->instance, sk_str_view_cstr("y")));
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.5f, (float)reader->read_float(reader->instance, sk_str_view_cstr("z")));
	reader->end_map(reader->instance);

	TEST_ASSERT_TRUE(reader->begin_seq_named(reader->instance, sk_str_view_cstr("intArray")));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	TEST_ASSERT_EQUAL_INT64(1, reader->get_int(reader->instance));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	TEST_ASSERT_EQUAL_INT64(2, reader->get_int(reader->instance));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	TEST_ASSERT_EQUAL_INT64(3, reader->get_int(reader->instance));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	TEST_ASSERT_EQUAL_INT64(4, reader->get_int(reader->instance));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	TEST_ASSERT_EQUAL_INT64(5, reader->get_int(reader->instance));
	TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
	reader->end_seq(reader->instance);

	TEST_ASSERT_TRUE(reader->begin_seq_named(reader->instance, sk_str_view_cstr("entities")));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	{
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("name")), "Entity1"));
		TEST_ASSERT_EQUAL_INT64(1, reader->read_int(reader->instance, sk_str_view_cstr("id")));
		TEST_ASSERT_TRUE(reader->read_bool(reader->instance, sk_str_view_cstr("active")) != 0);
		reader->begin_map_named(reader->instance, sk_str_view_cstr("position"));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("x")));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, 20.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("y")));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, 30.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("z")));
		reader->end_map(reader->instance);
		reader->begin_seq_named(reader->instance, sk_str_view_cstr("tags"));
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "player"));
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "enemy"));
		TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
		reader->end_seq(reader->instance);
		reader->end_map(reader->instance);
	}
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	{
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("name")), "Entity2"));
		TEST_ASSERT_EQUAL_INT64(2, reader->read_int(reader->instance, sk_str_view_cstr("id")));
		TEST_ASSERT_FALSE(reader->read_bool(reader->instance, sk_str_view_cstr("active")) != 0);
		reader->begin_map_named(reader->instance, sk_str_view_cstr("position"));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, -10.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("x")));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, -20.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("y")));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, -30.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("z")));
		reader->end_map(reader->instance);
		reader->begin_seq_named(reader->instance, sk_str_view_cstr("tags"));
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "static"));
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "obstacle"));
		TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
		reader->end_seq(reader->instance);
		reader->end_map(reader->instance);
	}
	TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
	reader->end_seq(reader->instance);

	reader->begin_map_named(reader->instance, sk_str_view_cstr("gameState"));
	TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("level")), "level1"));
	TEST_ASSERT_EQUAL_INT64(9000, reader->read_int(reader->instance, sk_str_view_cstr("score")));
	TEST_ASSERT_FALSE(reader->read_bool(reader->instance, sk_str_view_cstr("paused")) != 0);

	TEST_ASSERT_TRUE(reader->begin_seq_named(reader->instance, sk_str_view_cstr("players")));
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	{
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("name")), "Player1"));
		TEST_ASSERT_EQUAL_INT64(100, reader->read_int(reader->instance, sk_str_view_cstr("health")));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.5f, (float)reader->read_float(reader->instance, sk_str_view_cstr("speed")));
		reader->begin_seq_named(reader->instance, sk_str_view_cstr("inventory"));
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("item")), "Sword"));
		TEST_ASSERT_EQUAL_INT64(1, reader->read_int(reader->instance, sk_str_view_cstr("count")));
		reader->end_map(reader->instance);
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("item")), "Potion"));
		TEST_ASSERT_EQUAL_INT64(5, reader->read_int(reader->instance, sk_str_view_cstr("count")));
		reader->end_map(reader->instance);
		TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
		reader->end_seq(reader->instance);
		reader->end_map(reader->instance);
	}
	TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
	{
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("name")), "Player2"));
		TEST_ASSERT_EQUAL_INT64(85, reader->read_int(reader->instance, sk_str_view_cstr("health")));
		TEST_ASSERT_FLOAT_WITHIN(1e-6f, 6.0f, (float)reader->read_float(reader->instance, sk_str_view_cstr("speed")));
		reader->begin_seq_named(reader->instance, sk_str_view_cstr("inventory"));
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("item")), "Bow"));
		TEST_ASSERT_EQUAL_INT64(1, reader->read_int(reader->instance, sk_str_view_cstr("count")));
		reader->end_map(reader->instance);
		TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
		reader->begin_map(reader->instance);
		TEST_ASSERT_TRUE(sv_eq_cstr(reader->read_string(reader->instance, sk_str_view_cstr("item")), "Arrow"));
		TEST_ASSERT_EQUAL_INT64(30, reader->read_int(reader->instance, sk_str_view_cstr("count")));
		reader->end_map(reader->instance);
		TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
		reader->end_seq(reader->instance);
		reader->end_map(reader->instance);
	}
	TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
	reader->end_seq(reader->instance);
	reader->end_map(reader->instance);
}

static void binary_map_navigation_write(sk_archive_writer_t* writer) {
	writer->begin_map_named(writer->instance, sk_str_view_cstr("testMap"));
	writer->write_string(writer->instance, sk_str_view_cstr("stringKey"), sk_str_view_cstr("StringValue"));
	writer->write_int(writer->instance, sk_str_view_cstr("intKey"), 12345);
	writer->write_bool(writer->instance, sk_str_view_cstr("boolKey"), 1);
	writer->write_float(writer->instance, sk_str_view_cstr("floatKey"), 3.14);

	writer->begin_map_named(writer->instance, sk_str_view_cstr("nestedMap"));
	writer->write_string(writer->instance, sk_str_view_cstr("innerString"), sk_str_view_cstr("InnerValue"));
	writer->write_int(writer->instance, sk_str_view_cstr("innerInt"), 67890);
	writer->end_map(writer->instance);

	writer->begin_seq_named(writer->instance, sk_str_view_cstr("mapWithSeq"));
	writer->add_string(writer->instance, sk_str_view_cstr("Item1"));
	writer->add_string(writer->instance, sk_str_view_cstr("Item2"));
	writer->add_string(writer->instance, sk_str_view_cstr("Item3"));
	writer->end_seq(writer->instance);

	writer->end_map(writer->instance);
}

static void binary_map_navigation_check(sk_archive_reader_t* reader) {
	TEST_ASSERT_TRUE(reader->begin_map_named(reader->instance, sk_str_view_cstr("testMap")));

	u32 entry_count = 0u;
	u32 found_string = 0u;
	u32 found_int = 0u;
	u32 found_bool = 0u;
	u32 found_float = 0u;
	u32 found_nested_map = 0u;
	u32 found_seq = 0u;

	while (reader->next_map_entry(reader->instance)) {
		entry_count++;
		sk_str_view_t key = reader->get_current_key(reader->instance);

		if (sv_eq_cstr(key, "stringKey")) {
			found_string = 1u;
			TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "StringValue"));
		} else if (sv_eq_cstr(key, "intKey")) {
			found_int = 1u;
			TEST_ASSERT_EQUAL_INT64(12345, reader->get_int(reader->instance));
		} else if (sv_eq_cstr(key, "boolKey")) {
			found_bool = 1u;
			TEST_ASSERT_TRUE(reader->get_bool(reader->instance) != 0);
		} else if (sv_eq_cstr(key, "floatKey")) {
			found_float = 1u;
			TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.14f, (float)reader->get_float(reader->instance));
		} else if (sv_eq_cstr(key, "nestedMap")) {
			found_nested_map = 1u;
			reader->begin_map(reader->instance);
			u32 nested_count = 0u;
			u32 found_inner_string = 0u;
			u32 found_inner_int = 0u;
			while (reader->next_map_entry(reader->instance)) {
				nested_count++;
				sk_str_view_t nested_key = reader->get_current_key(reader->instance);
				if (sv_eq_cstr(nested_key, "innerString")) {
					found_inner_string = 1u;
					TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "InnerValue"));
				} else if (sv_eq_cstr(nested_key, "innerInt")) {
					found_inner_int = 1u;
					TEST_ASSERT_EQUAL_INT64(67890, reader->get_int(reader->instance));
				}
			}
			TEST_ASSERT_EQUAL_UINT32(2u, nested_count);
			TEST_ASSERT_TRUE(found_inner_string != 0u);
			TEST_ASSERT_TRUE(found_inner_int != 0u);
			reader->end_map(reader->instance);
		} else if (sv_eq_cstr(key, "mapWithSeq")) {
			found_seq = 1u;
			reader->begin_seq(reader->instance);
			TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
			TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "Item1"));
			TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
			TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "Item2"));
			TEST_ASSERT_TRUE(reader->next_seq_entry(reader->instance));
			TEST_ASSERT_TRUE(sv_eq_cstr(reader->get_string(reader->instance), "Item3"));
			TEST_ASSERT_FALSE(reader->next_seq_entry(reader->instance));
			reader->end_seq(reader->instance);
		}
	}

	TEST_ASSERT_EQUAL_UINT32(6u, entry_count);
	TEST_ASSERT_TRUE(found_string != 0u);
	TEST_ASSERT_TRUE(found_int != 0u);
	TEST_ASSERT_TRUE(found_bool != 0u);
	TEST_ASSERT_TRUE(found_float != 0u);
	TEST_ASSERT_TRUE(found_nested_map != 0u);
	TEST_ASSERT_TRUE(found_seq != 0u);
	reader->end_map(reader->instance);
}

SK_TEST(serialization_binary_full_roundtrip) {
	const sk_allocator_t* allocator = sk_allocator_default();
	sk_archive_writer_t writer;
	sk_archive_reader_t reader;

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_writer_init(&writer, allocator));
	binary_full_write(&writer);

	sk_blob_view_t data = sk_binary_archive_writer_data(&writer);
	TEST_ASSERT_TRUE(data.size > 0u);

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_reader_init(&reader, data, allocator));
	binary_full_check(&reader);

	sk_archive_reader_destroy(&reader);
	sk_archive_writer_destroy(&writer);
}

SK_TEST(serialization_binary_map_navigation) {
	const sk_allocator_t* allocator = sk_allocator_default();
	sk_archive_writer_t writer;
	sk_archive_reader_t reader;

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_writer_init(&writer, allocator));
	binary_map_navigation_write(&writer);

	sk_blob_view_t data = sk_binary_archive_writer_data(&writer);
	TEST_ASSERT_TRUE(data.size > 0u);

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_reader_init(&reader, data, allocator));
	binary_map_navigation_check(&reader);

	sk_archive_reader_destroy(&reader);
	sk_archive_writer_destroy(&writer);
}

SK_TEST(serialization_binary_named_map_seq) {
	const sk_allocator_t* allocator = sk_allocator_default();
	sk_archive_writer_t writer;
	sk_archive_reader_t reader;

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_writer_init(&writer, allocator));
	writer.write_bool(writer.instance, sk_str_view_cstr("testbool"), 1);

	writer.begin_seq_named(writer.instance, sk_str_view_cstr("seq"));
	writer.add_int(writer.instance, 3);
	writer.add_int(writer.instance, 4);
	writer.add_int(writer.instance, 5);
	writer.end_seq(writer.instance);

	writer.begin_map_named(writer.instance, sk_str_view_cstr("map"));
	writer.begin_map_named(writer.instance, sk_str_view_cstr("another-map"));
	writer.write_string(writer.instance, sk_str_view_cstr("zzzz"), sk_str_view_cstr("zzzzzzzzzzzzz"));
	writer.end_map(writer.instance);
	writer.write_string(writer.instance, sk_str_view_cstr("huh"), sk_str_view_cstr("huhhuh"));
	writer.end_map(writer.instance);

	writer.write_string(writer.instance, sk_str_view_cstr("testString"), sk_str_view_cstr("blahblahbbasdasd"));

	sk_blob_view_t data = sk_binary_archive_writer_data(&writer);
	TEST_ASSERT_TRUE(data.size > 0u);

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_reader_init(&reader, data, allocator));
	TEST_ASSERT_TRUE(sv_eq_cstr(reader.read_string(reader.instance, sk_str_view_cstr("testString")), "blahblahbbasdasd"));
	TEST_ASSERT_TRUE(reader.read_bool(reader.instance, sk_str_view_cstr("testbool")) != 0);

	TEST_ASSERT_TRUE(reader.begin_seq_named(reader.instance, sk_str_view_cstr("seq")));
	TEST_ASSERT_TRUE(reader.next_seq_entry(reader.instance));
	TEST_ASSERT_EQUAL_INT64(3, reader.get_int(reader.instance));
	TEST_ASSERT_TRUE(reader.next_seq_entry(reader.instance));
	TEST_ASSERT_EQUAL_INT64(4, reader.get_int(reader.instance));
	TEST_ASSERT_TRUE(reader.next_seq_entry(reader.instance));
	TEST_ASSERT_EQUAL_INT64(5, reader.get_int(reader.instance));
	TEST_ASSERT_FALSE(reader.next_seq_entry(reader.instance));
	reader.end_seq(reader.instance);

	TEST_ASSERT_TRUE(reader.begin_map_named(reader.instance, sk_str_view_cstr("map")));
	TEST_ASSERT_TRUE(sv_eq_cstr(reader.read_string(reader.instance, sk_str_view_cstr("huh")), "huhhuh"));
	TEST_ASSERT_TRUE(reader.begin_map_named(reader.instance, sk_str_view_cstr("another-map")));
	TEST_ASSERT_TRUE(sv_eq_cstr(reader.read_string(reader.instance, sk_str_view_cstr("zzzz")), "zzzzzzzzzzzzz"));
	reader.end_map(reader.instance);
	reader.end_map(reader.instance);

	sk_archive_reader_destroy(&reader);
	sk_archive_writer_destroy(&writer);
}

SK_TEST(serialization_binary_blob_roundtrip) {
	const sk_allocator_t* allocator = sk_allocator_default();
	sk_archive_writer_t writer;
	sk_archive_reader_t reader;
	const u8 blob[] = {0x00u, 0x01u, 0x02u, 0x7fu, 0xffu, 0x80u};

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_writer_init(&writer, allocator));
	writer.write_string(writer.instance, sk_str_view_cstr("name"), sk_str_view_cstr("blobtest"));
	writer.write_blob(writer.instance, sk_str_view_cstr("payload"), blob, sizeof(blob));

	sk_blob_view_t data = sk_binary_archive_writer_data(&writer);
	TEST_ASSERT_TRUE(data.size > 0u);

	TEST_ASSERT_EQUAL_INT(0, sk_binary_archive_reader_init(&reader, data, allocator));
	u32 entries = 0u;
	u32 found_blob = 0u;
	while (reader.next_map_entry(reader.instance)) {
		entries++;
		sk_str_view_t key = reader.get_current_key(reader.instance);
		if (sv_eq_cstr(key, "payload")) {
			found_blob = 1u;
			sk_blob_view_t got = reader.get_blob(reader.instance);
			TEST_ASSERT_EQUAL_UINT64(sizeof(blob), got.size);
			TEST_ASSERT_EQUAL_MEMORY(blob, got.data, sizeof(blob));
		} else {
			TEST_ASSERT_TRUE(sv_eq_cstr(reader.get_string(reader.instance), "blobtest"));
		}
	}
	TEST_ASSERT_EQUAL_UINT32(2u, entries);
	TEST_ASSERT_TRUE(found_blob != 0u);

	sk_archive_reader_destroy(&reader);
	sk_archive_writer_destroy(&writer);
}
#endif /* SK_TESTS */
