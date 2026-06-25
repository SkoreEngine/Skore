# Esoterica → Skore Animation Port — Progression Tracker

> **The fourth doc — the living one.** The other three are reference/spec; this one
> tracks *what is done, what is in flight, and what is next*. Update it as work lands.
>
> - `AnimationSystemPort.md` — runtime algorithm spec (milestones **M0–M9**)
> - `AnimationEditorPort.md` — editor/authoring spec (milestones **E0–E5**)
> - `AnimationMigrationPlan.md` — Skore integration seams + removal sequence + the **S0–S7** ladder
> - **this doc** — execution status, decision log, change log
>
> **Update protocol:** when a task lands, flip its checkbox and add a dated line to the
> Change Log. When a decision is made mid-implementation, append to the Decision Log.
> When reality diverges from the spec docs, record it under "Spec drift / corrections".

---

## Current status — at a glance

**We are here:** _Runtime M0–M4 done + test-verified; the S3 **editor** (E0/E1) is the next work._
The whole runtime spine is implemented under `Runtime/Source/Skore/Animation/` and passes
**15/15 doctest cases (265 assertions)** in `Tests/Source/Runtime/AnimationTests.cpp`.

**What exists (runtime, all `SK_API`, namespace `Skore::Anim` except the component):**
- value types: `AnimXForm.hpp` (`XForm` + `QuatInverse`/`QuatDelta`/`FastSlerp`), `Percentage.hpp`, `SyncTrack.*`
- `AnimationSkeleton.*` (`Anim::Skeleton` from the scene `Skeleton`), `Pose.*`, `BoneMask.*`, `Blender.*`
- `AnimationClip.*` (clip adapter + sampling + root motion; + test-only `CreateRootMotionTestClip`/`CreateSingleBoneTestClip`)
- `TaskSystem/`: `PoseBufferPool.*`, `Task.hpp`, `TaskSystem.*`, `Tasks/{SampleTask,BlendTask,DefaultPoseTask}.*`
- `Graph/`: `GraphValueType.hpp`, `SampledEvents.hpp` (stub), `GraphContext.*`, `GraphNode.*`, `GraphDefinition.*`, `GraphInstance.*`
- `Graph/Nodes/`: `ReferencePoseNode.*`, `AnimationClipNode.*`, `ControlParameterNodes.*` (Float only), `Blend1DNode.*` (+`ParameterizedBlendNode`,`FloatRange`)
- `Components/AnimationGraphComponent.*` — builds a single-clip graph, drives a `GraphInstance`; registered alongside `AnimationPlayer` in `SceneRegisterTypes.cpp` (Mecanim still intact).

**NEXT — S3 editor (E0/E1). Start here:**
1. Read Skore's existing editor to evolve, not rebuild (Editor doc §2): `Editor/Source/Skore/ImGui/GraphEditor.{hpp,cpp}` (the reusable canvas), `AnimatorEditor.{hpp,cpp}`, `Window/AnimatorGraphWindow.*`, `Window/AnimatorTreeViewWindow.*`, and the resource-schema style in `Runtime/.../Graphics/GraphicsResources.hpp` + how `SceneRegisterTypes.cpp` registers `Resources::Type<...>()`.
2. E0 — author the `.animgraph` resource schema (`AnimGraphResource`/`AnimNodeGraphResource`/`AnimNodeResource`/`AnimParameterResource`/`AnimConnectionResource`) per **AnimationEditorPort.md §4**.
3. E1 — node-type registry (`AnimNodeTypeDesc`, Editor §5) + the immediate-mode `GraphEditor`↔resources reconciliation loop (Editor §6); scaffold `Editor/Source/Skore/Animation/` + `RegisterAnimationEditorTypes()` and the `.animgraph` handler ALONGSIDE the Mecanim ones (**AnimationMigrationPlan.md §4.4, §5 Phase B**). Don't remove Mecanim yet (that's S6/Phase C–D).
- S3 gate (§7): in-editor, build a Blend1D of two clips on a Float param, preview, drag 0→1 → walk↔run no foot-slide.

**Build/verify:** I build + run tests myself — `vcvars64` → `ninja -C cmake-build-debug SkoreTests` → run (see [[build-and-test]] memory). New `.cpp` files need a reconfigure first (`cmake -S . -B cmake-build-debug`).

**Build state:** green. **15/15 cases, 265 assertions** pass.

```
[ pre-S0 ] → S0 → S1 → S2 → S3 → S4 → S5 → S6 → S7
            done done done M4✓/editor-next
```

---

## Pre-flight verification (done 2026-06-25)

Verified the load-bearing claims in `AnimationMigrationPlan.md §0` against the actual
tree so we don't re-check later. Results:

- ✅ **Seam structs present** — `RenderComponents.hpp`: `AnimationPlayer` (class, ~:196),
  `BoneNode` (struct, :246), `Skeleton` (class, :257), `Skeleton::UpdateWorldBones()` (:271).
