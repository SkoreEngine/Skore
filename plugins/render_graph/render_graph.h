#pragma once

/**
 * @file render_graph.h
 * @brief Render graph module API (fn-table).
 *
 * Implemented by the sk-render-graph plugin (SHARED, statically linked
 * sk-core). The plugin registers a static sk_render_graph_api_t on the app
 * context; hosts obtain it **only** via the app registry:
 *
 *   const sk_render_graph_api_t* graph =
 *       (const sk_render_graph_api_t*)app_api->get_api(
 *           ctx, SK_RENDER_GRAPH_API_TYPE_ID);
 *
 * Frame memory model (APX-150): each graph owns a linear bump frame arena
 * (reset, not freed, at begin) and persistent object/array pools that grow
 * only during setup or between frames. Steady-state frames never heap-allocate.
 * Mid-frame capacity failure is a defined error (no silent malloc).
 *
 * Pass sorting, barriers, and GPU resource creation land in later tasks.
 */

#include "app.h"
#include "common.h"
#include "render_device.h" /* handles + resource state / format enums */

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_render_graph_api_t in the app registry. */
#define SK_RENDER_GRAPH_API_TYPE_ID SK_TYPE_ID("sk.render_graph_api", 0xc77df474a355fb80ULL, 0x11b457b7c17d0220ULL)

/* ------------------------------------------------------------------ */
/* Enums                                                               */
/* ------------------------------------------------------------------ */

/** Pass dispatch kind (mirrors main RenderGraphPassType). */
typedef enum sk_rg_pass_type_t {
	SK_RG_PASS_COMPUTE = 0,
	SK_RG_PASS_GRAPHICS = 1,
	SK_RG_PASS_RAYTRACE = 2,
	SK_RG_PASS_TRANSFER = 3,
} sk_rg_pass_type_t;

/** Resource access declared on a pass dependency. */
typedef enum sk_rg_access_t {
	SK_RG_ACCESS_READ = 0,
	SK_RG_ACCESS_WRITE = 1,
	SK_RG_ACCESS_READ_WRITE = 2,
} sk_rg_access_t;

/* ------------------------------------------------------------------ */
/* Opaque objects                                                      */
/* ------------------------------------------------------------------ */

/** Frame graph instance owned by the module (create/destroy). */
typedef struct sk_render_graph_t sk_render_graph_t;

/** Pass builder handle valid for the current begin..execute frame. */
typedef struct sk_rg_pass_t sk_rg_pass_t;

/* ------------------------------------------------------------------ */
/* Descs (POD; name strings are non-owning views for the declare phase) */
/* ------------------------------------------------------------------ */

/**
 * Transient / persistent texture resource description.
 * extent width/height 0 → follow graph output size * scale.
 */
typedef struct sk_rg_texture_desc_t {
	sk_pixel_format_t format;
	sk_extent3d_t extent;
	f32 scale_x;
	f32 scale_y;
	u32 array_layers;
	u32 samples;
	u32 mip_levels;
	u32 usage; /* sk_resource_usage bits; 0 = infer from pass access */
	f32 clear_color[4];
	f32 clear_depth;
	i32 cubemap;
	i32 ping_pong;
	i32 persistent;
} sk_rg_texture_desc_t;

/** Buffer resource description. */
typedef struct sk_rg_buffer_desc_t {
	u64 size;
	u32 usage;
	i32 host_visible;
	i32 persistent_mapped;
	i32 per_frame;
	i32 ping_pong;
} sk_rg_buffer_desc_t;

/** Named subresource view of another graph texture. */
typedef struct sk_rg_view_desc_t {
	const_chr_t texture_name;
	sk_texture_view_type_t view_type;
	u32 base_mip_level;
	u32 mip_level_count;
	u32 base_array_layer;
	u32 array_layer_count;
} sk_rg_view_desc_t;

/** 2D pixel size for graph color/depth output (matches platform sk_extent_t layout). */
typedef struct sk_rg_extent_t {
	u32 width;
	u32 height;
} sk_rg_extent_t;

/* ------------------------------------------------------------------ */
/* Frame memory config + diagnostics                                   */
/* ------------------------------------------------------------------ */

/**
 * Result codes for the graph frame-memory substrate.
 * Mid-frame exhaustion is SK_RG_MEMORY_OUT_OF_SPACE (never silent malloc).
 */
