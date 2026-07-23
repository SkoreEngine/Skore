# Legacy Renderer Analysis — `DefaultRenderPipeline`

Reference map of the **old** renderer living in
`Runtime/Source/Skore/Graphics/Pipelines/`. Its purpose is to catalog every pass, module,
shader, and named resource so the passes can be ported to the **new** render graph
(`Runtime/Source/Skore/Graphics/RenderGraph.hpp` + `RenderPipelineNew.hpp`, shaders in
`Assets/ShadersNew`).

The last section maps the old framework primitives onto the new `RenderGraph` API and tracks
port status.

> Companion docs: [MaterialNodeSystem.md](MaterialNodeSystem.md) (node materials feeding the new
> forward pass), [MaterialSystemNextSteps.md](MaterialSystemNextSteps.md).

---

## 1. Old framework primitives

Header: `Runtime/Source/Skore/Graphics/RenderPipeline.hpp`.

| Primitive | Role |
|---|---|
| `RenderPipeline` | Top-level. `GetPipelineSetup()` returns an ordered list of **module** TypeIDs. |
| `RenderPipelineModule` | Groups passes. `GetSetup()` returns **pass** TypeIDs + resolve/options; `GetResources()` declares named textures/buffers/instance-data; `IsEnabled()` toggles the whole module (rebuilds graph when it flips). |
| `RenderPipelinePass` | One unit of work. `GetPassSetup()` returns type, `stage` (int ordering), `dependencies` (named resource + `Read`/`Write`/`ReadWrite`), `resolve`, and flags (`invertViewport`, `requireJitter`, `requireMotionVector`). Lifecycle: `Create()` → `Init()` → per-frame `Update()`/`Render(scene, cmd)` → `Destroy()`/`OnResize()`. |
| `RenderPipelineContext` | The graph runtime. Owns resources (by string name), barriers, framebuffers, the per-frame scene UBO + descriptor sets, camera state (incl. jitter/reverse-Z matrices), ping-pong, and the output attachments. Passes reach data via `context->GetTexture/GetBuffer/GetInstanceData/GetPrevTexture`. |

**Resource declaration** (`RenderPipelineResource`) covers `Texture`, `TextureView`, `Attachment`,
`Buffer`, `Reference`, and `Instance` (a plain C++ struct shared between passes by name). Textures
support `scale` (fraction of output size), `pingPong`, array layers, mips, and explicit
`ResourceUsage`.

**Pass ordering** is driven by the integer `stage` (see `PipelineCommon.hpp::PipelineRenderStage`),
**not** by list order. The context inserts barriers automatically from the declared
`Read`/`Write` dependencies; several passes additionally issue **manual** `ResourceBarrier` /
`MemoryBarrier` calls (Bloom, DepthLinearize, IrradianceVolume, TAA, shadows) — those are the
trickiest parts to port.

### Descriptor-set convention (old)
- **space0** = global (`RenderResourceCache::GetGlobalDescriptorSet()` — bindless textures, mesh data)
- **space1** = scene (`context->GetSceneDescriptorSet()` — camera UBO, light UBO, shadow map)
- **space2** = per-pass working set (culling buffers, gbuffer inputs, compute UAVs) — *except*
  shadows/skinning which use space2 for the cascade UBO and space3 for bones.

> ⚠️ This differs from the new descriptor layout (space0=global, space1=scene, **space3=object**;
> pipeline data picks the lowest free slot). Shaders must be re-authored to the new spaces when ported.

---

## 2. The default pipeline (`DefaultRenderPipeline.cpp`)

`DefaultRenderPipeline::GetPipelineSetup()` composes these modules (order shown; actual execution is
by stage):

```
CascadeShadowMapModule
// DepthPrePassModule        (registered but commented out)
DeferredRenderModule        → LightSetupPass, CullingPass, DeferredGBufferPass,
                              DeferredLightingPass, ForwardPass, DepthLinearizePass,
                              CompositePass, PostProcessPass
IndirectLightingModule      → ReflectionPass, IrradianceVolumeUpdate/Sampling/Debug
MotionVectorModule          → CameraMotionVector, ObjectMotionVector, SkinnedMotionVector
TemporalAntiAliasingModule
FXAARenderModule
BloomModule
XeGTAORenderModule
RmlUiRenderPipelineModule
```

`DeferredRenderModule` also declares the shared GBuffer/lighting attachments. Project setting
`DefaultRenderPipelineSettings` selects `ShadingMethod` (Deferred/Forward) and `AntiAliasingMethod`
(None/FXAA/MSAA/TAA), surfaced under **Engine/Rendering**.

### Execution order by stage

| Stage (int) | Pass(es) | Type |
|---|---|---|
| Culling (100) | CullingPass | Compute |
| ShadowPass (200) | CascadeShadowPass (cull-compute + N cascade depth draws) | Other* |
| DepthPrePass (300) | DepthPrePassPass *(module disabled)* | Graphics |
| GBuffer (400) | DeferredGBufferPass | Graphics (MRT) |
| DepthLinearize (500) | DepthLinearizePass (FidelityFX SPD) | Compute |
| Lighting (600) | LightSetupPass (CPU), DeferredLightingPass | Other / Compute |
| Forward (700) | ForwardPass (transparent + particles + skybox) | Graphics |
| Indirect (800) | ReflectionPass; IrradianceVolume Update(801)/Sampling(802)/Debug(803); XeGTAO (setup+main) | Compute/RT/Graphics |
| Composite (900) | CompositePass | Compute |
| PostProcess (1000) | PostProcessPass; MotionVector passes; TAA; FXAA (1000 + resolve 1001) | Compute/Graphics |
| UI (1200) | ProfilerOverlay; RmlUi | Graphics |
| Swapchain (1300) | SwapchainRenderPipelinePass (present) | Other |