- ✅ **CMake `GLOB_RECURSE`** in both `Runtime/CMakeLists.txt:1` and `Editor/CMakeLists.txt:1`.
  New files under `*/Source/Skore/Animation/` are auto-picked-up — just reconfigure CMake.
- ✅ **Registration touchpoints exist** — `RegisterAnimationTypes()` at
  `RegisterGraphicsTypes.cpp:15` (called at **:1311**, not :1335 as the doc says);
  `Reflection::Type<AnimationPlayer>();` at `SceneRegisterTypes.cpp:137`.
- ✅ **Esoterica reference source intact** — `C:\dev\Esoterica\Code\Engine\Animation`
  (runtime: `AnimationBlender/Clip/Pose/SyncTrack/RootMotion/BoneMask/Skeleton.*`,
  `Graph/`, `IK/`, `TaskSystem/`, `Components/`, `Systems/`) and
  `C:\dev\Esoterica\Code\EngineTools\Animation` (tools: `ToolsGraph/`, `ResourceCompilers/`,
  `ResourceEditors/`, `Events/`).
- ✅ **No `Animation/` dirs yet** in `Runtime/Source/Skore/` or `Editor/Source/Skore/` —
  confirms we are at the start.

### Spec drift / corrections (keep this current)

- ⚠️ **Line numbers have drifted** from the spec docs (e.g. the `RegisterAnimationTypes()`
  call is at `:1311`, the docs say `:1335`). Treat ALL line refs in the three spec docs as
  approximate — grep for the symbol before editing.
- ⚠️ **Repo path** — spec docs say root is `C:\dev\SkoreEngine\Skore`; the actual working
  tree is `C:\dev\SkoreEngine\NewAnimationSystem\Skore` (isolated copy for this effort).
  Mentally substitute when following the docs.
- ⚠️ **File names: Skore-ify, don't copy the spec's Esoterica-style names.** The migration
  plan's file layout (`Component_AnimationGraph`, `RuntimeGraph_Context`, `RuntimeGraphNode_*`,
  `Task_Sample`, …) uses Esoterica's `Prefix_Name` convention. Skore names files after the
  primary class in clean PascalCase with **no underscores/prefixes** (`LightComponent.hpp`,
  `AudioSource.hpp`, `RigidBody.hpp`). Translate every planned filename: the seam component is
  `AnimationGraphComponent.hpp/.cpp` (NOT `Component_AnimationGraph`). Apply this to all future
  files (e.g. `GraphContext.hpp`, `SampleTask.hpp`, `AnimationClipNode.hpp`).

---

## Milestone ladder (S0–S7)

Status legend: ⬜ not started · 🟦 in progress · ✅ done · ⏸ blocked

Each S-milestone overlays runtime (M) and editor (E) work from the spec docs and carries
its own verification gate from `AnimationMigrationPlan.md §7`.

### 🟦 S0 — Vertical slice (proves the seam) · _Migration Phase A step 1_ — code complete, awaiting user verify

Single clip → `Skeleton::bones` → `UpdateWorldBones`. No graph, no tasks. **Make-or-break
seam proof.** Refs: Migration §2, §3, §6 (S0), §7 (S0).

Files added under `Runtime/Source/Skore/Animation/`: `AnimXForm.hpp`, `SyncTrack.hpp`,
`Pose.hpp/.cpp`, `AnimationClip.hpp/.cpp`, `Components/AnimationGraphComponent.hpp/.cpp`.