typedef enum sk_rg_memory_result_t {
	SK_RG_MEMORY_OK = 0,
	/** Capacity exceeded while in-frame (or fixed-capacity path). */
	SK_RG_MEMORY_OUT_OF_SPACE = 1,
	/** Growth attempted while a frame is active. */
	SK_RG_MEMORY_GROW_BLOCKED = 2,
	/** Heap allocation failed during an allowed (out-of-frame) growth. */
	SK_RG_MEMORY_OOM = 3,
} sk_rg_memory_result_t;

/**
 * Capacities reserved at graph creation (or grown only between frames).
 * Zero fields are replaced with plugin defaults. Remembered so that after
 * warm-up / explicit growth, steady-state frames do not allocate again.
 */
typedef struct sk_rg_memory_config_t {
	/** Linear frame-arena bytes for per-frame scratch (strings, sort temps). */
	u64 frame_arena_bytes;
	/** Free-list pool slots for pass nodes. */
	u32 pass_capacity;
	/** Free-list pool slots for resource descriptors. */
	u32 resource_capacity;
	/** Growable-only-outside-frame edge list capacity (elements). */
	u32 edge_capacity;
	/** Growable-only-outside-frame barrier list capacity (elements). */
	u32 barrier_capacity;
} sk_rg_memory_config_t;

/**
 * Internal stats/diagnostics for the frame-memory model (tests + tooling).
 * Filled by get_memory_stats; all fields are snapshots of current state.
 */
typedef struct sk_rg_memory_stats_t {
	u64 frame_arena_capacity;
	u64 frame_arena_used;
	u64 frame_arena_high_water;
	u32 pass_capacity;
	u32 pass_live;
	u32 pass_high_water;
	u32 resource_capacity;
	u32 resource_live;
	u32 resource_high_water;
	u32 edge_capacity;
	u32 edge_count;
	u32 edge_high_water;
	u32 barrier_capacity;
	u32 barrier_count;
	u32 barrier_high_water;
	/** Count of successful capacity growths (arena or pools) via heap. */
	u32 growth_events;
	/** Total heap alloc/realloc operations performed by the memory system. */
	u32 heap_alloc_count;
	/** Non-zero while between begin and the next begin (frame active). */
	i32 in_frame;
	/** Last memory result from a failed op (0 if none / last succeeded). */
	i32 last_error;
} sk_rg_memory_stats_t;

/* ------------------------------------------------------------------ */
/* Callbacks (fn + userdata — no std::function)                        */
/* ------------------------------------------------------------------ */

/** Record the pass body into @p cmd. */
typedef void (*sk_rg_record_fn)(sk_rg_pass_t* pass, void_ptr_t scene, sk_command_buffer_t cmd, void_ptr_t user);

/** Called when output size changes between frames. */
typedef void (*sk_rg_resize_fn)(sk_render_graph_t* graph, sk_rg_extent_t extent, void_ptr_t user);

/** Fill push-constant bytes into @p dst (size set via pass_set_constants). */
typedef void (*sk_rg_constants_fn)(sk_render_graph_t* graph, void_ptr_t dst, void_ptr_t user);

/* ------------------------------------------------------------------ */
/* Module API table                                                    */
/* ------------------------------------------------------------------ */

/**
 * Global render-graph module API (one table per process after plugin load).
 * Every entry is non-null. Frame memory is live; full pass/GPU logic lands later.
 */
