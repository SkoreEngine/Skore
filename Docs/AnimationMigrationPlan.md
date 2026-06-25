# Esoterica Animation Graph — Skore Integration & Migration Plan

> The third of three docs. Read alongside:
> - `AnimationSystemPort.md` — runtime algorithm internals (milestones **M0–M9**)
> - `AnimationEditorPort.md` — editor/authoring internals (milestones **E0–E5**)
> - **this doc** — Skore-specific *integration seams, migration sequence, and execution order*.
>
> **Decision (locked):** full Esoterica-style stack. Reuse only (1) the `GraphEditor`
> widget, (2) the glTF/FBX-imported clip/skin/skeleton resources, and (3) the working
> `Skeleton`/`BoneNode` + skinning path. The Mecanim `AnimationPlayer` and its controller
> assets/editor are removed. Repo root: `C:\dev\SkoreEngine\Skore`.

## 0. Verified facts that shape the plan

- **CMake `GLOB_RECURSE` on `Source/*`** in `Runtime/CMakeLists.txt` and
  `Editor/CMakeLists.txt`. New files under `Runtime/Source/Skore/Animation/` and
  `Editor/Source/Skore/Animation/` are auto-picked-up — no CMake edits, just reconfigure.
- **Skinning seam = `Skeleton::bones` (local TRS) + `Skeleton::UpdateWorldBones()`**
  (`RenderComponents.cpp:1201`). `UpdateWorldBones` does FK into `localToWorldBones`, then fires
  `EntityEventType::SkeletonUpdated`; `SkinnedMeshRenderer::ProcessEvent` (`RenderComponents.cpp:172`)
  calls `UpdateBones()` → GPU. Animation just fills `skeleton->bones[i].{position,rotation,scale}`
  then calls `UpdateWorldBones()`. **Do NOT change this path.**
  `BoneNode{ String name; u32 parentIndex(U32_MAX=root); Vec3 position; Quat rotation; Vec3 scale; }`.
- **Scene update order** (`Scene.cpp:228-269`): ExecuteEvents → UpdateCharacterControllers →
  **FixedTickable + physics fixed step** → WriteBackTransforms → navigation → **Tickable OnUpdate (LAST)**.
  Physics runs BEFORE `OnUpdate`. This inverts Esoterica's
  Evaluate→PrePhysics→[physics]→PostPhysics ordering — the biggest seam subtlety (§2.3).
- **`AnimationPlayer` is the only consumer** of the Mecanim structs (`Component + Tickable`):
  `OnStart` finds child `Skeleton` (via `EntityFlags::HasSkeleton`) + `Transform`; `OnUpdate`
  (`RenderComponents.cpp:1010`) samples clips into `m_skeleton->bones`, calls `UpdateWorldBones`,
  then `ApplyRootMotion` to the `Transform`.
- **Clip import already produces what we need** (`FBXImporter.cpp:560-662`, `GLTFImporter.cpp`):
  `AnimationClipResource{Name,Duration,NumFrames,FrameRate,TimeBegin,TimeEnd,Channels(SubObjectList),
  KeyFramesBuffer(Buffer)}`; `AnimationChannelResource{Name(=bone name),KeyFrames,BufferOffset}` into a
  packed buffer of `AnimationKeyFrame{f64 time;Vec3 position;Quat rotation;Vec3 scale;}`, NumFrames
  entries/channel at BufferOffset, channels keyed BY NAME. **No sync-track or root-motion-delta data
  exists in the imported clip.**
