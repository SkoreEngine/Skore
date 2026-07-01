# Material System — Remaining Work

Snapshot of what is still missing after the 2026-07-01 batch (TBN/PBR shading, sampler + sRGB
handling, graph-instance imports, per-object custom material shaders, transparent pass, first
validation batch). Companion to the living checklist in `MaterialNodeSystem.md`; this file is the
prioritized "what's next" view.

Items are ordered by how much they block: shipping first, self-sufficiency second, tooling third,
expansion last.

---

## 1. Blocking a shipped build

### 1.1 Variant cooking ★ (needs a design decision)

Compiled graph variants attach to the shader that hosts them — and the default host,
`Skore://ShadersNew/ForwardOpaque.raster`, lives in the **embedded engine package inside the
runtime binary**. `ResourceAssets::ExportPackages` (called from `Editor.cpp` export) only writes
project + referenced packages, never the embedded one, so a runtime-only build finds no variants
and renders every graph material with the default gray template.

What already works in our favor: `ResourceAssetHandler::Export` serializes the current in-memory
resource state, so any variant compiled during the session ships with the asset that owns it — the
problem is purely *which asset owns the variant*.

Two designs:

- **Option A — project-level shader overrides.** Export every `IsMaterial` shader that holds graph
  variants as a cooked asset in the project export; the player's deserialize-by-UUID replaces the
  embedded resource. Smallest code change, but the project export mutates engine-package resources
  and the replace-on-load semantics need verification.
- **Option B (recommended) — variants cooked onto the graph asset.** Store each compiled
  `ShaderVariantResource` (SPIR-V + PipelineDesc + stages, tagged with the graph RID) in the
  `.matgraph` asset's cooked payload, and register it into the target shader's variant list when
  the graph loads at runtime. Keeps the engine package immutable and the dependency direction
  (graph → shader) clean. Needs: a cook step in `MaterialGraphHandler`, a runtime load hook, and a
  key for which shader the variant was compiled against (path id + source hash).

Either way, export needs a **warm-up pass**: compile the default forward variant for every graph in
the exported set (`RenderResourceCache::EnsureMaterialVariant`), because variants otherwise only
exist if the material happened to be rendered during the session. Pairings with custom material
shaders are only discoverable from scene data — accept "rendered at least once" for those, or scan
scenes at export.

### 1.2 Default material story

- Legacy `MaterialResource` renders **flat gray** in the new pass: `UpdateMaterialStorageData`
  still writes its PBR block to `MaterialDataBuffer` (b0), but `ForwardOpaque.raster` never reads
  it (`SampleSurface` in `GlobalBindingsNew.hlsli` is dead code). Decide: give the template a
  legacy-material read path, or migrate legacy assets to graph instances and delete the path.
- The mesh fallback is still `Skore://Materials/DefaultMaterial.material` (legacy → gray). A
  default **graph** material should replace it; `CreateGraphicsDefaultValues`
  (`GraphicsResources.cpp`) is currently commented out and is the natural place.

---

## 2. Making the new renderer self-sufficient

### 2.1 Real lighting

`ForwardOpaque.raster` now does Cook-Torrance GGX with TBN normals, but the light is one hardcoded
directional sun (×3 intensity) + constant ambient. Missing, in rough order:

