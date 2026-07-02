# Material System — Remaining Work

Snapshot of what is still missing after the 2026-07 batches (first batch: TBN/PBR shading,
sampler + sRGB handling, graph-instance imports, per-object custom material shaders, transparent
pass, first validation batch; second batch: legacy-material retirement — default graph material
fallback, legacy import/runtime paths deleted, sky-only `.material`, graph-typed material slots,
`.matgraph` thumbnails; third batch — 2026-07-02 render rework: the new pipeline is now
self-sufficient — real light list, cascade shadows, IBL, GPU culling, skybox, bloom + ACES
tonemap all landed in `PipelineNew`, so §2.1/§2.2 below are mostly closed). Companion to the living
checklist in `MaterialNodeSystem.md`; this file is the prioritized "what's next" view.

Items are ordered by how much they block: shipping first, self-sufficiency second, tooling third,
expansion last.

---

## 1. Blocking a shipped build

### 1.1 Variant cooking ★ (needs a design decision)

Compiled graph variants attach to the shader that hosts them — and the default host,
`Skore://ShadersNew/DefaultForward.shader`, lives in the **embedded engine package inside the
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

This is now strictly the top shipping blocker: since the mesh fallback is a graph instance
(`DefaultMaterial.matgraph`, see 1.2), even a scene with no authored materials renders the gray
template in a runtime-only build.

### 1.2 Default material story — done

Done in the 2026-07-01 retirement batch:

- Mesh fallback is `Skore://MaterialGraphs/DefaultMaterial.matgraph` (hand-authored `Kind=Instance`
  of `MaterialBase`, no overrides); `DefaultMaterial.material` deleted. Adding/removing the
  embedded assets needs a CMake re-configure (CMRC glob).
- The legacy PBR packing in `UpdateMaterialStorageData` is deleted; any non-graph material writes
  the white stub block (`WriteMaterialStubData`), so stale legacy assets render white rather than
  disappearing, and the old pipeline / material-mask readback keep working. `SampleSurface` in
  `GlobalBindingsNew.hlsli` and the b0 stub itself only die together with the old pipeline.
- The `MaterialResource` asset type is fully removed (struct/reflection/`.material` handler/
  field-visibility rows/sky enum+fields). The sky is no longer a material: `EnvironmentComponent`
  references a `TextureResource` directly and `EnvironmentResourceCache` bakes it into the cubemap +
  IBL. Material slots (`MaterialArray`) are typed to `MaterialGraphResource`.

**Out of scope (decided):** existing project `.material` PBR assets are *not* being migrated. With
the `.material` handler deleted they are orphaned and render as the white stub — re-author them as
`MaterialBase*` graph instances by hand where still needed. No UUID-preserving auto-migration is
planned.

---

## 2. Making the new renderer self-sufficient

### 2.1 Lighting, shadows, IBL — landed (2026-07-02)

`DefaultForward.hlsl`'s Cook-Torrance GGX shading is now fed by real scene data, not the hardcoded
sun:

- **Light list** — `LightSetupPassNew` gathers every `LightComponent` into a storage buffer
  (`LightDataNew`, scene space1 binding 10; type/direction/color/intensity/range/cone angles) plus a
  per-frame `LightBufferNew` UBO (light count, shadow light index, ambient, reflection, cascade
  splits + matrices).
- **Ambient + reflections (IBL)** — driven by `EnvironmentComponent`: skybox diffuse-irradiance and
  specular-prefilter cubemaps (or a flat ambient color) plus a runtime `BRDFLUTTexture`, bound on
  the scene set (bindings 5/7/8/9).
- **Shadows** — `CascadeShadowPassNew` renders a `MaxNumCascades`-layer CSM
  (`ShadowMapIndirectNew` shader) with GPU per-cascade culling, texel-snapped stabilization,
  interleaved cascade updates, depth bias, and a masked-alpha (`HAS_MASK`) variant;
  `LightSetupPassNew` binds the array texture + comparison sampler (bindings 3/4).

Remaining lighting polish: only the first directional light casts shadows (no point/spot shadows),
and no light culling/clustering yet if the light count grows large.

### 2.2 Rest of the PipelineNew passes — mostly landed

Since the first material batches: **GPU culling** (`CullingPassNew` — frustum + LOD select into a
flat indirect-draw buffer, consumed by `ForwardPassNew` via `DrawIndexedIndirectCount`), **skybox**
(`ForwardPassNew::RenderSky`, drawn between opaque and transparent using `SkyboxRender.raster`), and
**post-processing** (`PostProcessPassNew` — ACES tonemap + sharpen + bloom composite; `BloomPassNew`
— prefilter / downsample / upsample chain). The HDR `RGBA16_FLOAT` forward color is resolved to the
`RGBA8_UNORM` display output by `PostProcessPassNew`, so the earlier "no tonemap step" caveat is
gone.

Still missing vs the old pipeline: **depth prepass** and **anti-aliasing** (no MSAA/TAA/FXAA).

### 2.3 Transparent sorting

