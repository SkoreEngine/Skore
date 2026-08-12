# UI vision rubrics and assertion helper (APX-251)

Per-widget **strict vision rubrics** and a reusable assertion helper that asks
**grok vision** to confirm fine-grained rendering details — not a vague “does
this look right”.

This is the grading contract for APX-252…APX-255 (per-widget vision tests),
APX-257 (flexbox layout samples), and builds on the same grok-vision path used
for the snapshot audits (`docs/ui-vision-audit-round-*.md`).

---

## Layout

| Path | Role |
| --- | --- |
| `plugins/ui/testdata/vision/rubrics/*.txt` | Authored rubric text per widget family (source of truth on disk) |
| `plugins/ui/testdata/vision/fixtures/*` | Good / deliberately corrupted reference frames for helper verification |
| `tests/integration/ui_vision_assert.h` | C helper API |
| `tests/integration/ui_vision_assert.c` | Helper + unit/integration tests (embedded rubric catalog mirrors the `.txt` files) |
| `scripts/ui_vision_assert.py` | Grok-vision backend (xAI chat completions with image) |

---

## Widget families and hard rules

| Family | File | Must-pass detail (examples) |
| --- | --- | --- |
| `button` | `button.txt` | Clear rectangular chrome; disabled is dimmed |
| `checkbox` | `checkbox.txt` | **Checked = X mark** (two diagonals). Fail on filled square, dot, or checkmark/tick glyph |
| `radio` | `radio.txt` | **Checked = filled inner circle** inside the outer ring |
| `toggle` | `toggle.txt` | Pill track + distinct thumb; ON/OFF position differs |
| `slider` | `slider.txt` | **Distinct grab handle** on a track (not a bare progress bar) |
| `progress` | `progress.txt` | Fill fraction matches claim; **no** grab handle |
| `text_input` | `text_input.txt` | Field chrome; focused border/caret; disabled dimmed |
| `scrollbar` | `scrollbar.txt` | Track + thumb shorter than track |
| `scroll_view` | `scroll_view.txt` | Viewport clip; claimed scrollbars present |
| `panel` | `panel.txt` | Solid surface + border; children inside chrome |
| `label` | `label.txt` | Readable glyphs |
| `image` | `image.txt` | Non-zero region; bound texture when claimed |
| `window` | `window.txt` | Title bar band distinct from body; outer border (APX-254) |
| `tab` | `tab.txt` | Selected tab visually distinct from unselected (APX-254) |
| `menu` | `menu.txt` | Popup items + claimed separators (APX-254) |
| `table` | `table.txt` | Header differs from body; striping + column separators (APX-254) |
| `tooltip` | `tooltip.txt` | Compact floating popup with label (APX-254) |
| `flexbox` | `flexbox.txt` | Qualitative arrangement of coloured flex children (APX-257) |
| `disabled` | `disabled.txt` | Cross-cutting dimming rule for any family |

---

## C helper

```c
#include "ui_vision_assert.h"

sk_ui_vision_result_t result;
i32 rc = sk_ui_vision_assert_path(
    ui,                          /* may be NULL if only path grading */
    "path/to/frame.png",
    /*image=*/NULL,
    SK_UI_VISION_WIDGET_CHECKBOX,
    /*state_hint=*/"checked",
    /*scene_name=*/"widget_checkbox_checked",
    sk_filesystem_api(),
    &result);

/* rc: SK_UI_VISION_ASSERT_OK | FAIL | SKIPPED | ERROR */
/* result.passed, result.reason, result.saved_frame_path */
```

In-memory frames:

```c
rc = sk_ui_vision_assert_image(ui, &img, SK_UI_VISION_WIDGET_SLIDER,
                               "value=0.5", "widget_slider", fs, &result);
```

### Return codes

| Code | Meaning |
| --- | --- |
| `SK_UI_VISION_ASSERT_OK` (0) | Model `pass: true` |
| `SK_UI_VISION_ASSERT_FAIL` (1) | Model `pass: false` — **offending frame saved** as `{scene}_vision_fail.png` |
| `SK_UI_VISION_ASSERT_SKIPPED` (2) | No credentials / backend unavailable (use `TEST_IGNORE` in suites that need live vision) |
| `SK_UI_VISION_ASSERT_ERROR` (-1) | Hard failure (I/O, bad args, backend crash) |

`sk_ui_vision_result_t` always carries:

- `passed` — 1 only on OK  
- `reason` — model’s one-sentence deciding detail (or helper diagnostic)  
- `saved_frame_path` — non-empty on FAIL when the frame was written  
- `family_name` — canonical family string  

### Backend selection

| Env | Effect |
| --- | --- |
| `SK_UI_VISION_BACKEND=mock` | Use `SK_UI_VISION_MOCK_RESPONSE` JSON (deterministic unit tests) |
| `SK_UI_VISION_MOCK_RESPONSE` | `{"pass":true\|false,"reason":"..."}` (implies mock) |
| `SK_UI_VISION_SCRIPT` | Override path to `ui_vision_assert.py` |
| `XAI_API_KEY` / `SK_UI_VISION_API_KEY` | Live grok vision |
| `SK_UI_VISION_MODEL` | Default `grok-4.5` |
| `SK_UI_VISION_LIVE=1` | Force live fixture test to run when auth is only in `~/.grok/auth.json` |

---

## Python backend

```bash
python3 scripts/ui_vision_assert.py \
  --image plugins/ui/testdata/vision/fixtures/checkbox_checked_good.png \
  --rubric-file plugins/ui/testdata/vision/rubrics/checkbox.txt \
  --family checkbox --state checked
# → {"pass": true, "reason": "..."}
```

Exit `2` = skipped (no credentials). Exit `0` = graded (inspect JSON for pass/fail).

---

## Verification (APX-251)

1. **Mock plumbing** (`ui_vision_assert_mock_pass_and_fail_saves_frame`):  
   mock pass → OK + empty `saved_frame_path`; mock fail → FAIL + frame saved.
2. **Live good vs corrupt** (`ui_vision_assert_good_pass_corrupt_fail`):  
   - `checkbox_checked_good.png` → PASS (X mark)  
   - `checkbox_checked_corrupt_filled.png` → FAIL (filled square)  
   - `slider_good.png` → PASS (handle present)  
   - `slider_corrupt_no_handle.png` → FAIL (bar only)  
   Skips cleanly when no API credentials are available.

---

## Authoring a new per-widget vision test

1. Capture a single-widget frame with `sk_ui_capture_harness_capture` (pinned
   DejaVuSans when text is involved — APX-250).
2. Call `sk_ui_vision_assert_path` / `_image` with the matching family + state hint.
3. `TEST_ASSERT_EQUAL_INT(SK_UI_VISION_ASSERT_OK, rc)` (or ignore on SKIPPED if
   the suite is optional without keys).
4. On FAIL, open `{scene}_vision_fail.png` under `SK_TEST_ARTIFACT_DIR` /
   `build/test-artifacts` and read `result.reason`.

## Flexbox layout samples (APX-257)

Representative subset of the APX-256 flexbox geometry matrix (one case each:
wrap, justify space-between, align-items center, grow, gap, nesting) is rendered
with distinctly coloured children in `tests/integration/ui_flexbox_vision.c`.
Each case asserts parent-content-relative rects numerically, paints solid RGB
fills, then grades the qualitative arrangement via `SK_UI_VISION_WIDGET_FLEXBOX`
and a short human-readable `state_hint` (keep hints under ~200 chars — the
helper’s shell escape buffer is 256). Vision SKIPPED without credentials does
not fail the suite; numeric + painted bboxes still guard.