- [x] Create `Runtime/Source/Skore/Animation/` (+ `Components/` subdir); CMake configured (Ninja+MSVC).
- [x] `AnimXForm.hpp` — `Anim::XForm` QTS value type. (compose/inverse deferred to S1; identity rotation = `{0,0,0,1}` since `Quat()` is `{0,0,0,0}`). Spec §3.1.
- [x] `Pose.hpp/.cpp` — minimal `Anim::Pose` (parent-space XForms + bind-pose capture/restore). Spec §3.2.
- [x] Clip adapter — engine-side `Anim::AnimationClip` from `AnimationClipResource` (uncompressed reinterpret + bone-name→index reindex). Mirrors `AnimationPlayer::LoadClipData`. **Built component-owned, not RID-cached (D5 deferred — see Decision log).**
- [x] Default `SyncTrack` — one event `{start=0,duration=1}` per clip (ID field deferred to S1, R5). Migration §3.3.
- [x] `AnimationGraphComponent` (`Component` + `Tickable`): `OnStart` resolves child `Skeleton` + entity `Transform`; `OnUpdate` resets pose to bind, samples ONE clip, writes pose → `Skeleton::bones` (uniform-scale broadcast), `UpdateWorldBones()`. Migration §2.1–2.2.
- [x] Register `Reflection::Type<AnimationGraphComponent>();` in `SceneRegisterTypes.cpp` ALONGSIDE `AnimationPlayer` (kept). Migration §5 Phase A.1.
- [~] Build — user-side (per preference; I don't run builds). CMake configure is green.
- [ ] **Gate (§7 S0) — USER:** import an FBX → scene with skinned mesh + child `Skeleton` → add
      `AnimationGraphComponent`, set its `animationClip` to a clip → enter Play → mesh animates + loops;
      confirm `SkeletonUpdated` still drives `SkinnedMeshRenderer::UpdateBones` (GPU deform).

### ✅ S1 — M0 complete + M1 Blender (runtime done; gate tests written, user runs)

Full `Pose` state machine, `BoneMask`, clip root-motion API (port `ExtractRootMotionDelta`),
all Blender ops. Unit-test sync round-trip + blend goldens. Refs: Spec §3, §4; Migration §6 (S1).
Ported faithfully from the Esoterica source (`AnimationSyncTrack/Pose/Blender/RootMotion/BoneMask.*`).

- [x] `Percentage.hpp` — thin f32 wrapper (loop count / normalized time / clamp). Ported from Esoterica `Percentage`. Spec §1, §4.2.
- [x] `SyncTrack.hpp/.cpp` — full port: `SyncTrackTime`/`Range`, `GetTime`↔`GetPercentageThrough` (offset-aware), LCM blend ctor, `CalculateDurationSynchronized`, default track. (Event IDs are `u32`; ID-search helpers deferred to S5; R5.) Spec §4.2–4.4. Replaces the S0 stub.
- [x] `XForm` compose (`operator*`) + `GetInverse()` + Anim quat helpers (`FastSlerp`→`Slerp`, `QuatInverse`, `InvalidIndex`). Verified against Skore's Hamilton `Quat*Quat` / `Quat*Vec3` conventions. Spec §3.1.
- [x] `Anim::Skeleton` (`AnimationSkeleton.hpp/.cpp`) — parent indices + bind ref pose (parent- & model-space) + bone IDs + single LOD, built from the scene `Skeleton`. Spec §3.3; Migration §2.1.
- [x] Full `Pose` (`Pose.hpp/.cpp`) — `State` enum, `Reset(Type)`, ref/zero pose, lazy model-space cache (`CalculateModelSpaceTransforms`/`GetModelSpaceTransform`). Ported from Esoterica. Spec §3.2.
- [x] Refactored `AnimationClip` + `AnimationGraphComponent` onto the new API: clip builds against `Anim::Skeleton`, component drives a real `Anim::Pose` via `Reset(ReferencePose)` + `Sample` (dropped the ad-hoc bind capture).
- [x] `BoneMask.hpp/.cpp` — per-bone weights, `WeightInfo` fast-path flags, `ResetWeights`/`ScaleWeights`/`CombineWith`(`*=`)/`BlendFrom`/`BlendTo`. (SIMD 4-padding dropped; serialized-feather ctor + `BoneMaskPool`/`TaskList` deferred to S6/S2.) Spec §3.3, §4.7.
- [x] `Blender.hpp/.cpp` — parent-space (normal + masked), additive, to/from reference pose, `ApplyAdditiveToReferencePose`, model-space overlay, `BlendRootMotionDeltas`. Operand-reversal applied per D13. Spec §4.6–4.9.
- [x] Clip root-motion: `AnimationClip::GetRootMotionDelta` (loop-aware, root bone trajectory cached at build) + component `ApplyRootMotion` (strips root bone from pose, moves the entity `Transform`; XZ Transform mode for S1). Confirms D13 (loop-wrap `deltaRotA*deltaRotB` == Esoterica `post*pre` reversed). Spec §3.5, §4.9; Migration §2.4.
- [x] **Gate (§7 S1):** written as doctest cases in `Tests/Source/Runtime/AnimationTests.cpp` — sync `GetTime`↔`GetPercentageThrough` round-trip; LCM duration (walk+run→1.1s); Percentage loop/normalize; blend-self=identity; ref-pose endpoints; bone-mask 0/1 fast-paths. **User runs `SkoreTests`.** Root-motion-moves-Transform is the S0/S1 play-mode visual check (user side).

### ✅ S2 — M2 TaskSystem + M3 Graph core (code complete; user verifies build + in-editor)

Task/TaskSystem/PoseBufferPool + Sample/Blend/Reference tasks; node base classes,
GraphContext, Definition/Instance placement-new, control params. Refs: Spec §5, §7; Migration §6 (S2).

- [x] **M2 `TaskSystem/`** — `PoseBufferPool` (heap-owned buffers, zero-copy request/release), `Task` + `TaskContext` (transfer/steal + access/borrow + release), `TaskSystem` (RegisterTask, pre/post-physics schedule, additive-flatten final pose). Tasks: `SampleTask`, `BlendTask` + `AdditiveBlendTask` (weight-snap, optional `const BoneMask*`), `ReferencePoseTask`/`ZeroPoseTask`. Added `AnimationClip::Sample(Pose&, Percentage)`. All `SK_API`. (Secondary skeletons, cached poses, serialization, deferred-mask-task-list, dev-tools all deferred.) Spec §7.
- [x] **M3 node model** — `GraphValueType`, `SampledEvents` (S2 stub: empty ranges; full event system is M6), `GraphContext` (per-frame state, `m_updateID` for value-node caching), and the node bases `GraphNode`+`Definition` (placement-new `CreateNode`), `PoseNode`, `ValueNode`+typed, `GraphPoseNodeResult`, `InstantiationContext`. Spec §5.1–5.2.
- [x] **M3 definition/instance** — `GraphDefinition` (hand-buildable node-def array + per-node instance memory layout via `CreateNodeDefinition<T>`), `GraphInstance` (one `new u8[]` block + placement-new each node, `EvaluateGraph` → root `Update`, `ExecutePre/PostPhysicsPoseTasks`, manual destruct). Plus a clip-free `ReferencePoseNode` to test it. Spec §8.2, §8.4.
- [x] **Gate (M3 spine):** `Animation::GraphInstanceReferencePose` doctest — hand-build a 1-node graph, instantiate (placement-new), `EvaluateGraph` (registers a `ReferencePoseTask`), execute → final pose == reference pose. Validates the full definition→instance→evaluate→task→pose spine.
- [x] First clip node `AnimationClipNode` (advances time sync/unsync → registers `SampleTask` → outputs root-motion delta) + `AnimationGraphComponent` rewired to **build a single-clip graph and drive a real `GraphInstance`** (Evaluate → root motion → Execute pre/post → write pose → FK). The S0/S1 direct-clip path is replaced by the graph path. **In-editor gate (user): mesh animates via the graph.** Spec §4.5.
- [ ] Control parameters (deferred to S3/M4 with `Blend1D`; needs the interned-string-id, R5). Spec §5.4.
- [x] **Gate (M2 part):** `Animation::TaskSystemBlendDAG` doctest — hand-built ReferencePose→ReferencePose→Blend DAG yields the reference pose (exercises register/execute + zero-copy transfer/borrow). _(M3 gate: component drives a graph instance, mesh animates — pending M3.)_

### 🟦 S3 — M4 sync+basic nodes ✅ (runtime) + E0/E1 editor (next) · _Migration Phase B_

AnimationClipNode, Blend1DNode (synchronized); control/const/value nodes. Authoring
resources + node registry + GraphEditor reconciliation. Refs: Spec §4, §9; Editor §4–6; Migration §6 (S3).

- [x] **M4 runtime nodes** — `AnimationClipNode` (S2), `Blend1DNode` + `ParameterizedBlendNode` (parameterization → bracketing source pair + weight; blended sync track; synchronized update driving both sources with one range → phase lock; `BlendTask` + root-motion-delta blend), `ControlParameterFloatNode` (settable cached value) + `FloatRange` + `GraphInstance::SetControlParameterFloat`. Doctest `Blend1DNode`: param 0→clip0, 1→clip1, 0.5→exact midpoint. Spec §4.5, §9 (Tier 3). _(other const/value/math nodes + Bool/ID/Vector control params added as later nodes need them.)_
- [ ] Editor `Animation/` module scaffold + `RegisterAnimationEditorTypes()` ALONGSIDE Mecanim. Migration §5 Phase B.3.
- [ ] Authoring resource schema (`AnimGraphResource` et al.). Editor §4.
- [ ] Node-type registry (`AnimNodeTypeDesc`). Editor §5.
- [ ] GraphEditor ↔ resources reconciliation loop. Editor §6.
- [ ] `.animgraph` asset handler ALONGSIDE the controller handler. Migration §4.4, §5 Phase B.3.
- [ ] **Gate (§7 S3):** create `.animgraph`, drop 2 clip nodes into a Blend1D, wire a Float param,
      preview, drag 0→1 → walk↔run no foot-slide. Undo/redo via `UndoRedoScope`.

### ⬜ S4 — M5 compile + E2 preview

`GraphCompilationContext` + per-node compile; `.animgraph` → compiled `GraphDefinition`;
live preview in the Animator workspace via PlayMode. Refs: Spec §8; Editor §9; Migration §6 (S4).

- [ ] Compiled-definition runtime resource + `RegisterAnimationGraphTypes()`. Migration §4.2, §5 Phase A.2.
- [ ] Port `GraphDefinitionCompiler` / `GraphCompilationContext` (fed from resources). Spec §8.3; Editor §9.1.
- [ ] Live preview entity + skeletal mesh in `SceneViewWindow`. Editor §9.2.
- [ ] **Gate (§7 S4) — TARGET:** Blend1D walk↔run on a Float param, foot-synced, in editor preview.

### ⬜ S5 — M6 state machines + E3 params/SM

StateMachine/State/Transition nodes + conditions; parameters panel w/ live preview; SM
editing + nav stack + breadcrumbs. Refs: Spec §6; Editor §7, §8.4; Migration §6 (S5).

- [ ] Runtime: `StateMachineNode`, `StateNode`, `TransitionNode`, conditions. Spec §6.
- [ ] Editor: parameters panel (live preview), SM graph editing, nav stack + breadcrumbs. Editor §7, §8.4.
- [ ] **Gate (§7 S5):** idle→walk→run SM, synchronized transitions; outgoing-branch events marked inactive (no double-fire).

### ⬜ S6 — M8 layers/advanced + E4 variations/clip-events · _Migration Phase C/D (Mecanim removal)_

Layers/bone-masks/Blend2D/selectors/speed-scale/root-motion-override/2-bone-IK/warps;
variation hierarchy; `.animclip` sidecar + clip-event/sync timeline editor. **Mecanim removal lands here.**
Refs: Spec §9–10; Editor §8.5; Migration §3.3, §5 Phase C/D, §6 (S6).

- [ ] Advanced runtime nodes (layers, bone masks, Blend2D, selectors, speed-scale, root-motion override, 2-bone IK, warps). Spec §9 Tier 5–6, §10.
- [ ] Variation hierarchy + per-node overrides. Editor §4.5, §8.5.
- [ ] `.animclip` settings sidecar + sync/event timeline editor. Migration §3.3.
- [ ] **Mecanim removal — Phase C** (runtime): remove `AnimationPlayer` reflection/struct,
      `RegisterAnimationTypes()` body, Mecanim resource structs. Keep clip/skin/skeleton. Migration §5.5–5.7.
- [ ] **Mecanim removal — Phase D** (editor): remove `AnimatorEditor`, windows, controller handler;
      FIX `TypeActions.cpp` clip-preview parent dependency. Migration §5.8–5.9.
- [ ] **Gate (§7 S6):** upper-body additive aim layer w/ bone mask over lower-body locomotion;
      authored sync-event improves phase-lock; old Animator UI gone, `.animgraph` is the only path.

### ⬜ S7 — M9 physics/netcode + E5 debug/record

IK rig, ragdoll (real pre/post-physics split via new `Scene::Update` passes), task
serialization, live-debug attach + record/replay. Refs: Spec §7.5, §8.6, §10.4, §10.7;
Editor §9.3; Migration §2.3, §6 (S7).

- [ ] Real two-phase split in `Scene::Update` (animation-pre before physics, animation-post after WriteBackTransforms). Migration §2.3.
- [ ] IK rig + ragdoll (pre/post-physics tasks). Spec §10.4.
- [ ] Task serialization (networking/replay). Spec §8.6.
- [ ] Editor live-debug attach + record/replay scrubbing. Editor §9.3.
- [ ] **Gate (§7 S7):** ragdoll blend on death event; live-debug highlights active nodes; deterministic record/scrub.

> Port `DebugView_Animation` (active task tree, per-node time/sync, root-motion ring buffer)
> from S2 onward, gated behind `SK_DEVELOPMENT_TOOLS`.

---

## Removal / migration overlay (Phases A–D)

The new path is added and proven BEFORE old code is deleted (engine compiles at every step).
Tracks `AnimationMigrationPlan.md §5`.

- [ ] **Phase A** (lands at S0–S2) — add new runtime spine; both components coexist. Register
      `AnimationGraphComponent` alongside `AnimationPlayer`; add compiled-definition resource + `RegisterAnimationGraphTypes()`.
- [ ] **Phase B** (lands at S3) — add new editor module + `.animgraph` handler + `ResourceEditor_AnimationGraph`
      alongside the Mecanim Animator. Two editors coexist on the Animator workspace.
- [ ] **Phase C** (lands at S6) — remove Mecanim runtime (`AnimationPlayer`, `RegisterAnimationTypes` body,
      Mecanim resource structs). Keep clip/skin/skeleton + `SkinnedMeshRenderer`.
- [ ] **Phase D** (lands at S6) — remove Mecanim editor (`AnimatorEditor`, windows, controller handler);
      fix `TypeActions.cpp` clip-preview parent dependency in the SAME step.

---

## Decision log

Locked decisions carried from the spec docs (do not re-litigate without a reason):

- **D1 — Full Esoterica-style stack** (Migration, header). Reuse only the `GraphEditor` widget,
  the glTF/FBX-imported clip/skin/skeleton resources, and the `Skeleton`/`BoneNode` + skinning path.
  Mecanim `AnimationPlayer` + its controller assets/editor are removed.
- **D2 — Namespace `Skore::Anim`** for everything except the public `AnimationGraphComponent`
  (kept in `Skore` for reflection/serialization parity). Migration §1.1.
- **D3 — `AnimationGraphComponent` is `Tickable`, not `FixedTickable`** — animation runs at frame
  rate (parity with how `AnimationPlayer` lived in `OnUpdate`). Migration §2.3.
- **D4 — Collapse the Esoterica two-phase update into one `OnUpdate`** for M1–M8 (no ragdoll/IK):
  the task system runs everything pre-physics when there's no physics dependency. Design the component
  with private `EvaluateAndPrePhysics()` + `PostPhysics()` so M9/S7 can split across two `Scene::Update`
  passes without touching call sites. Migration §2.3.
- **D5 — Load clips into an engine-side runtime `Anim::AnimationClip` ONCE per RID (cache), not
  sample-from-resource per frame.** Keyframes are already uncompressed. Migration §3.1.
- **D6 — Runtime `Anim::Pose` bone-index order matches `Skeleton::bones` 1:1** — the final write is a
  straight index copy; name remap happens only in the clip adapter. Migration §2.1, §3.2.
- **D7 — `.animclip` settings sidecar** (recommended) carries authored sync events + root-motion
  config; clip-cache merges geometry (`AnimationClipResource`) + semantics. Lands at S6/E4. Migration §3.3.
- **D8 — Remove `AnimationAvatar*`** (superseded by BoneMask nodes + Variations). Migration §5.7.
- **D9 — Keep `WorkspaceTypes::Animator=3`** and its workspace desc, repurposed to host the new windows. Migration §1.2.

Decisions made during implementation:

- **D10 — D5 (shared RID-keyed clip cache) deferred to S1/S2.** S0 builds the runtime
  `Anim::AnimationClip` **component-owned** (one per component, freed in `OnDestroy`). The
  shared cache's key must incorporate skeleton identity (channels are reindexed to a
  specific skeleton's bone order), which needs the runtime `Anim::Skeleton` that doesn't
  exist until S1/S2. S0 still honors D5's *intent* (build the runtime clip once, never
  sample-from-resource per frame).