\* `Other` = the pass self-manages its render passes / dispatches rather than letting the framework
open a single render pass.

---

## 3. Pass & module catalog

Each entry lists: **type/stage**, **shaders**, **reads → writes** (named resources), and
**migration notes**.

### 3.1 Deferred core (`DeferredRenderModule`)

#### LightSetupPass — Other, Lighting(600)
- **Shaders:** none (CPU data-setup pass).
- **Reads:** `ShadowMapInstanceData` (instance). Scene `EnvironmentComponent` + `LightComponent`, IBL textures from the material cache.
- **Writes:** `LightInstanceData` (instance); creates+owns GPU UBO `LightBuffer`.
- **Logic:** builds the per-frame `LightBuffer` (`lightCount`, `shadowLightIndex`, `cascadeSplits`, `cascadeViewProjMat[MaxNumCascades]`, `lights[MAX_LIGHTS]`), sets `LightFlags` (ambient tex/color, reflection tex), then writes descriptors **into the scene set (space1)**: binding 2 = light UBO, 3 = shadow texture, 4 = shadow sampler, for every frame-in-flight. Only one shadowed directional light supported.
- **Migration:** the linchpin producer of `LightInstanceData`. Hard-coded scene-set binding indices couple to the shader's space1 layout. Must run before DeferredLighting/Forward/Composite/Reflection/Irradiance even though the coupling is through instance-data + the scene set, *not* a texture dependency.

#### CullingPass — Compute, Culling(100)
- **Shaders:** `Skore://Shaders/CullingPass.comp`.
- **Reads:** `scene->renderObjects.opaquePipelines` (CPU); global(space0) + scene(space1) sets. Push constant `i32 forcedLod`.
- **Writes:** `SceneCullingData` (instance) — per-pipeline indirect-draw buffers (`IndirectDrawBuffer`, `IndirectBuffer|UAV`, double-slack) + shared `IndirectDrawBufferCount`.
- **Logic:** own space2 set (binding 0 = array of 256 storage buffers, binding 1 = count buffer). Zero counts via `FillBuffer`, `MemoryBarrier`, `Dispatch((instanceDataCount+15)/16,1,1)`, `MemoryBarrier`. Rebuilds buffers when the scene changes or count grows.
- **Migration:** two coarse `MemoryBarrier()` calls (TODO in code) → express as real fill→dispatch→draw hazards. 256-element descriptor array caps pipeline count. Directly feeds DeferredGBufferPass.

#### DeferredGBufferPass — Graphics MRT, GBuffer(400), `invertViewport`
- **Shaders:** per-pipeline `DrawPipelineDesc::shader`; fallback `Skore://Shaders/DeferredGBufferIndirect.shader`. Variant macro `HAS_BONES`.
- **Reads:** `SceneCullingData` (instance); mesh index buffer; global(0)/scene(1)/skinning(2) sets.
- **Writes:** `GBufferAlbedoMetallic`, `GBufferRoughnessAO`, `GBufferNormals`, `GBufferEmissive`, `OutputDepthAttachment`.
- **Logic:** 4 color attachments → 4 default blend states; reverse-Z depth (`Greater`, write on). One pipeline per opaque bucket (cached per scene); draws `DrawIndexedIndirectCount` per pipeline using the culling buffers. Contains a commented-out non-indirect fallback path.
- **Migration:** hard order after CullingPass; blend-state array length is hardcoded to MRT count; reverse-Z + `invertViewport` load-bearing.

#### DeferredLightingPass — Compute, Lighting(600)
- **Shaders:** `Skore://Shaders/DeferredLighting.comp`.
- **Reads:** `LightInstanceData` (instance, via scene set), `GBufferAlbedoMetallic/RoughnessAO/Normals/Emissive`, `LinearDepthMipChain`.
- **Writes:** `LightAttachment` (UAV binding 0).
- **Logic:** own space2 set (0 = output UAV, 1–5 = gbuffer + linear depth SampledImage), scene set at 1. `Dispatch((w+7)/8,(h+7)/8,1)` (8×8). `UpdateState()` re-binds textures on resize.
- **Migration:** light data arrives via the **scene set** (populated by LightSetupPass), not this pass's own set → keep the ordering dependency explicit.

