#pragma once

/**
 * @file render_pipeline.h
 * @brief Host-side frame loop over the render_graph plugin (C++ main port).
 *
 * Ports `RenderPipelineContext` / `RenderPipeline::BuildRenderGraph` from
 * C++ main (audit §2.1–2.2). Hosts acquire the plugin table via the app
 * registry, own one `sk_render_graph_t`, and rebuild passes every frame:
 *
 *   begin → build_fn (pass/resource declaration) → execute
 *
 * There is no C++ dual path: call sites use only `sk_render_graph_api_t`.
 * Pipeline feature passes (bloom, shadows, …) register as build callbacks
 * when their C ports land; this header is the shared frame orchestration.
 */

#include "render_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Per-frame graph builder. Mirrors C++ `RenderPipeline::BuildRenderGraph`
 * / `DefaultPipelinePass::BuildRenderGraph`: declare resources and passes
 * on the graph for the current frame (after `api->begin`).
 *
 * @param api   Render-graph module table (non-null).
 * @param graph Live graph (non-null, in-frame after begin).
 * @param user  Opaque user pointer from the context.
 */
typedef void (*sk_rg_build_fn)(const sk_render_graph_api_t* api, sk_render_graph_t* graph, void_ptr_t user);

/**
 * Host-owned frame context (ports C++ `RenderPipelineContext`).
 * Not an `sk_*_api_t` — multi-instance, not a process-global module table.
 */
typedef struct sk_render_pipeline_context_t {
	const sk_render_graph_api_t* api;
	sk_render_graph_t* graph;
	sk_rg_build_fn build;
	void_ptr_t build_user;
} sk_render_pipeline_context_t;

/**
 * Create a graph on @p device and bind @p build as the per-frame builder.
 * @p api must be the table from `app_api->get_api(..., SK_RENDER_GRAPH_API_TYPE_ID)`.
 * @return 0 on success, non-zero if create failed.
 */
static inline i32 sk_render_pipeline_context_create(sk_render_pipeline_context_t* ctx, const sk_render_graph_api_t* api, sk_render_device_t device, sk_rg_build_fn build,
													void_ptr_t build_user) {
	sk_render_graph_t* graph;

	ctx->api = api;
	ctx->graph = NULL;
	ctx->build = build;
	ctx->build_user = build_user;

	graph = api->create(device);
	if (graph == NULL) {
		return -1;
	}
	ctx->graph = graph;
	return 0;
}

/**
 * Same as create, with explicit memory capacities (tests / warm capacity).
 */
static inline i32 sk_render_pipeline_context_create_with_config(sk_render_pipeline_context_t* ctx, const sk_render_graph_api_t* api, sk_render_device_t device,
																const sk_rg_memory_config_t* config, sk_rg_build_fn build, void_ptr_t build_user) {
	sk_render_graph_t* graph;

	ctx->api = api;
	ctx->graph = NULL;
	ctx->build = build;
	ctx->build_user = build_user;

	graph = api->create_with_config(device, config);
	if (graph == NULL) {
		return -1;
	}
	ctx->graph = graph;
	return 0;
}

/**
 * Destroy the graph and clear the context. Safe if create never succeeded.
 */
static inline void sk_render_pipeline_context_destroy(sk_render_pipeline_context_t* ctx) {
	if (ctx->graph != NULL && ctx->api != NULL) {
		ctx->api->destroy(ctx->graph);
	}
	ctx->graph = NULL;
	ctx->api = NULL;
	ctx->build = NULL;
	ctx->build_user = NULL;
}

/**
 * One frame: begin → build → execute (ports `RenderPipelineContext::Execute`).
 * @p scene is the optional opaque scene pointer passed to begin/record.
 * @p cmd is the RHI command buffer for this frame.
 */
static inline void sk_render_pipeline_context_execute(sk_render_pipeline_context_t* ctx, sk_command_buffer_t cmd, void_ptr_t scene) {
	const sk_render_graph_api_t* api = ctx->api;
	sk_render_graph_t* graph = ctx->graph;

	api->begin(graph, scene);
	if (ctx->build != NULL) {
		ctx->build(api, graph, ctx->build_user);
	}
	api->execute(graph, cmd);
}

/**
 * Look up the process-global render_graph table on the app registry.
 * Hosts must not link the plugin binary; only get_api after load.
 */
static inline const sk_render_graph_api_t* sk_render_graph_api_from_app(sk_app_context_t* context, const sk_app_api_t* host_api) {
	return (const sk_render_graph_api_t*)host_api->get_api(context, SK_RENDER_GRAPH_API_TYPE_ID);
}

#ifdef __cplusplus
}
#endif