- **D11 — Pose reset-to-bind each frame.** `OnUpdate` resets the working pose to the
  captured bind pose before sampling, so bones with no animation channel hold the bind
  pose (cleaner/more robust than the Mecanim path, which relied on stale `bones` state).
- **D12 — XForm uniform scale.** `XForm` stores a single `f32` scale (Esoterica convention);
  `BoneNode`'s `Vec3` scale is taken from `.x` on read and broadcast on write. Revisit only
  if a rig needs non-uniform bone scale (would also touch the skinning seam).
- **D13 — Quaternion multiply convention is REVERSED vs Esoterica.** Skore's `Quat * Quat` is
  standard Hamilton (`p*q` applies q then p); Esoterica's is the opposite (`a*b` applies a then b).
  Verified via Skore's `operator*` impl + Esoterica's `Pose::CalculateModelSpaceTransforms`
  (`childLocal * parentModel`) vs Skore's needed `parentModel * childLocal`. **Rule: when porting
  any *explicit* quaternion (or transform) multiply from Esoterica, swap the operands.** Applied in
  `XForm::operator*`, the Blender's additive rotation, `ModelSpaceBlend` FK + `QuatDelta`, and will
  apply to all future IK/warp/root-motion math. Translation+scale (Esoterica packs into one `Vec4`)
  is blended as separate `Vec3` translation + `f32` scale.

