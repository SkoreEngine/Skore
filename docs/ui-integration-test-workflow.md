# UI integration test workflow (APX-263)

How to run the widget-vision, flexbox, and interaction suites with **one command**,
how CI wires them, how vision credentials are gated, and how to add coverage for
a new widget so it is not left untested by default.

Related design/reference docs:

| Doc | Role |
| --- | --- |
| `docs/ui-vision-rubrics.md` | Per-family rubric contract + helper API |
| `docs/ui-test-engine-design.md` | Code-driven test engine (imgui_test_engine peer) |
| `docs/ui-automation-api.md` | Query / action / harness APIs |
| `docs/ui-snapshot-integration-tests.md` | Capture harness inventory + artifact paths |
| `plugins/ui/ui_test.h` | Authoring API (`SK_UI_TEST`, click/type/assert) |

---

## One command

From the repo root (after a normal CMake configure):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
./scripts/run-ui-integration-tests.sh
```

That script:

1. Builds `sk-integration-tests` and `sk-tests` (skip with `--no-build`).
2. Prefers Mesa lavapipe when `VK_ICD_FILENAMES` is unset
   (`/usr/share/vulkan/icd.d/lvp_icd.json`).
3. Runs **widget-vision**, **flexbox**, **interaction** (integration + plugin),
   and the vision-helper unit tests via `SK_TEST_FILTER` prefix wildcards.
4. Writes PNGs under `build/test-artifacts` (override with `SK_TEST_ARTIFACT_DIR`).

Subset:

```bash
SUITES=widget,flexbox ./scripts/run-ui-integration-tests.sh
SUITES=interaction ./scripts/run-ui-integration-tests.sh --no-build
```

Equivalent CTest entry (integration binary only, no plugin interaction suite):

```bash
ctest --test-dir build -R sk-ui-integration-suites --output-on-failure
```

Filter tokens (also usable manually):

```bash
# Trailing '*' is a prefix match (exact names still work; no bare substring).
export SK_TEST_FILTER='ui_widget_vision_*,ui_flexbox_vision_*,ui_ix_*'
(cd build/bin && ./sk-integration-tests)

