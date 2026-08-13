#pragma once

/**
 * @file resource_serialize.h
 * @brief JSON Serialize/Deserialize for repository resources (asset types).
 *
 * Implements the contract in docs/repository-assets-json-serialization-contract.md:
 * envelope keys (format / format_version / type / uuid / fields), field names
 * matching registered descriptors, UUID-encoded references (never RID integers),
 * package documents with a flat resources table, and non-zero i32 errors on
 * validation failure (no exceptions, no partial silent construction).
 *
 * Uses sk_archive_writer_t / sk_archive_reader_t (JSON backend via yyjson inside
 * sk-foundation). Callers typically init a JSON writer/reader, call these helpers, then
 * emit or destroy.
 *
 * ## Per-type field coverage (APX-186 / APX-193)
 *
 * Every registered repository asset type is (de)serialized by walking its
 * sk_resource_field_t descriptors — not by hand-written per-type tables.
 * JSON keys are the exact PascalCase descriptor names (Name, PathId, …).
 *
 * ## Reference / handle encoding (APX-194)
 *
 * In-memory handles are session-local RIDs (`sk_rid_t`); they are **never**
 * written to JSON. Cross-resource Reference, SubObject, ReferenceArray, and
 * SubObjectList fields encode as canonical UUID strings
 * (`%016llx-%016llx` of lo/hi), or are omitted when `SK_RID_ZERO`.
 *
 * On load, UUID strings resolve to live RIDs via `find_by_uuid`:
 * - **Package documents** use a two-pass algorithm (create every shell first,
 *   then apply refs) so forward references and mutual soft links resolve
 *   regardless of `resources[]` order. Missing targets return non-zero
 *   (`SK_RES_SER_MISSING_REF` class) and the load is rolled back — no silent
 *   null/dangling handles.
 * - **Single-resource documents** resolve when the target is already live;
 *   otherwise leave `SK_RID_ZERO` (self-contained package loads are required
 *   for graphs; single-doc dumps are not).
 *
 * ## Cycles, depth, and large graphs (APX-191)
 *
 * Package reachability is a **non-recursive BFS** over Reference / SubObject /
 * ReferenceArray / SubObjectList edges with a visited set. Soft-reference
 * cycles (self-ref, mutual A↔B, multi-node A→B→C→A) **terminate** and do not
 * stack-overflow; each resource is emitted once. Deep ownership chains and
 * large flat asset sets are bounded by heap, not call-stack depth.
 *
 * ## Threading (APX-191)
 *
 * These APIs **do not claim multi-thread safety**. Callers must serialize all
 * use of a given `sk_repository_t` with this module on a single thread (or
 * under an external lock). The repository itself supports lock-free concurrent
 * `read` with at most one exclusive `write` (see repository.h Concurrency);
 * resource (de)serialize mixes `read` / `write` / create and is not a
 * concurrent public surface — no internal locks are added here.
 *
 * ## Binary / non-UTF-8 data (APX-191)
 *
 * Arbitrary bytes belong in **Blob** fields (JSON array of 0..255 integers).
 * **String** fields are UTF-8 text (JSON string encoding). Non-UTF-8 binary
 * in String fields is **unsupported**. Embedded NULs cannot round-trip through
 * C string accessors. Buffer fields store only an opaque handle id (payload
 * out of band).
 *
 * ## Fields that cannot be fully represented under the contract
 *
 * | Kind / field | Representation | Note |
 * | --- | --- | --- |
 * | ResourceAsset.Type (NONE) | omitted from `fields` | Reserved parity slot; no JSON value |
 * | Buffer (OriginalData, Dependency.Data) | `{"id": <u64>}` only | Opaque handle; byte payload is out of band until a buffer layer lands |
 * | SubObjectList.prototype_removed | omitted in v1 | Editor/runtime override state, not durable asset JSON |
 * | Non-finite Float (NaN / Inf) | not representable | Standard JSON has no NaN/Inf; serialize returns non-zero |
 * | Non-UTF-8 String payloads | unsupported | Use Blob for arbitrary bytes |
 * | SubObject ownership cycles | unsupported | Hierarchy parent chain assumes a tree/DAG; soft REFERENCE cycles are fine |
 * | Vectors / quats / mat / color / enum | not used by asset types | Generic kinds reserved; not required for APX-186 types |
 * | Concurrent multi-thread (de)serialize | unsupported | Single-thread / external lock only (see Threading) |
 */

#include "common.h"
#include "repository.h"
#include "serialization.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_filesystem_api_t sk_filesystem_api_t;

/** Envelope format string for a single resource document. */
#define SK_RESOURCE_JSON_FORMAT "sk.resource"

/** Envelope format string for a package (multi-resource) document. */
#define SK_RESOURCE_PACKAGE_JSON_FORMAT "sk.resource_package"

/** Current format_version written by this module. */
#define SK_RESOURCE_JSON_FORMAT_VERSION 1u

/**
 * Serialize one live resource into @p writer as a single sk.resource object.
 * The writer must already be at map (root) scope. References / sub-objects are
 * encoded as UUID strings (or omitted when SK_RID_ZERO). Owned children are
 * not inlined; use sk_resource_serialize_package_json for a full graph.
 *
 * @param repository Repository owning @p rid (must not be NULL).
 * @param repo_api   Repository function table (must not be NULL).
 * @param rid        Live resource RID.
 * @param writer     Archive writer (typically JSON; must not be NULL).
 * @return 0 on success; non-zero on failure (unknown rid, OOM, writer error).
 */
i32 sk_resource_serialize_json(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t rid, sk_archive_writer_t* writer);