- **Registration touchpoints (verified):**
  - Runtime resources: `RegisterGraphicsTypes.cpp` `RegisterAnimationTypes()` lines 15-107, called at
    1335. Clip schema (KEEP) lines 1252-1274.
  - Runtime component reflection: `SceneRegisterTypes.cpp:137` `Reflection::Type<AnimationPlayer>();`.
  - Editor windows: `Editor.cpp:49-50` includes, `1720-1721` `Reflection::Type<Animator*Window>()`,
    `1690` `RegisterAnimatorEditorTypes()` decl + `1724` call, `1652-1656` Animator workspace desc.
  - Editor handler: `ResourceAssets.cpp:3152` decl + `3253` call `RegisterAnimationControllerHandler`.
  - Handler impl `Handlers/AnimationControllerHandler.cpp` (`.animcontroller`, opens Animator).
  - `WorkspaceTypes::Animator = 3` (`EditorCommon.hpp:70`). Asset open routes by extension via
    `handlersByExtension` (`ResourceAssets.cpp:56,790-797`).
  - Other refs: `AnimatorEditor.{hpp,cpp}`, `AnimatorGraphWindow`, `AnimatorTreeViewWindow`,
    `EditorWorkspace.{hpp,cpp}` (`m_animatorEditor`,`GetAnimatorEditor`),
    `TypeActions.cpp:125-141` (clip preview requires parent == `AnimationControllerResource`).
- **No `.animcontroller` assets exist on disk** (glob found none) — no asset-file migration. Only
  in-scene `AnimationPlayer` *components* (serialized by type id) are a removal concern.

## 1. Module layout & build integration

### 1.1 Runtime — `Runtime/Source/Skore/Animation/` (GLOB auto-picks-up)
```
AnimXForm.hpp                       // QTS value type (NOT Skore's Transform component)
Pose.hpp/.cpp  BoneMask.hpp/.cpp  RootMotion.hpp/.cpp  Target.hpp
AnimationClip.hpp/.cpp              // engine-side runtime clip form + SyncTrack + root-motion
SyncTrack.hpp/.cpp  Blender.hpp/.cpp
Graph/      RuntimeGraph_Context|Definition|Instance|Node|ValueTypes|SampledEvents.*
Graph/Nodes/  RuntimeGraphNode_*.{hpp,cpp}
TaskSystem/ Task.* TaskSystem.* TaskPosePool.* TaskSerializer.* Tasks/Task_*.*
IK/         IKRig.* IKChainSolver.*           (M9)
Components/ Component_AnimationGraph.hpp/.cpp  // the new seam component (§2)
AnimationResources.hpp              // compiled-definition (.animgraph runtime) resource structs
RegisterAnimationGraphTypes.cpp     // new runtime registration entry point (§4, §5)
```
- Namespace `Skore::Anim` for everything except the public `AnimationGraphComponent` (keep in `Skore`
  beside other components for reflection/serialization parity).
- Add `void RegisterAnimationGraphTypes();` called where `RegisterAnimationTypes()` is called
  (`RegisterGraphicsTypes.cpp:1335`) for graph/node-def resources; register the
  `AnimationGraphComponent` reflection in `SceneRegisterTypes.cpp` (replacing line 137).

### 1.2 Editor — `Editor/Source/Skore/Animation/`
```
ToolsGraph/ ToolsGraph_Definition|Compilation|Variations.*   // resource-backed (AnimationEditorPort §4-5)
ToolsGraph/Nodes/  (descriptor table + per-node compile fns) // §5 registry
NodeRegistry.hpp/.cpp                                        // AnimNodeTypeDesc table
Windows/ AnimGraphWindow.* AnimParametersWindow.* AnimVariationsWindow.* AnimCompileLogWindow.*
ResourceEditor_AnimationGraph.*                              // replaces AnimatorEditor
Handlers/AnimationGraphHandler.cpp                           // .animgraph asset handler (§4.4)
ResourceCompilers/ ResourceCompiler_AnimationGraph.*  ResourceCompiler_AnimationClip.*
RegisterAnimationEditorTypes.cpp                            // new editor registration entry point
```
- Reuse `Editor/Source/Skore/ImGui/GraphEditor.{hpp,cpp}` unchanged (immediate-mode,
  Blueprint+StateMachine visuals, Flow/Value pins, inline pin widgets, returns `GraphEditorResult` diff).
- Add `void RegisterAnimationEditorTypes();` declared/called near `Editor.cpp:1690/1724` (replacing
  `RegisterAnimatorEditorTypes()`). Keep `WorkspaceTypes::Animator=3` + its desc (`Editor.cpp:1652-1656`),
  repurposed to host the new windows.

## 2. New runtime component (`AnimationGraphComponent`) — the seam

Replaces `AnimationPlayer`. `Component + Tickable` (NOT FixedTickable — see §2.3).