export SK_TEST_FILTER='ui_author_ix_*'
(cd build/bin && ./sk-tests)
```

---

## What each suite covers

| Suite | Sources | Binary | Needs |
| --- | --- | --- | --- |
| **Widget vision** | `tests/integration/ui_widget_vision.c` | `sk-integration-tests` | Vulkan ICD for capture; API key for live vision |
| **Flexbox vision** | `tests/integration/ui_flexbox_vision.c` | `sk-integration-tests` | same |
| **Interaction (behaviour)** | `plugins/ui/ui_interaction_tests.c` | `sk-tests` (plugin) | soft-render engine only |
| **Interaction + vision** | `tests/integration/ui_interaction_suite.c` | `sk-integration-tests` | soft-render; API key for post-click vision |
| **Vision helper** | `tests/integration/ui_vision_assert.c` | `sk-integration-tests` | mock always; live optional |
| **Text screenshot (APX-268)** | `tests/integration/ui_text_screenshot.c` | `sk-integration-tests` + `sk-text-screenshot` | Vulkan ICD; no vision needed |

Structural pixel/geometry asserts always run. Live grok-vision grades are
**optional** unless credentials (or `SK_UI_VISION_REQUIRED=1`) say otherwise.

### Deterministic text rendering screenshots (APX-268)

`sk-text-screenshot` renders a fixed text-sample suite (pangram, font sizes
8–96px, colored/alpha-blended text, 2x content-scale text, printable-ASCII
glyph grid + raw atlas dump) through the headless offscreen renderer and
writes PNG captures into `{SK_TEST_ARTIFACT_DIR}/text-screenshot/msdf/` (all
UI text renders through the MSDF pipeline since APX-271):

```bash
cmake --build build --target sk-text-screenshot
build/bin/sk-text-screenshot --verify
# repeat + diff the trees to prove byte-identical determinism
```

`--verify` re-captures every sample and byte-compares (raw readback + PNG
bytes). The same suite runs from CTest as `sk-text-screenshot`, and inside
`sk-integration-tests` as `ui_text_screenshot_determinism` /
`ui_text_screenshot_suite_completeness` (`SUITES=text-screenshot` via
`run-ui-integration-tests.sh`). Rotation is not supported by the UI (no
transform API), so the suite covers scaling only.

---

## Vision credentials gate (never silent pass)

| Situation | Behaviour |
| --- | --- |
| `XAI_API_KEY` or `SK_UI_VISION_API_KEY` set (or `~/.grok/auth.json`) | Live grades run; FAIL fails the test and saves `{scene}_vision_fail.png` |
| No credentials | Each skip prints `VISION SKIPPED: scene=…` to stderr; after structural work the test is **Unity IGNORE** with a clear message — **not** a silent PASS |
| `SK_UI_VISION_REQUIRED=1` and no credentials | Same message but **FAIL** (used if a job must not go green without vision) |
| Mock | `SK_UI_VISION_BACKEND=mock` + `SK_UI_VISION_MOCK_RESPONSE='{"pass":true\|false,"reason":"…"}'` |

Helpers: `sk_ui_vision_gate_begin` / `_note_skipped` / `_finish` in
`tests/integration/ui_vision_assert.h`. Suites call them from env init/destroy
so multi-state tests still run all structural captures before IGNORE.

---

## CI wiring

GitHub Actions (`.github/workflows/ci.yml`):

1. **Matrix `build-and-test`** (Linux): installs `mesa-vulkan-drivers`, runs
   full `ctest` with lavapipe, uploads `build/test-artifacts/` always.
2. **Job `ui-integration`**: runs `scripts/run-ui-integration-tests.sh` on
   Linux Debug. Passes optional `secrets.XAI_API_KEY` /
   `secrets.SK_UI_VISION_API_KEY`. Uploads UI artifacts (including
   `*_vision_fail.png`) on every run so a red job has a downloadable frame.

Local / Apex gates: `scripts/run-ui-integration-tests.sh` is the same entry
point; see also CTest label `ui-integration` on `sk-ui-integration-suites`.

### Broken widget → failing job + downloadable frame

1. A structural assert (bbox/coverage) fails → test FAIL; capture harness
   already wrote `{scene}.png` under the artifact root.
2. A live vision grade fails → FAIL + `{scene}_vision_fail.png` + reason on
   stderr; CI artifact `ui-integration-test-artifacts` contains the PNG.
3. An authoring assert fails → `sk_ui_test` writes a fail frame path into the
   Unity message (`ui_author_*_fail_*.png`).

Reproduce vision fail without a real widget bug:

```bash
export SK_UI_VISION_BACKEND=mock
export SK_UI_VISION_MOCK_RESPONSE='{"pass":false,"reason":"deliberate mock fail"}'
# Note: widget suites unset mock for live grades; use the helper test instead:
export SK_TEST_FILTER=ui_vision_assert_mock_pass_and_fail_saves_frame
(cd build/bin && ./sk-integration-tests)
# → vision_mock_fail_vision_fail.png under build/test-artifacts
```

---

## How to add a new widget test (default coverage path)

When you add or change a widget, cover it in **both** structural vision and
interaction where it has user input.

### 1. Per-widget vision (`ui_widget_vision.c`)

1. Add a `SK_TEST(ui_widget_vision_<family>)` (or extend the existing family
   test with new states).
2. Build a minimal scene with `sk_ui_capture_harness_capture` (pin DejaVuSans
   when text is involved — `load_test_font = 1`).
3. Structural asserts first: coverage / bbox / mark ink so a stubbed paint path
   fails without any API key.
4. Call `uwv_vision_grade` / `sk_ui_vision_assert_image` with the matching
   `SK_UI_VISION_WIDGET_*` family and a short `state_hint`.
5. Ensure `uwv_env_init` / `uwv_env_destroy` wrap the test (vision gate).
6. Name scenes `ui_widget_vision_<family>_<state>` so filters and artifacts stay
   discoverable.
7. If the family is new: add rubric text under
   `plugins/ui/testdata/vision/rubrics/<family>.txt`, enum + catalog entry in
   `ui_vision_assert.c`, and a row in `docs/ui-vision-rubrics.md`.

### 2. Interaction suite (`ui_interaction_tests.c`)

1. Prefer `SK_UI_TEST(ix_<behaviour>)` in the plugin so it ships with `sk-tests`.
2. Build the tree with stable test ids (`widget_*` id params / `node_set_id`).
3. Drive input only through the authoring API / test engine
   (`sk_ui_click_item`, `sk_ui_type_into`, `sk_ui_drag_item_to`, …) so the same
   path hosts use is exercised.
4. Assert model state (`sk_ui_value_equals`, `sk_ui_item_state`) after steps.
5. Optional: add a post-interaction vision case in
   `tests/integration/ui_interaction_suite.c` if paint after the action matters.

### 3. Flexbox / layout

Geometry matrix unit tests live under `plugins/ui/clay_adapter.c`
(`ui_flex_matrix_*`). Qualitative painted arrangements that need vision go in
`ui_flexbox_vision.c` with `SK_UI_VISION_WIDGET_FLEXBOX` and a short state hint.

### 4. Checklist so new widgets get coverage by default

- [ ] Widget has a `ui_widget_vision_*` state matrix (or is listed as intentionally
      out of scope in the triage doc).
- [ ] If interactive: at least one `SK_UI_TEST(ix_*)` behavioural case.
- [ ] Rubric file exists for the family; hard rules (X mark, thumb, etc.) are
      explicit — not “looks fine”.
- [ ] `./scripts/run-ui-integration-tests.sh` is green locally (structural) and,
      when keys are available, vision grades pass.
- [ ] On deliberate FAIL, a frame appears under `build/test-artifacts/`.

---

## How the vision rubric works

1. **Authored text** per family in `plugins/ui/testdata/vision/rubrics/*.txt`
   (source of truth on disk; mirrored in the C catalog for offline unit tests).
2. **Capture** a single-widget (or flex sample) PNG via the harness / soft-render.
3. **Helper** `sk_ui_vision_assert_path` / `_image` shells to
   `scripts/ui_vision_assert.py` (or mock), sending the image + rubric + state
   hint to grok vision.
4. **Result**: `pass` / `fail` + one-sentence `reason`. On fail the helper saves
   `{scene}_vision_fail.png` under the shared artifact root.
5. **Return codes**: `OK` / `FAIL` / `SKIPPED` / `ERROR` — suites must not treat
   SKIPPED as OK without going through the credential gate.

See `docs/ui-vision-rubrics.md` for the full family table and env vars
(`SK_UI_VISION_MODEL`, `SK_UI_VISION_SCRIPT`, mock knobs).

---

## How to write an interaction test with the engine

Minimal plugin test (runs under `sk-tests`, also filtered by the one-command
runner as `ui_author_ix_*`):

```c
#include "ui_test.h"

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

SK_UI_TEST(ix_my_widget_click) {
  const sk_ui_api_t* ui = t->ui;
  sk_ui_context_t* ctx = t->ctx;
  sk_ui_node_t root = ui->context_root(ctx);
  sk_ui_node_t n = ui->widget_checkbox(ctx, root, 0, "my-cb");
  /* size + place with absolute layout so hit targets are stable */
  /* ... */
  TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
  TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "my-cb"));
  TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
  sk_ui_value_equals(t, "my-cb", "1");
}