#### ForwardPass — Graphics, Forward(700), `invertViewport`
- **Shaders:** `Skore://Shaders/ForwardMeshRender.shader` (transparent meshes, `HAS_BONES` variant), `Skore://Shaders/SkyboxRender.raster`, `Skore://Shaders/ParticleRender.raster`.
- **Reads/Writes:** `LightAttachment` (ReadWrite), `OutputDepthAttachment` (ReadWrite).
- **Logic:** transparent meshes (reverse-Z `Greater`, depth write on), GPU particles (`Draw(maxParticles*6)`, premultiplied-alpha, depth test only), skybox (`Draw(36)`, `GreaterEqual`, translation-stripped `Mat34(view)`). Push constant `MeshPushConstants{world,materialIndex,vertexByteOffset,vertexLayoutIndex,pad}`. Per-scene pipeline cache; CPU frustum + layer/culling-mask cull. Bones at set 3, particle emitter/material set at set 3.
- **Migration:** **partially superseded** by `ForwardPassNew` (opaque+transparent). Particles + skybox not yet ported.

#### DepthLinearizePass — Compute, DepthLinearize(500)
- **Shaders:** `Skore://Shaders/FidelityFX/DepthLinearize_SPD.shader` (variant `Default`); host headers `FidelityFX/ffx_a.h`, `ffx_spd.h`.
- **Reads:** `OutputDepthAttachment`.
- **Writes:** `LinearDepthMipChain` (R32_FLOAT hi-Z mip chain, published via `SetTexture`).
- **Logic:** AMD Single-Pass Downsampler builds the whole linear-depth mip chain in one dispatch. `mipCount = floor(log2(max(w,h)))+1` clamped to 12. Owns `atomicBuffer` (reset each frame) + per-mip UAV views (binding 1 = all mips, binding 2 must point to mip 6). Push constant `SpdConstants` from `SpdSetup`; manual barriers Undefined→General→ShaderReadOnly.
- **Migration:** exact SPD binding layout required; hi-Z should become a graph transient with per-mip UAV views (`CreateView`). Consumed by DeferredLighting, Reflection, Irradiance sampling, XeGTAO, CameraMotion.

#### CompositePass — Compute, Composite(900)
- **Shaders:** `Skore://Shaders/DefaultComposite.comp` (variant `Default`, `allowImmediateSet`).
- **Reads:** `LightInstanceData`, `LightAttachment`, `GBufferAlbedoMetallic/RoughnessAO/Normals`, `OutputDepthAttachment`, `ReflectionAttachment`, `IrradianceVolumeAttachment`; also samples `XeGTAOOutputName` (SSAO) and `diffuseIrradianceTexture` (**not** declared deps).
- **Writes:** `ColorAttachment` (UAV binding 0).
- **Logic:** immediate descriptor binds (11 slots). Push `DefaultCompositePushConstants{ambientLight,ambientMultiplier,outputSize,flags,farClip}`; `flags` starts from `indirectLightFlags`, ORs SSAO/reflection availability; missing inputs fall back to engine white textures. `Dispatch((w+7)/8,(h+7)/8,1)`.
- **Migration:** preserve the fallback-to-white + flag-bit behavior; `XeGTAOOutputName` read without a declared dependency (audit when moving to explicit deps).

#### PostProcessPass — Compute, PostProcess(1000)
- **Shaders:** `Skore://Shaders/PostProcess.comp` (`allowImmediateSet`).
- **Reads:** `ColorAttachment`, `ResolvedHDR` (declared but unused in body), `BloomTexture`.
- **Writes:** `OutputColorAttachment` (UAV).
- **Logic:** tonemap/post; `PostProcessPushConstants{bloomIntensity}` from a `BloomComponent` if present; bloom optional (white fallback). `Dispatch((w+7)/8,(h+7)/8,1)`.
- **Migration:** simple leaf. Resolve the dead `ResolvedHDR` dependency (ordering vs. AA-resolve) before porting.

### 3.2 Shadows (`CascadeShadowMapModule`)

`CascadeShadowPass.cpp` is a thin registration shim; all logic is in `CascadeShadowPassBase` +
`CascadeShadowMapModuleBase` (`Modules/CascadeShadowMapModule.cpp/.hpp`).

- **Type/Stage:** Other, ShadowPass(200) — self-manages render passes + an internal cull dispatch.
- **Shaders:** `Skore://Shaders/CullingShadowPass.comp` (cull), `Skore://Shaders/ShadowMapIndirect.shader` (variants `HAS_BONES`, `HAS_MASK`). Legacy `Skore://Shaders/ShadowMap.shader` in a dead comment.
- **Writes:** `ShadowMapInstanceData` (instance) — `shadowTexture` (D32 array, one layer/cascade), `shadowSampler` (comparison/PCF), `cascadeSplits`, `cascadeViewProjMat`. Texture is **module-owned side data**, surfaced via instance-data (not a graph attachment).
- **Logic:** picks first directional shadow-casting light; practical-split cascade scheme (`SplitLambda` 0.85); stable/texel-snapped ortho matrices to kill shimmer; **interleaved cascade updates** (cascade `i` every `min(1<<i, 8)` frames, or on camera/light movement) to amortize cost. GPU cull → per-(cascade × shadow-pipeline) indirect buffers → `DrawIndexedIndirectCount` per cascade. **Standard-Z** shadows (clear depth 1.0, `LessEqual`) vs. reverse-Z main camera. Depth bias (const 1.0, slope 1.5).
- **Migration:** split into a cull compute node + N layered depth-only raster nodes; make the shadow array a first-class graph resource; preserve cross-frame state (`m_frameCounter`, `m_lastFrustumCenter`, `m_lastLightDir`) and the standard-Z convention. Uses old space2=cascade UBO / space3=bones convention.

