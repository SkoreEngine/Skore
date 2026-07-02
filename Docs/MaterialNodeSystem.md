# Material Node System — Roadmap & Status

Living tracking document for the node-based material system (editor-authored shader graphs that
compile to HLSL → SPIR-V) and its integration with the new render pipeline (`PipelineNew` /
`Assets/ShadersNew`). The prioritized remaining-work breakdown lives in
`MaterialSystemNextSteps.md`.

**Status legend:** `[x]` done · `[~]` partial / in progress · `[ ]` not started

---

## 1. Current state (what exists)

The pipeline works end-to-end **including runtime consumption**: author a graph → generated HLSL is
spliced into the forward template shader → compiled to SPIR-V as a per-material shader variant →
rendered by `ForwardPassNew` with a real parameter buffer and bindless textures, lit by the full
`PipelineNew` chain (light list, cascade shadows, IBL, GPU culling, skybox, bloom + ACES tonemap).

- [x] **Resource data model** — `MaterialGraphResource` / `MaterialGraphNodeResource` /
  `MaterialGraphConnectionResource` / `MaterialGraphPinValueResource` /
  `MaterialParameterOverrideResource` (`Runtime/.../Graphics/GraphicsResources.hpp`, registered in
  `RegisterGraphicsTypes.cpp`). One resource backs both authoring kinds via
  `MaterialKind::Graph | Instance`; `GraphAlphaMode::Opaque | Mask | Blend` + `MaskCutoff` select
  alpha handling. Graph-level render settings on the output node: `GraphShadingModel::DefaultLit |
  Unlit` (shown as "Material"; Unlit bypasses `ShadeForward` via `SK_MATERIAL_UNLIT` and disables all
  pins except Base Color + the alpha-mode pin), `GraphRenderFace::Front | Back | Both` (→ pipeline
  `CullMode`), `DepthWrite` (Bool; unset = on unless Blend) and `DepthTest` (`CompareOp`; unset =
  `Greater`). Resolved into `MaterialResourceCache` (owning graph, so instances inherit) and carried
  through `DrawPipelineDesc` into the forward pass pipelines.
- [x] **Polymorphic node registry** — abstract `MaterialNode : Object` + one subclass per node,
  auto-discovered via reflection (`Editor/.../MaterialGraph/MaterialNode.{hpp,cpp}`). Adding a node =
  subclass + `Reflection::Type<MyNode>()` in `RegisterMaterialNodes()`. 33 concrete node types.
- [x] **HLSL codegen + SPIR-V compile** — `MaterialGraphCompiler` (post-order DFS from the output
  node). Generated globals + body are spliced into the host shader's source (default host:
  `Skore://ShadersNew/DefaultForward.shader` → `DefaultForward.hlsl`) at the
  `@SK_MATERIAL_GLOBALS@` / `@SK_MATERIAL_GRAPH@` tokens, filling `EvaluateMaterial`'s
  `SurfaceOutput`. Type coercion via `MaterialConvertExpr`; generic math pins resolve to the widest
  connected type.
- [x] **Generic material shader registration** — a shader is flagged `ShaderResource::IsMaterial`
  by `ShaderHandler` solely via `material: true` in its `.shader` config; entry points are
  auto-detected (`ShaderManager::DetectShaderStages`, supports VS/PS/GS/CS + ray-tracing stages). Compiled graph shaders are stored as `ShaderVariantResource`
  sub-objects tagged with the owning graph (`ShaderVariantResource::Material`) and resolved by
  `ShaderResource::GetVariant(shader, material, name)` (follows the instance → parent chain).
- [x] **On-demand variant compilation** — `RenderResourceCache::EnsureMaterialVariant` delegates to
  a resolver the editor registers (`RegisterMaterialVariantResolver` →
  `MaterialGraphCompiler::EnsureMaterialVariant`), cached by graph version + hash of the generated
  HLSL; recompiles on graph change. **Editor-only:** runtime builds have no resolver (see §4
  cooking).
