#pragma once

/**
 * @file serialization.h
 * @brief Archive (de)serialization abstraction + binary and JSON backends.
 *
 * C port of the main-branch ArchiveWriter / ArchiveReader interface. Writers
 * and readers are multi-instance function-pointer tables (same shape as
 * sk_allocator_t, not a process-global sk_*_api_t): callers hold a table value,
 * pass it and its opaque instance to every call, and can swap backends
 * (binary, json, ...) at the call site without changing serialization code.
 *
 * The binary backend reproduces the main custom binary format: every map field
 * is `[u32 name size][name][u64 value size][value]`, every map/seq scope is
 * prefixed by a u64 byte-count placeholder patched on End, and a bool is a
 * single byte (0/1). Values are written in host byte order (matching main);
 * this is a fast custom format, not a portable interchange format.
 *
 * The JSON backend uses yyjson (mutable doc for writing, parse for reading)
 * privately inside sk-foundation: yyjson headers never appear in this public API.
 * Blobs are encoded as JSON arrays of byte values (0..255), matching main.
 * Emit the document as a pretty-printed string via
 * sk_json_archive_writer_emit_as_string.
 *
 * Not thread-safe: a writer/reader instance is owned by one thread at a time.
 */

#include "allocator.h"
#include "common.h"
#include "path.h" /* sk_str_view_t */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Non-owning byte range (binary payloads, blob reads, GetData).
 * @p data may be NULL when @p size is 0.
 */
typedef struct sk_blob_view_t {
	const u8* data;
	u64 size;
} sk_blob_view_t;

/** Empty blob view. */
#define SK_BLOB_VIEW_EMPTY ((sk_blob_view_t){NULL, 0u})

/*
 * Writer table. Backends keep state in the opaque instance and implement every
 * entry. All values are written into the current scope: Write* for named fields
 * (map scope), Add* for anonymous values (seq scope).
 */
typedef struct sk_archive_writer_t {
	/** Opaque backend state (allocated by sk_binary_archive_writer_init). */
	void_ptr_t instance;

	/** Write a named bool field (0/1, one byte on the wire). */
	void (*write_bool)(void_ptr_t instance, sk_str_view_t name, i32 value);
	/** Write a named 64-bit signed integer field. */
	void (*write_int)(void_ptr_t instance, sk_str_view_t name, i64 value);
	/** Write a named 64-bit unsigned integer field. */
	void (*write_uint)(void_ptr_t instance, sk_str_view_t name, u64 value);
	/** Write a named 64-bit float field. */
	void (*write_float)(void_ptr_t instance, sk_str_view_t name, f64 value);
	/** Write a named string field (raw bytes, not null-terminated on the wire). */
	void (*write_string)(void_ptr_t instance, sk_str_view_t name, sk_str_view_t value);
	/** Write a named blob field. */
	void (*write_blob)(void_ptr_t instance, sk_str_view_t name, const_ptr_t data, u64 size);

	/** Append an anonymous bool value to the current sequence. */
	void (*add_bool)(void_ptr_t instance, i32 value);
	/** Append an anonymous 64-bit signed integer value to the current sequence. */
	void (*add_int)(void_ptr_t instance, i64 value);
	/** Append an anonymous 64-bit unsigned integer value to the current sequence. */
	void (*add_uint)(void_ptr_t instance, u64 value);
	/** Append an anonymous 64-bit float value to the current sequence. */
	void (*add_float)(void_ptr_t instance, f64 value);
	/** Append an anonymous string value to the current sequence. */
	void (*add_string)(void_ptr_t instance, sk_str_view_t value);
	/** Append an anonymous blob value to the current sequence. */
	void (*add_blob)(void_ptr_t instance, const_ptr_t data, u64 size);

	/** Open an anonymous map scope. */
	void (*begin_map)(void_ptr_t instance);
	/** Open a named map field scope. */
	void (*begin_map_named)(void_ptr_t instance, sk_str_view_t name);
	/** Close the current map scope. */
	void (*end_map)(void_ptr_t instance);

	/** Open an anonymous sequence scope. */
	void (*begin_seq)(void_ptr_t instance);
	/** Open a named sequence field scope. */
	void (*begin_seq_named)(void_ptr_t instance, sk_str_view_t name);
	/** Close the current sequence scope. */
	void (*end_seq)(void_ptr_t instance);

	/** Free backend state (owned by the backend's allocator). */
	void (*destroy)(void_ptr_t instance);
} sk_archive_writer_t;

/*
 * Reader table. Read* look up a named field in the current map scope; Get*
 * read the value at the current iteration position (after Next*). Borrowed
 * string/blob views stay valid until the reader is destroyed (they point into
 * the underlying data).
 */
