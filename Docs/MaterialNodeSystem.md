# Material Node System — Roadmap & Status

Living tracking document for the node-based material system (editor-authored shader graphs that
compile to HLSL → SPIR-V). The editor compile path works today; production runtime consumption is
still a separate milestone.

**Status legend:** `[x]` done · `[~]` partial / in progress · `[ ]` not started

---

## 1. Current state (what exists)

Core editor pipeline is working end-to-end: author a graph → generate HLSL → compile to SPIR-V in
the editor. The generated shader is still a standalone preview/test pixel shader, not yet the real
runtime `MaterialSample` / `SampleMaterial` path.

- [x] **Resource data model** — `MaterialGraphResource` / `MaterialGraphNodeResource` /
  `MaterialGraphConnectionResource` (`Runtime/.../Graphics/GraphicsResources.hpp`, registered in
  `RegisterGraphicsTypes.cpp`). Connections reference node sub-objects; `RID.id` is the visual id.
- [x] **Polymorphic node registry** — abstract `MaterialNode : Object` + one subclass per node,
  auto-discovered via reflection (`Editor/.../MaterialGraph/MaterialNode.{hpp,cpp}`). Adding a node =
  subclass + `Reflection::Type<MyNode>()` in `RegisterMaterialNodes()`.
- [x] **HLSL codegen + SPIR-V compile** — `MaterialGraphCompiler` (post-order DFS from the output
  node → standalone `MainPS` → `CompileShader`). Type coercion via `MaterialConvertExpr`. Runtime
  integration with `Materials.hlsli` is not done yet.
- [x] **Editor window** — `MaterialGraphEditorWindow` (add/move/delete/connect, inline default-value
  widgets on unconnected input pins, Build button + HLSL/log panel). Constant/parameter values are
  edited in the **Properties window** (node properties), not in the graph toolbar.
- [x] **Asset handler** — `.matgraph` create/open + content-browser "Create > New Material Graph".
- [x] **Tests** — `Tests/Source/Editor/MaterialGraphTests.cpp` (16 cases, incl. generic-math
  type promotion + live SPIR-V compile).

### Nodes implemented so far
- [x] Material Output (master) — Base Color, Metallic, Roughness, Emissive, Normal, Ambient Occlusion, Opacity
- [x] Constant Float · Constant Color · Constant Vector2
- [x] Parameters (named, instance-overridable): Float · Int · Bool · Color · Vector2/3/4 · Texture2D
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

These are higher-impact than any single node. Treat them as release gates before growing the Tier 2/3
node library too far.

- [ ] **★ Runtime material pipeline integration** — generated graph code must feed the real
  `MaterialSample` / `SampleMaterial` path instead of a standalone `MainPS`. It needs to match the
  existing `GlobalBindings.hlsli` / `Materials.hlsli` contract: `MaterialDataBuffer`, material index,
  bindless textures, sampler array, mip feedback writes, alpha discard, roughness floor, and
  tangent-space normal → world-space TBN conversion. Decide the asset boundary: `.matgraph` compiles
  into a `ShaderResource`/variant, or `MaterialResource` references a compiled graph shader.
- [~] **★ Material Parameters + Material Instances** — named exposed params (Scalar/Vector/Color/
  Texture/Bool) packed into a generated parameter table, overridable per-instance without recompiling.
  *The keystone feature.* **Done:** the **Parameters** node category (Float/Int/Bool/Color/Vec2-4/
  Texture2D), each with a `Name` + default value stored on the node resource (`ParameterName` /
  `Value` / `Texture`) and edited in the Properties window; and **Material Instances**, unified into
  the **same `MaterialGraphResource`** via a `Kind` field (`MaterialKind::Graph | Instance`). A graph
  authors the node network; an instance sets `Kind=Instance`, references a `Parent` graph, and stores a
  sparse `Parameters` list of `MaterialParameterOverrideResource` (ParameterName / Value / Texture).
  One `.matgraph` handler backs both, with two content-browser entries ("New Material Graph" →
  Kind=Graph → node editor; "New Material Instance" → Kind=Instance → Properties panel). The
  Properties-window editor (dispatched when the selected asset is Kind=Instance) shows a parent-graph
  picker then lists the parent's named parameters with a per-parameter override checkbox + value editor
  (scalar/vector/color/texture); parent selection is restricted to graph-kind materials (no
  instance-of-instance). Identifying parameter nodes uses a new `MaterialNode::IsParameter()` virtual.
  **Remaining:** collect parameters into a stable `MaterialParams` layout, emit HLSL reads instead of
  inlined defaults, specify packing/alignment, validate empty/duplicate names, apply the instance
  overrides into the runtime parameter buffer (no recompile), and route texture parameter overrides
  through the texture table.