- [x] **Runtime parameter buffer** — `MaterialParamLayout::Build(material)`
  (`GraphicsResources.cpp`) is the single layout contract shared by editor codegen and the runtime
  packer: one 4-byte-aligned slot per unique named parameter (deduped) + per fixed-texture node,
  256-byte block per material (`MaterialParamBlockSize`). Codegen emits
  `ByteAddressBuffer SK_MaterialParamBuffer : register(t7, space2)` +
  `MatParamFloat/Vec2/Vec3/Vec4/Texture` reads; runtime packs defaults + instance overrides
  (`RenderResourceCache::PackGraphMaterialParams`) and uploads at `materialIndex * 256`. Parameter
  edits re-pack without recompiling.
- [x] **Bindless texture wiring** — texture slots live in the param block as real bindless indices
  resolved from `TextureResourceCache` (−1 → white fallback); codegen samples
  `BindlessTextures[NonUniformResourceIndex(idx)]` and emits `WriteMipmapFeedback` per sample.
- [x] **Render pass integration** — `ForwardPassNew` (opaque + sky + transparent in one pass)
  batches drawcalls by `DrawPipelineDesc` (now includes `materialGraph`), builds one pipeline per
  bucket with `GraphicsPipelineDesc::material` selecting the compiled variant, falls back to the
  default gray template variant, and pushes `materialIndex` via push constants. Opaque draws go
  through the GPU-culled indirect path (`CullingPassNew` → `DrawIndexedIndirectCount`).
- [x] **Editor window** — `MaterialGraphEditorWindow` (add/move/delete/connect, inline default-value
  widgets on unconnected input pins, Build button + HLSL/log panel). Constant/parameter values are
  edited in the **Properties window** (node properties), not in the graph toolbar.