### 2.1 Ownership & lifecycle
```cpp
class SK_API AnimationGraphComponent : public Component, public Tickable {
    SK_CLASS(AnimationGraphComponent, Component);
    void OnStart() override;            // resolve Skeleton + Transform (mirror AnimationPlayer::OnStart)
    void OnUpdate(f64 dt) override;     // collapsed two-phase update (§2.3)
    void SetGraph(RID animGraph);       // compiled-definition asset
    static void RegisterType(NativeReflectType<AnimationGraphComponent>&);
private:
    TypedRID<...AnimGraphDefResource> m_graph;
    Anim::GraphInstance*  m_instance = nullptr;    // owns TaskSystem + SampledEventsBuffer (standalone)
    const Anim::GraphDefinition* m_definition = nullptr;
    Skeleton*  m_skeleton  = nullptr;              // child with EntityFlags::HasSkeleton
    Transform* m_transform = nullptr;
    bool       m_applyRootMotion = false;
    Anim::XForm m_rootMotionDelta;
};
```
- `OnStart`: copy `AnimationPlayer::OnStart` — walk children for `EntityFlags::HasSkeleton`, grab
  `Skeleton`, grab entity `Transform`; then resolve active-variation `GraphDefinition` + `new GraphInstance`.
- **Skeleton binding:** build the runtime `Anim::Skeleton` once from the scene `Skeleton` component
  (parent indices + bind TRS, both already on `BoneNode`). Runtime `Anim::Pose` bone-index order MUST
  match `Skeleton::bones` 1:1 so the final write is a straight index copy (no name remap at the seam,
  only in the clip adapter §3).

### 2.2 Writing the final pose to `Skeleton::bones`
```cpp
const Anim::Pose* pose = m_instance->GetPrimaryPose();   // parent-space transforms
for (u32 i = 0; i < m_skeleton->bones.Size(); ++i) {
    const Anim::XForm& x = pose->m_parentSpaceTransforms[i];
    BoneNode& b = m_skeleton->bones[i];
    b.position = x.m_translation;
    b.rotation = x.m_rotation;
    b.scale    = Vec3{x.m_scale, x.m_scale, x.m_scale};  // XForm uses uniform scale
}
m_skeleton->UpdateWorldBones();                          // fires SkeletonUpdated -> SkinnedMeshRenderer
```
Exactly the seam `AnimationPlayer::OnUpdate` used (`RenderComponents.cpp:1136`). Uniform-scale
broadcast is the only impedance match.

### 2.3 Two-phase model → single OnUpdate
Esoterica: `EvaluateGraph(dt)` → move by root motion → `ExecutePrePhysics` → [PHYSICS] →
`ExecutePostPhysics`. Skore's `OnUpdate` runs AFTER physics fixed-step.

**Decision (M1-M8, no ragdoll/IK): collapse to one stage in OnUpdate.** The task system runs everything
in `UpdatePrePhysics` when there's no physics dependency (runtime guide §7.4, §12).
```cpp
void AnimationGraphComponent::OnUpdate(f64 dt) {
    if (!m_instance || !m_skeleton) return;
    PushControlParams();                                       // gameplay -> control params
    auto r = m_instance->EvaluateGraph(dt, WorldXForm(), nullptr/*phys*/, nullptr, false);
    m_rootMotionDelta = r.m_rootMotionDelta;
    if (m_applyRootMotion) ApplyRootMotion(m_rootMotionDelta, dt);  // §2.4
    m_instance->ExecutePrePhysicsPoseTasks(FinalWorldXForm());      // runs ALL tasks (no phys dep)
    m_instance->ExecutePostPhysicsPoseTasks();                      // no-op when no phys dep
    WriteFinalPoseToSkeleton();                                     // §2.2
}
```
- Matches how `AnimationPlayer` already lived entirely in `OnUpdate`. **Do NOT use FixedTickable**:
  animation runs at frame rate; keep `Tickable` for parity.
- **M9 real split:** physics happens before `OnUpdate` in Skore, so the faithful port adds two explicit
  animation passes to `Scene::Update` (`Scene.cpp:228-269`): an animation-pre pass before the
  FixedTickable/physics block and an animation-post pass after `WriteBackTransforms`. Design the
  component now with private `EvaluateAndPrePhysics()` + `PostPhysics()` so M9 can split across two scene
  passes without touching call sites. (Cheaper fallback: accept 1-frame-late ragdoll read-back, all in
  `OnUpdate`.)

