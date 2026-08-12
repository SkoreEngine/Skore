# APX-269: MSDF vs FreeType vision comparison

Vision inspection of the APX-268 screenshot harness. **No rendering code
was changed.** Defects below are filed for a follow-up fix.

## Run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSK_ENABLE_CLANG_TIDY=OFF
cmake --build build --target sk-text-screenshot
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
(cd build/bin && ./sk-text-screenshot --mode both --verify \
    --out-dir ../../docs/notes/apx-269-text-screenshot)
```

- Harness: `sk-text-screenshot` (APX-268), viewport 640×480, DejaVuSans.ttf
- Modes: `freetype` (legacy R8 coverage) and `msdf` (msdf-atlas-c RGB8)
- `--verify`: every sample byte-identical on a second capture (both modes)
- Vulkan: lavapipe. Paint uses the **CPU MSDF coverage path** (matches the
  HLSL median + `screen_px_range` + smoothstep; `SK_UI_MSDF_GPU` unset
  because GPU atlas sampling is tofu on lavapipe)
- Captures: `docs/notes/apx-269-text-screenshot/text-screenshot/{freetype,msdf}/`
- Side-by-side / 4× diffs / zooms: `docs/notes/apx-269-text-screenshot/compare/`

Build note (not a render defect): default `SK_ENABLE_CLANG_TIDY=ON` currently
fails `cert-flp30-c` on a float induction loop in
`plugins/ui/font_msdf.c` (`SK_TESTS` only). Tidy was off only to produce
these frames.

## Checklist (what was graded)

| Criterion | How it was judged |
| --- | --- |
| Crisp edges at large sizes | 48px / 96px on `sizes` |
| Small-size legibility, no missing strokes | 8px / 12px on `sizes`; 6px logical on `scaled` |
| Sharp corners (MSDF vs classic SDF rounding) | T / k / x / w / b at 48–96px |
| No halo / fringe / RGB channel split | Large white glyphs; yellow on blue |
| Spacing and baseline vs FreeType | `glyph_grid` row spans and first-row x-extent |
| Color and alpha | `colors_alpha` (opaque + 0.25–0.60) |
| No atlas bleed | `atlas_msdf.png` cell gutters + `glyph_grid` |

## Verdict per sample

| Sample | Verdict | Artifact | Suspected cause |
| --- | --- | --- | --- |
| `pangram` | **regression** | Soft, dim 20px glyphs; extra fringe vs FreeType | **distance range** + **shader AA** |
| `sizes` | **regression** | 3–4 px halo / glow at 48px and 96px | **distance range** + **shader AA** |
| `colors_alpha` | **regression** | Olive/green fringe on yellow over blue; softer coverage | **shader AA** (wide coverage × blend) |
| `scaled` | **regression** | Soft halo at 28 px physical; 6 px logical still readable | **distance range** + **shader AA** |
| `glyph_grid` | **pass** (notes) | All ASCII present; baseline matches; slight extra advance | minor **layout metrics** |
| `atlas_msdf` | **pass** | 2 px gray gutters; no neighbor intrusion | — (atlas generation OK) |

Overall: **MSDF is not ready to replace FreeType.** Letterforms, corners,
color *identity*, alpha, and atlas packing are in the right family. The
blocking defect is a wide anti-aliased band (halo) from a distance-range /
screen-px-range mismatch. That also dims mid-size text and dirty-blends
colored glyphs.

---

## D1 — large-size halo / glow — **REGRESSION** (blocking)

- **Samples:** `sizes.png` (48px and 96px), also visible on `scaled.png`
- **Images:**
  - [freetype/sizes.png](apx-269-text-screenshot/text-screenshot/freetype/sizes.png)
  - [msdf/sizes.png](apx-269-text-screenshot/text-screenshot/msdf/sizes.png)
  - [compare/sizes_side_by_side.png](apx-269-text-screenshot/compare/sizes_side_by_side.png)
  - [compare/msdf_sizes_96_T_zoom.png](apx-269-text-screenshot/compare/msdf_sizes_96_T_zoom.png)
  - [compare/freetype_sizes_96_T_zoom.png](apx-269-text-screenshot/compare/freetype_sizes_96_T_zoom.png)
  - [compare/msdf_sizes_96_brown_zoom.png](apx-269-text-screenshot/compare/msdf_sizes_96_brown_zoom.png)
  - [compare/freetype_sizes_96_brown_zoom.png](apx-269-text-screenshot/compare/freetype_sizes_96_brown_zoom.png)
- **Wrong:** Every large glyph is wrapped in a 3–4 px soft gray outline.
  96px “T” / “brown” look backlit. Interior fill is solid, so this is not
  empty-atlas tofu.
- **Correct (FreeType):** 1 px stair-step AA, no glow.
- **Not this defect:** 48px “The quick” overlapping the next “48px” /
  96px line — both renderers, harness row height `size + 20` is too short.
- **Suspected cause: distance range + shader AA.** Bake uses C-API
  `px_range {0, 2}` then remaps `unorm = 0.5 + 0.5 * (v / 2)`. The shader
  (`render.c` `ui_ps_hlsl` and CPU twin `ui_msdf_coverage`) assumes a
  mid-gray edge and `smoothstep(-0.5, 0.5, spr * (med - 0.5))`. If the
  baked signed range is narrower than the shader’s `px_range` (C API
  endpoints vs C++ width-2, or `v / 2` vs a ±1 mapping), `spr` stays near
  the 1.0 clamp and the whole field becomes the AA band — a 2 atlas-px
  range at 96 px display is several screen pixels of halo.
- **Fix direction (do not apply here):** make bake range, unorm remap, and
  `g_push.pxRange` / `atlas.px_range` one contract; then tighten
  `screen_px_range` so large sizes get ~1 px AA. Victor Chlumsky’s
  `screenPxRange = pxRange * screenSize / atlasSize` (no 1.0 floor at
  large sizes) is the usual target.

## D2 — mid-size text is dim and soft — **REGRESSION**

- **Samples:** `pangram.png` (20 px), `sizes.png` 20 px line, `scaled.png`
  14 px logical / 28 px physical
- **Images:**
  - [freetype/pangram.png](apx-269-text-screenshot/text-screenshot/freetype/pangram.png)
  - [msdf/pangram.png](apx-269-text-screenshot/text-screenshot/msdf/pangram.png)
  - [compare/pangram_side_by_side.png](apx-269-text-screenshot/compare/pangram_side_by_side.png)
  - [compare/msdf_pangram_head_zoom.png](apx-269-text-screenshot/compare/msdf_pangram_head_zoom.png)
  - [compare/freetype_pangram_head_zoom.png](apx-269-text-screenshot/compare/freetype_pangram_head_zoom.png)
- **Wrong:** MSDF 20 px peak luminance ~206 vs FreeType ~243; mean
  non-background luminance 126 vs 179. More non-background pixels (7428 vs
  5484) from the fringe. Same wrapping and glyph set.
- **Suspected cause:** same as D1. With `spr` clamped to 1.0, interior
  median ~0.75 covers at ~0.84 — permanently dim. Same family as D1,
  different size.

## D3 — yellow-on-blue olive fringe — **REGRESSION**

- **Sample:** `colors_alpha.png`
- **Images:**
  - [freetype/colors_alpha.png](apx-269-text-screenshot/text-screenshot/freetype/colors_alpha.png)
  - [msdf/colors_alpha.png](apx-269-text-screenshot/text-screenshot/msdf/colors_alpha.png)
  - [compare/colors_alpha_side_by_side.png](apx-269-text-screenshot/compare/colors_alpha_side_by_side.png)
  - [compare/msdf_colors_yellow_zoom.png](apx-269-text-screenshot/compare/msdf_colors_yellow_zoom.png)
  - [compare/freetype_colors_yellow_zoom.png](apx-269-text-screenshot/compare/freetype_colors_yellow_zoom.png)
- **Wrong:** “Opaque yellow” mean RGB ~`(134, 140, 74)` vs FreeType
  `~(178, 182, 47)`. Extra green/blue in the fringe. Opaque white, magenta,
  cyan 0.25, blue 0.35, green 0.60 still read as the intended hues.
- **Not RGB channel split:** 96 px white “T” max |R−G|/|R−B| is 16 (MSDF)
  vs 15 (FreeType). Median is combining channels; the olive is
  `(1,1,0) * coverage` over `(0.10, 0.15, 0.55)` with a too-wide coverage
  kernel.
- **Suspected cause: shader AA** (wide coverage from D1), not a bad median.

## What passed

### Sharp corners

96 px “T”, “k”, “x”, “b”, “w” keep right-angle / pointed corners. This is
**not** the classic single-channel SDF rounded-corner failure. MSDF edge
coloring (INKTRAP, seed 0) is doing its job. Do not “fix” corners; fix D1.

### Small sizes / missing strokes

8 px and 12 px on `sizes`, and 6 px logical (12 px physical) on `scaled`,
stay readable. No dropped hairlines on `e`, `s`, `8`, or punctuation.
MSDF is slightly heavier than hinted FreeType at 8 px — acceptable.

Zooms:
[msdf 8px](apx-269-text-screenshot/compare/msdf_sizes_8px_zoom.png) ·
[freetype 8px](apx-269-text-screenshot/compare/freetype_sizes_8px_zoom.png)

### Layout metrics vs FreeType

`glyph_grid` first ink row is y=11..24 on **both** paths (baseline
aligned). First-row x-extent is 19–138 (FreeType) vs 19–141 (MSDF): about
3 px extra over 16 glyphs. Unhinted em advances vs FreeType hinted
advances. Not blocking.

[msdf grid](apx-269-text-screenshot/text-screenshot/msdf/glyph_grid.png) ·
[freetype grid](apx-269-text-screenshot/text-screenshot/freetype/glyph_grid.png)

Tilde at 16 px is a mid-line dash in **both** faces (DejaVu + hinting),
not a missing glyph.

### Atlas bleed

[atlas_msdf.png](apx-269-text-screenshot/text-screenshot/msdf/atlas_msdf.png)
is 256×256 RGB8. Cells have a ~2 px gray gutter (`UI_MSDF_SPACING_PX`).
Zooms
[tl](apx-269-text-screenshot/compare/atlas_msdf_tl_zoom.png) and
[mid](apx-269-text-screenshot/compare/atlas_msdf_mid_zoom.png) show
distance fields stopping at the pad; no neighbor contour leaking into
the next box. FreeType page
[atlas_freetype.png](apx-269-text-screenshot/text-screenshot/freetype/atlas_freetype.png)
is a packed R8 coverage sheet (expected).

### Color / alpha (aside from D3 fringe)

Opaque white, 0.50 white, 0.25 cyan, 0.50 magenta, 0.35 blue, and 0.60
green over the two boxes and the root all composite in the right
direction. Alpha is applied; nothing is forced opaque or dropped.

## Suggested fix order (next task)

1. **D1 / D2** — unify `px_range` bake ↔ unorm ↔ shader. Re-run
   `sk-text-screenshot --mode both` and inspect `sizes` 96 px first.
2. **D3** — should collapse once the AA band is ~1 px; re-check yellow
   on blue.
3. Optional: hinted vs unhinted advance (3 px / 16 glyphs) if UI
   measurement must match FreeType exactly.

Do not treat the 48/96 px *scene overlap* as an MSDF bug.