typedef struct sk_archive_reader_t {
	/** Opaque backend state (allocated by sk_binary_archive_reader_init). */
	void_ptr_t instance;

	/** Read a named bool field; 0/1 when present, 0 when the name is absent. */
	i32 (*read_bool)(void_ptr_t instance, sk_str_view_t name);
	/** Read a named i64 field; 0 when the name is absent. */
	i64 (*read_int)(void_ptr_t instance, sk_str_view_t name);
	/** Read a named u64 field; 0 when the name is absent. */
	u64 (*read_uint)(void_ptr_t instance, sk_str_view_t name);
	/** Read a named f64 field; 0.0 when the name is absent. */
	f64 (*read_float)(void_ptr_t instance, sk_str_view_t name);
	/** Read a named string field; empty view when the name is absent. */
	sk_str_view_t (*read_string)(void_ptr_t instance, sk_str_view_t name);

	/** Get the bool at the current iteration position. */
	i32 (*get_bool)(void_ptr_t instance);
	/** Get the i64 at the current iteration position. */
	i64 (*get_int)(void_ptr_t instance);
	/** Get the u64 at the current iteration position. */
	u64 (*get_uint)(void_ptr_t instance);
	/** Get the f64 at the current iteration position. */
	f64 (*get_float)(void_ptr_t instance);
	/** Get the string at the current iteration position (borrowed view). */
	sk_str_view_t (*get_string)(void_ptr_t instance);
	/** Get the blob at the current iteration position (borrowed view). */
	sk_blob_view_t (*get_blob)(void_ptr_t instance);

	/** Open the anonymous sequence value at the current position. */
	void (*begin_seq)(void_ptr_t instance);
	/** Open a named sequence field; 0 when the name is absent. */
	i32 (*begin_seq_named)(void_ptr_t instance, sk_str_view_t name);
	/** Advance to the next sequence entry; 0 when the sequence is exhausted. */
	i32 (*next_seq_entry)(void_ptr_t instance);
	/** Close the current sequence scope. */
	void (*end_seq)(void_ptr_t instance);

	/** Open the anonymous map value at the current position. */
	void (*begin_map)(void_ptr_t instance);
	/** Open a named map field; 0 when the name is absent. */
	i32 (*begin_map_named)(void_ptr_t instance, sk_str_view_t name);
	/** Advance to the next map entry; 0 when the map is exhausted. */
	i32 (*next_map_entry)(void_ptr_t instance);
	/** Name of the current map entry (after NextMapEntry). */
	sk_str_view_t (*get_current_key)(void_ptr_t instance);
	/** Close the current map scope. */
	void (*end_map)(void_ptr_t instance);

	/** Free backend state (owned by the backend's allocator). */
	void (*destroy)(void_ptr_t instance);
} sk_archive_reader_t;

/**
 * Initialize a binary archive writer (fills @p out; the table owns an opaque
 * heap context allocated with @p allocator). The writer mirrors the main custom
 * binary format exactly. Destroy with sk_archive_writer_destroy.
 *
 * @param out       Destination writer table (must not be NULL).
 * @param allocator Allocator for the buffer + stack (must not be NULL).
 * @return 0 on success, non-zero on allocation failure.
 */
i32 sk_binary_archive_writer_init(sk_archive_writer_t* out, const sk_allocator_t* allocator);

/**
 * Initialize a binary archive reader over @p data (borrowed; the caller keeps
 * @p data alive for the reader's lifetime). The root scope is the top-level
 * map, matching the writer's output.
 *
 * @param out       Destination reader table (must not be NULL).
 * @param data      Bytes produced by a binary writer (GetData).
 * @param allocator Allocator for the iteration stack (must not be NULL).
 * @return 0 on success, non-zero on allocation failure.
 */
i32 sk_binary_archive_reader_init(sk_archive_reader_t* out, sk_blob_view_t data, const sk_allocator_t* allocator);

/**
 * Destroy a writer (any backend) and free its instance.
 * @param writer Writer table; instance is cleared (must not be NULL).
 */
void sk_archive_writer_destroy(sk_archive_writer_t* writer);

/**
 * Destroy a reader (any backend) and free its instance.
 * @param reader Reader table; instance is cleared (must not be NULL).
 */
void sk_archive_reader_destroy(sk_archive_reader_t* reader);

/**
 * Raw bytes produced by a binary writer (size-prefixed map/seq format).
 * Borrowed view: valid until the writer is destroyed or more data is written.
 * Empty view if the writer hit an out-of-memory condition while writing.
 *
 * @param writer Binary writer table (must not be NULL).
 * @return The serialized buffer.
 */
sk_blob_view_t sk_binary_archive_writer_data(const sk_archive_writer_t* writer);

/**
 * Initialize a JSON archive writer (yyjson mutable document; root is an
 * object). Fills @p out with the same function-pointer table shape as the
 * binary backend so call sites can swap backends. Destroy with
 * sk_archive_writer_destroy.
 *
 * @param out       Destination writer table (must not be NULL).
 * @param allocator Allocator for the context, yyjson, and emit buffer.
 * @return 0 on success, non-zero on allocation failure.
 */
i32 sk_json_archive_writer_init(sk_archive_writer_t* out, const sk_allocator_t* allocator);

/**
 * Pretty-printed JSON text of the document (YYJSON_WRITE_PRETTY |
 * YYJSON_WRITE_ESCAPE_UNICODE), matching main EmitAsString. Borrowed view into
 * a buffer owned by the writer: valid until the next emit, further writes, or
 * destroy. Empty view on failure.
 *
 * @param writer JSON writer table (must not be NULL).
 * @return UTF-8 JSON text (not necessarily null-terminated in the view size).
 */
sk_str_view_t sk_json_archive_writer_emit_as_string(const sk_archive_writer_t* writer);

/**
 * Initialize a JSON archive reader over @p json (copied/parsed by yyjson; the
 * caller need not keep @p json after init). Root must be a JSON object. Uses
 * the same function-pointer table as the binary reader.
 *
 * @param out       Destination reader table (must not be NULL).
 * @param json      JSON text produced by a JSON writer (or compatible).
 * @param allocator Allocator for the context, parse tree, and blob decode buf.
 * @return 0 on success, non-zero on allocation or parse failure.
 */
i32 sk_json_archive_reader_init(sk_archive_reader_t* out, sk_str_view_t json, const sk_allocator_t* allocator);

#ifdef __cplusplus
}
#endif