- [~] **★ Texture / bindless wiring** — codegen emits a bindless `MaterialTextures[]` array + sampler
  and samples it; texture slot is currently the node index (placeholder). The `Texture` reference field
  on `MaterialGraphNodeResource` is now consumed: drag-drop a `TextureResource` onto a node assigns it
  (or onto the canvas to auto-create a pre-wired sample node), with a thumbnail shown. **Remaining:**
  build a stable per-material texture table, dedupe repeated texture references, resolve real bindless
  indices via `RenderResourceCache`, choose sampler indices from texture wrap/filter settings, provide
  missing-texture fallbacks, and track color-space intent (sRGB base/emissive vs linear data/normal/
  mask textures).
- [ ] **★ Graph validation + diagnostics** — validate before codegen/build instead of relying on
  fallbacks. Check: one reachable output node, no cycles, valid node types, valid pin indices, one
  connection per input, stage compatibility, required texture/parameter data, duplicate/empty
  parameter names, and unsupported node combinations. Report warnings for dead/unreachable nodes and
  surface errors in the editor with node/pin context.
- [ ] **★ Type + stage model expansion** — `MaterialDataType` is currently scalar/vector only. Before
  Custom HLSL, static switches, texture objects, sampler objects, vertex-stage outputs, or real bool/int
  branches, add explicit pin categories for numeric values, bool/int values, textures/samplers, and
  shader stage availability (pixel, vertex, shared).
- [ ] **★ Custom HLSL / Expression node** — raw-code escape hatch with typed pins. Ships before the
  library is complete, but after the type/stage model and validation rules are strong enough to keep
  arbitrary snippets contained.
- [ ] **★ Static Switch → shader variants** — compile-time branches emitting macro variants (shader
  system already supports macros/variants). Avoids runtime branch cost. Needs a variant key model,
  default values, material-instance override rules, and shader cache invalidation.
- [ ] **Material Functions / Subgraphs** — reusable nested node groups. Needs input/output signature
  resources, dependency tracking, cycle detection across graph assets, local parameter scoping, and
  preview/build invalidation when a function changes.
- [ ] **Reroute / knot nodes** + **Comment frames** — graph ergonomics.
- [~] **Per-node preview thumbnails** (engine has `PreviewGenerator`) + live material preview —
  texture nodes already render the assigned texture's thumbnail (`ResolveThumbnail`). **Remaining:**
  live preview of a node's *computed output* + a full material preview using the same generated shader
  contract as runtime rendering.
- [ ] **Codegen quality** — constant folding / dead-code elimination · deterministic output order ·
  expression/common-subexpression reuse · DDX/DDY-aware sampling · optional debug names/source mapping
  from HLSL compiler errors back to graph nodes.
- [ ] **Asset/versioning/migration** — add graph schema versioning and migration rules for node/pin
  changes. Pin layout changes should not silently reconnect old graphs to the wrong semantic input.
- [ ] **Test coverage gates** — add graph validation tests, golden generated-HLSL snapshots,
  serialization/migration tests, parameter layout/override tests, texture-table/dedupe tests,
  alpha-mode/static-switch variant tests, and at least one runtime/preview render smoke test.

---

## 5. Suggested build order

1. **Runtime material pipeline integration** — make generated graph code feed `MaterialSample` /
   `SampleMaterial` and produce a runtime-consumable shader resource/variant.
2. **Material parameters + material instances** — stable parameter table, override resource, generated
   `MaterialParams` reads, and no-recompile runtime updates.
3. **Texture/bindless table** — real texture references, deduped material texture table, sampler
   selection, color-space handling, and missing-texture fallbacks.
4. **Graph validation + diagnostics** — reject invalid graphs before shader compile and show useful
   node/pin errors in the editor.
5. **Type/stage model expansion** — unblock Custom HLSL, static switches, texture/sampler pins, and
   future vertex-stage outputs.
6. **Custom HLSL + Static Switches** — escape hatch and compile-time variants once validation and
   caching rules are in place.
7. Fill remaining Tier 1 math/vector/utility, then **Reroute + Comment** for ergonomics.
8. Add **World Position Offset** and other advanced outputs after vertex-stage codegen exists.

> Most library nodes are mechanical (subclass + register). The real engineering is runtime shader
> integration, parameter/instance binding, texture table binding, validation, and versioning.
