# APX-264: UI Font Rendering Path and msdf-gen C API Audit

**Task:** Audit the current FreeType bitmap font path and the vendored msdf-atlas-gen C API; produce an integration plan to replace the UI font renderer with MSDF (no rendering behavior change in this task).

**Branch / goal context:** `feature/replace-ui-font-renderer-by-msdf-gen` — replace UI font renderer by msdf-gen.

**Scope of this document:** inventory only. No shader, atlas, or paint behavior was modified.

---

## 1. FreeType call sites (UI / font / render)

All production FreeType use for UI text lives in **`plugins/ui/font.c`**. There is a link-smoke in `plugins/ui/ui.c` (tests only). `paint.c` and `render.c` never call FreeType directly; they consume the font-system API and CPU atlas pages.

### 1.1 Face loading and library lifetime

| Call | File / function | Role |
|------|-----------------|------|
| `FT_Init_FreeType` | `font.c` → `ui_font_system_create_impl` | Create `FT_Library` owned by `sk_ui_font_system_t`. |
| `FT_Done_FreeType` | `font.c` → `ui_font_system_destroy_impl` (and create failure path) | Tear down library. |
| `FT_New_Memory_Face` | `font.c` → `ui_font_load_bytes` | Load face from copied TTF/OTF bytes (path load reads file then calls this). |
| `FT_Done_Face` | `font.c` → destroy paths (`ui_font_destroy_impl`, system destroy, load failure) | Release face. |

**Notes:**

- Path load: `ui_font_load_path_impl` reads via `sk_filesystem_api_t`, then `ui_font_load_bytes`.
- Face data: font owns a private `file_bytes` copy; FreeType sees that buffer for the face lifetime.
- Face index is always `0` (first face in the file).

**Test-only (not production paint path):**

| Call | File | Role |
|------|------|------|
| `FT_Init_FreeType` / `FT_Done_FreeType` | `ui.c` (SK_TESTS) | Prove FreeType still links. |

### 1.2 Metrics and cmap queries

| Call / field | File / function | Role |
|--------------|-----------------|------|
| `FT_Set_Pixel_Sizes(face, 0, pixel_size)` | `ui_font_set_pixel_size` | Set physical pixel size before metrics or raster. |
| `face->units_per_EM`, `ascender`, `descender`, `height` | `ui_font_get_metrics_impl` | Scale design units → pixel metrics: `ascent`, `descent`, `line_height`, `pixel_size`. |
| `FT_Get_Char_Index(face, codepoint)` | `ui_font_glyph_index_impl` | Unicode → glyph index (0 if missing). |

**Convention:** FreeType-style (ascent typically positive, descent negative). Public type: `sk_ui_font_metrics_t` in `ui.h`.

Physical pixel size is **not** FreeType-derived: callers use `sk_ui_font_pixel_size(logical_font_size, content_scale)` = `round(logical * scale)` (min 1 when logical > 0).

### 1.3 Glyph rasterization

| Call / field | File / function | Role |
|--------------|-----------------|------|
| `FT_Set_Pixel_Sizes` | `ui_font_get_glyph_impl` (cache miss) | Size before load. |
| `FT_Load_Glyph(face, glyph_index, UI_FONT_LOAD_FLAGS)` | `ui_font_get_glyph_impl` | Load + rasterize. |
| `slot->advance.x/y` | same | Advance in 26.6 → float px (`/ 64`). |
| `slot->bitmap_left`, `slot->bitmap_top` | same | Bearings → `bearing_x` / `bearing_y`. |
| `slot->bitmap` (`width`, `rows`, `pitch`, `buffer`) | same | Grayscale coverage buffer; packed into atlas. |

**Load flags (byte-stable, harness-pinned):**

```c
#define UI_FONT_LOAD_FLAGS (FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)
```

No LCD filter, no light target, no system hinter variation. Capture harness expects this.

**Pitch handling:** FreeType pitch may be negative (bottom-up). Negative pitch is flipped into a top-down temporary buffer before packing; positive pitch packs in place with that pitch as row stride.

**Cache:** key `(font_id, pixel_size, glyph_index)` → `sk_ui_glyph_t`. Empty bitmaps (e.g. space) still cache with `width=height=0` and non-zero advance.