typedef struct sk_render_graph_api_t {
	/* module lifecycle (plugin-global) */
	i32 (*init)(void);
	void (*shutdown)(void);

	/* graph object */
	/** Create with default memory capacities. */
	sk_render_graph_t* (*create)(sk_render_device_t device);
	/**
	 * Create with explicit memory capacities (@p config may be NULL → defaults).
	 * Zero fields in @p config are filled with the same defaults as create().
	 */
	sk_render_graph_t* (*create_with_config)(sk_render_device_t device, const sk_rg_memory_config_t* config);
	void (*destroy)(sk_render_graph_t* graph);

	/* declare resources (names valid until next begin or retained intern pool) */
	void (*create_texture)(sk_render_graph_t* g, const_chr_t name, const sk_rg_texture_desc_t* desc);
	void (*create_buffer)(sk_render_graph_t* g, const_chr_t name, const sk_rg_buffer_desc_t* desc);
	void (*create_view)(sk_render_graph_t* g, const_chr_t name, const sk_rg_view_desc_t* desc);
	void (*import_textures)(sk_render_graph_t* g, const_chr_t name, const sk_texture_t* textures, u32 count, sk_resource_state_t state);
	void_ptr_t (*create_instance)(sk_render_graph_t* g, const_chr_t name, u64 size);
	void_ptr_t (*get_instance)(sk_render_graph_t* g, const_chr_t name);

	/* passes */
	sk_rg_pass_t* (*add_pass)(sk_render_graph_t* g, const_chr_t name, sk_rg_pass_type_t type);
	void (*pass_read)(sk_rg_pass_t* p, const_chr_t name);
	void (*pass_write)(sk_rg_pass_t* p, const_chr_t name);
	void (*pass_read_write)(sk_rg_pass_t* p, const_chr_t name);
	void (*pass_resolve)(sk_rg_pass_t* p, const_chr_t name);
	void (*pass_stage)(sk_rg_pass_t* p, i32 stage);
	void (*pass_set_pipeline)(sk_rg_pass_t* p, sk_pipeline_t pipeline);
	void (*pass_set_descriptor_set)(sk_rg_pass_t* p, u32 set, sk_descriptor_set_t ds);
	void (*pass_set_record)(sk_rg_pass_t* p, sk_rg_record_fn fn, void_ptr_t user);
	void (*pass_set_resize)(sk_rg_pass_t* p, sk_rg_resize_fn fn, void_ptr_t user);
	void (*pass_set_constants)(sk_rg_pass_t* p, u32 size, u32 stage_mask, sk_rg_constants_fn fn, void_ptr_t user);
	void (*pass_dispatch)(sk_rg_pass_t* p, u32 x, u32 y, u32 z);
	void (*pass_dispatch_indirect)(sk_rg_pass_t* p, sk_buffer_t indirect);
	void (*pass_trace_rays)(sk_rg_pass_t* p, u32 w, u32 h, u32 d);

	/* accessors */
	sk_texture_t (*get_texture)(const sk_render_graph_t* g, const_chr_t name);
	sk_texture_t (*get_prev_texture)(const sk_render_graph_t* g, const_chr_t name);
	sk_texture_view_t (*get_texture_view)(const sk_render_graph_t* g, const_chr_t name);
	sk_buffer_t (*get_buffer)(const sk_render_graph_t* g, const_chr_t name);
	sk_buffer_t (*get_prev_buffer)(const sk_render_graph_t* g, const_chr_t name);

	/* outputs / frame */
	void (*set_color_output)(sk_render_graph_t* g, const_chr_t name);
	void (*set_depth_output)(sk_render_graph_t* g, const_chr_t name);
	void (*set_output_size)(sk_render_graph_t* g, sk_rg_extent_t size);
	sk_rg_extent_t (*get_output_size)(const sk_render_graph_t* g);
	void (*set_current_output_index)(sk_render_graph_t* g, u32 index);

	/**
	 * Start a frame: reset the linear frame arena (offset → 0, capacity retained),
	 * return pass nodes to the free-list, and clear edge/barrier counts (capacity
	 * retained). Marks the graph in-frame so pool/arena growth is blocked.
	 */
	void (*begin)(sk_render_graph_t* g, void_ptr_t scene /* optional opaque */);
	void (*execute)(sk_render_graph_t* g, sk_command_buffer_t cmd);

	/* debug / tests */
	u32 (*topology_build_count)(const sk_render_graph_t* g);
	/**
	 * Fill @p out with frame-memory diagnostics (bytes used, high-water,
	 * growth events, heap-alloc count, in-frame flag, last error).
	 * @p out must not be NULL.
	 */
	void (*get_memory_stats)(const sk_render_graph_t* g, sk_rg_memory_stats_t* out);
} sk_render_graph_api_t;

/**
 * Register the static sk_render_graph_api_t on the app context.
 * Called from sk_plugin_entry_point.
 * @param context App context (must not be NULL).
 * @param app_api App module table (must not be NULL).
 */
void sk_render_graph_init(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