_(Append new decisions here as implementation forces them.)_

---

## Open questions / risks

- **R1 — Mecanim removal & serialized scenes (Migration §5.5):** scenes that serialized an
  `AnimationPlayer` component may fail to resolve its type after removal. Mitigation: keep an empty
  deprecated `AnimationPlayer : Component` stub for one migration window, OR verify the entity loader
  skips unknown component TypeIDs. **Decide at S6.** Recommended: stub for one window, delete later.
- **R2 — `TypeActions.cpp` clip-preview dependency (Migration §5.9):** clip preview currently requires
  parent == `AnimationControllerResource`. Must repoint to `AnimGraphResource::PreviewEntity` or the new
  `.animclip` asset in the SAME step as the controller-resource removal. **Address at S6/Phase D.**
- **R3 — Skeleton LOD:** spec assumes a two-level skeleton LOD (`m_numBonesToSampleAtLowLOD`). Skore's
  `Skeleton`/`BoneNode` may not expose this — confirm or stub a single-LOD path at S1.
- **R4 — `Quat` helpers:** spec needs `Quat::FastSlerp` and `Quat::Delta` (inverse-compose). Verify/add
  to Skore's math lib at S1 (Spec §1 mapping table).
- **R5 — Interned `StringID`:** event/param IDs need a cheap, hashable interned string id. Confirm Skore
  has one or add it (Spec §1). Needed from S2/S3.
