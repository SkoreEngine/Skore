# Material Node System — Roadmap & Status

Living tracking document for the node-based material system (editor-authored shader graphs that
compile to HLSL → SPIR-V).

**Status legend:** `[x]` done · `[~]` partial / in progress · `[ ]` not started

---

## 1. Current state (what exists)

Core pipeline is working end-to-end: author a graph → generate HLSL → compile to SPIR-V in the editor.

- [x] **Resource data model** — `MaterialGraphResource` / `MaterialGraphNodeResource` /
  `MaterialGraphConnectionResource` (`Runtime/.../Graphics/GraphicsResources.hpp`, registered in
  `RegisterGraphicsTypes.cpp`). Connections reference node sub-objects; `RID.id` is the visual id.
- [x] **Polymorphic node registry** — abstract `MaterialNode : Object` + one subclass per node,
  auto-discovered via reflection (`Editor/.../MaterialGraph/MaterialNode.{hpp,cpp}`). Adding a node =
  subclass + `Reflection::Type<MyNode>()` in `RegisterMaterialNodes()`.
- [x] **HLSL codegen + SPIR-V compile** — `MaterialGraphCompiler` (post-order DFS from the output
  node → standalone `MainPS` → `CompileShader`). Type coercion via `MaterialConvertExpr`.
- [x] **Editor window** — `MaterialGraphEditorWindow` (add/move/delete/connect, value inspector,
  inline default-value widgets on unconnected input pins, Build button + HLSL/log panel).
- [x] **Asset handler** — `.matgraph` create/open + content-browser "Create > New Material Graph".
- [x] **Tests** — `Tests/Source/Editor/MaterialGraphTests.cpp` (16 cases, incl. generic-math
  type promotion + live SPIR-V compile).

### Nodes implemented so far
- [x] Material Output (master) — Base Color, Metallic, Roughness, Emissive, Normal, Ambient Occlusion, Opacity
- [x] Constant Float · Constant Color · Constant Vector2
- [x] Texture Coordinate (UV0)
- [x] Sample Texture 2D · Normal Map · Tiling & Offset
- [x] Multiply · Add · Lerp
- [x] Subtract · Divide · Power · Min · Max · Step
- [x] One Minus · Saturate · Clamp · Smoothstep · Remap
- [x] Dot · Normalize · Length

---

## 2. Master / Output node expansion

Map the master node to the full PBR `MaterialSample` (baseColor, alpha, normal, roughness, metallic,
emissive, occlusion). Pixel-stage outputs first; vertex-stage later.

- [x] Base Color (Vec3)
- [x] Metallic (Float)
- [x] Roughness (Float)
- [x] Emissive (Vec3)
- [x] Normal (Vec3, tangent-space) — pin + surface var emitted; TBN→world transform comes with runtime integration
- [x] Ambient Occlusion (Float)
- [x] Opacity (Float) — single pin for now; blend-vs-cutout (Opacity Mask) split deferred to AlphaMode support
- [ ] World Position Offset (Vec3, **vertex stage**) — requires emitting a vertex shader, not just PS
- [ ] *Advanced surface outputs:* Clear Coat · Anisotropy · Subsurface · Refraction

---

## 3. Node library

### Tier 1 — core (build first)
- [~] Texture Coordinate (UV0 done; add UV1)
- [ ] Vertex Color
- [ ] World Normal · World Position · View Direction
- [ ] Time
- [ ] Camera Position · Object Position
- [x] Sample Texture 2D
- [x] Normal Map sample (unpack + strength)
- [x] Tiling & Offset (UV transform)
- [x] Subtract · Divide · Power
- [x] Min / Max · Clamp / Saturate · One-Minus
- [x] Dot · Normalize · Length
- [ ] Fresnel
- [ ] Make/Combine (float→vector) · Split/Break · Component Mask (swizzle)
- [x] Lerp
- [x] Step · Smoothstep · Remap

### Tier 2 — production baseline
- [ ] Math: Abs · Floor/Ceil/Frac · Fmod · Sqrt · Sign · Sin/Cos · Cross · Reflect · Distance · Atan2
- [ ] Color: Desaturate/Luminance · HSV↔RGB · sRGB↔Linear · Contrast/Saturation · Blend modes · Channel pack
- [ ] UV: Panner (scroll) · Rotator · Flipbook (sprite sheet) · Polar coords
- [ ] Texture: Sample Cubemap · Sample Texture Array · Triplanar
- [ ] Logic: If/Comparison · Boolean ops · runtime Switch

### Tier 3 — advanced / polish
- [ ] Procedural: Perlin/Simplex noise · Voronoi/Worley · Gradient · Checkerboard · Hash/Random
- [ ] Normals: Normal Blend (reoriented/whiteout) · Normal-from-height (bump) · Derive Normal Z
- [ ] Effects: Dither (alpha-to-coverage) · Parallax/POM · Depth Fade · Posterize

---

## 4. System features (not nodes, but required for "production")

These are higher-impact than any single node.

- [ ] **★ Material Parameters + Material Instances** — named exposed params (Scalar/Vector/Color/
  Texture/Bool) packed into a constant buffer / `MaterialData`, overridable per-instance without
  recompiling. Touches the resource model + codegen. *The keystone feature.*
- [ ] **★ Custom HLSL / Expression node** — raw-code escape hatch with typed pins. Ships before the
  library is complete.
- [ ] **★ Static Switch → shader variants** — compile-time branches emitting macro variants (shader
  system already supports macros/variants). Avoids runtime branch cost.
- [ ] **Material Functions / Subgraphs** — reusable nested node groups.
- [ ] **Reroute / knot nodes** + **Comment frames** — graph ergonomics.
- [~] **Per-node preview thumbnails** (engine has `PreviewGenerator`) + live material preview —
  texture nodes already render the assigned texture's thumbnail (`ResolveThumbnail`). **Remaining:**
  live preview of a node's *computed output* + a full material preview.
- [~] **Texture / bindless wiring** — codegen emits a bindless `MaterialTextures[]` array + sampler
  and samples it; texture slot is currently the node index (placeholder). The `Texture` reference
  field on `MaterialGraphNodeResource` is now consumed: drag-drop a `TextureResource` onto a node
  assigns it (or onto the canvas to auto-create a pre-wired sample node), with a thumbnail shown.
  **Remaining:** resolve real runtime bindless indices via the material's texture table (ties into
  parameters + runtime consumption).
- [ ] **Codegen quality** — constant folding / dead-code elimination · DDX/DDY-aware sampling.
- [ ] **Runtime consumption** — feed generated SPIR-V into a `ShaderResource` and the real
  `Materials.hlsli` / `SampleMaterial` surface pipeline (currently a standalone PS).

---

## 5. Suggested build order

1. **Master node outputs** — add Normal, AO, Opacity/Mask (pixel stage).
2. **Sample Texture 2D + Normal Map + Tiling/Offset** — wire bindless textures via the `Texture` field.
3. **Scalar/Vector Parameter nodes + the instance/param buffer** — the production keystone.
4. **Custom HLSL node** — usable before the library is "done."
5. Fill Tier 1 math/vector/utility, then **Reroute + Comment** for ergonomics.

> Most library nodes are mechanical (subclass + register). The real engineering is in #1–#3 (master
> outputs, texture/bindless wiring, parameters), which extend the resource model and codegen.