### 3.3 Indirect lighting (`IndirectLightingModule`)

Declares attachments `ReflectionAttachment` (RGBA16F), `IrradianceVolumeAttachment` (RGBA16F), and
instance `IrradianceVolumeData`. All passes share `Skore://Shaders/DefaultIndirectLighting.shader`
via distinct variants.

#### ReflectionPass — Compute, Indirect(800)
- **Variant:** `Reflection`. Uses a 512² BRDF LUT.
- **Reads:** `LightInstanceData`, `ColorAttachment`, gbuffer (Albedo/Roughness/Normals), `LinearDepthMipChain`; **previous-frame** `ColorAttachment` (`GetPrevTexture`), `specularMapTexture`, `scene->renderObjects.tlas`.
- **Writes:** `ReflectionAttachment`.
- **Logic:** screen-space reflections + ray-traced fallback (bails without TLAS). Push `ReflectionPushConstants` (camera, prev viewProj, proj/invView, thickness 10, rayBias 0.01, mip levels…). Set 0 has 12 bindings incl. TLAS at 11.
- **Migration:** needs TLAS + temporal prev-color + hi-Z. Runs at stage 800 (before the irradiance passes despite listing order).

#### IrradianceVolumeUpdatePass — Raytrace, Indirect+1(801), enabled with `IrradianceVolumeComponent`
- **Variants:** `IrradianceProbeTrace` (RT), `IrradianceProbeBlendIrradiance`, `IrradianceProbeBlendDistance`, `IrradianceProbeRelocate`, `IrradianceProbeClassify` (compute).
- **Reads:** `LightInstanceData`, `scene->renderObjects.tlas`, sky cubemap.
- **Writes:** `IrradianceVolumeData` (volumeBuffer + irradiance/distance/probeData arrays); toggles `LightFlags::GlobalIlluminationEnabled`.
- **Logic:** DDGI. Persistent-mapped multi-frame ring buffer (`MaxIrradianceVolumes=16`), one cascade updated per frame (round-robin), camera-follow scrolling, random ray rotation. Heavy explicit barrier choreography trace→blend→relocate→classify. `EnsureResources` keyed on config version.
- **Migration:** heaviest pass; preserve per-frame ring slicing (`frameOffset`) and barrier ordering. Requires TLAS; degrades gracefully without it.

#### IrradianceVolumeSamplingPass — Compute, Indirect+2(802)
- **Variant:** `IrradianceVolumeSample`.
- **Reads:** `LightInstanceData`, `GBufferAlbedoMetallic`, `GBufferNormals`, `LinearDepthMipChain`, `IrradianceVolumeData`.
- **Writes:** `IrradianceVolumeAttachment`.
- **Logic:** samples DDGI probes at gbuffer surfaces; `Dispatch((w+7)/8,(h+7)/8,1)`. Depends on the Update pass having published `IrradianceVolumeData`.

#### IrradianceVolumeDebugPass — Graphics, Indirect+3(803), `invertViewport`
- **Variant:** `IrradianceProbeDebug`. Editor-only probe visualization (sphere mesh instanced per probe), gated by `PipelineOption` toggles. Reads/writes `LightAttachment` + `OutputDepthAttachment`. Port last / keep editor-gated.

### 3.4 Motion vectors (`MotionVectorModule`)

Declares attachment `MotionVector` (RG16F). `IsEnabled()` = `context->IsMotionVectorRequired()` (a
consumer like TAA declares `requireMotionVector`).

- **CameraMotionVectorPass** — Compute, PostProcess(1000). Shader `Skore://Shaders/ComputeCameraMotion.comp`. Reads `LinearDepthMipChain`, writes `MotionVector` (full-screen camera-only motion).
- **ObjectMotionVectorPass** — Graphics, PostProcess, `invertViewport`. Shader `Skore://Shaders/ObjectMotionVector.raster`. Overwrites moving opaque objects (current vs. previous MVP). Per-frame structured `DrawData` buffer, two-pass count-then-fill, reverse-Z `GreaterEqual` depth-test/no-write.
- **SkinnedMotionVectorPass** — Graphics, PostProcess, `invertViewport`. Shader `Skore://Shaders/SkinnedMotionVector.raster`. Same but skinned; needs current + previous skinning descriptor sets (double-buffered skinning), carries `boneBufferIndex`/`bonesChanged`.
- **Migration:** ordering camera→object→skinned is load-bearing (all ReadWrite `MotionVector`/`OutputDepth`). Requires jitter bookkeeping + previous-frame camera/object transforms + `movedRenderables` list.

### 3.5 Anti-aliasing

#### TemporalAntiAliasingModule / TAA pass — Compute, PostProcess(1000), `requireJitter` + `requireMotionVector`
- **Shader:** `Skore://Shaders/TAA.shader` (variant `ResolveTemporal`, `allowImmediateSet`).
- **Reads:** `MotionVector`, `ColorAttachment`, `OutputDepthAttachment`. **Declared write:** `ResolvedHDR` — but the body reprojects history and **copies the resolved result back into `ColorAttachment`** (CopyDest), publishing `HistoryTexture`.
- **Logic:** two ping-pong history textures (`RGBA16F`, output-sized), `resetHistory` on first frame/resize. `Dispatch((w+7)/8,(h+7)/8,1)`. `IsEnabled()` = setting `AntiAliasingMethod == TAA`.
- **Migration:** history textures are cross-frame state (not aliasable transients); use `pingPong`. Resolve the `ResolvedHDR`-vs-copy-back discrepancy. Requires camera jitter (`projection` jittered, `projectionNoJitter` kept) + MotionVectorModule upstream.