- **R6 — Remaining test gaps (after the numeric backfill).** Still unverified by unit tests, lower priority
  or needs scaffolding: (a) **additive blend** numerics — and note the **additive-scale** issue (zero pose has
  scale 1, so additive scale adds 1; matches Esoterica but is only correct once additive poses are authored as
  deltas — revisit at M8/S6); (b) **model-space (`GlobalBlendTask`) blend** — flagged "verify at S6" anyway;
  (c) the SyncTrack **blended-track constructor** (event layout, not just the duration scalar); (d) **clip
  sampling through the graph** + the clip adapter (`CreateFromResource`) — currently only the in-editor visual
  check; (e) TaskSystem **zero-copy buffer reuse** (pool high-water mark) — spec §13 wanted this asserted.

---

## Change log

- **2026-06-25** — Reviewed all three spec docs; verified seam structs, CMake glob, registration
  touchpoints, and Esoterica reference source against the tree. Recorded spec drift (line numbers,
  repo path). Created this progression tracker. Status: pre-S0, ready to begin S0.
- **2026-06-25** — **S0 implemented.** Added `Runtime/Source/Skore/Animation/` module: `AnimXForm.hpp`,
  `SyncTrack.hpp`, `Pose.hpp/.cpp`, `AnimationClip.hpp/.cpp`, `Components/Component_AnimationGraph.hpp/.cpp`.
  Registered `AnimationGraphComponent` in `SceneRegisterTypes.cpp` alongside `AnimationPlayer` (both coexist).
  CMake configured (Ninja+MSVC, `Build/`). Decisions D10–D12. Build/verify handed to user (they build the
  engine themselves; I stopped running builds). Next: S1.