### 1.4 What FreeType is *not* used for

- No kerning (`FT_Get_Kerning` unused).
- No outline extraction (`FT_Outline_*` unused in UI).
- No GPU upload (CPU atlas only in font system).
- msdfgen’s internal FreeType use (`thirdparty/msdf-atlas-gen/msdfgen/ext/import-font.cpp`) is **behind** the C API / C++ smoke tests, not the UI plugin paint path today.

---

## 2. Glyph atlas structure, upload path, shaders

### 2.1 CPU atlas (`plugins/ui/font.c`)

| Property | Current value |
|----------|----------------|
| Format | **R8** grayscale coverage (`u8*`, pitch == width) |
| Default page | 512×512 (`UI_FONT_DEFAULT_PAGE`) |
| Max page | 2048 (`UI_FONT_MAX_PAGE`) |
| Packing | **stb_rect_pack** (`stbrp_*`) |
| Per-glyph padding | 1 px each side (`UI_FONT_PAD_PX`) |
| Growth | Double (or pow2 for need); on full page, grow in place (preserve top-left ink, re-init packer, reserve old region) or add a new page |
| Multi-page | Heap-stable `ui_atlas_page_live_t*` array; UVs rescale on grow |
| Generation | Bumps on pack ink and on grow; GPU uses this as dirty flag |
| Public view | `sk_ui_atlas_page_t { width, height, generation, const u8* pixels }` |

Cache → atlas placement fields on `sk_ui_glyph_t`: `width/height`, normalized `u0,v0,u1,v1` (top-left origin in page), `page_index`.

### 2.2 GPU upload path (`plugins/ui/render.c`)

Frame flow (host-driven):

1. **paint** builds `sk_ui_draw_list_t`
2. **`ui_renderer_prepare_impl`** (outside render pass): VB/IB update + atlas uploads
3. begin render pass
4. **`ui_renderer_encode_impl`**: viewport, scissor, bind, draw_indexed
5. end render pass

**Atlas upload details:**

- For each `SK_UI_DRAW_CMD_MESH` with `texture_kind == SK_UI_DRAW_TEX_FONT`, load CPU page via `font_atlas_get_page`, then `ui_render_ensure_atlas_page`.
- GPU texture: `SK_PIXEL_FORMAT_R8_UNORM`, usage `SHADER_RESOURCE | COPY_DEST`.
- Upload: host-visible **staging buffer** → `copy_buffer_to_texture` with barriers `UNDEFINED → COPY_DEST → SHADER_READ`.
- Capacity: `UI_ATLAS_PAGE_CAP = 16` GPU page slots / font descriptor sets.
- Skip re-upload when `width/height/generation` match last upload.
- Descriptor set: binding 0 sampled image, binding 1 sampler; rebuilt when page re-uploads.

### 2.3 Sampler / filtering

Shared sampler `"ui-sampler"`:

| Setting | Value |
|---------|--------|
| min / mag | `SK_FILTER_MODE_LINEAR` |
| mipmap | `SK_FILTER_MODE_NEAREST` |
| address U/V/W | `SK_TEXTURE_ADDRESS_CLAMP_TO_EDGE` |
| max_lod | 0 |

### 2.4 Vertex and fragment shaders (embedded HLSL in `render.c`)

Compiled at renderer create via DXC plugin (`vs_6_0` / `ps_6_0` → SPIR-V). Temporary pattern (TODO: cooked assets).

**Vertex (`ui_vs_hlsl`):**

- Inputs: `float2 pos`, `float2 uv`, `uint color` (packed RGBA8).
- Push constants `UIPush`: `float4 st` (scale.xy, translate.zw) + `uint mode` (+ pad to 32 bytes).
- Transform: `ndc = pos * scale + translate`; unpack color to float4.

**Fragment (`ui_ps_hlsl`):**

- Sample `g_texture` with `g_sampler`.
- **mode == 1 (font):** `float4(color.rgb, color.a * tex.r)` — R channel as coverage alpha.
- **mode == 0 (solid/image):** `tex * color`.