### 2.4 Root motion → entity Transform (replacing ApplyRootMotion)
Port `AnimationPlayer::ApplyRootMotion` (`RenderComponents.cpp:935`) driven by the graph's single
`rootMotionDelta` (`Anim::XForm`) instead of per-layer deltas:
```cpp
void ApplyRootMotion(const Anim::XForm& delta, f32 dt) {
    if (!m_transform) return;
    Vec3 worldDelta = m_transform->GetRotation() * delta.m_translation;
    m_transform->SetPosition(m_transform->GetPosition() + worldDelta);
    f32 yaw = Quat::Yaw(delta.m_rotation);
    m_transform->SetRotation(m_transform->GetRotation() * Quat::AngleAxis(yaw, Vec3(0,1,0)));
}
```
- Keep the `CharacterController::SetLinearVelocity` (gravity-Y-preserving) path for velocity-mode root
  motion — that integration is unchanged.
- Axis masking + Transform-vs-Velocity mode are now graph/clip-driven (`RootMotionOverrideNode`).
- We do NOT zero the root bone in the component: the graph/clip already strips root motion from the
  sampled pose.

## 3. Clip adapter — reading the existing `AnimationClipResource`

`SampleTask`/`AnimationClipNode` must produce an `Anim::Pose` from the name-keyed, uncompressed
`AnimationClipResource`. M0 expects a `SyncTrack` + loop-aware `GetRootMotionDelta` on the clip.

### 3.1 Decision: load into an engine-side runtime clip form ONCE (not sample-from-resource per frame)
- Build `Anim::AnimationClip` the first time a clip RID is referenced (cache by RID). Mirrors
  `AnimationPlayer::LoadClipData` (`RenderComponents.cpp:471`) but the result is shared/immutable, owned
  by a clip-cache, not per-component.
- Why: keyframes are already uncompressed (straight reinterpret + reindex); precompute per-bone-INDEX
  channels (resource is keyed by NAME), the SyncTrack, and the root trajectory once; per-frame resource
  reads + name lookups would be wasteful.

### 3.2 Bone-name → skeleton-bone-index mapping
- Runtime `Anim::Skeleton` (from the scene `Skeleton`) owns canonical bone order. Build
  `HashMap<String,u32>` from `Skeleton::bones[i].name` (as `LoadClipData` does, `RenderComponents.cpp:491-498`).
- Each `AnimationChannelResource.Name` → skeleton index; channels for bones absent from the skeleton are
  skipped (current behavior, line 507).
- Read NumFrames keyframes at BufferOffset as `AnimationKeyFrame` — identical to the importer write
  (`FBXImporter.cpp:589-595`) and current read (`RenderComponents.cpp:517-523`).
- Store per-bone-INDEX arrays so `SampleTask` writes the pose by index; bones with no channel keep the
  bind pose.

### 3.3 SyncTrack & root-motion (the missing import data)
- **M0 default sync track:** every clip gets one default event `{id=invalid,start=0,duration=1}` at
  runtime-clip-build time (runtime guide §4.2). Unblocks M0-M4 with zero import changes.
- **Authored data: add an `.animclip` settings sidecar resource (recommended):**
  ```
  AnimClipSettingsResource {
    Clip,             // Reference -> AnimationClipResource
    RootMotionBone,   // String
    ExtractRootMotion,// Bool
    SyncEvents,       // SubObjectList -> AnimSyncEventResource { Id:String, StartTime:Float }
    // future: foot/transition/warp/ragdoll event tracks (runtime guide §11)
  }
  ```
  Clip-cache merges geometry (`AnimationClipResource`) + semantics (`AnimClipSettingsResource`).
  Durations are gaps-to-next, cyclic (runtime guide §4.2). This becomes the clip-event timeline editor
  home (E4).