- **2026-06-25** — Renamed `Component_AnimationGraph.{hpp,cpp}` → `AnimationGraphComponent.{hpp,cpp}` to
  match Skore's file-naming convention (clean PascalCase after the class, no `Component_` prefix); fixed the
  include in `SceneRegisterTypes.cpp`. Recorded the naming rule under spec drift (applies to all future files).
- **2026-06-25** — **S1 started.** Ported `Percentage.hpp` and the full `SyncTrack.hpp/.cpp` from the
  Esoterica source (offset-aware `GetTime`/`GetPercentageThrough`, LCM blend ctor, `CalculateDurationSynchronized`);
  replaced the S0 SyncTrack stub and repointed `AnimationClip`. Added small local `Lerp`/`Gcd`/`LCM` helpers
  (Skore `Math` lacks scalar versions). Remaining S1: XForm compose/inverse, `Anim::Skeleton`, full `Pose`,
  `BoneMask`, clip root-motion, `Blender`.
- **2026-06-25** — **S1 pose foundation.** Added `XForm` compose/inverse + `QuatInverse`/`FastSlerp`
  (`AnimXForm.hpp`), `Anim::Skeleton` (`AnimationSkeleton.hpp/.cpp`), and the full `Pose`
  (`Pose.hpp/.cpp`, state machine + lazy model-space cache), ported from Esoterica. Verified Skore's `Quat`
  is standard Hamilton so `childGlobal = childLocal * parentGlobal` composes correctly. Refactored
  `AnimationClip` (now builds against `Anim::Skeleton`) and `AnimationGraphComponent` (drives a real `Pose`
  via `Reset(ReferencePose)`) onto the new API. Remaining S1: `BoneMask`, clip root-motion, `Blender`.
- **2026-06-25** — **S1 blending core.** Ported `BoneMask` (`BoneMask.hpp/.cpp`) and the full `Blender`
  (`Blender.hpp/.cpp`: parent-space normal/masked, additive, to/from reference, apply-additive,
  model-space overlay, root-motion-delta blend) from the Esoterica source. Recorded D13 — Skore's
  quaternion multiply is reversed vs Esoterica, so explicit quat multiplies are operand-swapped
  (verified end-to-end). Added `QuatDelta` to `AnimXForm.hpp`. Remaining S1: clip root-motion +
  component `ApplyRootMotion` (final piece), then the S1 unit-test gate.
- **2026-06-25** — **S1 complete (code).** Added clip root-motion (`AnimationClip::GetRootMotionDelta`,
  loop-aware, root-bone trajectory cached at build) and the component's `ApplyRootMotion` (+ reflected
  `applyRootMotion` field), mirroring the proven `AnimationPlayer` convention — which independently confirms
  D13. Wrote the S1 gate as doctest cases (`Tests/Source/Runtime/AnimationTests.cpp`): Percentage, SyncTrack
  round-trip + LCM=1.1s, blend-self=identity, ref-pose endpoints, bone-mask fast paths. S1 runtime spine done;
  user builds + runs `SkoreTests`. Next: S2 (TaskSystem + Graph core).