**Pipeline:** triangle list, alpha blend (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`), no depth test, no cull. Vertex stride = `sizeof(sk_ui_draw_vertex_t)` (`x,y,u,v,color`).

**Mode selection:** `ui_render_mode_for_cmd` → `UI_MODE_FONT` when `texture_kind == SK_UI_DRAW_TEX_FONT`.

---

## 3. UI text draw path (string → draw call)

### 3.1 High-level pipeline

```
widget / TEXT node ("text" prop, style.font_size, color, wrap, align)
        │
        ▼
ui_paint_impl (paint.c)  — tree walk, dirty rebuild of draw list
        │
        ▼
ui_paint_emit_text       — for TEXT nodes and widgets that carry "text"
        │                   (label, button, text_input, menu*, tab, title bar, …)
        │
        ├─ sk_ui_font_pixel_size(font_size, avg_content_scale)
        ├─ ui_font_get_metrics_impl          (FT metrics)
        ├─ UTF-8 decode + wrap / align
        ├─ per codepoint:
        │     ui_font_glyph_index_impl       (FT cmap)
        │     ui_font_get_glyph_impl         (FT raster + atlas cache)
        │     place quad: pen + bearing, baseline from ascent
        │
        ▼
sk_ui_draw_list_t  (vertices, indices, MESH / PUSH_CLIP / POP_CLIP cmds)
        │
        ▼
ui_renderer_prepare_impl — VB/IB + R8 atlas page uploads
        │
        ▼
ui_renderer_encode_impl  — bind font desc set, push mode=1, draw_indexed
```

### 3.2 Layout / metrics during paint

- **Content scale:** average of paint scale_x/scale_y (HiDPI).
- **Baseline:** `content_y + vertical_align_pad + metrics.ascent`; next line `+= metrics.line_height`.
- **Glyph rect:**  
  `gx0 = pen_x + bearing_x`, `gy0 = baseline_y - bearing_y`,  
  size = glyph bitmap width/height.
- **Advance:** `pen_x += g.advance_x` (no kerning).
- **Wrap:** soft wrap at spaces when `wrap` prop set; hard `\n`; max 32 lines.
- **Align:** horizontal 0/1/2 left/center/right; vertical top/center/bottom.
- **Selection / caret:** solid quads (not font texture).

### 3.3 Current glyph emission strategy (important)

`ui_paint_emit_text` **does not primarily emit `SK_UI_DRAW_TEX_FONT` quads**. After APX-244 (lavapipe / staging upload “tofu”):

- **Default path:** walk CPU R8 atlas coverage and emit **horizontal solid-quad runs** with alpha modulated by average coverage (`ui_paint_add_solid_quad`). Letterforms without GPU texture sampling.
- **Fallback only:** if atlas page lookup fails, emit textured FONT quad via `ui_paint_add_textured_quad(..., SK_UI_DRAW_TEX_FONT, page_index, ...)`.

So the **GPU font shader path is implemented and still used when FONT cmds appear**, but **production paint currently prefers CPU solid quads**. MSDF integration must:

1. Restore (or prefer) true textured quads for MSDF sampling, and  
2. Keep a CPU fallback story only if headless captures still need it (likely replace with CPU MSDF evaluate or re-verify GPU upload).

### 3.4 Batching

`ui_paint_ensure_mesh` batches by `(texture_kind, texture_id, clip)`. FONT meshes break batches per atlas page. Solid glyph runs batch with other solid geometry.

### 3.5 Public API surface (unchanged contract today)

From `ui.h` / API table: `font_system_create/destroy`, `font_load_path/memory`, `font_destroy`, `font_get_metrics`, `font_glyph_index`, `font_get_glyph`, `font_atlas_page_count/get_page`, cache stats. Paint params: `sk_ui_paint_params_t { font_system, font }`.

---

## 4. msdf-gen / msdf-atlas-gen C API surface

**Header:** `thirdparty/msdf-atlas-gen/c_api/include/msdf_atlas_c.h`  
**Library:** `msdf-atlas-c` (wraps `msdf-atlas-gen` + `msdfgen`; FreeType PRIVATE via msdfgen ext).  
**API version:** `MSDF_ATLAS_C_API_VERSION 1`  
**Docs / smoke:** `thirdparty/msdf-atlas-gen/README.md`, `c_api/tests/msdf_atlas_c_smoke.c`, `tests/msdf_atlas_smoke.cpp` (C++).

UI code must use **`<msdf_atlas_c.h>`** and link **`msdf-atlas-c`**, not the C++ headers (engine is C-first).

### 4.1 Opaque handles and lifecycle

| Handle | Create | Destroy |
|--------|--------|---------|
| `msdf_atlas_font_t` | `msdf_atlas_font_open` / `open_memory` | `msdf_atlas_font_destroy` |
| `msdf_atlas_charset_t` | `charset_create` / `create_ascii` | `charset_destroy` |
| `msdf_atlas_glyphset_t` | `glyphset_create` | `glyphset_destroy` |
| `msdf_atlas_packer_t` | `packer_create(config\|NULL)` | `packer_destroy` |
| `msdf_atlas_generator_t` | `generator_create(config\|NULL)` | `generator_destroy` |

Destroy accepts NULL (no-op, `MSDF_ATLAS_OK`). Handles are not thread-safe; generator may use internal workers during `generate()` (still synchronous return).

### 4.2 Available function groups (complete surface)

**Version / errors**

- `msdf_atlas_version`
- `msdf_atlas_error_string`
- `msdf_atlas_last_error_message`

**Config**

- `msdf_atlas_config_default`
- `msdf_atlas_config_validate`

**Charset**

- create / create_ascii / destroy  
- add / remove / contains / size  
- parse (charset syntax: `"a-z0-9"`, `"U+0041-U+005A"`, quoted, brackets)

**Font**

- open / open_memory / destroy  
- get_metrics / get_glyph_count / get_glyph_index  

**Glyphset (geometry load — the “request glyph” stage)**

- load_charset / load_glyphset / load_glyph_range  
- edge_color  
- get_count / get_layout / get_advance (kerning if `MSDF_ATLAS_LOAD_KERNING`)

**Packer**

- create / destroy / apply_config  
- set/unset dimensions, constraint, spacing, scale, min scale, unit_range, pixel_range, miter, padding  
- grid: columns/rows/cell/fixed_origin / has_cutoff  
- pack / get_dimensions / get_scale / get_pixel_range / get_cell_dimensions / get_columns / get_rows / get_fixed_origin  

**Generator**

- create / destroy / set_thread_count / set_flags / resize  
- generate  
- get_bitmap / get_layout_count / get_layout / get_layout_all  

There is **no single-glyph “render this codepoint to a malloc’d bitmap and free it” API**. Glyph MSDF bitmaps are produced only as **regions of a packed atlas** after load → edge_color → pack → generate.

### 4.3 How a glyph MSDF bitmap is requested

Canonical pipeline (from header + README + smoke):

```
msdf_atlas_config_default(&config)   // then override image_type, ranges, dims…
msdf_atlas_font_open(_memory)(…)
msdf_atlas_charset_*                 // which codepoints
msdf_atlas_glyphset_create
msdf_atlas_glyphset_load_charset(set, font, font_scale_ems, charset, flags, &loaded)
msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_INKTRAP, 3.0, seed)
msdf_atlas_packer_create(&config, &packer)
msdf_atlas_packer_pack(packer, set)  // writes atlas_* into glyph layouts
msdf_atlas_generator_create(&config, &generator)
msdf_atlas_generator_generate(generator, set)
msdf_atlas_generator_get_bitmap(generator, &bitmap)
msdf_atlas_generator_get_layout_all(generator, &layouts, &count)
// copy pixels + layouts out; then destroy handles
```

Per-glyph plane bounds and advance are valid after **load**; atlas rects after **pack/generate**.

### 4.4 Channel layout and pixel format

**Image types (`msdf_atlas_image_type_t`):**

| Type | Meaning | Default pixel format if `PIXEL_UNKNOWN` |
|------|---------|----------------------------------------|
| HARD_MASK / SOFT_MASK | Binary / AA coverage | R8 |
| SDF / PSDF | Single-channel distance | R32F |
| **MSDF** | Multi-channel SDF (**default**) | **RGB32F** |
| **MTSDF** | MSDF + true distance in A | **RGBA32F** |

**Explicit pixel formats:** R8, RGB8, RGBA8, R32F, RGB32F, RGBA32F. Channel count must match image type (1 / 3 / 4); component type (byte vs float) may be chosen.

**Bitmap POD (`msdf_atlas_bitmap_t`):**

- `pixels` row-major  
- `width`, `height`, `channel_count`  
- `pixel_format`  
- `row_stride_bytes` (≥ width × channels × component size)

**Y direction:** default `MSDF_ATLAS_Y_BOTTOM_UP`; option `Y_TOP_DOWN` for top-first rows (UI atlas today is top-down).

**MSDF channel semantics (runtime shader contract):** RGB encode multi-channel signed distances; median of channels is used at sample time. MTSDF adds true SDF in alpha (useful for outlines). Values are typically 0.5 at the edge for mid-range mapping controlled by pixel range.

### 4.5 Distance / pixel range parameters

On `msdf_atlas_config_t` / packer:

| Field | Meaning | Default |
|-------|---------|---------|
| `px_range` | Distance range in **pixels** (`msdf_atlas_range_t { lower, upper }`) | **`{2.0, 2.0}`** |
| `unit_range` | Distance range in **font units** | `{0,0}` (unset) |
| Combined | `packer_get_pixel_range` after pack | pixel + converted unit |

Validation: `0 <= lower <= upper`. Smoke test asserts post-pack range lower/upper == 2.0.

**C vs C++ pitfall:** C++ `msdfgen::Range(2.0)` is symmetric width semantics; C API uses explicit endpoints. Prefer C `{2.0, 2.0}` as the documented default field range (2 px of range support).

Also relevant: `miter_limit` (default 1.0), `spacing`, inner/outer padding (px and unit), edge coloring angle threshold (smoke uses **3.0°** with **INKTRAP**).

### 4.6 Memory ownership and free functions

| Data | Owner | Caller free? | Invalidated by |
|------|-------|--------------|----------------|
| Handles | caller create/destroy | destroy only | — |
| `open_memory` buffer | **caller** | yes, after font destroy | must outlive font |
| `bitmap.pixels` | **generator** | **never free** | generate / resize / destroy |
| layout array from generator | **generator** | **never free** | generate / resize / destroy |
| error / version strings | library static / TLS | never free | next API call (last_error) |

There is **no** `msdf_atlas_free_*` for bitmaps. Integration **must copy** atlas pixels and glyph layouts into skore-owned storage before destroying the generator.

### 4.7 Error reporting

- Every fallible call returns `msdf_atlas_error_t`; **`MSDF_ATLAS_OK == 0`**.
- Codes include: INVALID_ARGUMENT, OUT_OF_MEMORY, OUT_OF_RANGE, INVALID_STATE, UNSUPPORTED, IO, FONT_LOAD, GLYPH_LOAD, CHARSET_PARSE, PACK, GENERATE.
- `msdf_atlas_error_string(code)` — static, never NULL.
- `msdf_atlas_last_error_message()` — thread-local detail after failure; NULL after success; valid until next `msdf_atlas_*` on that thread.

Notable state errors: pack/generate on empty set; grid setters on tight packer; advance without kerning load; DIAGONAL edge coloring **UNSUPPORTED** on pinned msdfgen.

---

## 5. Integration plan (concrete)

### 5.1 Design decisions (chosen for next implementation tasks)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **C API** | `msdf-atlas-c` / `<msdf_atlas_c.h>` only | Matches v2 C-first rule; FreeType face load for MSDF geometry is inside msdfgen. |
| **What stays FreeType (UI plugin)** | Prefer **drop direct FT for raster** and use msdf C API for font open + metrics + glyph geometry. Optionally keep a thin FT face only if a transitional dual-path is needed for harness bit-stability — **default plan: remove FT raster path**. | msdf API already exposes metrics, glyph index, advance, kerning. |
| **What stays FreeType (process)** | Vendored FreeType remains linked through **msdfgen PRIVATE** (and until UI unlinks it, `freetype` on `sk-ui`). | No second FreeType vendor. |
| **Image type** | **MSDF** (`MSDF_ATLAS_IMAGE_MSDF`) | 3-channel median decode; good quality without needing outline from A. Upgrade path to MTSDF later. |
| **Atlas pixel format (GPU)** | **RGB8** (`MSDF_ATLAS_PIXEL_RGB8`) stored / uploaded as `SK_PIXEL_FORMAT_RGBA8_UNORM` or RGB8 if available; **quantize float MSDF → unorm8** at copy time, mid-gray 0.5 ≈ edge | Matches current UI sampler simplicity; avoids float textures. Smoke default is RGB32F — convert on ingest. |
| **Atlas layout** | **Tight pack**, dimensions **power-of-two square** constraint (same as C++ smoke), **Y_TOP_DOWN** to match UI UV top-left convention | Reuse multi-page growth later if needed; v1 can start single large page / charset-prebaked. |
| **Distance range** | **`px_range = {2.0, 2.0}`** (library default) | Proven by smoke; shader uses `pxRange = 2.0` for screen-space fwidth scaling. |
| **Edge coloring** | **INKTRAP**, angle **3.0**, seed **0** | README/smoke recommendation. |
| **Charset strategy** | Runtime: on-demand growth of glyphset + repack/regenerate **or** prewarm ASCII + Latin-1; start with **ASCII prewarm + on-demand miss repack** | UI currently rasters per-size; MSDF atlas is **scale-independent** in field space — store **em-normalized** advances/plane bounds and scale by `pixel_size` / em at layout time. |
| **Scale model** | Generate at fixed em scale (e.g. font_scale 1.0, packer auto scale); layout uses **plane_bounds × (pixel_size / em)** and **advance × same** | Removes per-DPI re-raster cache keys; content scale becomes pure layout scale. |
| **Kerning** | Load with `MSDF_ATLAS_LOAD_KERNING`; paint may adopt later (optional, not required for first drop-in) | Current path has no kerning. |
| **Paint emission** | Prefer **textured FONT quads** again (one quad per glyph), not CPU solid runs | MSDF needs multi-channel sample; solid R8 runs are wrong for MSDF. |
| **Shader** | Extend fragment **mode==1** to median MSDF: `median(r,g,b)`, then `screenPxRange` via `fwidth` / `pxRange`, smoothstep or clamp to alpha | Keep mode 0 solid path. |
| **CPU capture / lavapipe** | Verify GPU upload on target; if solid path still required for goldens, add **CPU median evaluate** of MSDF atlas into temporary coverage or accept new goldens | APX-244 workaround must not remain the only path. |

### 5.2 Files to change (implementation order)

**Phase A — font system (CPU atlas generation)**

| File | Change |
|------|--------|
| `plugins/ui/CMakeLists.txt` | `target_link_libraries(sk-ui PRIVATE msdf-atlas-c)` (and drop direct `freetype` when no longer needed). |
| `plugins/ui/font.c` | Replace FT raster + R8 pack with msdf pipeline: open font, load/extend charset, edge_color, pack, generate; own RGB8 (or RGBA8) page buffer + glyph cache keyed by `(font_id, glyph_index)` **without pixel_size** (or with range id). Map `msdf_atlas_glyph_layout_t` → `sk_ui_glyph_t` (scale plane bounds at query time by `pixel_size`). |
| `plugins/ui/ui.h` | Document atlas format change (R8 → MSDF RGB); optional fields: `px_range`, atlas channel count; keep API symbols stable if possible. |
| `plugins/ui/ui_internal.h` | Any internal font helpers. |

**Phase B — GPU + shaders**

| File | Change |
|------|--------|
| `plugins/ui/render.c` | Atlas texture format R8 → RGBA8 (or RGB); upload path `ui_render_upload_texture_rgba8`; fragment shader MSDF median + range; push constant may need `px_range` (or hardcode 2.0 initially). |
| `plugins/ui/paint.c` | Emit textured FONT quads from glyph UVs/metrics; remove (or gate) R8 solid-run path; scale bearings/advances from em metrics × pixel size. |

**Phase C — tests / harness / docs**

| File | Change |
|------|--------|
| `plugins/ui/font.c` tests, `paint.c` tests | Update expectations for cache keys (no per-size bitmap), atlas ink checks for multi-channel. |
| `tests/integration/ui_*.c`, goldens under `plugins/ui/testdata/` | Re-golden where AA/edges change (MSDF ≠ FreeType gray). |
| `tests/integration/ui_capture_harness.h` | Drop FT_LOAD_* comments; document MSDF range / format pins. |
| `docs/ui-plugin.md`, `docs/ui-system-design.md` | Mark MSDF as current (v1 said no msdfgen). |
| Optional | Resource cooker later: `core/resource_asset_builtins.c` Font shell — **out of first UI swap**. |

### 5.3 Suggested data flow after integration

```
TTF bytes
  → msdf_atlas_font_open_memory (caller-owned copy in sk_ui_font_t)
  → glyphset load (charset grows on miss) + edge_color
  → packer (tight, pot square, px_range 2)
  → generator → RGB32F/RGB8 bitmap copy into sk_ui_atlas_page (RGB/RGBA8)
  → sk_ui_glyph_t: UVs from atlas rect; advance/bearing from plane bounds × (pixel_size / em_size)
