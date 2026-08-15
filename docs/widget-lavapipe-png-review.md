# Lavapipe widget PNG review (check #3)

Shared path for **WIDGET_MANIFEST.md** acceptance check #3: render a named
sk-ui widget offscreen under Mesa lavapipe, write PNGs, and **look at them**.

This is a **committed sandbox host**, not a test.

- Do **not** register it with CTest / `SK_TEST` / skore-test-suite.
- Do **not** shell out to grok or any other LLM CLI. Image judgement is the
  agent opening the PNGs in-session.
- Reuse `sk_ui_api_t::cpu_image_write_png` (existing PNG path). Do not add
  another encoder.

Host: `sandbox/dock_preview_sandbox.c` + `sandbox/widget_review.c` → `sk-sandbox`.
Catalog names and applicable states live in `sandbox/widget_review.c` and
follow `docs/WIDGET_MANIFEST.md`.

---

## Run recipe (every later widget task)

From the `skore/` repo root. Cwd for the binary **must** be `build/bin` so
`{app_folder}/plugins` resolves (`sk-ui`, `sk-vulkan-render-device`,
`sk-dxc-compiler`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sk-sandbox

# Force the Mesa lavapipe software Vulkan ICD (headless / no GPU).
# Set both names: older loaders read VK_ICD_FILENAMES; newer prefer VK_DRIVER_FILES.
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json

(cd build/bin && ./sk-sandbox --widget button --out ../widget-review)
```

Windows (same flags; use a software ICD or a real adapter if lavapipe is absent):

```bat
cmake --build cmake-build-debug --target sk-sandbox
cd cmake-build-debug\bin
sk-sandbox.exe --widget button --out ..\widget-review
```

List catalog names without touching the GPU:

```bash
./build/bin/sk-sandbox --list
```

Capture one state while iterating:

```bash
(cd build/bin && ./sk-sandbox --widget button --state hovered --out ../widget-review)
```

---

## Where PNGs land

`--out` is a **directory** when `--widget` is set (default: `./widget-review`
relative to the process cwd). Parent directories are created by
`cpu_image_write_png`.

With the recipe above (cwd `build/bin`, `--out ../widget-review`):

```
build/widget-review/button_default.png
build/widget-review/button_hovered.png
build/widget-review/button_pressed.png
build/widget-review/button_disabled.png
build/widget-review/button_focused.png
```

Naming: `{catalog_name}_{state}.png`.

### Verified from a clean checkout (APX-344)

Reproduced on Linux with Mesa lavapipe from a fresh `build/` directory using
only the commands above (no extra packages, flags, or env vars). From cwd
`build/bin`, `--widget button --out ../widget-review` wrote all five
`build/widget-review/button_{default,hovered,pressed,disabled,focused}.png`
frames. Each decoded as a readable button with label text: hovered brightens
the fill, pressed darkens it, disabled greys it out, focused adds a light
outline around the same default fill. `--list` and the single-state
`--state <name>` capture also worked as written.

States (as applicable to the family): `default`, `hovered`, `pressed`,
`disabled`, `focused`. Pressed is `SK_UI_STATE_ACTIVE`.

Without `--widget`, `sk-sandbox` still writes the dock drop-preview PNG
(`--out` is a **file**, default `./dock_preview.png`).

---

## Loop

1. Build `sk-sandbox`.
2. Run under the lavapipe env vars above.
3. Open the PNGs. Exit code 0 is not a visual check.
4. Fix paint / style / layout. Re-run the same command. Repeat until the
   frame matches the **editor** overload in `WIDGET_MANIFEST.md`, not a
   generic sample.

Do not tick the manifest “lavapipe PNG reviewed” box until that visual
judgement is done for the editor behaviour listed there.

---

## Adding a later widget scene

1. Keep the `--widget` name in the catalog in `sandbox/widget_review.c`
   (already listed from the manifest).
2. Add a `sandbox_widget_build_*` that creates the factory node, sets a
   stable id, and returns the interaction target.
3. Point the catalog row’s `build` at that function. Leave `states` as the
   applicable default/hovered/pressed/disabled/focused mask.
4. Use this same recipe. Do not add CTest coverage for the PNGs.

Worked scene that proves the loop: `--widget button`.
