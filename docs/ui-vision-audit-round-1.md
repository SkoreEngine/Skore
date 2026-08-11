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

### D1 — APX-247 — missing body-red BOX

- **Snapshot:** `ui_integration_layout_nested.png`
- **Region:** body BOX under `panel-main` (expected ~`[24,52]..[231,93]`)
- **Wrong:** zero pixels near `rgba(217,76,51)`; lower panel content is empty panel-gray
- **Correct:** solid body-red bar height ~40, stretched to panel content width, under header row (matches golden `plugins/ui/testdata/ui_integration_layout_nested.png`)
- **Likely sources:** `tests/integration/ui_integration.c`, `plugins/ui/paint.c`, `plugins/ui/clay.c` / `clay_adapter.c`, `plugins/ui/widgets.c`, `plugins/ui/ui.c`

### D2 — APX-248 — header buttons oversized and overflow panel

- **Snapshot:** `ui_integration_layout_nested.png`
- **Region:** `btn-a` / `btn-b` in `row-header`
- **Wrong:** live fills ~108×40 at `(26–133,26–65)` and `(144–251,26–65)`; `btn-b` past panel right (~239). Authored size is 96×28.
- **Correct:** both buttons fully inside panel content (golden fills ~`(26–126)` / `(129–229)`, y `26–52`)
- **Likely sources:** `plugins/ui/widgets.c` (button padding/box model), `plugins/ui/clay.c` / `clay_adapter.c`, `plugins/ui/style.c`, `tests/integration/ui_integration.c`

## Notes for later rounds

- Compare new captures against this list by **D# / APX key**, not only by pixel delta.
- Fixture font `plugins/ui/testdata/skore_test_font.ttf` intentionally uses 4-point rectangular contours per glyph — solid white blocks for text are not a defect.
- Golden `ui_integration_layout_nested.png` still has the body bar; live capture does not (golden is stale relative to current layout bug, or layout regressed after bless).