- **Root-motion delta API** (runtime guide §3.5): precompute uncompressed per-frame root trajectory from
  the RootMotionBone channel; expose loop-aware `GetRootMotionDelta(from,to)` (composes `post*pre` on
  wrap) and `GetRootMotionDeltaNoLooping(from,to)`. `AnimationPlayer::ExtractRootMotionDelta`
  (`RenderComponents.cpp:836`) is a working reference for the segment-A/segment-B loop-wrap math.
- **Build order:** M0 = default-sync + auto-root (root bone = `parentIndex==U32_MAX`, like
  `AnimationPlayer::FindRootBoneIndex` line 823). The `.animclip` sidecar + sync editor is E4, layered on
  without breaking M0.

## 4. `.animgraph` resource schema + handler + workspace wiring

Authoring (tools) resources in the EDITOR module; compiled `GraphDefinition` resource in RUNTIME.

### 4.1 Authoring resources (AnimationEditorPort §4), Skore enum-field style
- `AnimGraphResource{ Name(String), Skeleton(Reference), RootGraph(SubObject->AnimNodeGraphResource),
  ControlParameters(SubObjectList->AnimParameterResource), VirtualParameters(SubObjectList),
  Variations(SubObject->AnimVariationHierarchyResource), PreviewEntity(Reference->EntityResource) }`
- `AnimNodeGraphResource{ Kind(Enum GraphKind{BlendTree,ValueTree,StateMachine,TransitionConduit}),
  Nodes(SubObjectList->AnimNodeResource), Connections(SubObjectList->AnimConnectionResource),
  EntryState(Reference), ViewOffset(Vec2), ViewScale(Float) }`
- `AnimConnectionResource{ FromNode(Reference), FromPin(UInt), ToNode(Reference), ToPin(UInt) }`
- `AnimNodeResource{ TypeId(UInt), Name(String), Position(Vec2), Settings(SubObject),
  DynamicPins(SubObjectList), ChildGraph(SubObject->AnimNodeGraphResource),
  SecondaryGraph(SubObject->AnimNodeGraphResource), VariationData(SubObject->AnimVariationDataResource) }`
- `AnimParameterResource{ Name, Group, Type(Enum ValueType{Bool,Float,Vector,ID,Target,BoneMask}),
  IsVirtual(Bool), PreviewBool, PreviewFloat, PreviewMin, PreviewMax, PreviewVector(Vec3),
  PreviewID(String), ExpectedIDs(SubObjectList), ComputeGraph(SubObject->AnimNodeGraphResource) }`
- `AnimVariationHierarchyResource{ Variations(SubObjectList->AnimVariationResource) }`;
  `AnimVariationResource{ Id(String), Parent(String), Skeleton(Reference) }`;
  `AnimVariationDataResource{ DefaultData(SubObject), Overrides(SubObjectList) }`
- Use `ResourceFieldProperties::NoUI` on cross-link refs (FromNode/ToNode/EntryState), as the Mecanim
  `From`/`To`/`Position` fields did (`RegisterGraphicsTypes.cpp:59-60,72`).

### 4.2 Compiled runtime definition resource (RUNTIME)
Holds compile output (runtime guide §8.1): polymorphic node-def blob (serialized by type), instance
memory offsets/size/alignment, persistent-node indices, root-node index, control/virtual param ID
tables, referenced/external slots, resource LUT. **One compiled definition per variation.** Node-def
array as a `Buffer` via Skore's binary archive writer; clip refs as `ReferenceArray` so resource deps
are tracked.

### 4.3 Node-type registry (editor) — AnimationEditorPort §5
Single `AnimNodeTypeDesc` table keyed by TypeId: pure-data (name, category, header color, `GraphNodeType`
visual, allowed graph kinds, input pins, dynamic-pin support, output type, child-graph kind, settings
TypeID) + two code hooks: `compile(GraphCompilationContext&, RID node)->i16` and optional `drawExtra`.
Editor builds the create-menu/pins/colors from the table; `compile` is the near-verbatim port of
Esoterica's per-node `Compile()`.