#### FXAARenderModule — two passes, PostProcess
- **FXAARenderPass** — Compute. `Skore://Shaders/fxaa/FXAA.comp`. Reads `OutputColorAttachment`, writes `FXAAOutput` (RGBA8_UNORM). Push `FXAAConstants{subPix 0.75, edgeThreshold 0.063}`.
- **FXAAResolvePass** — Graphics, PostProcess+1(1001). `Skore://Shaders/FullscreenTexture.raster`. Draws `FXAAOutput` back into `OutputColorAttachment` (fullscreen triangle).
- **Migration:** runs on LDR/tonemapped color (8-bit intermediate) → ordering vs. tonemap matters. Mutually exclusive with TAA via the same setting. `IsEnabled()` = `AntiAliasingMethod == FXAA`.

### 3.6 Post / misc modules

#### BloomModule / BloomPass — Compute, PostProcess, enabled with `BloomComponent`
- **Shader:** `Skore://Shaders/Bloom.shader` (variants `BloomPrefilter`, `BloomDownsample`, `BloomUpsample`, `allowImmediateSet`).
- **Reads:** `LightAttachment`. **Writes:** `BloomTexture` (published via `SetTexture`; graph placeholder is `None`-typed).
- **Logic:** owns `bloomMipChain` (RG11B10F, half-res, up to `MaxBloomMips=7`) + per-mip views. Prefilter (threshold + soft-knee) → downsample chain → upsample chain. Push `BloomPushConstants{texelSize, threshold, softKnee, bloomRadius}` from `BloomComponent`. `Dispatch((dstW+7)/8,(dstH+7)/8,1)`. Extensive hand-managed per-mip General↔ShaderReadOnly barriers.
- **Migration:** let the graph own the mip chain + per-mip barriers/views; a commented "must be bound but unused" 4th descriptor slot needs verifying against `Bloom.shader` reflection.

#### XeGTAORenderModule — two passes, Indirect, enabled with `SSAOComponent`
- **XeGTAOSetup** — Other (CPU buffer fill). Writes `XeGTAOConstants` (persistent-mapped UBO) from camera + `SSAOComponent`. Reads `LinearDepthMipChain`.
- **XeGTAOMainPass** — Compute. `Skore://Shaders/XeGTAO.shader` (variant `XrGTAOUltra` — note spelling). Reads `LinearDepthMipChain`, `XeGTAOConstants`, `GBufferNormals`; writes `XeGTAOOutputName` (R8_UINT) + `XeGTAOWorkingEdges` (R8_UNORM). Manual 6-binding descriptor set. `Dispatch((w+7)/8,(h+7)/8,1)`.
- **Migration:** direct port of Intel XeGTAO — constant struct layout + variant name must match the HLSL exactly. Two-phase (setup→main) ordering + persistent-mapped UBO. Only the main pass is wired (prefilter/denoise not ported).

#### DepthPrePassModule — Graphics, DepthPrePass(300), `invertViewport` *(module currently disabled in the pipeline)*
- **Shader:** `Skore://Shaders/DepthPrePass.shader` (`HAS_BONES` variant). Writes `OutputDepthAttachment` (D32, reverse-Z `Greater`). Lazy per-opaque-bucket pipelines, CPU frustum + layer/material cull. Global(0)/scene(1)/bones(3) sets.

#### SwapchainRenderModule — Other, Swapchain(1300)
- No shaders. Owns `GPUSwapchain` (`BGRA8_UNORM`, vsync), handles acquire/present/resize/minimize, registers swapchain textures as `OutputColorAttachment` (→ `Present` state), sets itself as main context. Reads `OutputColorAttachment`.
- **Migration:** infrastructure node; keep the resize (`WaitIdle`+recreate) and minimize (`DisableContext`) paths + `SetMainContext` side effect.

#### ProfilerOverlayModule — Graphics, UI(1200)
- No direct shaders (uses `DrawList` immediate mode + `Skore://Fonts/DejaVuSans.font`). F5 toggles visibility + `Profiler::SetActive`. Draws CPU/GPU profiler panels over `OutputColorAttachment` (ReadWrite/load). Safe to port late or omit for shipping.

#### RmlUiRenderPipelineModule — UI(1200)
- UI rendering (see [RmlUi integration memory]). Draws over `OutputColorAttachment`.

---

## 4. Named-resource catalog

Shared string keys the passes communicate through (constants in `PipelineCommon.hpp` where noted).

**Attachments / textures**

