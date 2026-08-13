#pragma once

/**
 * @file resource_serialize.h
 * @brief Contract delta for APX-278 (apply onto today's `core/resource_serialize.h`).
 *
 * Every public `sk_resource_serialize_*` / `sk_resource_deserialize_*` function
 * gains `const sk_repository_api_t* repo_api` immediately after `repository`.
 * There is no process getter and no fallback. Callers pass
 * `app_api->repository_api(ctx)` or `assets_ctx->repo_api`.
 *
 * The two file I/O helpers additionally take `const sk_filesystem_api_t* fs`
 * (stateless table entries only: open/read/write/close/get_file_size). They
 * do not need `sk_filesystem_context_t`.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exact new signatures (other parameters and return codes unchanged):
 *
 * i32 sk_resource_serialize_json(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                sk_rid_t rid, sk_archive_writer_t* writer);
 * i32 sk_resource_deserialize_json(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                  sk_archive_reader_t* reader, sk_rid_t* out_rid);
 * i32 sk_resource_serialize_package_json(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                        sk_rid_t root_rid, sk_archive_writer_t* writer);
 * i32 sk_resource_deserialize_package_json(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                          sk_archive_reader_t* reader, sk_rid_t* out_root);
 * i32 sk_resource_serialize_json_alloc(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                      sk_rid_t rid, const sk_allocator_t* allocator,
 *                                      char** out_json, u32* out_size);
 * i32 sk_resource_deserialize_json_string(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                         sk_str_view_t json, const sk_allocator_t* allocator,
 *                                         sk_rid_t* out_rid);
 * i32 sk_resource_serialize_package_json_alloc(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                              sk_rid_t root_rid, const sk_allocator_t* allocator,
 *                                              char** out_json, u32* out_size);
 * i32 sk_resource_deserialize_package_json_string(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                                 sk_str_view_t json, const sk_allocator_t* allocator,
 *                                                 sk_rid_t* out_root);
 * i32 sk_resource_serialize_package_json_to_file(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                                sk_rid_t root_rid, const_chr_t path,
 *                                                const sk_filesystem_api_t* fs);
 * i32 sk_resource_deserialize_package_json_from_file(sk_repository_t* repository, const sk_repository_api_t* repo_api,
 *                                                    const_chr_t path, sk_rid_t* out_root,
 *                                                    const sk_filesystem_api_t* fs);
 */

#ifdef __cplusplus
}
#endif