### 4.4 Asset handler (replaces AnimationControllerHandler)
New `Handlers/AnimationGraphHandler.cpp` (same shape as `AnimationControllerHandler.cpp`):
```cpp
struct AnimationGraphHandler : ResourceAssetHandler {
    StringView Extension() override { return ".animgraph"; }
    TypeID GetResourceTypeId() override { return TypeInfo<AnimGraphResource>::ID(); }
    StringView GetDesc() override { return "Animation Graph"; }
    void OpenAsset(RID asset) override {
        Editor::GetWorkspaceOfType(WorkspaceTypes::Animator)
            ->GetAnimationGraphEditor().Open(Resources::Read(asset).GetSubObject(ResourceAsset::Object));
    }
    RID Create(UUID, UndoRedoScope* scope) override { /* AnimGraphResource + default RootGraph */ }
};
void RegisterAnimationGraphHandler() {
    ProjectBrowserWindow::AddMenuItem({ .itemName="Create/New Animation Graph", /*...*/
        .userData = TypeInfo<AnimGraphResource>::ID() });
    Reflection::Type<AnimationGraphHandler>();
}
```
Declare+call alongside other handlers in `ResourceAssets.cpp` (replacing the controller decl/call at
3152/3253). Routing is automatic via `handlersByExtension[".animgraph"]`, exactly as `.animcontroller`.

### 4.5 Workspace wiring
Keep `WorkspaceTypes::Animator=3` + desc (`Editor.cpp:1652-1656`). Replace
`EditorWorkspace::m_animatorEditor` (`EditorWorkspace.hpp:54`) + `GetAnimatorEditor()` (`.hpp:22`) with
`m_animationGraphEditor`/`GetAnimationGraphEditor()` of the new `ResourceEditor_AnimationGraph`. Update
the two call sites (handler `OpenAsset` + any window using `GetAnimatorEditor()`).

## 5. Safe removal / migration sequence (engine compiles at every step)

New code is added and proven BEFORE old code is deleted; registrations removed in reverse dependency order.

**Phase A — add new runtime spine (old code untouched, both coexist):**
1. Add `Runtime/Source/Skore/Animation/` core value types + clip adapter + `AnimationGraphComponent`
   (stub `OnUpdate` sampling ONE clip into `Skeleton::bones` — the S0 slice, §6). Reconfigure CMake.
   Register the component reflection in `SceneRegisterTypes.cpp` ALONGSIDE the still-present
   `AnimationPlayer` (add `Reflection::Type<AnimationGraphComponent>();` near line 137; keep 137). Both
   components exist. Build green. First provable milestone.
2. Add the compiled-definition runtime resource + `RegisterAnimationGraphTypes()` (graph/node-def
   resources). Call from `RegisterGraphicsTypes.cpp:1335` ALONGSIDE `RegisterAnimationTypes()`. Build green.

**Phase B — add new editor (old Animator untouched):**
3. Add `Editor/Source/Skore/Animation/` (registry, compiler, windows, `.animgraph` handler,
   `ResourceEditor_AnimationGraph`). Add `RegisterAnimationEditorTypes()` decl/call near
   `Editor.cpp:1690/1724` ALONGSIDE `RegisterAnimatorEditorTypes()`. Add `RegisterAnimationGraphHandler`
   decl/call in `ResourceAssets.cpp` ALONGSIDE the controller one. Add `m_animationGraphEditor`/
   `GetAnimationGraphEditor()` to `EditorWorkspace` ALONGSIDE the Mecanim members. Two editors coexist on
   the Animator workspace. Build green.
4. Bring the new system to usefulness (M1-M7 / E1-E3): author + preview `.animgraph` assets.

**Phase C — remove Mecanim runtime (only after new path is default):**
5. Remove `Reflection::Type<AnimationPlayer>();` (`SceneRegisterTypes.cpp:137`). RISK: scenes that
   serialized an `AnimationPlayer` component fail to resolve its type. Mitigation: keep an empty
   deprecated `AnimationPlayer : Component` stub (no Tickable/fields/behavior) registered for one
   migration window, OR verify the entity loader skips unknown component TypeIDs and accept the drop.
   Recommended: stub for one window, delete later.
6. Delete `AnimationPlayer` + Mecanim runtime structs (`RuntimeAnimLayer/Parameter/Condition/State/
   Transition`, `AnimClipData`, `AnimChannel`) from `RenderComponents.hpp` (lines 112-244) and `.cpp`
   (impl block 295-1168). KEEP `SkinnedMeshRenderer`/`BoneNode`/`Skeleton`/`RendererComponent` in the same
   file. Clean excision (nothing else calls `AnimationPlayer`). Build green.
