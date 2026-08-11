# UI vision audit round 3 (APX-243)

Final audit after two coder fix rounds. Fresh integration snapshots were
produced and inspected with vision, then compared against round 1 defect IDs
**D1** / **D2** and the round 2 FIXED sign-off.

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
| 1 | `ui_capture_offscreen_readback.png` | 64×64 | OK — solid red clear with green 16×16 square upper-left |
| 2 | `ui_capture_harness_deterministic.png` | 64×64 | OK — red root, green 16×16 top-left, blue bar along bottom |
| 3 | `ui_capture_harness_clear_color.png` | 64×64 | OK — final frame is solid black clear |
| 4 | `ui_integration_empty_frame.png` | 96×96 | OK — solid black empty frame; pixel-identical to golden |
| 5 | `ui_integration_single_button.png` | 128×96 | OK — blue button on white; no label (no font by design); pixel-identical to golden |
| 6 | `ui_integration_layout_nested.png` | 256×192 | OK — D1 and D2 remain resolved; pixel-identical to golden |
| 7 | `ui_integration_text_glyphs.png` | 192×96 | OK — readable “UI 42” on dark panel; pixel-identical to golden |

## Round 1 defects — final status

### D1 — APX-247 — missing body-red BOX — **FIXED (confirmed round 3)**

- **Snapshot:** `ui_integration_layout_nested.png`
- **Round 1 wrong:** zero body-red pixels; lower panel content empty gray
- **Round 2:** FIXED — body-red bar present; pixel-identical to golden
- **Round 3 actual:**
  - body-red bbox `(25,53)-(230,92)` (206×40), **8240** pixels
  - histogram body-red fraction ~0.168
- **Expected:** solid body-red bar height ~40, stretched to panel content width
- **Conclusion:** still FIXED; matches expected layout and committed golden

### D2 — APX-248 — header buttons oversized and overflow panel — **FIXED (confirmed round 3)**

- **Snapshot:** `ui_integration_layout_nested.png`
- **Round 1 wrong:** buttons ~108×40; `btn-b` past panel right (~239)
- **Round 2:** FIXED — joint fill inside panel; no overflow; golden match
- **Round 3 actual:**
  - Joint button fill bbox `(26,26)-(229,51)` (204×26), **4888** pixels
  - Two blue buttons fully inside panel with a visible gap between them
  - **0** button fill pixels with x≥232 or y≥54
- **Expected:** both buttons fully inside panel; outer ~96×28 border-box; no overflow
- **Conclusion:** still FIXED; matches expected layout and committed golden

## Outcome classification

**Outcome A — no visual defects remain.**

- **Quarantined (recurring across 3 rounds / survived 2 fix attempts):** none
- **New defects first seen in round 3:** none
- **Regressions** relative to round 1 OK scenes or round 2 sign-off: none

## Final sign-off

**APX-243 vision audit round 3: SIGNED OFF.**

All earlier defects are resolved:

| ID | APX | Snapshot | Status |
| -- | --- | -------- | ------ |
| D1 | APX-247 | `ui_integration_layout_nested.png` | FIXED (rounds 2 and 3) |
| D2 | APX-248 | `ui_integration_layout_nested.png` | FIXED (rounds 2 and 3) |

No new defects and no regressions were observed on any of the 7 snapshots.
**No new coder tasks filed. No items quarantined for human review.**

Inspected snapshots (complete list):

1. `ui_capture_offscreen_readback.png`
2. `ui_capture_harness_deterministic.png`
3. `ui_capture_harness_clear_color.png`
4. `ui_integration_empty_frame.png`
5. `ui_integration_single_button.png`
6. `ui_integration_layout_nested.png`
7. `ui_integration_text_glyphs.png`

## Notes

- Identity list remains D1/D2 only; both closed FIXED across rounds 2 and 3.
- Fixture / scene font for `ui_integration_text_glyphs` now paints readable
  “UI 42” (post tofu fix); solid tofu blocks are no longer the live baseline.
- Automated fix loop is complete; goldens under
  `plugins/ui/testdata/ui_integration_*.png` remain the regression lock.
