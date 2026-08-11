# UI vision audit round 1 (APX-239)

Fresh integration snapshots were produced and inspected with vision.
This log is the identity list for later rounds (round 2/3 compare against
these defect IDs). It does **not** implement fixes.

## Run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sk-integration-tests
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
(cd build/bin && ./sk-integration-tests)
```

- Suite: **43 tests, 1 failure, 0 ignored**
- Failure: `ui_integration_layout_nested` body-red `cpu_image_assert_bbox`
  (PNG still written before the assert)
- Artifact root: `build/test-artifacts/` — **7** fresh PNGs

## Snapshots inspected

| # | File | Size | Verdict |
| - | ---- | ---- | ------- |
| 1 | `ui_capture_offscreen_readback.png` | 64×64 | OK — red clear + green box |
| 2 | `ui_capture_harness_deterministic.png` | 64×64 | OK — red root, green 16×16, blue bar |
| 3 | `ui_capture_harness_clear_color.png` | 64×64 | OK — final frame is explicit black clear |
| 4 | `ui_integration_empty_frame.png` | 96×96 | OK — solid black |
| 5 | `ui_integration_single_button.png` | 128×96 | OK — blue button on white; no label (no font by design) |
| 6 | `ui_integration_layout_nested.png` | 256×192 | **DEFECTS D1, D2** |
| 7 | `ui_integration_text_glyphs.png` | 192×96 | OK for fixture — SkoreTest TTF uses solid quad glyphs for `UI 42` |

## Defects filed (identity list)

Each defect is a separate goal task assigned to the **coder** worker.

### D1 — APX-247 — missing body-red BOX — **FIXED (APX-240 / APX-247)**

- **Snapshot:** `ui_integration_layout_nested.png`
- **Region:** body BOX under `panel-main` (expected ~`[24,52]..[231,93]`)
- **Wrong:** zero pixels near `rgba(217,76,51)`; lower panel content is empty panel-gray
- **Correct:** solid body-red bar height ~40, stretched to panel content width, under header row
- **Root cause:** Clay adapter mapped AUTO-width in-flow children as FIT. Empty body BOX (height only, no text) collapsed to zero width under a column panel whose default `align_items` is STRETCH.
- **Fix:** `plugins/ui/clay_adapter.c` — when parent `align_items` / child `align_self` is STRETCH and the cross-axis size is AUTO, map that axis as GROW so the child fills the parent content size. `ui-panel` class also sets `align_items: STRETCH` explicitly.
- **Verified (APX-240 / APX-247):** live body fill bbox `(25,53)-(230,92)` (206×40), ~8240 body-red pixels, histogram body-red fraction ~0.17; `ui_integration_layout_nested` PASS; units `ui_clay_column_stretch_empty_box_fills_content_width` and `ui_clay_nested_border_box_stretch_space_between` lock content width 206.

### D2 — APX-248 — header buttons oversized and overflow panel — **FIXED (APX-240 / APX-248)**

- **Snapshot:** `ui_integration_layout_nested.png`
- **Region:** `btn-a` / `btn-b` in `row-header`
- **Wrong:** live fills ~108×40 at `(26–133,26–65)` and `(144–251,26–65)`; `btn-b` past panel right (~239). Authored size is 96×28.
- **Correct:** both buttons fully inside panel content; outer 96×28; fills ~`(26–119)` / `(136–229)`, y `26–51`
- **Root cause (two parts):**
  1. In-flow POINT sizes were expanded by padding+border (content-box) so ui-button 96×28 with pad 6 + border 1 became ~110×42 outer.
  2. `justify-content: space-between` collapsed to flex-start under Clay (no native packing), so free space was not distributed and the oversized pair still packed left — but the pad expansion alone was enough to overflow.
- **Fix:** `plugins/ui/clay_adapter.c` — POINT width/height map 1:1 as border-box (Clay FIXED is outer); space-between approximated with anonymous main-axis GROW spacers (authored gap as spacer min, Clay childGap zeroed to avoid double gap). `ui-button` class documents border-box POINT sizing.
- **Verified (APX-240 / APX-248):** joint button fill bbox `(26,26)-(229,51)` (204×26, 4888 pixels); fills stay inside panel content (no button pixels at x≥232 or y≥54). Units `ui_clay_button_point_size_is_border_box` (outer 96×28, content 82×14) and `ui_clay_nested_border_box_stretch_space_between` (trailing-edge placement, no overflow). `ui_integration_layout_nested` PASS vs golden.

## Notes for later rounds

- Compare new captures against this list by **D# / APX key**, not only by pixel delta.
- Fixture font `plugins/ui/testdata/skore_test_font.ttf` intentionally uses 4-point rectangular contours per glyph — solid white blocks for text are not a defect.
- After APX-240, live `ui_integration_layout_nested.png` matches the committed golden (body bar present, buttons in-bounds).