7. Delete `RegisterAnimationTypes()` body (`RegisterGraphicsTypes.cpp:15-107`) + its call (1335); keep
   `RegisterAnimationGraphTypes()`. Delete Mecanim resource structs/enums from
   `GraphicsResources.hpp:356-489` (AnimationParameterType/Resource, AnimationTransitionCondition/
   ConditionResource/Resource, AnimationStateResource, AnimationLayerBlendMode, RootMotionMode,
   RootMotionAxes, AnimationLayerResource, AnimationAvatarBoneResource, AnimationAvatarResource,
   AnimationControllerResource). KEEP AnimationKeyFrame*, AnimationChannelResource, AnimationClipResource,
   SkinResource, SkinBindResource, DCCAsset. **DECISION:** remove AnimationAvatar* (superseded by BoneMask
   nodes + Variations; only the Mecanim layer used it). Build green.

**Phase D — remove Mecanim editor:**
8. Remove `RegisterAnimatorEditorTypes()` decl/call (`Editor.cpp:1690/1724`),
   `Reflection::Type<AnimatorTreeViewWindow/AnimatorGraphWindow>()` (1720-1721), includes (49-50). Remove
   `RegisterAnimationControllerHandler` decl/call (`ResourceAssets.cpp:3152/3253`). Delete
   `Handlers/AnimationControllerHandler.cpp`, `AnimatorEditor.{hpp,cpp}`, `Window/AnimatorGraphWindow.*`,
   `Window/AnimatorTreeViewWindow.*`. Remove `m_animatorEditor` + `GetAnimatorEditor()` from
   `EditorWorkspace.{hpp,cpp}` (.hpp:22,54) + the include `"AnimatorEditor.hpp"` (.hpp:3).
9. FIX `TypeActions.cpp:125-141` — clip preview currently requires parent == `AnimationControllerResource`
   (reads `PreviewEntity` off it). Repoint to `AnimGraphResource::PreviewEntity` or to the new `.animclip`
   settings asset. This is the non-obvious dependency that breaks once the controller resource is deleted —
   fix in the SAME step as the resource removal.

**Build-breakage risk summary (search-verified):** referencing files are `RegisterGraphicsTypes.cpp`,
`SceneRegisterTypes.cpp`, `RenderComponents.{hpp,cpp}`, `GraphicsResources.hpp`, `AnimatorTreeViewWindow.cpp`,
`TypeActions.cpp`, `AnimationControllerHandler.cpp`, `AnimatorEditor.cpp` — all addressed above.
`SkinnedMeshRenderer` does NOT reference animation (skinning safe). No on-disk `.animcontroller` assets
(no asset migration). Importers + `AnimationClipResource` unchanged.

## 6. Consolidated milestones (runtime M0-M9 + editor E0-E5)

Earliest provable milestone = single clip animating a skinned mesh, BEFORE graph/task complexity.

- **S0 — Vertical slice (M0 partial):** AnimXForm, Pose, engine-side AnimationClip from
  `AnimationClipResource` (§3), default SyncTrack, `AnimationGraphComponent` whose `OnUpdate` samples ONE
  clip into `Skeleton::bones` + `UpdateWorldBones`. No graph/tasks. **PROVES THE SEAM.** (Phase A step 1.)
- **S1 — M0 complete + M1 Blender:** full Pose state, BoneMask, clip root-motion API (port
  `ExtractRootMotionDelta`), all Blender ops. Unit-test sync round-trip + blend goldens (guide §13).
- **S2 — M2 TaskSystem + M3 Graph core:** Task/TaskSystem/PoseBufferPool + Sample/Blend/Reference tasks;
  GraphNode/PoseNode/ValueNode, GraphContext, Definition/Instance placement-new, control params. Component
  drives a hand-built instance.
- **S3 — M4 sync+basic nodes + E0 schema + E1 canvas:** AnimationClipNode, Blend1DNode (synchronized);
  control/const/value nodes. Authoring resources (§4) + node registry + GraphEditor reconciliation;
  create/move/link blend-tree nodes; property panel via reflected Settings.