/**
 * Deserialize one sk.resource document from @p reader into @p repository.
 * Creates (or reuses via UUID) a resource, then applies fields. Reference
 * UUIDs resolve via find_by_uuid; missing targets become SK_RID_ZERO (single-
 * document loads are not required to be self-contained).
 *
 * @param repository Repository (must not be NULL; types must already be
 *                   registered).
 * @param repo_api   Repository function table (must not be NULL).
 * @param reader     Archive reader over a JSON object root (must not be NULL).
 * @param out_rid    Receives the created/loaded RID (must not be NULL).
 * @return 0 on success; non-zero on bad format, unsupported version, unknown
 *         type, OOM, or field apply failure. On failure @p out_rid is
 *         SK_RID_ZERO. A resource shell created by this call is destroyed on
 *         failure so the repository is not left partially mutated. UUID-
 *         idempotent reuse of an already-live resource discards the write view
 *         without committing, so pre-existing data is unchanged.
 */
i32 sk_resource_deserialize_json(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_archive_reader_t* reader, sk_rid_t* out_rid);

/**
 * Serialize @p root_rid and every resource reachable through Reference,
 * ReferenceArray, SubObject, and SubObjectList fields into a package document
 * (format sk.resource_package, flat resources array, BFS reachable order
 * from the root — root first, then children in field-walk order).
 *
 * @param repository Repository (must not be NULL).
 * @param repo_api   Repository function table (must not be NULL).
 * @param root_rid   Package root (or any resource graph root).
 * @param writer     Archive writer at map scope (must not be NULL).
 * @return 0 on success; non-zero on failure.
 */
i32 sk_resource_serialize_package_json(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t root_rid, sk_archive_writer_t* writer);

/**
 * Deserialize a package document: create every resource in resources[], then
 * apply fields. Missing reference UUID targets that are not live in the
 * repository fail with a non-zero code (package loads are self-contained).
 * The load is transactional: creates and field commits are recorded on an
 * internal undo scope and rolled back on any failure so the repository is not
 * left partially mutated (pre-load resource set and field values restored).
 *
 * @param repository Repository (must not be NULL).
 * @param repo_api   Repository function table (must not be NULL).
 * @param reader     Archive reader over a package JSON object (must not be NULL).
 * @param out_root   Receives the root resource RID (must not be NULL).
 * @return 0 on success; non-zero on validation / resolution failure.
 */
i32 sk_resource_deserialize_package_json(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_archive_reader_t* reader, sk_rid_t* out_root);

/**
 * Convenience: serialize one resource to a heap JSON string via the JSON
 * archive backend. Caller frees @p out_json with @p allocator.
 *
 * @param repository Repository (must not be NULL).
 * @param rid        Live resource.
 * @param allocator  Allocator for the output string (must not be NULL).
 * @param out_json   Receives NUL-terminated JSON (must not be NULL).
 * @param out_size   Optional byte length excluding NUL.
 * @return 0 on success; non-zero on failure (@p *out_json left NULL).
 */
i32 sk_resource_serialize_json_alloc(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t rid, const sk_allocator_t* allocator, char** out_json,
									 u32* out_size);

/**
 * Convenience: deserialize one resource from a JSON string view.
 *
 * @param repository Repository (must not be NULL).
 * @param json       JSON text (object root).
 * @param allocator  Allocator for the temporary reader (must not be NULL).
 * @param out_rid    Receives the loaded RID (must not be NULL).
 * @return 0 on success; non-zero on parse / validation failure.
 */
i32 sk_resource_deserialize_json_string(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_str_view_t json, const sk_allocator_t* allocator, sk_rid_t* out_rid);

/**
 * Convenience: package serialize to a heap JSON string. Caller frees @p out_json.
 */
i32 sk_resource_serialize_package_json_alloc(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t root_rid, const sk_allocator_t* allocator, char** out_json,
											 u32* out_size);

/**
 * Convenience: package deserialize from a JSON string view.
 */
i32 sk_resource_deserialize_package_json_string(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_str_view_t json, const sk_allocator_t* allocator,
												sk_rid_t* out_root);

/**
 * Serialize a package graph to a single JSON file at @p path (creates/truncates).
 * Uses the host filesystem API (requires sk-foundation linked).
 *
 * @param repository Repository (must not be NULL).
 * @param repo_api   Repository function table (must not be NULL).
 * @param root_rid   Package root (or graph root).
 * @param path       UTF-8 destination file path (must not be NULL/empty).
 * @param fs         Filesystem table (stateless entries; must not be NULL).
 * @return 0 on success; non-zero on serialize or I/O failure.
 */
i32 sk_resource_serialize_package_json_to_file(sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t root_rid, const_chr_t path,
											   const sk_filesystem_api_t* fs);

/**
 * Deserialize a package graph from a JSON file at @p path.
 * Missing path, unreadable file, parse errors, and validation failures all
 * return non-zero; on failure @p out_root is SK_RID_ZERO and the repository is
 * not left partially mutated (same transaction rules as the string API).
 *
 * @param repository Repository (must not be NULL; types must be registered).
 * @param repo_api   Repository function table (must not be NULL).
 * @param path       UTF-8 source file path (must not be NULL/empty).
 * @param out_root   Receives the root resource RID (must not be NULL).
 * @param fs         Filesystem table (stateless entries; must not be NULL).
 * @return 0 on success; non-zero on I/O / parse / validation failure.
 */
i32 sk_resource_deserialize_package_json_from_file(sk_repository_t* repository, const sk_repository_api_t* repo_api, const_chr_t path, sk_rid_t* out_root,
												   const sk_filesystem_api_t* fs);

#ifdef __cplusplus
}
#endif