| Name | Format | Produced by | Consumed by |
|---|---|---|---|
| `GBufferAlbedoMetallic` | RGBA8_UNORM | DeferredGBuffer | DeferredLighting, Composite, Reflection, IrradianceSampling |
| `GBufferRoughnessAO` | RG8_UNORM | DeferredGBuffer | DeferredLighting, Composite, Reflection |
| `GBufferNormals` | RG16_FLOAT | DeferredGBuffer | DeferredLighting, Composite, Reflection, IrradianceSampling, XeGTAO |
| `GBufferEmissive` | RG11B10_FLOAT | DeferredGBuffer | DeferredLighting |
| `LightAttachment` | RGBA16_FLOAT | DeferredLighting | ForwardPass (RW), Composite, Bloom, IrradianceDebug |
| `ColorAttachment` | RGBA16_FLOAT (pingPong) | Composite | Reflection (prev), TAA, FXAA-source(via OutputColor), PostProcess |
| `OutputDepthAttachment` (`OutputDepthName`) | D32_FLOAT | DeferredGBuffer / DepthPrePass | many (DepthLinearize, Forward, Composite, MotionVector, TAA) |
| `OutputColorAttachment` (`OutputColorName`) | swapchain BGRA8_UNORM | PostProcess → Swapchain | FXAA, ProfilerOverlay, RmlUi |
| `LinearDepthMipChain` (`LinearDepthMipChainName`) | R32_FLOAT + mips | DepthLinearize | DeferredLighting, Reflection, IrradianceSampling, XeGTAO, CameraMotion |
| `ReflectionAttachment` | RGBA16_FLOAT | ReflectionPass | Composite |
| `IrradianceVolumeAttachment` | RGBA16_FLOAT | IrradianceSampling | Composite |
| `MotionVector` | RG16_FLOAT | MotionVector passes | TAA |
| `BloomTexture` | (mip chain RG11B10F) | BloomPass | PostProcess |
| `XeGTAOOutputName` | R8_UINT | XeGTAOMain | Composite (SSAO) |
| `XeGTAOWorkingEdges` | R8_UNORM | XeGTAOMain | XeGTAOMain (internal) |
| `FXAAOutput` | RGBA8_UNORM | FXAARenderPass | FXAAResolvePass |
| `ResolvedHDR` | — | TAA (declared) | PostProcess (declared, unused) |
| `HistoryTexture` | RGBA16_FLOAT ×2 | TAA | TAA (temporal) |

**Buffers**

| Name | Usage | Owner |
|---|---|---|
| `LightBuffer` | ConstantBuffer, host-visible | LightSetupPass |
| `IndirectDrawBuffer` (per pipeline) | IndirectBuffer\|UAV | CullingPass |
| `IndirectDrawBufferCount` | IndirectBuffer\|UAV\|CopyDest | CullingPass |
| `XeGTAOConstants` | ConstantBuffer, persistentMapped | XeGTAOSetup |
| `IrradianceVolumeBuffer` | ShaderResource\|UAV, persistentMapped ring | IrradianceVolumeUpdate |
| shadow cull indirect/count/UBO | IndirectBuffer\|UAV / ConstantBuffer | CascadeShadowPass |

**Instance data (shared C++ structs, `PipelineCommon.hpp`)**

| Name | Struct | Producer → consumers |
|---|---|---|
| `LightInstanceData` | `LightPassInstanceData` (light buffer, ambient/reflection, IBL textures, `indirectLightFlags`) | LightSetup → Forward, DeferredLighting, Composite, Reflection, Irradiance |
| `SceneCullingData` | indirect-draw + count buffers per pipeline | Culling → DeferredGBuffer |
| `ShadowMapInstanceData` | shadow texture/sampler, cascade splits + view-proj | CascadeShadow → LightSetup |
| `IrradianceVolumeData` | DDGI volume buffer + probe arrays | IrradianceUpdate → Sampling/Debug |

`LightFlags` bits (`indirectLightFlags`): `HasAmbientTexture`, `HasAmbientColor`, `HasReflectionTexture`,
`HasSSAOTexture`, `SSREnabled`, `GlobalIlluminationEnabled` — set by various passes, read by Composite.

---

## 5. Shader inventory (`Assets/Shaders/`)

| Shader asset | Kind | Used by |
|---|---|---|
| `CullingPass.comp` | compute | CullingPass |
| `CullingShadowPass.comp` | compute | CascadeShadowPass (cull) |
| `ShadowMapIndirect.shader` | raster | CascadeShadowPass (`HAS_BONES`/`HAS_MASK`) |
| `ShadowMap.shader` | raster | *legacy, dead code* |
| `DeferredGBufferIndirect.shader` | raster | DeferredGBuffer (fallback) |
| `DeferredLighting.comp` | compute | DeferredLightingPass |
| `ForwardMeshRender.shader` | raster | ForwardPass (transparent) |
| `SkyboxRender.raster` | raster | ForwardPass (skybox) |
| `ParticleRender.raster` | raster | ForwardPass (particles) |
| `FidelityFX/DepthLinearize_SPD.shader` | compute | DepthLinearizePass |
| `FidelityFX/SPD.shader`, `ffx_a.h`, `ffx_spd.h` | compute/hdr | SPD host |
| `DefaultComposite.comp` | compute | CompositePass |
| `PostProcess.comp` | compute | PostProcessPass |
| `DefaultIndirectLighting.shader` | multi (RT+compute+raster) | Reflection + all Irradiance passes (8 variants) |
| `ComputeCameraMotion.comp` | compute | CameraMotionVectorPass |
| `ObjectMotionVector.raster` | raster | ObjectMotionVectorPass |
| `SkinnedMotionVector.raster` | raster | SkinnedMotionVectorPass |
| `TAA.shader` | compute | TAA (`ResolveTemporal`) |
| `fxaa/FXAA.comp`, `fxaa/fxaa3_11.h` | compute | FXAARenderPass |
| `FullscreenTexture.raster` | raster | FXAAResolvePass |
| `Bloom.shader` | compute | BloomPass (3 variants) |
| `XeGTAO.shader`, `XeGTAO.hlsl` | compute | XeGTAOMainPass (`XrGTAOUltra`) |
| `DepthPrePass.shader` | raster | DepthPrePass |
| `Tools/GenBRDFLUT.comp` | compute | ReflectionPass BRDF LUT |