- **S4 — M5 compile + E2 preview:** GraphCompilationContext + per-node compile; `.animgraph` → compiled
  GraphDefinition; live preview entity in the Animator workspace via PlayMode.
  **TARGET: Blend1D walk↔run on a Float param, foot-synced, in editor preview.**
- **S5 — M6 state machines + E3 params/SM:** StateMachine/State/Transition nodes + conditions; parameters
  panel w/ live preview; SM editing + nav stack + breadcrumbs (states own blend trees, transitions own
  conduits).
- **S6 — M8 layers/advanced + E4 variations/clip-events:** LayerBlendNode, bone-mask nodes, Blend2D,
  selectors, speed-scale, root-motion override, 2-bone IK, warps; variation hierarchy + per-node overrides;
  `.animclip` sidecar + clip-event/sync timeline editor (§3.3). **Mecanim removal (Phase C/D) lands here.**
- **S7 — M9 physics/netcode + E5 debug/record:** IK rig, ragdoll (real pre/post-physics split via new
  `Scene::Update` passes, §2.3 option 1); task serialization; live-debug attach + record/replay.

Removal overlay: Phase A at S0, Phase B at S3, Phase C/D at S6.

## 7. Verification per milestone

- **S0:** Import an FBX (importer already makes `AnimationClipResource`). Scene with skinned mesh + child
  `Skeleton`; add `AnimationGraphComponent`, point at one clip RID, enter Play (`PlayMode::Simulating`).
  Mesh animates+loops; confirm `SkeletonUpdated` still drives `SkinnedMeshRenderer::UpdateBones` (GPU
  deform). Make-or-break seam proof.
- **S1:** Unit tests: sync `GetTime`↔`GetPercentageThrough` round-trip; LCM duration (walk+run→1.1s);
  blend-self=identity; bone-mask 0/1 fast-paths. Visual: root motion moves the Transform (debug trail).
- **S2:** Hand-build Sample→Sample→Blend DAG; assert pose-pool high-water mark does not grow (buffer
  transfer reuse). Drive the component from a hand-built instance; mesh animates.
- **S3/S4:** In the Animator workspace create a `.animgraph`, drop two AnimationClip nodes into a Blend1D,
  wire a Float param, Preview, drag 0→1 → walk↔run with NO foot sliding (sync proof). Verify undo/redo via
  UndoRedoScope; compile-log errors clickable-to-node.
- **S5:** Author idle→walk→run SM with synchronized transitions; preview param-driven transitions; confirm
  outgoing-branch events marked inactive (no double-fire) via debug HUD.
- **S6:** Upper-body additive aim layer w/ bone mask; preview lower-body locomotion + upper-body aim.
  Author a sync-event on the `.animclip` sidecar; confirm phase-lock improves. Confirm old Animator UI gone
  and `.animgraph` is the only path.
- **S7:** Ragdoll blend on death event; live-debug attach highlights active nodes; record+scrub frames
  deterministically.

Port `DebugView_Animation` (active task tree, per-node time/sync event, root-motion ring buffer) from S2
onward, gated behind `SK_DEVELOPMENT_TOOLS`.

## Critical files
- `Runtime/Source/Skore/Scene/Components/RenderComponents.cpp` — seam `UpdateWorldBones` :1201;
  `AnimationPlayer` to remove :295-1168; clip-load ref :471; root-motion ref :836-972
- `Runtime/Source/Skore/Graphics/GraphicsResources.hpp` — KEEP clip resources :204-246; REMOVE Mecanim
  resources :356-489
- `Runtime/Source/Skore/Graphics/RegisterGraphicsTypes.cpp` — `RegisterAnimationTypes` :15-107 remove;
  call :1335; clip schema :1252-1274 keep
- `Editor/Source/Skore/Editor.cpp` — Animator workspace desc :1652-1656; editor registration
  :1690,1720-1724; includes :49-50
- `Editor/Source/Skore/Resource/Handlers/AnimationControllerHandler.cpp` — handler pattern to clone for
  `.animgraph`; `ResourceAssets.cpp` decl/call :3152/3253