- **2026-06-25** — **Build fix (user-reported).** `Blender.cpp`: the helper `struct AdditiveBlend`
  collided with the `Blender::AdditiveBlend` method — inside that method (and `BlendRootMotionDeltas`/
  `ApplyAdditiveToReferencePose`) unqualified `AdditiveBlend` bound to the member function, so
  `…<AdditiveBlend>` failed with C2672 "type expected". Renamed the helper struct → `AdditiveBlendFn`.
  (Lesson: don't name a Blender-internal helper the same as a `Blender` member function.)
- **2026-06-25** — **Link fix (user-reported).** `SkoreTests` failed to link the `Anim::` value types
  (`LNK2019`/`LNK1120`): `SkoreRuntime` is a DLL and these classes weren't exported. Added `SK_API` to
  `SyncTrack`, `Skeleton`, `Pose`, `BoneMask`, `Blender`, `AnimationClip` (header-only `XForm`/`Percentage`
  don't need it). Recorded as a recurring rule for all S2+ classes used by tests/editor.
- **2026-06-25** — **S2/M2 (TaskSystem) complete.** Ported the deferred pose-task system under
  `Animation/TaskSystem/`: `PoseBufferPool` (heap-owned buffers for stable pointers + zero-copy reuse),
  `Task`/`TaskContext` (transfer/steal vs access/borrow), `TaskSystem` (RegisterTask, pre/post-physics
  schedule, additive-flatten), and tasks `SampleTask`/`BlendTask`/`AdditiveBlendTask`/`ReferencePoseTask`/
  `ZeroPoseTask`. Added `AnimationClip::Sample(Pose&, Percentage)` (refactored channel loop into a shared
  helper). M2 doctest (`TaskSystemBlendDAG`) added. Simplified for S2: single skeleton, optional raw
  `BoneMask*` (deferred mask-task-list is M8), no cached poses/serialization/dev-tools. Next: M3 graph core.
- **2026-06-25** — **S2/M3 node model.** Added `Animation/Graph/`: `GraphValueType`, `SampledEvents`
  (S2 stub), `GraphContext` (hpp/cpp), and the node bases `GraphNode`/`Definition` (placement-new
  `CreateNode`), `PoseNode`, `ValueNode`+typed, `GraphPoseNodeResult`, `InstantiationContext` — ported from
  Esoterica's RuntimeGraph_Node/Context/ValueTypes/SampledEvents (standalone only; referenced/external
  graphs, recording, serialization, layers, dev-tools all stripped). Remaining M3: `GraphDefinition` +
  `GraphInstance` (placement-new instantiation), `AnimationClipNode`, component wiring + test.
- **2026-06-25** — **S2/M3 definition/instance.** Added `GraphDefinition` (hand-buildable; `CreateNodeDefinition<T>`
  computes the per-node instance memory layout) and `GraphInstance` (`new u8[]` block + placement-new each
  node via `InstantiateNode`, `EvaluateGraph` + `ExecutePre/PostPhysicsPoseTasks`, manual destruct), plus a
  clip-free `ReferencePoseNode`. Doctest `GraphInstanceReferencePose` validates the whole spine (definition →
  placement-new instance → evaluate → task → reference pose). Remaining to close S2: `AnimationClipNode` +
  wire `AnimationGraphComponent` to drive a `GraphInstance`.
- **2026-06-25** — **S2 complete (code).** Added `AnimationClipNode` (advances time synchronized/
  unsynchronized, registers a `SampleTask`, outputs the clip root-motion delta) + `AnimationClip::GetSamplingDuration`.
  Rewrote `AnimationGraphComponent` to hand-build a single-clip `GraphDefinition` and drive a real
  `GraphInstance` each frame (Evaluate → ApplyRootMotion → Execute pre/post → write pose → strip root bone →
  `UpdateWorldBones`), replacing the S0/S1 direct-clip path. S2 (M2+M3) done end-to-end; user verifies the
  in-editor "mesh animates via the graph" gate. Next: S3 (M4 nodes + E0/E1 editor).
- **2026-06-25** — **Test backfill (numeric).** Audited coverage — the existing 8 cases were mostly spine /
  fast-path. Added 6 numeric tests for the load-bearing + D13-sensitive math: `XFormComposeAndInverse`
  (translation/scale compose + `X*X⁻¹`=identity), `QuatHelpers` (`QuatInverse`, `QuatDelta`), `PoseModelSpaceFK`
  (3-bone chain with a 90° bend → b2 at (-1,1,0)), `BlendIntermediateWeight` (slerp/lerp midpoint at w=0.5),
  `BoneMaskOps` (scale/blend/combine), `RootMotionDelta` (forward + loop-wrap + rotation, via a new test-only
  `AnimationClip::CreateRootMotionTestClip`). 14 cases total.
- **2026-06-26** — **Tests built + run (green).** Built `SkoreTests` incrementally (vcvars → `ninja -C
  cmake-build-debug SkoreTests`) and ran `--test-case=Animation::*`: **14/14 cases, 256/256 assertions pass.**
  The D13 quaternion convention is now validated, not assumed. Going forward I build + run the test suite
  myself to validate (procedure recorded in memory).
- **2026-06-26** — **S3/M4 runtime nodes (Blend1D + control params).** Ported `ParameterizedBlendNode` +
  `Blend1DNode` (`Graph/Nodes/Blend1DNode.*`, with `FloatRange`), `ControlParameterFloatNode`
  (`Graph/Nodes/ControlParameterNodes.*`), a `SampledEventsBuffer::BlendEventRanges` stub, the test-only
  `AnimationClip::CreateSingleBoneTestClip`, and `GraphInstance::SetControlParameterFloat`. Built + ran:
  **15/15 cases, 265 assertions pass** (incl. the new `Blend1DNode` blend-at-0.5 test). Two build gotchas hit
  + recorded in memory: new .cpp files need a reconfigure (ninja reglob doesn't trigger it); nested structs
  (`Definition`/`Parameterization`) with out-of-line methods need their own `SK_API`. Next: E0/E1 editor.