`ForwardPassNew` renders the transparent bucket in bucket order; per-drawcall back-to-front sorting
by view depth is missing, so overlapping transparents can pop.

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
- Stage compatibility checks — now live: the World Position Offset subgraph is compiled as vertex
  stage, so pixel-only constructs need flagging (texture sampling is already handled via
  `SampleLevel`, but future pixel-only nodes won't be).
- Dead/unreachable node warnings.
- Surfacing errors on the actual node/pin in `MaterialGraphEditorWindow` instead of the build log.

### 3.2 Live previews

Texture nodes show the assigned texture's thumbnail, and `.matgraph` assets now have
project-browser thumbnails (`MaterialGraphPreviewGenerator`, sphere + material) — but the preview
scene renders through the legacy preview pipeline, where graph materials are the white stub, so the
thumbnail doesn't show the actual graph output. Missing: previews through the new pipeline (same
generated shader contract as runtime rendering) and per-node preview of the *computed* output.

### 3.3 Test gates

35 editor test cases exist (codegen incl. Tier 1 nodes + vertex-stage WPO, bindless/sRGB,
validation, live SPIR-V compile of both stages). Missing: golden generated-HLSL snapshots,
serialization/migration tests, parameter layout/override tests, an alpha-mode variant test, and a
real render smoke test (GPU test infra exists — see `Tests/Source/GPU/`).

---

## 4. System expansion

### 4.1 Type + stage model expansion (gates everything below)

`MaterialDataType` is scalar/vector only. Add pin categories for bool/int values, texture/sampler
objects, and shader-stage availability (pixel / vertex / shared). Prerequisite for Custom HLSL and
static switches. (A narrow vertex-stage path shipped with World Position Offset —
`MaterialCodegenContext::vertexStage` — but per-pin stage tagging is still this item.)

### 4.2 Custom HLSL node → Static Switches → Material Functions

- **Custom HLSL / Expression node** — raw-code escape hatch with typed pins; after 4.1 and
  validation are strong enough to contain arbitrary snippets.
- **Static Switch → shader variants** — compile-time branches as macro variants (the shader system
  already supports variants); needs a variant key model, instance override rules, and cache
  invalidation.
- **Material Functions / subgraphs** — signature resources, cross-asset dependency + cycle
  tracking, build invalidation.

### 4.3 Node library gaps

- **Tier 1: done (2026-07-02)** — Vertex Color, World Normal/Position, View Direction, Time,
  Camera/Object Position, Fresnel, Combine/Split/Component Mask, Constant Vec3/Vec4, and UV1 all
  landed, plus the **World Position Offset** output with its vertex-stage codegen
  (`@SK_MATERIAL_VERTEX_GRAPH@` splice; ignored by compute/RT hosts; shadows don't apply WPO yet).
  Input nodes read the shared `MaterialInputs` struct, so non-raster material hosts support them by
  filling the struct.
- **Tier 2:** math batch (Abs/Floor/Frac/Fmod/Sqrt/Sign/Sin/Cos/Cross/Reflect/Distance/Atan2),
  color ops (desaturate, HSV↔RGB, sRGB↔linear, blend modes), UV ops (panner, rotator, flipbook,
  polar), cubemap/array/triplanar sampling, logic (if/comparison, boolean, switch).
- **Tier 3:** procedural noise, normal blend/from-height, dither, parallax/POM, depth fade,
  posterize.

Mostly mechanical: subclass + `Reflection::Type<>()` in `RegisterMaterialNodes()`.

### 4.4 Ergonomics + hygiene

- Reroute/knot nodes + comment frames.
- Graph schema versioning + migration rules (pin layout changes must not silently rewire old
  graphs).
- Codegen quality: common-subexpression reuse, constant folding/DCE, deterministic output,
  compiler-error mapping back to graph nodes.

---

## Needs visual verification (not code work)

Nothing in the 2026-07 batches has been checked visually yet:

- Overall scene brightness / exposure — the PBR rewrite (energy-conserving diffuse `/π`, AO on the
  ambient term) now feeds through real lights, IBL, and the ACES tonemap in `PostProcessPassNew`, so
  the whole response curve wants a look.
- Lighting correctness: multiple `LightComponent`s (directional/point/spot), ambient + reflected
  IBL from `EnvironmentComponent`, and cascade shadows (splits, stabilization, no obvious peter-
  panning / acne with the current depth bias).
- Bloom + tonemap output (`BloomComponent` on/off, intensity) and the skybox drawing behind opaque
  and correctly under transparents.
- A reimported GLTF model through the new base graphs: normal-map orientation (TBN handedness),
  masked foliage (cutoff), metallic/roughness response, sRGB base color.
- Transparent pass compositing over opaque (load-not-clear, blend, no depth write).
- The new default fallback: a mesh with no material assigned should render
  `DefaultMaterial.matgraph` (MaterialBase defaults), not gray/white — requires the CMake
  re-configure so the embedded package picks up the new asset.
- The sky path end-to-end (skybox render + IBL bake) now that the sky is a `TextureResource` on
  `EnvironmentComponent`, not a material.
- The Tier 1 node batch in a real scene: a Time-driven World Position Offset (vertex animation —
  also confirms the un-offset shadow limitation is acceptable), Fresnel rim on a sphere, UV1
  sampling on a mesh that has a second UV set, and vertex color / object position reads.
