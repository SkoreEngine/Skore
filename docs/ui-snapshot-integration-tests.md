# UI snapshot integration tests (APX-238 harness inventory)

Inventory of integration tests that render UI and write PNG snapshot artifacts.
This document establishes **run commands, output paths, naming, counts, and
prerequisites** only — not image defect analysis.

Verified on Linux with Mesa lavapipe (`VK_ICD_FILENAMES=…/lvp_icd.json`),
Debug Ninja build, 2026-08-11.

---

## Run command(s)

From the repo root (`skore/`):

```bash
# Configure + build (once)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sk-integration-tests

# Optional: force a software Vulkan ICD when no GPU is present
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json

# Run full integration suite (cwd must be build/bin so plugins resolve)
(cd build/bin && ./sk-integration-tests)

# Equivalent via CTest
ctest --test-dir build -R sk-integration-tests --output-on-failure
```

Golden regeneration (overwrites **checked-in** goldens under
`plugins/ui/testdata/`, not the per-run snapshot root):

```bash
./scripts/regen-ui-goldens.sh
# or: BUILD_DIR=build SK_UI_REGEN_GOLDENS=1 (cd build/bin && ./sk-integration-tests)
```

Optional debug:

| Env | Effect |
| --- | --- |
| `SK_TEST_ARTIFACT_DIR` | Override snapshot PNG root (default: `build/test-artifacts`) |
| `SK_UI_REGEN_GOLDENS=1` | Bless / rewrite committed goldens |
| `SK_UI_MEASURE=1` | Print measured histograms/bboxes for UI integration scenes |

---

## Snapshot output directory and naming

**Primary always-on snapshot root** (compile-time `SK_TEST_ARTIFACT_DIR`):

```
build/test-artifacts/
```

Path resolution order (see `plugins/ui/image_write.c`):

1. Runtime env `SK_TEST_ARTIFACT_DIR`
2. Compile-time `SK_TEST_ARTIFACT_DIR` → `${CMAKE_BINARY_DIR}/test-artifacts`
3. Fallback `{temp}/skore-test-artifacts`

**Naming scheme:** `{sanitized_scene_name}.png`

- Scene / test name is sanitized to one path component (alphanumeric, `-`, `_`, `.`; other chars → `_`).
- Written via `ui->test_artifact_png_path(fs, name, …)` + `ui->cpu_image_write_png(…)`.
- The capture harness writes the PNG **before** returning so later assertion failures still leave an inspectable frame.

**Secondary / conditional paths:**

| Path | When written |
| --- | --- |
| `build/bin/ui_fixture_actual.png`, `build/bin/ui_fixture_diff.png` | `ui_offscreen_draw_list_golden` on golden mismatch (or bootstrap); uses `SK_INTEGRATION_ARTIFACT_DIR` (= runtime output dir) |
| `plugins/ui/testdata/ui_fixture_golden.png` | Only with `SK_UI_REGEN_GOLDENS=1` |
| `plugins/ui/testdata/ui_integration_*.png` | Committed goldens for compare (not re-written on a normal pass) |
| `{root}/{name}_actual.png`, `_expected.png`, `_diff.png` | On `cpu_image_compare_golden` mismatch |

---

## Integration tests that write PNG snapshots

Source files under `tests/integration/`:

| Test (`SK_TEST`) | File | Always writes snapshot? | Artifact name |
| --- | --- | --- | --- |
| `ui_capture_offscreen_readback` | `ui_capture.c` | Yes | `ui_capture_offscreen_readback.png` |
| `ui_capture_harness_deterministic` | `ui_capture_harness.c` | Yes | `ui_capture_harness_deterministic.png` |
| `ui_capture_harness_clear_color` | `ui_capture_harness.c` | Yes (same path twice) | `ui_capture_harness_clear_color.png` |
| `ui_integration_empty_frame` | `ui_integration.c` | Yes (via harness) | `ui_integration_empty_frame.png` |
| `ui_integration_single_button` | `ui_integration.c` | Yes (via harness) | `ui_integration_single_button.png` |
| `ui_integration_layout_nested` | `ui_integration.c` | Yes (via harness) | `ui_integration_layout_nested.png` |
| `ui_integration_text_glyphs` | `ui_integration.c` | Yes (via harness) | `ui_integration_text_glyphs.png` |
| `ui_offscreen_draw_list_golden` | `ui_render.c` | **No** on pass | Only mismatch/regen under `build/bin/` / testdata golden |