Shared includes: `PBR.hlsli`, `Lights.hlsli`, `LightCommon.hlsli`, `Materials.hlsli`,
`Common.hlsli`, `Tonemapping.hlsli`, `Bindings.hlsli`.

---

## 6. Migration to the new `RenderGraph`

New API: `RenderGraph.hpp` + `RenderPipelineNew.hpp`. A pipeline is a `RenderPipelineNew`
subclass; individual passes are `DefaultPipelinePassNew` subclasses (reflection-discovered) each
implementing `BuildRenderGraph(RenderGraph&)`. See [RenderGraph design] / [RenderGraph migration]
memories and the reference implementation `Graphics/PipelineNew/ForwardPassNew.cpp`.

### API mapping

| Old (`RenderPipeline*`) | New (`RenderGraph`) |
|---|---|
| `RenderPipelineModule` / `RenderPipelinePass` | `DefaultPipelinePassNew::BuildRenderGraph(rg)` |
| `GetResources()` → `RenderPipelineResource{Attachment/Texture}` | `rg.Create(name, RenderGraphTextureDesc{...})` |
| `RenderPipelineResource{Buffer}` | `rg.Create(name, RenderGraphBufferDesc{...})` (`.perFrame`, `.pingPong`) |
| `RenderPipelineResource{Instance}` | `rg.CreateInstance<T>(name)` / `rg.GetInstanceData<T>(name)` |
| TextureView resource | `rg.CreateView(name, RenderGraphViewDesc{...})` |
| TLAS reference | `rg.Create(name, RenderGraphAccelStructDesc{...})` / `rg.GetTopLevelAS` |
| `GetPassSetup().type = Compute/Graphics/Raytrace` | `rg.AddComputePass(name, shaderPath)` / `AddGraphicsPass(name)` / `AddRaytracePass(name, rid)` |
| `dependencies{name, Read/Write/ReadWrite}` | `.Read(name)` / `.Write(name)` / `.WriteRead(name)` |
| `resolve` | `.Resolve(name)` |
| `stage` | `.Stage(RenderStage::X)` |
| `invertViewport` / `requireJitter` / `requireMotionVector` | `.InvertViewport()` / `.RequireJitter()` / `.RequireMotionVector()` |
| `clearColor` on resource | `.ClearColor(name, color)` or `RenderGraphTextureDesc.clearColor` |
| push constants | `.Constants<T>([](RenderGraph& rg, T& c){...})` |
| descriptor-set override | `.DescriptorSet(set, ds)` (compute) / `GraphicsPipelineDesc.descriptorSetsOverride` (graphics, in the Render lambda) |
| `Render(scene, cmd)` | `.Render([](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd){...})` |
| manual `Dispatch(x,y,z)` w/ hardcoded 8×8 tiling | `.Dispatch(x,y,z)` / `.Dispatch(extent)` — numthreads come from shader reflection (`PipelineDesc::numThreads`) |
| indirect dispatch | `.DispatchIndirect(buffer)` |
| `TraceRays(w,h,d)` | `.TraceRays(w,h,d)` |
| `OnResize(size)` | `.Resize([](RenderGraph& rg, Extent e){...})` |
| `context->GetTexture/GetBuffer` | `rg.GetTexture/GetBuffer(name)` |
| `context->GetPrevTexture` (temporal) | `rg.GetPrevTexture(name)` / `rg.GetPrevBuffer(name)` |
| `context->SetTexture(name, tex)` (published transient) | declare with `rg.Create` + `.Write` (graph owns it) |
| `context->GetSceneDescriptorSet()` | `rg.GetSceneDescriptorSet()` |
| `context->SetColorOutput/SetDepthOutput/SetOutputAttachments` | same names on `rg` |
| graphics pipeline built in `Render()` lambda | `rg.GetOrCreatePipeline(key, lambda)` (graph caches renderpass/framebuffer) |
| `pingPong` texture | `RenderGraphTextureDesc.pingPong` |

**Notable new capabilities:** memory aliasing (`AnalyzeMemoryAliasing` / `ComputeRenderGraphAliasPlan`)
lets transients share memory; the graph derives barriers per-subresource automatically (so most
manual `ResourceBarrier`/`MemoryBarrier` choreography in Bloom/DepthLinearize/Irradiance/TAA/shadows
can be dropped); `RenderStage` adds a dedicated `Transparent = 750` between Forward and Indirect.

### Port status & suggested order