- Light list (the old pipeline's `LightSetupPass` contract or a new one).
- Shadow pass — `RenderSceneObjects` already populates `shadowPipelines`; nothing consumes it in
  `PipelineNew`.
- IBL / indirect (the old pipeline has `IndirectLightingModule` for reference).

### 2.2 Rest of the PipelineNew passes

Only `ForwardOpaquePassNew` and `ForwardTransparentPassNew` exist. Still missing vs the old
pipeline: culling, depth prepass, skybox (the Skybox material type only has the legacy path),
post-processing/tonemapping, AA. Note the forward output is `RGBA16_FLOAT` with no tonemap step —
currently relies on whatever consumes `ForwardColor`.

### 2.3 Transparent sorting

`ForwardTransparentPassNew` renders bucket-ordered; per-drawcall back-to-front sorting by view
depth is missing, so overlapping transparents can pop.

### 2.4 Editor affordance for custom material shaders

The runtime honors `DrawPipelineDesc::shader` when the shader is flagged `IsMaterial`, and the
resolver splices the graph into that shader's own source — but there is no UI to assign a shader
per mesh/material, so the feature is only reachable from code (`RenderSceneObject::SetShader`).

---

## 3. Editor / tooling quality

### 3.1 Validation, part 2

Current checks (empty parameter names, duplicate names across parameter types, unconnected master,
cycles, unknown node types, missing output) are **log-only**. Missing:

- Valid pin indices + one-connection-per-input enforcement.
- Stage compatibility checks (matters once vertex-stage outputs exist).
- Dead/unreachable node warnings.
- Surfacing errors on the actual node/pin in `MaterialGraphEditorWindow` instead of the build log.

### 3.2 Live previews

Texture nodes show the assigned texture's thumbnail. Missing: per-node preview of the *computed*
output, and a material-sphere preview that uses the same generated shader contract as runtime
rendering (the engine's `PreviewGenerator` machinery exists for the legacy `.material`).

### 3.3 Test gates

21 editor test cases exist (codegen, bindless/sRGB, validation, live SPIR-V compile). Missing:
golden generated-HLSL snapshots, serialization/migration tests, parameter layout/override tests, an
alpha-mode variant test, and a real render smoke test (GPU test infra exists — see
`Tests/Source/GPU/`).

---

## 4. System expansion

### 4.1 Type + stage model expansion (gates everything below)

`MaterialDataType` is scalar/vector only. Add pin categories for bool/int values, texture/sampler
objects, and shader-stage availability (pixel / vertex / shared). Prerequisite for Custom HLSL,
static switches, and vertex-stage outputs.

### 4.2 Custom HLSL node → Static Switches → Material Functions

- **Custom HLSL / Expression node** — raw-code escape hatch with typed pins; after 4.1 and
  validation are strong enough to contain arbitrary snippets.
- **Static Switch → shader variants** — compile-time branches as macro variants (the shader system
  already supports variants); needs a variant key model, instance override rules, and cache
  invalidation.
- **Material Functions / subgraphs** — signature resources, cross-asset dependency + cycle
  tracking, build invalidation.

### 4.3 Node library gaps

- **Tier 1:** Vertex Color · World Normal · World Position · View Direction · Time · Camera/Object
  Position · Fresnel · Make/Combine · Split/Break · Component Mask (swizzle) · Constant Vec3/Vec4 ·
  UV1 on Texture Coordinate.
- **Tier 2:** math batch (Abs/Floor/Frac/Fmod/Sqrt/Sign/Sin/Cos/Cross/Reflect/Distance/Atan2),
  color ops (desaturate, HSV↔RGB, sRGB↔linear, blend modes), UV ops (panner, rotator, flipbook,
  polar), cubemap/array/triplanar sampling, logic (if/comparison, boolean, switch).
- **Tier 3:** procedural noise, normal blend/from-height, dither, parallax/POM, depth fade,
  posterize.
- **World Position Offset** output — requires vertex-stage codegen (after 4.1).

Mostly mechanical: subclass + `Reflection::Type<>()` in `RegisterMaterialNodes()`.

### 4.4 Ergonomics + hygiene

- Reroute/knot nodes + comment frames.
- Graph schema versioning + migration rules (pin layout changes must not silently rewire old
  graphs).
- Codegen quality: common-subexpression reuse, constant folding/DCE, deterministic output,
  compiler-error mapping back to graph nodes.

---

## Needs visual verification (not code work)

Nothing in the 2026-07-01 batch has been checked visually yet:

- Overall scene brightness — the PBR rewrite changes the response curve (energy-conserving diffuse
  `/π`, sun ×3 to compensate, AO now only on ambient).
- A reimported GLTF model through the new base graphs: normal-map orientation (TBN handedness),
  masked foliage (cutoff), metallic/roughness response, sRGB base color.
- Transparent pass compositing over opaque (load-not-clear, blend, no depth write).