Related non-snapshot note: `vulkan_offscreen_triangle_render` writes a **PPM**
(`build/bin/sk-triangle-render.ppm`), not a PNG.

---

## How many snapshots a full run produces

When Vulkan is available and UI capture tests run (not `TEST_IGNORE`):

| Kind | Count | Location |
| --- | --- | --- |
| Always-on UI capture PNGs | **7** | `build/test-artifacts/*.png` |
| Golden-mismatch extras | 0 on clean golden pass | `{root}/{name}_{actual,expected,diff}.png` |
| `ui_fixture_*` under `build/bin/` | 0 on clean golden pass | only mismatch/regen |

Observed full-run PNG list (this inventory run):

1. `ui_capture_offscreen_readback.png` (64×64)
2. `ui_capture_harness_deterministic.png` (64×64)
3. `ui_capture_harness_clear_color.png` (64×64)
4. `ui_integration_empty_frame.png` (96×96)
5. `ui_integration_single_button.png` (128×96)
6. `ui_integration_layout_nested.png` (256×192)
7. `ui_integration_text_glyphs.png` (192×96)

Without a Vulkan ICD/adapter the UI capture tests **skip** and produce **0**
snapshot PNGs.

---

## Setup prerequisites

1. **Build tools:** CMake ≥ 3.22, Ninja, C compiler (see root `README.md`).
2. **Linux packages (typical):** X11/OpenGL headers for GLFW link; Vulkan loader + ICD.
   - GPU optional: Mesa lavapipe (`mesa-vulkan-drivers`, `lvp_icd.json`) works headless.
3. **Build targets:** `sk-integration-tests` (pulls `sk-ui`, `sk-vulkan-render-device`,
   `sk-dxc-compiler`, and peers into `build/bin/plugins/`).
4. **Working directory:** run binary from `build/bin` (CTest already sets this).
5. **Display:** **not required** for these offscreen capture tests (no window / swapchain).
6. **DXC runtime:** must load next to plugins (`libdxcompiler` vendored/copied by build).
7. **Compile defs of interest:** `SK_TESTS`, `SK_TEST_ARTIFACT_DIR`, `SK_UI_GOLDEN_DIR`,
   `SK_INTEGRATION_ARTIFACT_DIR` (set in `tests/integration/CMakeLists.txt`).

---

## End-to-end verification (APX-238)

Commands used:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sk-integration-tests
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
(cd build/bin && ./sk-integration-tests)
```

Results:

- **7 fresh PNG files** written under `build/test-artifacts/` (mtimes after run start).
- Suite summary: **43 tests, 1 failure, 0 ignored**.
- Failure: `ui_integration_layout_nested` structural bbox assert
  (`cpu_image_assert_bbox` for body-red color; harness still wrote the PNG first).
- `ui_offscreen_draw_list_golden` **PASS** (0 failing pixels vs golden; no
  `ui_fixture_actual.png` written).
- No image content / defect review performed (out of scope for this task).

---

## Related unit-test goldens (not integration)

For completeness only — soft-render widget/sample goldens live under
`#ifdef SK_TESTS` in `plugins/ui/*.c` and are exercised by `sk-tests`, writing
checked-in PNGs under `plugins/ui/testdata/widgets/` and
`plugins/ui/testdata/sample/` when `SK_UI_REGEN_GOLDENS=1`. Those are **not**
the integration snapshot path inventoried above.