- [x] **Asset handler** — `.matgraph` create/open (`MaterialGraphHandler`), content-browser
  "Create > New Material Graph" and "Create > New Material Instance" (same resource, different
  `Kind`; instances are edited in the Properties window with parent picker + per-parameter override
  checkboxes). Project-browser thumbnails via `MaterialGraphPreviewGenerator` (sphere + material;
  still rendered by the legacy preview pipeline, so graph output isn't shown yet — see §4).
- [x] **Import path** — `MaterialImporter::ImportMaterial` maps imported (GLTF/FBX) materials to
  instances of `Assets/MaterialGraphs/MaterialBase{,_Masked,_Blend}.matgraph` (picked by alpha mode
  via the importer-owned `MaterialImportAlphaMode` enum) with sparse named-parameter overrides and
  reimport-stable UUIDs. Graph instances are the **only** import path: the legacy standalone
  `MaterialResource` importer and its `SK_IMPORT_MATERIAL_AS_GRAPH_INSTANCE` switch are deleted, and
  "Extract Materials" picks the asset extension per resource type (`.matgraph` for graph instances).
- [x] **Tests** — `Tests/Source/Editor/MaterialGraphTests.cpp` (22 cases, incl. generic-math
  type promotion, bindless/sRGB codegen, channel-select codegen, validation, and live SPIR-V compile).

### Nodes implemented so far
- [x] Material Output (master) — Base Color, Metallic, Roughness, Emissive, Normal, Ambient Occlusion, Opacity, Opacity Mask
- [x] Constant Float · Constant Color · Constant Vector2
- [x] Parameters (named, instance-overridable): Float · Int · Bool · Channel · Color · Vector2/3/4 · Texture2D
- [x] Texture Coordinate (UV0)
- [x] Sample Texture 2D · Normal Map · Tiling & Offset · Channel Select (RGBA + runtime channel index → Float;
  the base graphs route Metallic/Roughness/Occlusion through it so importers and instances can remap
  packed-texture channels via the `MetallicChannel`/`RoughnessChannel`/`OcclusionChannel` parameters, 0=R 1=G 2=B 3=A)
- [x] Multiply · Add · Lerp
- [x] Subtract · Divide · Power · Min · Max · Step
- [x] One Minus · Saturate · Clamp · Smoothstep · Remap
- [x] Dot · Normalize · Length

---

## 2. Master / Output node expansion

- [x] Base Color (Vec3)
- [x] Metallic (Float) — consumed by the forward pass GGX shading
- [x] Roughness (Float) — consumed by the forward pass GGX shading
- [x] Emissive (Vec3)
- [x] Normal (Vec3, tangent-space) — tangent fetched in `MainVS` (`GetVertexTangent`, w carries the
  bitangent sign), TBN transform in `MainPS` (`ResolveWorldNormal`); falls back to the vertex normal
  on meshes without tangents
- [x] Ambient Occlusion (Float) — applied to the ambient term
- [x] Opacity (Float) + Opacity Mask (Float) — separate pins; `GraphAlphaMode` selects behavior
  (Opaque forces 1.0, Mask emits `discard` below `MaskCutoff`, Blend writes alpha)
- [x] Output-node render settings (Properties window) — Material (`Default Lit | Unlit`),
  Render Face (`Front | Back | Both`), Depth Write, Depth Test (`CompareOp` values)
- [ ] World Position Offset (Vec3, **vertex stage**) — requires vertex-stage codegen
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
- [ ] Constant Vector3 / Vector4 (params exist, constants don't)
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

- [x] **★ SurfaceOutput consumption in the forward pass** — `DefaultForward.hlsl` shades with
  Cook-Torrance GGX (metallic/roughness), TBN-transformed graph normals, AO on ambient, and opacity
  as alpha, now fed by real scene lighting: `LightSetupPassNew` (multi-light storage buffer +
  ambient/reflection IBL + BRDF LUT) and `CascadeShadowPassNew` (directional CSM). **Remaining
  polish:** point/spot shadows and light culling/clustering (see `MaterialSystemNextSteps.md` §2.1).
- [x] **★ Texture sampling quality** — texture slots in the param block are 8 bytes: bindless index
  + sampler index resolved from the assigned texture's wrap/filter settings (instance overrides
  re-pack without recompiling). sRGB decode is a per-node Bool property (`Value.x`) on
  `sample_texture` / `param_texture2d`, baked into the generated HLSL; `normal_map` always samples
  linear. The `MaterialBase*` graphs mark `BaseColorTexture` as sRGB.
- [~] **★ Runtime-without-editor (variant cooking)** — variants are compiled on demand by the
  editor-registered resolver only; a runtime-only build gets the default gray shader for every graph
  material. **Findings (2026-07):** `ResourceAssetHandler::Export` serializes the current in-memory
  resource state, so variants compiled during the session *would* ship with the shader that owns
  them — but the forward shader lives in the embedded `Skore://` package (inside the runtime
  binary), which `ExportPackages` never writes, so its runtime-added variants are lost in a shipped
  build. Two viable designs: (A) export material-capable shaders that hold graph variants as
  project-level cooked overrides (needs deserialize-by-UUID to replace the embedded resource); or
  (B) cook each compiled variant into the *graph asset's* payload and register it into the shader's
  variant list at load (keeps the engine package immutable; dependency direction graph→shader is
  cleaner). Option B recommended. Either way, export needs a warm-up pass that compiles the default
  forward variant for every graph in the exported set (pairings with custom shaders are only known
  from scene data).
- [~] **★ Legacy material migration** — imports produce graph instances only (legacy import path
  deleted; existing imports migrate on reimport). The mesh fallback is now
  `Skore://MaterialGraphs/DefaultMaterial.matgraph` (hand-authored instance of `MaterialBase`;
  `DefaultMaterial.material` deleted — remember the CMake re-configure for the embedded package).
  The legacy PBR packing in `UpdateMaterialStorageData` is gone: any non-graph material writes a
  white stub `MaterialData` block (`WriteMaterialStubData`), so stale legacy assets render white
  instead of disappearing and the old pipeline keeps compiling. `MaterialArray` slots are typed to
  `MaterialGraphResource` — pickers/drag-drop accept graphs only. The `MaterialResource` asset type
  is fully removed (struct, reflection, `.material` handler, field-visibility rows, sky enum/fields
  all deleted); the sky no longer routes through a material at all — it's a `TextureResource`
  referenced directly by `EnvironmentComponent`, baked into IBL by `EnvironmentResourceCache`.
  **Out of scope (decided):** old project `.material` assets are orphaned (no handler) and render as
  the white stub; they will *not* be auto-migrated — re-author as `MaterialBase*` graph instances by
  hand where still needed.
- [~] **★ Custom material shaders per object** — done: `DrawPipelineDesc::shader` is honored by the
  forward passes when the shader is flagged `IsMaterial` (falls back to `DefaultForward.shader`
  otherwise), and the variant resolver splices the graph into *that shader's own source*
  (`LoadShaderTemplate`). **Remaining:** an editor affordance to assign the shader per
  mesh/material.
- [~] **★ Transparent + shadow passes** — `ForwardPassNew` renders the transparent buckets on top
  of opaque (load, blend on, depth test Greater / write off) and `CascadeShadowPassNew` now consumes
  the shadow buckets (GPU-culled directional CSM). **Remaining:** per-drawcall back-to-front sorting
  of transparents; point/spot shadow casters.
- [~] **★ Graph validation + diagnostics** — compile log now reports: empty parameter names,
  duplicate parameter names across different parameter types, and nothing-connected-to-master
  (plus the existing cycle / unknown-type / missing-output messages). `MaterialParamLayout::Build`
  no longer aliases same-name slots of mismatched kinds. **Remaining:** valid pin indices, one
  connection per input, stage compatibility, dead-node warnings, and surfacing errors with node/pin
  context in the editor (log only today).
- [ ] **★ Type + stage model expansion** — `MaterialDataType` is currently scalar/vector only.
  Before Custom HLSL, static switches, texture objects, sampler objects, vertex-stage outputs, or
  real bool/int branches, add explicit pin categories for numeric values, bool/int values,
  textures/samplers, and shader stage availability (pixel, vertex, shared).
- [ ] **★ Custom HLSL / Expression node** — raw-code escape hatch with typed pins; after the
  type/stage model and validation rules are strong enough to keep arbitrary snippets contained.
- [ ] **★ Static Switch → shader variants** — compile-time branches emitting macro variants (the
  shader system already supports macros/variants). Needs a variant key model, default values,
  material-instance override rules, and cache invalidation.
- [ ] **Material Functions / Subgraphs** — reusable nested node groups.
- [ ] **Reroute / knot nodes** + **Comment frames** — graph ergonomics.
- [~] **Per-node preview thumbnails** + live material preview — texture nodes render the assigned
  texture's thumbnail, and `.matgraph` assets get project-browser thumbnails
  (`MaterialGraphPreviewGenerator`) — but the preview scene renders through the legacy preview
  pipeline, where graph materials are the white stub. **Remaining:** previews through the new
  pipeline / generated shader contract, plus live preview of a node's computed output.
- [ ] **Codegen quality** — constant folding / dead-code elimination · deterministic output order ·
  common-subexpression reuse · DDX/DDY-aware sampling · error mapping back to graph nodes.
- [ ] **Asset/versioning/migration** — graph schema versioning + migration rules for node/pin
  changes.
- [~] **Test coverage gates** — 21 cases: bindless texture codegen (index + sampler slot), sRGB
  decode, validation warnings/clean-graph, and the SPIR-V end-to-end test compiles a graph with a
  texture node against the real template. **Remaining:** golden generated-HLSL snapshots,
  serialization/migration tests, parameter layout/override tests, and a runtime render smoke test.

---

## 5. Suggested build order

*(done 2026-07: stale tests · TBN/PBR SurfaceOutput consumption · sampler + sRGB handling ·
graph-instance imports · per-object custom material shaders · transparent pass · first validation
batch · legacy-material retirement: default graph material fallback, legacy import/runtime paths
deleted, sky-only `.material`, graph-typed material slots, `.matgraph` thumbnails · render rework:
real light list, cascade shadows, IBL, GPU culling, skybox, bloom + ACES tonemap in `PipelineNew`)*

*(explicitly out of scope: one-time migration of legacy `.material` PBR assets — re-author by hand)*

1. **Variant cooking** for runtime-only builds — option B from §4 (cook variants onto the graph
   asset + register into the shader at load), plus an export warm-up pass. The top blocker: with the
   default material a graph instance, even an empty scene needs a cooked variant.
2. **Validation, part 2** — pin-index/stage checks, dead-node warnings, editor surfacing with
   node/pin context.
3. **Renderer polish** — transparent back-to-front sorting, depth prepass, AA; point/spot shadows.
4. **Type/stage model expansion** → **Custom HLSL + Static Switches**.
5. Fill remaining Tier 1 nodes, **Reroute + Comment**, then **World Position Offset** once
   vertex-stage codegen exists.

> Most library nodes are mechanical (subclass + register). With lighting/shadows/culling/post now
> landed, the remaining engineering is **variant cooking**, validation surfacing, and versioning —
> the compile/bind/shade/light chain works end-to-end in the editor.