#endif
#endif
```

Authoring API surface (`plugins/ui/ui_test.h`):

| Call | Role |
| --- | --- |
| `sk_ui_test_step` / `sk_ui_test_yield` | Advance frames |
| `sk_ui_click_item` / `hover` / `drag_item_to` / `type_into` / `open_menu_path` | High-level actions |
| `sk_ui_item_exists` / `item_rect` / `item_state` / `value_equals` | Asserts (+ fail frame) |
| `sk_ui_test_capture_frame` | Explicit soft-render PNG |

Lower-level engine (`ui->test_engine_*` on `sk_ui_api_t`): create/step/click by
id / drag / key — used by the authoring layer. Design notes:
`docs/ui-test-engine-design.md`.

Integration-side tests that load `sk-ui` from disk (plugin path next to the
binary) live in `ui_interaction_suite.c` and can attach vision after the same
actions.

---

## Environment reference

| Env | Effect |
| --- | --- |
| `SK_TEST_FILTER` | Comma-separated exact names and/or `prefix*` tokens |
| `SK_TEST_ARTIFACT_DIR` | PNG root (default `build/test-artifacts`) |
| `XAI_API_KEY` / `SK_UI_VISION_API_KEY` | Live vision |
| `SK_UI_VISION_REQUIRED=1` | Missing credentials → FAIL instead of IGNORE |
| `SK_UI_VISION_BACKEND=mock` | Deterministic grades via `SK_UI_VISION_MOCK_RESPONSE` |
| `VK_ICD_FILENAMES` | Vulkan ICD (lavapipe path on headless Linux) |
| `SUITES` | Runner suite list (`widget,flexbox,interaction,…`) |
| `BUILD_DIR` | Runner build directory (default `build`) |

---

## Verification checklist (APX-263)

- [x] Single command: `./scripts/run-ui-integration-tests.sh`
- [x] CI job runs the same script; artifacts uploaded always
- [x] Vision without credentials → clear SKIP/IGNORE (not silent PASS)
- [x] Vision with credentials → real grades; FAIL saves downloadable frame
- [x] Docs for new widget test, vision rubric, interaction authoring
