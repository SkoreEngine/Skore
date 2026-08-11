# UI vision audit round 2 (APX-241)

Fresh integration snapshots were produced and inspected with vision after the
round 1 coder fixes (APX-240 / APX-247 / APX-248). This log is the identity
comparison against round 1 defect IDs **D1** and **D2**.

## Run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sk-integration-tests
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export SK_UI_MEASURE=1
(cd build/bin && ./sk-integration-tests)
```

- Suite: **43 tests, 0 failures, 0 ignored**
- Artifact root: `build/test-artifacts/` — **7** fresh PNGs
- Integration goldens under `plugins/ui/testdata/ui_integration_*.png`:
  **0 differing pixels** vs live captures for empty_frame, single_button,
  layout_nested, and text_glyphs

## Snapshots inspected

| # | File | Size | Verdict |
| - | ---- | ---- | ------- |
| 1 | `ui_capture_offscreen_readback.png` | 64×64 | OK — solid red clear with green square in upper-left |
| 2 | `ui_capture_harness_deterministic.png` | 64×64 | OK — red root, green 16×16 top-left, blue bar along bottom |
| 3 | `ui_capture_harness_clear_color.png` | 64×64 | OK — final frame is solid black clear |
| 4 | `ui_integration_empty_frame.png` | 96×96 | OK — solid black empty frame |
| 5 | `ui_integration_single_button.png` | 128×96 | OK — blue button centered on white; no label (no font by design) |
| 6 | `ui_integration_layout_nested.png` | 256×192 | OK — D1 and D2 resolved; see below |
| 7 | `ui_integration_text_glyphs.png` | 192×96 | OK — “UI 42” on dark panel; fixture font is solid-block glyphs by design |

## Round 1 defects — status

### D1 — APX-247 — missing body-red BOX — **FIXED**

- **Snapshot:** `ui_integration_layout_nested.png`
- **Round 1 wrong:** zero body-red pixels; lower panel content empty gray
- **Round 2 actual:** solid body-red bar present under the header row
  - `SK_UI_MEASURE` bbox `body_red: (25,53)-(230,92)` (206×40), **8240** pixels
  - Histogram body-red fraction ~0.168
- **Expected:** solid body-red bar height ~40, stretched to panel content width
- **Conclusion:** matches expected layout and committed golden (pixel-identical)

### D2 — APX-248 — header buttons oversized and overflow panel — **FIXED**

- **Snapshot:** `ui_integration_layout_nested.png`
- **Round 1 wrong:** buttons ~108×40; `btn-b` past panel right (~239)
- **Round 2 actual:**
  - Joint button fill bbox `button_bg: (26,26)-(229,51)` (204×26), **4888** pixels
  - Buttons fully inside panel content; visible gap between the two blue fills
  - **0** button fill pixels with x≥232 or y≥54
- **Expected:** both buttons fully inside panel; outer ~96×28 border-box; no overflow
- **Conclusion:** matches expected layout and committed golden (pixel-identical)

## Regressions and new defects

- **No regressions** relative to round 1 OK scenes (capture harness, empty frame,
  single button, text glyphs).
- **No new visual defects** found on any of the 7 snapshots.
- `ui_integration_layout_nested` structural asserts and golden compare **PASS**.

## Sign-off

**APX-241 vision audit round 2: SIGNED OFF.**

All round 1 defects (D1/APX-247, D2/APX-248) are **FIXED**. No new defects and
no regressions were observed. **No new coder tasks filed.**

Inspected snapshots (complete list):

1. `ui_capture_offscreen_readback.png`
2. `ui_capture_harness_deterministic.png`
3. `ui_capture_harness_clear_color.png`
4. `ui_integration_empty_frame.png`
5. `ui_integration_single_button.png`
6. `ui_integration_layout_nested.png`
7. `ui_integration_text_glyphs.png`

## Notes for round 3

- Identity list remains D1/D2 only; both closed FIXED here.
- Fixture font solid blocks are still not defects.
- Round 3 can treat this as a clean baseline unless a later fix reintroduces
  pixel drift against `plugins/ui/testdata/ui_integration_*.png`.