paint: UTF-8 → glyph_index → get_glyph → textured quad
render: upload RGBA atlas; PS samples median MSDF → alpha * vertex color
```

### 5.4 What remains on FreeType

| Concern | Owner after integration |
|---------|-------------------------|
| Outline / geometry import | **msdfgen ext** (internal FT) |
| Face open for UI metrics | **msdf C API** (not UI-direct FT) |
| Bitmap raster `FT_Load_Glyph` + R8 | **Removed** from UI |
| Link dependency | FreeType stays as msdfgen PRIVATE; UI links `msdf-atlas-c` |

### 5.5 Risks and migration notes

1. **Golden images will change** — MSDF edges differ from FreeType gray coverage; plan re-golden, do not expect pixel-identical captures.
2. ** lavapipe upload** — solid-run workaround must be replaced carefully; verify prepare/upload before switching paint.
3. **Dynamic charset repack** — full repack invalidates UVs/generations; mirror current page `generation` bump semantics.
4. **Multi-size UI** — one MSDF atlas serves all sizes; large display sizes need adequate `px_range` and atlas glyph scale (auto packer scale) so fields are not under-sampled.
5. **API stability** — keep `font_get_glyph(system, font, pixel_size, glyph_index, out)` signature; ignore pixel_size for raster, use it only for metric scaling.

### 5.6 Out of scope for the first code swap (follow-ups)

- Cooked `.font` resource / editor importer (`resource_asset_builtins` Font shell).
- MTSDF outlines / shadows.
- Full Unicode streaming atlas eviction.
- Shader asset pipeline (still embedded HLSL until APX-134 follow-through).

---

## 6. Quick reference: current vs target

| Item | Current (v1 UI) | Target (MSDF) |
|------|-----------------|---------------|
| Rasterizer | FreeType `FT_LOAD_RENDER` | msdf-atlas-c generator |
| Atlas | R8 coverage pages | RGB(A)8 MSDF, tight pack |
| Distance range | n/a | px_range **2.0** |
| Cache key | font + **pixel_size** + glyph | font + glyph (scale at layout) |
| Paint quads | CPU solid runs (primary) | Textured FONT quads |
| PS mode 1 | `alpha *= tex.r` | median MSDF + range AA |
| FreeType in UI | direct | via msdf only (unlink direct when ready) |

---

## 7. Source map (audit anchors)

| Area | Primary paths |
|------|----------------|
| FreeType UI | `plugins/ui/font.c` |
| Paint / text | `plugins/ui/paint.c` (`ui_paint_emit_text`) |
| GPU / shaders | `plugins/ui/render.c` |
| Public types / API | `plugins/ui/ui.h` |
| Link | `plugins/ui/CMakeLists.txt` |
| msdf C API | `thirdparty/msdf-atlas-gen/c_api/include/msdf_atlas_c.h` |
| msdf smoke | `thirdparty/msdf-atlas-gen/c_api/tests/msdf_atlas_c_smoke.c`, `tests/msdf_atlas_smoke.cpp` |
| Vendor notes | `thirdparty/msdf-atlas-gen/README.md` |

---

*End of APX-264 audit. No rendering behavior was changed by this document.*
