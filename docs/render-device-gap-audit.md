# RenderDevice/RHI gap audit vs skore main

Task: APX-47. Branch: v2. Reference: `Runtime/Source/Skore/Graphics/Device.hpp`
and `Runtime/Source/Skore/Graphics/Devices/Vulkan/*` on `main` (C# engine).

## Method

- Compared `plugins/render_device/render_device.h` (v2 RHI surface) and
  `plugins/render_device/render_device.c` (stub bodies) against `main`'s
  `GPUDevice` / `GPUCommandBuffer` / per-resource interfaces.
- Design constraint honored: pipelines/descriptor sets are **not** RID in the
  RHI. Every creation/bind datum must be passed into the RHI; resource reads
  happen outside the RHI (caller resolves shader/material RIDs to data).
  Converting pipelines/descriptor sets to RID inside the RHI is out of scope.

## Verdict

- The v2 header already exposes a near-complete **surface** for shared
  primitives: buffer, texture, view, sampler, shader, render pass, framebuffer,
  swapchain, command buffer (incl. secondary + `execute_commands`), queue,
  fence/semaphore, query pool, and acceleration structures. All entry points
  exist and all bodies are stubs in `render_device.c`.
- Real gaps vs main are: (A) device/adapter enumeration plus feature/limit
  queries, (B) missing pipeline/attachment/query desc fields, (C) missing
  command-buffer operations, (D) swapchain/AS/queue query + mutation APIs,
  (E) every implementation being a no-op stub, and (F) RID-style call sites to
  re-plumb so the RHI receives full data.

## Gap list

### A. Device / adapter (port `VulkanAdapter` + `VulkanDevice` from main)

- [ ] Adapter enumeration: `GetAdapters()`, `SelectAdapter()`, `GetScore()`,
      `GetName()` — v2 has none.
- [ ] Device queries: `GetProperties()` / `GetFeatures()` / `GetAPI()` →
      `DeviceProperties` (type/name/vendor/driver), `DeviceFeatures`
      (tessellation, geometry, bindless, BDA, drawIndirectCount, rayTracing,
      resolveDepth, memoryBudget, …), `DeviceLimits` (max texture sizes, work
      group limits, min offset alignments, timestamp period, …). Add C enums
      `sk_device_type_t` and `sk_graphics_api_t`.
- [ ] `GetMemoryBudgets()` → `MemoryHeapBudget` (usage / budget / deviceLocal).
- [ ] Desc getters on resources (`GetDesc`, `GetExtent`, `GetFormat`,
      `GetImageCount`, `GetCurrentImageIndex`, `GetMappedData`) — add the ones
      callers need.

### B. Pipeline / attachment / query desc fields main has, v2 lacks

- [ ] `DescriptorSetLayoutBinding`: add `renderType` (`RenderType` enum: Void,
      Int, Float, Vector, Matrix, Image, Sampler, Array, RuntimeArray, Struct,
      …) and `size`; group bindings per **set** (`DescriptorSetLayout.set`)
      instead of v2's flat `sk_descriptor_set_desc_t`.
- [ ] `PipelineDesc`: add `outputVariables` and vertex input `stride`.
- [ ] `GraphicsPipelineDesc`: add `material`, `vertexInputStride`,
      `allowImmediateSet`, `descriptorSetsOverride`, and
      `conservativeRasterizationMode` (+ `ConservativeRasterizationMode` enum:
      Disabled/Overestimate/Underestimate).
- [ ] `ComputePipelineDesc`: add `allowImmediateSet`, `descriptorSetsOverride`.
- [ ] `RayTracingPipelineDesc`: v2 only has `max_recursion_depth`. Main passes
      `shader` + `variant` + `descriptorSetsOverride` (RID-based there). v2 must
      instead receive the shader handle + full set overrides, no RID.
- [ ] `QueryPoolDesc`: add `returnAvailability`.
- [ ] `AttachmentDesc`: add `stencilLoadOp`, `stencilStoreOp`, `sampleCount`;
      `RenderPassDesc`: add `resolveAttachments`.
- [ ] `GeometryTrianglesDesc`: add `transformBuffer`, `transformOffset`.

### C. Missing command-buffer operations (main `GPUCommandBuffer`)

- [ ] `MemoryBarrier()` — full memory barrier.
- [ ] `ResourceBarrier(BLAS/TLAS, oldState, newState)` — AS barriers.
- [ ] `CopyAccelerationStructure(BLAS|TLAS src→dst, compress)`.
- [ ] `CopyQueryPoolResults(..., GPUBuffer* dst, dstOffset, stride)` — GPU-side
      query copy; v2 only has host `get_query_pool_results`.
- [ ] Inline descriptor writes (`SetTexture/SetBuffer/SetSampler/
      SetTextureView/SetAccelerationStructure(pipeline, set, binding, …)`) are
      RID-based in main — **do not port that form**; keep v2's
      `update_descriptor_set` with full writes.

### D. Swapchain / AS / queue APIs

- [ ] Swapchain queries: `GetExtent`, `GetFormat`, `GetImageCount`,
      `GetCurrentImageIndex`, `GetTextures` (v2 has `get_swapchain_image` only).
- [ ] BLAS: `IsCompacted()`, `GetCompactedSize()`.
- [ ] TLAS: `UpdateInstances()`, `UpdateInstance(index, instance)`,
      `SetInstanceCount()`.
- [ ] Queue: `SubmitAndWait()` convenience.

### E. Incomplete implementations (`render_device.c` — 100% stubs)

- [ ] Every entry is a no-op: creates return zero handles, state setters and
      copies no-op, `map` returns NULL. `acquire_next_image` / `present` return
      `SK_DEVICE_RESULT_ERROR`; `get_query_pool_results` returns `-1`;
      memory-requirement queries return all-zero structs.
- [ ] Real behavior lands in the upcoming `vulkan_render_device` plugin
      (APX-49 / APX-50), not in the shared stub.

## F. Call sites still assuming RID-based pipeline/descriptor reads (re-plumb in APX-48)

1. `sk_graphics_pipeline_desc_t.vertex_shader` / `.fragment_shader` are
   `void_ptr_t` labelled "sk_shader_t handle or opaque RID" — the RHI would read
   shader data through them. Replace with typed `sk_shader_t`; layout data
   already arrives via `sk_pipeline_desc_t`.
2. `sk_compute_pipeline_desc_t.compute_shader` — same.
3. `sk_descriptor_set_override_t.descriptor_set`, `sk_graphics_pipeline_desc_t.render_pass`
   and `.previous_pipeline` are `void_ptr_t` opaque. Replace with typed
   `sk_descriptor_set_t` / `sk_render_pass_t` / `sk_pipeline_t`.
4. `bind_descriptor_set(…, pipeline, set_index, …)` reads the pipeline's
   descriptor layout at bind time. Pass the set layout explicitly (pipeline
   handle is fine only as an RHI-owned layout owner, never a resource RID).
5. `push_constants(…, pipeline, …)` reads the pipeline's push-constant ranges.
   Same treatment.
6. Not ported by design: main's `CreateDescriptorSet(RID shader, variant, set)`,
   `TraceRays(pipeline, …)`, and inline `SetTexture/…(pipeline, set, binding, …)`.
   v2 replaces them with full-data `update_descriptor_set` and full-SBT
   `trace_rays`.

## Out of scope (explicitly)

- Converting pipelines/descriptor sets to RID inside the RHI.
- RID resource lookups inside the RHI (shader/material graphs resolved by
  callers, passed in as data).
- Already-deferred items from `render_device.h`: subpasses, dynamic rendering
  helpers, mesh/task draws, timeline semaphores, multi-queue ownership transfer,
  stencil-separate load/store (now in gap B), capability/feature queries (gap A).
- Integration tests (later milestone per goal).