> **Scope decision (2026-07):** the new pipeline is **forward-only** for now. Deferred
> (Culling/GBuffer/DeferredLighting), DepthLinearize, ReflectionPass, IrradianceVolume and XeGTAO
> are intentionally **not** migrated. Everything CompositePass contributed (diffuse irradiance +
> HDR specular reflections) now happens inside the forward shader (`ShadersNew/DefaultForward.hlsl`
> + `ShadersNew/LightsNew.hlsli`).

| Old pass/module | New status | Notes |
|---|---|---|
| ForwardPass (opaque+transparent mesh) | ✅ `ForwardPassNew` | opaque draws are GPU-driven (`DrawIndexedIndirectCount` from `SceneCullingDataNew`, instance data via `BaseInstance` + `useInstanceData` push constant; direct-draw fallback while culling warms up); transparent stays CPU direct draws. PBR light loop + IBL + cascade shadows via `LightsNew.hlsli`. Particles + skybox **not** ported. |
| CullingPass | ✅ `CullingPassNew` | flat indirect buffer with per-pipeline segments (`drawOffsets`, 0xFFFFFFFF sentinel) instead of the 256-buffer descriptor array. No `MemoryBarrier()`: Transfer fill → compute cull → shader-less read pass; graph buffer deps emit the CopyDest/General/ShaderReadOnly(+indirect) barriers. Buffer growth via generation-suffixed names (stale ones age out via `CollectUnusedResources`). |
| LightSetupPass | ✅ `LightSetupPassNew` | light UBO (`LightBufferNew`) + env IBL textures + BRDF LUT written into the scene set (space1: b2/t5/t7/t8/s9); binds shadow map + comparison sampler (bindings 3/4) and copies cascade splits/matrices from `ShadowMapInstanceDataNew`. |
| CompositePass | ✅ folded into forward | diffuse irradiance (cubemap/flat color) + split-sum HDR specular evaluated in `DefaultForward.hlsl::EvaluateAmbient`. SSAO/SSR/GI inputs dropped with the forward-only scope. |
| CascadeShadowMapModule | ✅ `CascadeShadowPassNew` | not generic (no `Base` split). CPU cascade math + interleaved updates at build time; GPU shadow culling into flat per-(cascade,pipeline) slots; one graphics pass per updated cascade writing a layer view of the graph-owned `ShadowMap` array (standard-Z, `clearDepth = 1.0`). `ShadersNew/CullingShadowPassNew.comp` + `ShadowMapIndirectNew.shader` (HAS_MASK; **no HAS_BONES yet** — skinned casters render bind pose). |
| BloomModule | ✅ `BloomPassNew` | graph-owned RG11B10F half-res mip chain + per-mip views; prefilter/downsample/upsample as declarative compute passes (3 separate `.comp` files — graph pipelines don't take variants); per-mip barriers derived from view deps. Runs at Composite(900) so it lands before PostProcess. Enabled by `BloomComponent`. |
| PostProcessPass | ✅ `PostProcessPassNew` | `ShadersNew/PostProcessNew.comp`: sharpen + bloom (white-import fallback when no `BloomComponent`) + ACES into `OutputColor` (color output). A shader-less `OutputColorToSampled` pass hands the output to the viewport in `ShaderReadOnly`. |
| DeferredGBufferPass | ⏸ skipped (forward-only) | |
| DeferredLightingPass | ⏸ skipped (forward-only) | |
| DepthLinearizePass | ⏸ skipped (forward-only) | no hi-Z consumers left in the simplified pipeline. |
| ReflectionPass | ⏸ skipped (forward-only) | HDR specular IBL in the forward shader replaces it for now. |
| IrradianceVolume (Update/Sampling/Debug) | ⏸ skipped (forward-only) | |
| XeGTAORenderModule | ⏸ skipped (forward-only) | |
| MotionVectorModule (3 passes) | ⬜ | ordering camera→object→skinned; `RequireMotionVector`. |
| TemporalAntiAliasing | ⬜ | ping-pong history + jitter + motion vectors. |
| FXAARenderModule | ⬜ | compute + fullscreen resolve. |
| DepthPrePassModule | ⬜ | currently disabled even in old pipeline. |
| SwapchainRenderModule | ⬜ | infra: present + output attachments. |
| ProfilerOverlay / RmlUi | ⬜ | UI stage; port late. |

**Cross-cutting porting risks to watch:**
1. **Descriptor spaces** change (old space2 working-set / space3 bones → new space3=object, lowest-free-slot). Shaders must be re-authored — most old `.shader`/`.comp` cannot be reused verbatim.
2. **Reverse-Z vs standard-Z**: main camera + gbuffer + forward use reverse-Z (`Greater`); shadows use standard-Z (`LessEqual`, clear 1.0). Easy to flip.
3. **Instance-data channels** (`LightInstanceData`, `SceneCullingData`, `ShadowMapInstanceData`, `IrradianceVolumeData`) are the implicit ordering glue — recreate them and keep producer→consumer order explicit.
4. **Undeclared samples** (Composite reads SSAO, PostProcess declares unused `ResolvedHDR`) must become real `.Read()` dependencies.
5. **Cross-frame state** (TAA history, shadow interleaving counters, DDGI ring buffer, previous skinning sets) must survive graph rebuilds — use `pingPong`/`perFrame`, not aliasable transients.
6. **Variant cooking** for shipped builds (see MaterialNodeSystem doc) — embedded `Skore://` shaders aren't exported yet.
