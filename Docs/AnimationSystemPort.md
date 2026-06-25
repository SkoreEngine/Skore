# Porting the Esoterica Animation System to Skore — Implementation Guide

> A step-by-step engineering guide for re-implementing Bobby Anguelov's Esoterica
> animation system (compiled animation graph + deferred pose-task scheduler + node
> editor) inside the **Skore** engine.
>
> Scope: **everything** — runtime graph, task system, state machines, blending,
> IK, motion warping, ragdoll, variations, networking serialization, and the editor.
> Deep code-level detail is provided for the four hardest areas:
> **sync tracks + blending**, **state machine + transitions**, **task system + pose
> pool**, and **graph compilation + serialization**.
>
> Source of truth analysed: `C:\dev\Esoterica\Code\Engine\Animation` (runtime) and
> `C:\dev\Esoterica\Code\EngineTools\Animation` (authoring). Skore target:
> `C:\dev\SkoreEngine\Skore`.

---

## Table of Contents

0. [Mental model & architecture](#0-mental-model--architecture)
1. [Mapping Esoterica → Skore](#1-mapping-esoterica--skore)
2. [Phased roadmap (milestones)](#2-phased-roadmap-milestones)
3. [Core value types you must establish first](#3-core-value-types-you-must-establish-first)
4. [Deep Dive A — Sync tracks & the Blender](#4-deep-dive-a--sync-tracks--the-blender)
5. [The graph runtime — nodes, context, definition, instance](#5-the-graph-runtime)
6. [Deep Dive B — State machine, states & transitions](#6-deep-dive-b--state-machine-states--transitions)
7. [Deep Dive C — The task system & pose pool](#7-deep-dive-c--the-task-system--pose-pool)
8. [Deep Dive D — Graph compilation & serialization](#8-deep-dive-d--graph-compilation--serialization)
9. [The node catalogue to implement](#9-the-node-catalogue-to-implement)
10. [Advanced features](#10-advanced-features)
11. [Editor / authoring on Skore's GraphEditor](#11-editor--authoring)
12. [ECS integration & the per-frame update](#12-ecs-integration--the-per-frame-update)
13. [Testing & validation strategy](#13-testing--validation-strategy)
14. [Gotchas checklist](#14-gotchas-checklist)

---

## 0. Mental model & architecture

Esoterica's animation system is built on three hard separations. Internalising
these before you write any code will save you weeks.

### 0.1 The three separations

**(1) Definition vs Instance.** A graph is compiled once into an immutable,
shareable **`GraphDefinition`** (a flat array of node *Definition* structs + a
precomputed memory layout). At runtime each character gets a **`GraphInstance`**:
a single contiguous memory block where every node's mutable state is
`placement-new`'d at a precomputed offset. The definition is read-only and shared
across thousands of instances; the instance holds only per-character state.

**(2) Graph evaluation vs Pose computation.** Evaluating the graph does **not**
compute poses. It walks the node tree and produces three lightweight things:
- a **task index** (an `int8` into a per-frame task list — the recipe for the pose),
- a **root-motion delta** (a single transform), and
- a **sampled-event range** (a span into a per-frame event buffer).

A second phase — the **TaskSystem** — executes that task DAG to actually sample
clips and blend pose buffers. This split is what enables zero-copy pose-buffer
reuse, pre/post-physics scheduling, multithread-ability, and network serialization.

**(3) Pose vs Value.** Every node is either a **PoseNode** (produces an animation
pose; carries time/duration/sync-track state) or a **ValueNode<T>** (produces a
typed value: `bool`, `float`, `Vec3`, `StringID`, `Target`, or `BoneMask`).
Value nodes cache per-frame; pose nodes are driven top-down from the root.

### 0.2 The per-frame data flow

```
                gameplay sets control params
                          │
   GraphInstance.EvaluateGraph(dt, worldXform, updateRange?)
                          │  walks node tree top-down from root PoseNode
                          ▼
        ┌───────────────────────────────────────────┐
        │  each PoseNode.Update() returns:           │
        │    GraphPoseNodeResult {                   │
        │       int8  taskIdx;            ───────────┼──► registers Tasks into TaskSystem
        │       Transform rootMotionDelta;           │
        │       SampledEventRange events; ───────────┼──► appended to SampledEventsBuffer
        │    }                                       │
        └───────────────────────────────────────────┘
                          │
   move character by rootMotionDelta (gameplay/physics owns this)
                          │
   GraphInstance.ExecutePrePhysicsPoseTasks(finalWorldXform)
                          │   TaskSystem.UpdatePrePhysics()  ── samples + blends pose buffers
                       [ PHYSICS TICK ]                       ── ragdoll bodies simulate
   GraphInstance.ExecutePostPhysicsPoseTasks()
                          │   TaskSystem.UpdatePostPhysics() ── ragdoll get-pose, IK, final pose
                          ▼
              final Pose → SkeletalMesh for skinning
```

Keep this picture in mind: **Evaluate (logic) → move character → Execute (pose)**,
split around physics.

---

## 1. Mapping Esoterica → Skore

You already have: a math/transform lib, a skeleton/pose/clip runtime, a clip import
pipeline, and ECS + reflection/serialization. Here is how Esoterica's primitives
map onto Skore. (Verified against `Skore/Runtime/Source/Skore/Core` and `/Scene`.)

| Esoterica (`EE::`) | Skore (`Skore::`) | Notes |
|---|---|---|
| `Transform` (value: quat+trans+scale) | **New: `Anim::XForm`** | ⚠️ Skore's `Transform` is an ECS *Component*. Do **not** reuse it per-bone. Define a tiny value type (see §3.1). |
| `Quaternion` | `Quat` | Has `Quat::Slerp`. You must add a `FastSlerp` and `Quat::Delta` (inverse-compose). |
| `Vector` / `Float3` | `Vec3` / `Vec4` | `Vec3::Lerp`, `Vec3::Normalize` exist. |
| `Matrix` | `Mat4` | `Mat4::Inverse`, decompose helpers exist. |
| `StringID` | Use Skore's interned string id (or add one) | Needed everywhere for event/param IDs. Must be cheap to compare + hashable. |
| `Percentage` | add a thin `Percentage` wrapper over `f32` | Carries loop-count helpers — see §4. |
| `IReflectedType` + `BinaryInputArchive` | `Object` + `BinaryArchiveReader/Writer` + `Reflection::Type<T>()` | Drives node-definition (de)serialization. |
| `Resource::ResourcePtr` / `ResourceID` | `RID` + `Resources::` | For clip/skeleton/graph references. |
| Resource compiler / loader | Skore resource compiler + runtime loader | For the graph asset & clip asset. |
| `EntitySystem` / `WorldSystem` | `Component` + `Tickable`/`FixedTickable` | See §12. |
| Graph editor (ImGui node-graph) | **`Skore::GraphEditor`** (already exists!) | `Editor/Source/Skore/ImGui/GraphEditor.hpp` already supports Blueprint + StateMachine node types. Reuse it. |
| Jolt-backed physics | `Skore::PhysicsScene`, `RigidBody`, Jolt | For ragdoll nodes (last milestone). |

**Where the code lives.** Create:
- Runtime: `Skore/Runtime/Source/Skore/Animation/` (mirror Esoterica's
  `Engine/Animation` layout: `/Graph`, `/Graph/Nodes`, `/TaskSystem`, `/IK`).
- Editor: `Skore/Editor/Source/Skore/Animation/` (the tools-graph + compiler + editor).

**Namespace.** `namespace Skore::Anim { ... }` keeps it isolated and short.

---

## 2. Phased roadmap (milestones)

Build it bottom-up. Each milestone is independently testable. **Do not** skip
ahead — every later layer assumes the earlier one is rock-solid.

| Milestone | Delivers | Depends on | Risk |
|---|---|---|---|
| **M0 — Foundations** | `XForm`, `Percentage`, `Pose` w/ state, `BoneMask`, clip sampling returns `Pose`, sync track baked onto clips, root-motion delta extraction | your existing clip runtime | low |
| **M1 — Blender** | all blend ops (parent-space, masked, additive, to/from ref, model-space, root-motion) | M0 | **high** (get it right!) |
| **M2 — Task system** | `Task`, `TaskSystem`, `PoseBufferPool`, Sample/Blend/Reference tasks, deferred DAG execution | M0, M1 | high |
| **M3 — Graph core** | `GraphNode`/`PoseNode`/`ValueNode`, `GraphContext`, `GraphDefinition`, `GraphInstance`, placement-new instantiation, control params | M2 | high |
| **M4 — Sync & basic nodes** | `AnimationClipNode`, `Blend1DNode` w/ synchronized update, value/const/parameter nodes, float/bool math | M3, §4 | **high** (sync) |
| **M5 — Compilation + asset** | tools-graph data model, `GraphCompilationContext`, node `Compile()`, resource loader, runtime instantiation from compiled asset | M3 | medium |
| **M6 — State machines** | `StateMachineNode`, `StateNode`, `TransitionNode`, state/transition conditions, sampled-event branch marking | M4 | high |
| **M7 — Editor** | flow-graph + state-machine authoring on Skore's `GraphEditor`, live preview, control-param panel | M5 | medium |
| **M8 — Layers & advanced** | `LayerBlendNode`, bone-mask nodes, `Blend2DNode`, selectors, speed-scale, root-motion override, two-bone IK, motion warps | M6 | medium |
| **M9 — Physics & netcode** | IK rig, powered/simulated ragdoll (pre/post-physics), task serialization for replication, graph state recording | M8 | medium |

A useful "vertical slice" target after **M4**: a character that plays a single
clip and blends a walk↔run on a float parameter, with correct foot-locked sync.
That proves the entire spine (graph → task → pose) works.

---

## 3. Core value types you must establish first

You have a clip runtime, so most of this exists. Confirm/adapt the following —
they are load-bearing for everything else.

### 3.1 `XForm` — the per-bone transform value type

⚠️ **Critical adaptation.** Esoterica's `Transform` is a value type (rotation
quaternion + translation + uniform/non-uniform scale) used for every bone of every
pose. Skore's `Transform` is an ECS component — wrong tool. Define:

```cpp
namespace Skore::Anim
{
    // Quat-Translation-Scale. Packed for cache friendliness.
    struct XForm
    {
        Quat m_rotation = Quat::Identity();
        Vec3 m_translation = {0,0,0};
        f32  m_scale = 1.0f;            // Esoterica uses uniform scale per-bone

        static XForm Identity() { return {}; }

        // compose: this then parent  (childGlobal = childLocal * parentGlobal)
        XForm operator*(const XForm& parent) const;
        XForm GetInverse() const;
    };
}
```

> Esoterica uses **uniform** scale per bone (a single float, stored in the W of the
> translation vector as an optimization). Keep scale uniform unless you have a
> hard requirement — non-uniform scale complicates blending and skinning.

Convention used throughout Esoterica (match it exactly): a bone's **parent-space**
(a.k.a. local) transform composed with its parent's **model-space** (a.k.a. global)
transform yields its model-space transform: `global = local * parentGlobal`.

### 3.2 `Pose` — with a state machine

A `Pose` stores **parent-space** transforms (one `XForm` per bone) and a
lazily-computed **model-space** cache. It tracks a *state* enum that the blender
uses for fast-paths and additive detection:

```cpp
class Pose
{
public:
    enum class Type  { None, ReferencePose, ZeroPose };
    enum class State { Unset, Pose, ReferencePose, ZeroPose, AdditivePose };

    const Skeleton* m_pSkeleton;
    Vector<XForm>   m_parentSpaceTransforms;   // local/parent-space, ALWAYS valid
    Vector<XForm>   m_modelSpaceTransforms;    // global cache, may be empty (stale)
    State           m_state = State::Unset;

    bool IsPoseSet()      const { return m_state != State::Unset; }
    bool IsAdditivePose() const { return m_state == State::AdditivePose; }
    bool IsZeroPose()     const { return m_state == State::ZeroPose; }

    void Reset(Type t, bool calcModelSpace=false);   // set to ref/zero pose
    void CopyFrom(const Pose* rhs);
    void ClearModelSpaceTransforms();                // call after ANY local edit
    void CalculateModelSpaceTransforms();            // walk hierarchy, fill cache
    int  GetNumBones(Skeleton::LOD lod) const;
};
```

**Rules to enforce (the blender depends on them):**
- After mutating any parent-space transform, **clear the model-space cache**.
- A *zero pose* has all rotations = identity, translations = 0 (the additive
  identity). A *reference pose* is the skeleton's bind pose.
- `State::AdditivePose` is *sticky through blends*: blending two additive poses
  yields additive; blending additive with normal yields normal.

### 3.3 `Skeleton` — LOD + parent indices + named bone masks

Confirm your skeleton exposes:
- `Vector<int32> m_parentBoneIndices` (parent index per bone; root = -1/InvalidIndex).
- `Vector<XForm> m_parentSpaceReferencePose` and a cached model-space ref pose.
- A **two-level LOD**: a `m_numBonesToSampleAtLowLOD` threshold. `GetNumBones(LOD)`
  returns the full count at High and the threshold at Low. Bones must be ordered
  parent-before-child so a single linear pass computes globals.
- Optional: embedded **named bone masks** (`StringID → BoneMask`) for the
  bone-mask nodes (M8).

### 3.4 The `SyncTrack` baked onto each clip

This is the spine of synchronized blending. Build it at **import time** from the
clip's sync-event track (or fall back to a single default event). Full algorithm
in §4. Each `AnimationClip` stores one `SyncTrack`.

### 3.5 Root motion on the clip

Your clip must expose:
```cpp
XForm GetRootMotionDelta(Percentage fromTime, Percentage toTime) const;          // loop-aware
XForm GetRootMotionDeltaNoLooping(Percentage fromTime, Percentage toTime) const; // raw
```
Store the **uncompressed** per-frame root trajectory (precision matters for
locomotion) plus precomputed average linear/angular velocity. Loop-aware delta:
if `fromTime > toTime` the clip wrapped, so compose `delta(from→1) * delta(0→to)`
— **order matters** (see §4.9).

---

## 4. Deep Dive A — Sync tracks & the Blender

> This is the single hardest part of the system and the one that, done wrong,
> produces foot-sliding, popping, and desynced blends. Implement it in M0–M1 and
> unit-test it before touching the graph.

### 4.1 Why sync tracks exist

Two locomotion clips (walk, run) have different durations and different foot-plant
timings. Blending them by raw time produces foot-sliding. Instead, each clip is
annotated with **sync events** ("LeftDown", "RightDown", …) that mark *semantic
phases*. Blending is done in **event space** so both clips advance through
"LeftDown → RightDown" together regardless of their real durations. This is
*phase-locking*.

### 4.2 Data model

```cpp
struct SyncTrackTime          // a position within a sync track
{
    int32      m_eventIdx = 0;            // which event (0..N-1), can exceed N when looping
    Percentage m_percentageThrough = 0;  // [0,1) through THAT event
    float ToFloat() const { return m_eventIdx + m_percentageThrough; }
};

struct SyncTrackTimeRange { SyncTrackTime m_startTime, m_endTime; };

class SyncTrack
{
    struct Event { StringID m_ID; Percentage m_startTime; Percentage m_duration; };
    InlineVector<Event,10> m_syncEvents;
    int32                  m_startEventOffset = 0;   // logical "first event" shift
};
```

`Percentage` is a float wrapper that knows how to split into a loop count and a
normalized `[0,1)` fraction:
```cpp
void Percentage::GetLoopCountAndNormalizedTime(int32& outLoops, Percentage& outNorm) const;
```

**Default track.** A clip with no authored sync events gets a single event
`{id=invalid, start=0, duration=1}`. The whole clip is one phase — so unsynced
clips still flow through the same machinery.

**Durations are gaps-to-next, cyclic.** When building from markers:
```cpp
for (i in 0 .. N-2) events[i].duration = events[i+1].startTime - events[i].startTime;
events[N-1].duration = 1.0f - (events[N-1].startTime - events[0].startTime); // wraps to first
```
The last event's duration *wraps around* the loop point — this is what makes
looping seamless.

### 4.3 Time conversions (exact formulas)

**Percentage-through-clip → SyncTrackTime** (`GetTime`): normalize off the loop
count, then find the event whose `[start, start+duration]` contains the position:
```
for the normalized p in [0,1):
   find event e with (events[e].start + events[e].duration) >= p   // first one
   result.eventIdx          = e
   result.percentageThrough = (p - events[e].start) / events[e].duration
   result.eventIdx += loopCount * N
   result.eventIdx = clampToTrack(result.eventIdx - (withOffset ? startEventOffset : 0))
```
There's a special "before the first event's start" case (only with offset/looping)
that maps you into the *last* event of the previous loop.

**SyncTrackTime → percentage-through-clip** (`GetPercentageThrough`), the inverse:
```
e = clampToTrack(time.eventIdx + (withOffset ? startEventOffset : 0))
p = events[e].startTime + events[e].duration * time.percentageThrough
while (p > 1) p -= 1;   // wrap
```

**Advance by delta** (`UpdateEventTime`): convert start→percentage, add delta,
convert back. This is how unsynchronized root nodes advance.

`clampToTrack` is a modulo that handles negatives:
```cpp
int clampToTrack(int i) const { int n=GetNumEvents(); int c=i%n; return c<0 ? c+n : c; }
```

### 4.4 Blended sync track + LCM duration scaling (the crown jewel)

When two pose nodes blend, their two sync tracks are merged into one via the
**least common multiple** of their event counts, then durations are blended:

```cpp
SyncTrack::SyncTrack(const SyncTrack& t0, const SyncTrack& t1, float blendWeight)
{
    int n0 = t0.GetNumEvents(), n1 = t1.GetNumEvents();
    int LCM = LowestCommonMultiple(n0, n1);              // e.g. LCM(4,2)=4
    float scale0 = float(n0)/LCM, scale1 = float(n1)/LCM;

    Percentage start = 0;
    for (int i=0; i<LCM; ++i) {
        const auto& e0 = t0.GetEvent(i);                 // GetEvent applies startEventOffset
        const auto& e1 = t1.GetEvent(i);
        float d0 = e0.m_duration * scale0;
        float d1 = e1.m_duration * scale1;
        float d  = Lerp(d0, d1, blendWeight);
        m_syncEvents.push_back({ blendWeight > 0.5f ? e1.m_ID : e0.m_ID, start, d });
        start += d;
    }
    // normalize back to [0,1]
    float inv = 1.0f / start;
    for (auto& e : m_syncEvents) { e.m_startTime *= inv; e.m_duration *= inv; }
    m_syncEvents.back().m_duration = 1.0f - m_syncEvents.back().m_startTime;
}
```

And the **blended duration** (how long the blended clip plays this frame):
```cpp
static float CalculateDurationSynchronized(float dur0, float dur1,
        int n0, int n1, int LCM, float w)
{
    float sd0 = dur0 * (float(LCM)/n0);
    float sd1 = dur1 * (float(LCM)/n1);
    return Lerp(sd0, sd1, w);
}
```
Worked example: walk(4 events, 1.0s) blended 50% with run(2 events, 0.6s):
`LCM=4`, `sd0=1.0`, `sd1=1.2`, duration = `lerp(1.0,1.2,.5)=1.1s`. Both clips
advance through their (scaled) events at the same rate → no foot sliding.

### 4.5 The synchronized update (how phase-lock propagates)

A `PoseNode::Update` takes an **optional** `SyncTrackTimeRange*`:
- **`nullptr`** → *unsynchronized*: advance by `deltaTime / duration` and compute
  the node's own range.
- **non-null** → *synchronized*: the parent dictates the event-space range; the
  node maps it to its own clip time via `GetPercentageThrough`.

A blend node computes **one** range and passes the **same pointer to both
children**, guaranteeing phase-lock:

```cpp
GraphPoseNodeResult Blend1DNode::Update(GraphContext& ctx, const SyncTrackTimeRange* pRange)
{
    MarkNodeActive(ctx);
    EvaluateBlendSpace(ctx);                 // pick source0/source1 + blendWeight from param

    SyncTrackTimeRange range;
    if (pRange) {
        range = *pRange;                     // we are synchronized by our parent
    } else {                                  // we are a root: derive from dt
        Percentage dPct = (m_duration>0) ? Percentage(ctx.m_deltaTime/m_duration) : 0;
        range.m_startTime = m_blendedSyncTrack.GetTime(m_currentTime);
        range.m_endTime   = m_blendedSyncTrack.GetTime(Percentage::Clamp(m_currentTime+dPct, allowLooping));
    }

    // *** both children get the SAME range — never mutate between them ***
    GraphPoseNodeResult r0 = m_pSource0->Update(ctx, &range);
    GraphPoseNodeResult r1 = m_pSource1->Update(ctx, &range);

    GraphPoseNodeResult result;
    if (r0.HasTasks() && r1.HasTasks()) {
        result.m_taskIdx        = ctx.m_pTaskSystem->RegisterTask<BlendTask>(GetNodeIndex(),
                                       r0.m_taskIdx, r1.m_taskIdx, m_blendWeight);
        result.m_rootMotionDelta = Blender::BlendRootMotionDeltas(r0.m_rootMotionDelta,
                                       r1.m_rootMotionDelta, m_blendWeight);
    } else {
        result = r0.HasTasks() ? r0 : r1;
    }
    result.m_sampledEventRange = ctx.m_pSampledEventsBuffer->BlendEventRanges(
                                     r0.m_sampledEventRange, r1.m_sampledEventRange, m_blendWeight);

    m_previousTime = GetSyncTrack().GetPercentageThrough(range.m_startTime);
    m_currentTime  = GetSyncTrack().GetPercentageThrough(range.m_endTime);
    return result;
}
```

The leaf `AnimationClipNode` turns the event-space range back into clip time:
```cpp
if (pRange) {                                  // synchronized
    m_previousTime = GetSyncTrack().GetPercentageThrough(pRange->m_startTime);
    m_currentTime  = GetSyncTrack().GetPercentageThrough(pRange->m_endTime);
} else {                                        // unsynchronized: advance by dt
    Percentage d = Percentage(ctx.m_deltaTime / m_duration);
    m_previousTime = m_currentTime;
    m_currentTime  = allowLooping ? (m_currentTime + d).Wrapped(&m_loopCount)
                                  : (m_currentTime + d).Clamped();
}
// then: register a SampleTask, sample root-motion delta, sample events for [prev,cur]
```

> **Note:** "play in reverse" and time-sync are mutually exclusive — Esoterica
> logs a warning. When synchronized, reverse has no meaning.

### 4.6 The Blender — core blend functions

Two function objects parameterize all blends:

```cpp
struct BlendFunction {                                  // normal interpolation
    static Quat Rotation(const Quat& a, const Quat& b, float t){ return Quat::FastSlerp(a,b,t); }
    static Vec4 TransScale(const Vec4& a, const Vec4& b, float t){ return Vec4::Lerp(a,b,t); }
};
struct AdditiveBlendFunction {                          // a + t*(delta)
    static Quat Rotation(const Quat& a, const Quat& b, float t){
        Quat target = b * a;                            // apply b as a delta on top of a
        return Quat::Slerp(a, target, t);
    }
    static Vec4 TransScale(const Vec4& a, const Vec4& b, float t){
        return a + b * t;                               // additive offset
    }
};
```

### 4.7 Parent-space blend (the workhorse)

```cpp
template<class F>
void Blender::ParentSpaceBlend(Skeleton::LOD lod, const Pose* src, const Pose* tgt,
                               float w, const BoneMask* mask, Pose* out, bool layered=false)
{
    Pose::State finalState = (src->IsAdditivePose() && tgt->IsAdditivePose())
                           ? Pose::State::AdditivePose : Pose::State::Pose;

    if (w == 0.0f) { if (src!=out) out->CopyFrom(src); }                 // fast path
    else if (!layered && !mask && w == 1.0f) { if (tgt!=out) out->CopyFrom(tgt); }
    else {
        int n = out->GetNumBones(lod);
        for (int b=0; b<n; ++b) {
            float bw = mask ? w * mask->GetWeight(b) : w;               // per-bone weight
            if (bw == 0.0f)      { out->SetTransform(b, src->GetTransform(b)); continue; }
            if (!layered && bw == 1.0f) { out->SetTransform(b, tgt->GetTransform(b)); continue; }
            const XForm& s = src->m_parentSpaceTransforms[b];
            const XForm& t = tgt->m_parentSpaceTransforms[b];
            out->SetRotation(b,   F::Rotation(s.m_rotation,   t.m_rotation,   bw));
            out->SetTransScale(b, F::TransScale(s.TransScale(), t.TransScale(), bw));
        }
        out->ClearModelSpaceTransforms();
    }
    out->m_state = finalState;
}
```
`AdditiveBlend(...)` = the same with `F = AdditiveBlendFunction` and `layered=true`.
The **bone mask multiplies** the global weight: `bw = w * mask.weight[b]`. Mask 0 ⇒
source, mask 1 ⇒ full blend. This is the basis of partial-body animation.

### 4.8 Reference-pose & model-space (overlay) blends

- `ParentSpaceBlendToReferencePose(src, w, mask, out)`: blend `src → bind pose`.
  `w==1` ⇒ `out.Reset(additive ? ZeroPose : ReferencePose)`.
- `ParentSpaceBlendFromReferencePose(tgt, w, mask, out)`: blend `bind pose → tgt`.
- `ApplyAdditiveToReferencePose(additive, w, mask, out)`: bake an additive pose
  onto the bind pose to produce a normal pose. Used to "flatten" the final pose if
  the graph's output is additive (see §7.4).
- `ModelSpaceBlend(base, layer, w, mask, out)` (a.k.a. *Global blend*): the most
  expensive. It converts both poses' rotations to **model space**, blends rotations
  there (so an overlay "feels" world-correct), converts back to local, then does a
  final local-space blend with the layer weight. **Requires a bone mask.** Used by
  the layer system's "ModelSpace" blend mode for aim/look overlays.

### 4.9 Root-motion blend & looping

```cpp
XForm Blender::BlendRootMotionDeltas(const XForm& src, const XForm& tgt, float w,
                                     RootMotionBlendMode mode = Blend)
{
    if (mode == IgnoreTarget) return src;
    if (mode == IgnoreSource) return tgt;
    if (w <= 0) return src;
    if (w >= 1) return tgt;
    // mode == Blend:    slerp rotation, lerp translation
    // mode == Additive: AdditiveBlendFunction on rotation + translation
    ...
}
```
Loop-aware delta extraction in the clip (when `prev > cur`, you wrapped):
```cpp
XForm pre  = clip->GetRootMotionDeltaNoLooping(prev, 1.0f);
XForm post = clip->GetRootMotionDeltaNoLooping(0.0f, cur);
delta = post * pre;     // ORDER MATTERS: move by pre, then post
```

### 4.10 Sync/Blend gotchas (memorize)

1. Pass the **identical** `SyncTrackTimeRange` to all synchronized children —
   never mutate it as it descends.
2. **Clear the model-space cache** after every parent-space mutation.
3. Additive result state is `Additive` only if **both** inputs are additive.
4. Bone-mask weight is **multiplicative** with the global weight (0 global ⇒ 0
   regardless of mask).
5. Snap `w` within epsilon of 1.0 to exactly 1.0 so fast-paths trigger
   (Esoterica does this in the `BlendTask` constructor).
6. Loop wrap composes root-motion deltas in `post * pre` order.
7. Single-frame clips: force `prev = cur = 1.0` (no interpolation).
8. Reverse playback inverts the event-query range `[1-cur, 1-prev]`.

---

## 5. The graph runtime

(Nodes, context, definition, instance — milestone M3. Deep compile/serialize detail
is in §8.)

### 5.1 Node base classes

```cpp
class GraphNode
{
public:
    struct Definition : public Object        // immutable, shared, serialized
    {
        int16 m_nodeIdx = InvalidIndex;      // index in the flat definition array
        virtual void InstantiateNode(const InstantiationContext& ctx, InstantiationOptions o) const = 0;
        virtual void Load(BinaryArchiveReader&);   // via EE_SERIALIZE_GRAPHNODEDEFINITION macro
        virtual void Save(BinaryArchiveWriter&) const;
    protected:
        template<class T> T* CreateNode(const InstantiationContext& ctx, InstantiationOptions o) const;
    };

    virtual GraphValueType GetValueType() const = 0;
    virtual void  Initialize(GraphContext&);
    void          Shutdown(GraphContext&);
    bool          IsNodeActive(uint32 updateID) const { return m_lastUpdateID == updateID; }
    void          MarkNodeActive(GraphContext&);
private:
    const Definition* m_pDefinition = nullptr;
    uint32 m_lastUpdateID = ~0u;
    int32  m_initializationCount = 0;
};

class PoseNode : public GraphNode            // produces a pose
{
public:
    virtual const SyncTrack& GetSyncTrack() const = 0;
    int32      GetLoopCount()  const { return m_loopCount; }
    Percentage GetCurrentTime() const { return m_currentTime; }
    Seconds    GetDuration()   const { return m_duration; }
    virtual GraphPoseNodeResult Update(GraphContext&, const SyncTrackTimeRange* = nullptr) = 0;
protected:
    int32 m_loopCount=0; Seconds m_duration=0; Percentage m_currentTime=0, m_previousTime=0;
};

template<class T> class ValueNode : public GraphNode  // bool/float/Vec3/StringID/Target/BoneMask
{
public:
    T    GetValue(GraphContext& c){ T v; GetValueInternal(c,&v); return v; }
    void SetValue(const T& v){ SetValueInternal(&v); }
protected:
    virtual void GetValueInternal(GraphContext&, void*) = 0;
    virtual void SetValueInternal(const void*) {}
};
```

`GraphPoseNodeResult` is the lightweight thing pose nodes return:
```cpp
struct GraphPoseNodeResult {
    int8              m_taskIdx = InvalidIndex;       // recipe into the TaskSystem
    XForm             m_rootMotionDelta = XForm::Identity();
    SampledEventRange m_sampledEventRange;            // [start,end) into the event buffer
    bool HasTasks() const { return m_taskIdx != InvalidIndex; }
};
```

`GraphValueType` enum: `{ Unknown, Bool, ID, Float, Vector, Target, BoneMask, Pose, Special }`.

### 5.2 GraphContext (per-frame state)

```cpp
struct GraphContext
{
    uint64        m_graphUserID;            // owning entity id
    const Skeleton* m_pSkeleton;
    uint32        m_updateID;               // increments each evaluate; powers value-node caching
    float         m_deltaTime;
    XForm         m_worldTransform, m_worldTransformInverse;
    PhysicsScene* m_pPhysicsScene = nullptr;

    TaskSystem*           m_pTaskSystem;
    SampledEventsBuffer*  m_pSampledEventsBuffer;
    BoneMaskPool*         m_pBoneMaskPool;
    GraphLayerContext*    m_pLayerContext = nullptr;   // non-null only inside a layer
    const Pose*           m_pPreviousPose = nullptr;

    SampledEventRange GetEmptySampledEventRange() const;
};
```
The `m_updateID` is the trick that lets a value node be referenced by N parents but
evaluate **once per frame**: `if (!WasUpdated(ctx)) { recompute; MarkNodeActive(ctx); }`.

### 5.3 Definition & Instance

See §8 for the full compile/instantiate/serialize treatment. The essentials:
- **`GraphDefinition`**: flat `Vector<GraphNode::Definition*>`, per-node memory
  offsets/size/alignment, persistent-node indices, root-node index, control- &
  virtual-parameter ID tables, referenced/external graph slots, resource table.
- **`GraphInstance`**: one big aligned allocation; every node `placement-new`'d at
  its offset; pointer wiring done by index; owns (if standalone) the `TaskSystem`,
  `SampledEventsBuffer`, `RootMotionDebugger`.

### 5.4 Control parameters (the gameplay API)

Control parameters are just the first *N* nodes (a `ControlParameterXNode` per
parameter). Gameplay drives the graph by setting them before `EvaluateGraph`:

```cpp
int16 idx = instance->GetControlParameterIndex("Speed"_id);
instance->SetControlParameterValue<float>(idx, characterSpeed);
```
`SetControlParameterValue<bool>` calls `DirectlySetValue` on the node. **Virtual
parameters** are read-only nodes that compute a value from graph logic on demand.

A typed `GraphController` wrapper makes this safe and cheap (bind once by
name+type, then set):
```cpp
template<class T> struct ControlParam {
    int16 m_idx = InvalidIndex;
    void TryBind(GraphInstance* g, StringID id){ if (g->IsControlParameter(id, ValueTypeOf<T>)) m_idx = g->GetControlParameterIndex(id); }
    void Set(GraphInstance* g, const T& v){ if (m_idx!=InvalidIndex) g->SetControlParameterValue<T>(m_idx, v); }
};
```

---

## 6. Deep Dive B — State machine, states & transitions

> Milestone M6. The state machine is itself a `PoseNode`, so it composes with the
> rest of the graph: you can blend a state machine against a clip, put one inside
> a layer, etc.

### 6.1 Data model

```cpp
struct StateMachineNode::Definition : PoseNode::Definition
{
    using StateIndex = int16;

    struct TransitionDefinition {
        StateIndex m_targetStateIdx   = InvalidIndex;
        int16      m_conditionNodeIdx = InvalidIndex;   // BoolValueNode
        int16      m_transitionNodeIdx= InvalidIndex;   // TransitionNode (does the blend)
        bool       m_canBeForced      = false;          // may interrupt a transitioning-in state
    };
    struct StateDefinition {
        int16 m_stateNodeIdx          = InvalidIndex;   // StateNode to run when active
        int16 m_entryConditionNodeIdx = InvalidIndex;   // optional: pick among entry candidates
        InlineVector<TransitionDefinition,5> m_transitions;
    };

    InlineVector<StateDefinition,5> m_stateDefinitions;
    StateIndex                      m_defaultStateIdx = InvalidIndex;
};
```

Runtime the node keeps parallel arrays of instantiated `StateNode*`,
`BoolValueNode*` (conditions), and `TransitionNode*` pointers, plus:
```cpp
StateIndex      m_activeStateIdx;
TransitionNode* m_pActiveTransition = nullptr;   // non-null while blending between states
```

> A **conduit** in the editor is just the visual container for the transition's
> condition value-tree; at runtime it compiles down to the
> `m_conditionNodeIdx` + `m_transitionNodeIdx` pair above.

### 6.2 The update loop

```cpp
GraphPoseNodeResult StateMachineNode::Update(GraphContext& ctx, const SyncTrackTimeRange* pRange)
{
    MarkNodeActive(ctx);

    // 1) Evaluate transitions out of the current state (only if not already mid-transition,
    //    or if a forceable transition is allowed to interrupt).
    SelectTransition(ctx);

    // 2) Update the active branch.
    GraphPoseNodeResult result;
    if (m_pActiveTransition != nullptr) {
        result = m_pActiveTransition->Update(ctx, pRange);   // blends source->target states
        if (m_pActiveTransition->IsComplete(ctx)) {          // target fully in
            m_activeStateIdx   = m_transitionTargetStateIdx;
            m_pActiveTransition = nullptr;
        }
    } else {
        result = m_states[m_activeStateIdx]->Update(ctx, pRange);
    }

    // 3) Surface the active state's sync track / time onto the SM node itself.
    m_duration    = ActiveChild()->GetDuration();
    m_currentTime = ActiveChild()->GetCurrentTime();
    return result;
}
```

`SelectTransition` walks the active state's `m_transitions`, evaluates each
`m_conditionNodeIdx` (a `BoolValueNode`) in order, and on the first that returns
true, starts that transition (initializing the `TransitionNode` with source =
current branch, target = new state). The `m_canBeForced` flag lets a transition
fire **while another transition is still blending in** (chained dodges/attacks).

**Initial state.** On `Initialize`, evaluate each state's `m_entryConditionNodeIdx`
to pick among candidate entry states (or use `m_defaultStateIdx`). This is how
"enter the state machine already in the right state for the current params" works.

### 6.3 The StateNode

A `StateNode` wraps a child pose node and adds per-state bookkeeping:
```cpp
struct StateNode::Definition : PoseNode::Definition {
    int16 m_childNodeIdx;
    int16 m_layerWeightNodeIdx     = InvalidIndex;  // optional float
    int16 m_layerRootMotionWeightNodeIdx = InvalidIndex;
    int16 m_layerBoneMaskNodeIdx   = InvalidIndex;
    // timed/frame events to emit while in state:
    Vector<TimedEvent> m_entryEvents, m_executeEvents, m_exitEvents;
    StringID m_runtimeFootEventBitFlags;
};
```
On entry/while-active/on-exit it:
- tracks **elapsed time in state** and **loop count** (consumed by state conditions),
- emits **graph events**: `Entry` (first update), `FullyInState` (once no longer
  transitioning), `Exit` (when a transition out begins),
- samples its **timed events** and forwards the child's **sampled animation events**,
- exposes **layer weight / root-motion weight / bone mask** for when the state is
  used inside a layer.

### 6.4 The TransitionNode (source→target blend)

This is where sync-aware blending earns its keep. Key options in the definition:
```cpp
struct TransitionNode::Definition : PoseNode::Definition {
    int16 m_targetStateNodeIdx;
    int16 m_durationOverrideNodeIdx = InvalidIndex;   // float, else fixed
    Seconds m_duration = 0.2f;
    enum class TimeMatchMode { None, Synchronized, MatchSourceTime,
                               MatchSyncEventIndex, MatchSyncEventID,
                               MatchSyncEventPercentage, ... };
    TimeMatchMode m_timeMatchMode;
    EasingFunc    m_blendWeightEasing;                 // ease the 0→1 weight
    int16 m_boneMaskNodeIdx = InvalidIndex;            // optional masked transition
    bool  m_canBeForced;
};
```

Update outline:
```cpp
GraphPoseNodeResult TransitionNode::Update(GraphContext& ctx, const SyncTrackTimeRange* pRange)
{
    MarkNodeActive(ctx);

    // advance transition clock → blendWeight in [0,1] via easing
    m_transitionProgress += ctx.m_deltaTime / m_duration;
    float w = Ease(m_blendWeightEasing, Clamp01(m_transitionProgress));

    // Decide how source & target advance in time:
    GraphPoseNodeResult srcResult, tgtResult;
    if (m_timeMatchMode == Synchronized) {
        // build a blended sync track and drive BOTH with one SyncTrackTimeRange
        SyncTrackTimeRange range = ComputeBlendedRange(ctx);
        srcResult = m_pSourceNode->Update(ctx, &range);
        tgtResult = m_pTargetNode->Update(ctx, &range);
    } else {
        // unsynchronized: target may be time-offset to match a source sync event
        srcResult = m_pSourceNode->Update(ctx, nullptr);
        tgtResult = m_pTargetNode->Update(ctx, nullptr);
    }

    // Blend pose (register a BlendTask), root motion, and events
    GraphPoseNodeResult r;
    r.m_taskIdx = ctx.m_pTaskSystem->RegisterTask<BlendTask>(GetNodeIndex(),
                      srcResult.m_taskIdx, tgtResult.m_taskIdx, w, BoneMaskList(ctx));
    r.m_rootMotionDelta = Blender::BlendRootMotionDeltas(srcResult.m_rootMotionDelta,
                                                         tgtResult.m_rootMotionDelta, w);
    r.m_sampledEventRange = ctx.m_pSampledEventsBuffer->BlendEventRanges(
                                srcResult.m_sampledEventRange, tgtResult.m_sampledEventRange, w);

    // Mark the outgoing (source) events as "inactive branch" so gameplay can ignore them.
    ctx.m_pSampledEventsBuffer->MarkEvents(srcResult.m_sampledEventRange, /*isActiveBranch=*/false);
    return r;
}
```

**Time-match modes** decide how the target clip is positioned at transition start:
- `Synchronized` — full phase-lock for the whole transition (locomotion↔locomotion).
- `MatchSourceTime` — target starts at the same %through as the source.
- `MatchSyncEventID/Index/Percentage` — target jumps to the sync event that
  matches the source's current event (e.g. continue on the correct foot).

**Source pose caching.** If a transition is interrupted by a *forceable* transition
mid-blend, the in-progress blended pose is cached (via a `CachedPoseWriteTask`/
`CachedPoseReadTask`, see §7.5) so the new transition can blend *from* it without a
visible pop. This is why the task system needs cached-pose storage.

### 6.5 State & transition conditions

These are `BoolValueNode`s that read the **source state's** runtime data:

```cpp
// StateCompletedConditionNode → true when source state finished (minus transition duration)
bool StateCompletedConditionNode::GetValueInternal(...) {
    auto* s = m_pSourceState;
    Percentage remaining = 1.0f - s->GetCurrentTime();
    return (remaining * s->GetDuration()) <= m_transitionDuration;
}

// TimeConditionNode → flexible time/loop comparisons
enum class ComparisonType { PercentageThroughState, PercentageThroughSyncEvent,
                            ElapsedTime, LoopCount };
enum class Operator { LessThan, LessThanEqual, GreaterThan, GreaterThanEqual };
```
There are also **event-condition** value nodes (ID event present, foot event in
source state, sync-event index reached, transition-event allowed) — these read the
`SampledEventsBuffer` filtered to the source state's range. They drive most
gameplay-aware transitions ("only exit attack during the recovery window").

### 6.6 State-machine gotchas

1. Track **transitioning-in vs transitioning-out** so a state knows whether to emit
   `Entry`/`FullyInState`/`Exit`.
2. Mark outgoing-branch sampled events as **inactive branch** — otherwise gameplay
   double-fires events during a blend.
3. Only allow a new transition to interrupt an in-progress one if its
   `m_canBeForced` is set; otherwise wait for completion.
4. Keep sync alive across the transition for locomotion (use `Synchronized` or a
   `MatchSyncEvent*` mode) or feet will pop.
5. The SM node must surface the active child's duration/sync-track so *its* parents
   (a blend above it) can still synchronize against it.

---

## 7. Deep Dive C — The task system & pose pool

> Milestone M2 (and revisited for cached poses in M6, ragdoll in M9). This is the
> layer that turns the graph's task indices into real pose math.

### 7.1 Why deferred tasks at all

Evaluating the graph just **registers tasks** and returns indices. Nothing is
computed until `UpdatePrePhysics`/`UpdatePostPhysics`. Benefits:
- **Zero-copy buffer reuse**: a blend *steals* one input's pose buffer as its output.
- **Pre/post-physics split**: ragdoll/IK tasks can run after the physics tick.
- **Dead-branch elimination**: only tasks reachable from the final task run.
- **Serializable**: a frame's task list is a compact, replicable recipe.

### 7.2 Task base + context

```cpp
enum class TaskUpdateStage : uint8 { Any, PrePhysics, PostPhysics };
using TaskDependencies = InlineVector<int8,2>;          // most tasks have ≤2 deps

class Task : public Object
{
public:
    Task(TaskUpdateStage stage, TaskDependencies deps = {}) : m_updateStage(stage), m_dependencies(deps) {}
    virtual void Execute(const TaskContext&) = 0;
    virtual bool AllowsSerialization() const { return false; }
    bool         HasPhysicsDependency() const { return m_updateStage != TaskUpdateStage::Any; }
    int8         GetResultBufferIndex() const { return m_bufferIdx; }
    const TaskDependencies& GetDependencyIndices() const { return m_dependencies; }
protected:
    PoseBuffer* TransferDependencyPoseBuffer(const TaskContext&, int8 depSlot); // steal (zero-copy)
    PoseBuffer* AccessDependencyPoseBuffer(const TaskContext&, int8 depSlot);   // borrow (read)
    void        ReleaseDependencyPoseBuffer(const TaskContext&, int8 depSlot);  // release a borrow
    void        MarkTaskComplete(const TaskContext&);
    int8 m_bufferIdx = InvalidIndex;
    TaskDependencies m_dependencies;
    TaskUpdateStage  m_updateStage = TaskUpdateStage::Any;
    bool m_isComplete = false;
};

struct TaskContext {
    int8                       m_currentTaskIdx;
    InlineVector<Task*,2>      m_dependencies;     // resolved dependency task ptrs
    PoseBufferPool&            m_posePool;
    BoneMaskPool&              m_boneMaskPool;
    Skeleton::LOD              m_skeletonLOD;
    XForm                      m_worldTransform, m_worldTransformInverse;
    TaskUpdateStage            m_updateStage;
    float                      m_deltaTime;
};
```

A graph node registers a task and gets its index back, which becomes
`GraphPoseNodeResult.m_taskIdx`:
```cpp
int8 idx = ctx.m_pTaskSystem->RegisterTask<SampleTask>(GetNodeIndex(), m_pAnimation, sampleTime);
```
A blend node consumes **two child task indices** and registers a `BlendTask`
depending on them — that's how the DAG is built bottom-up while the graph is
walked top-down.

### 7.3 The pose buffer pool (the clever bit)

```cpp
int8 PoseBufferPool::RequestPoseBuffer();          // marks a buffer used, returns its index
void PoseBufferPool::ReleasePoseBuffer(int8 idx);  // marks free, resets poses
```
A `PoseBuffer` holds the primary skeleton pose at `[0]` plus secondary-skeleton
poses at `[1..]`. The two access patterns (seen in `BlendTask::Execute`) are the
heart of the zero-copy design:

```cpp
void BlendTask::Execute(const TaskContext& ctx)
{
    PoseBuffer* pSrc   = TransferDependencyPoseBuffer(ctx, 0);  // STEAL dep0's buffer → becomes our output
    PoseBuffer* pTgt   = AccessDependencyPoseBuffer(ctx, 1);    // BORROW dep1's buffer (read-only)
    PoseBuffer* pFinal = pSrc;                                  // we write into the stolen buffer

    if (m_boneMaskTaskList.HasTasks()) {
        auto mask = m_boneMaskTaskList.GenerateBoneMask(ctx.m_boneMaskPool);
        Blender::ParentSpaceBlend(ctx.m_skeletonLOD, &pSrc->m_poses[0], &pTgt->m_poses[0],
                                  m_blendWeight, mask.m_pBoneMask, &pFinal->m_poses[0]);
        if (mask.m_maskPoolIdx != InvalidIndex) ctx.m_boneMaskPool.ReleaseMask(mask.m_maskPoolIdx);
    } else {
        Blender::ParentSpaceBlend(ctx.m_skeletonLOD, &pSrc->m_poses[0], &pTgt->m_poses[0],
                                  m_blendWeight, nullptr, &pFinal->m_poses[0]);
    }
    // ... blend secondary poses [1..] the same way ...
    ReleaseDependencyPoseBuffer(ctx, 1);    // we're done borrowing dep1
    MarkTaskComplete(ctx);                  // our m_bufferIdx now holds the result
}
```
- **Transfer** = take ownership of a dependency's output buffer (it had no other
  consumer) and reuse it as your output → **no allocation, no copy**.
- **Access** = borrow read-only; you must **Release** it when done.
- A `SampleTask` `Request`s a fresh buffer and samples the clip into it.

> `GlobalBlendTask` (model-space/layer blend) transfers dep **1** (the layer) and
> borrows dep 0 (the base) — the opposite of `BlendTask` — because it writes the
> result over the layer buffer. Watch which dependency you transfer per task type.

### 7.4 Scheduling & execution (pre/post physics)

```cpp
void TaskSystem::UpdatePrePhysics(float dt, const XForm& world, const XForm& worldInv)
{
    m_taskContext = {... PrePhysics ...};
    m_prePhysicsTaskIndices.clear();
    m_hasCodependentPhysicsTasks = false;

    if (m_hasPhysicsDependency) {
        // Gather every chain that must run before physics (recursively add deps first).
        for (int8 i=0; i<m_tasks.size(); ++i)
            if (m_tasks[i]->GetRequiredUpdateStage() == PrePhysics)
                if (!AddTaskChainToPrePhysicsList(i)) { m_hasCodependentPhysicsTasks = true; break; }

        if (m_hasCodependentPhysicsTasks) {        // pathological: bail to reference pose
            RegisterTask<ReferencePoseTask>(...); m_tasks.back()->Execute(m_taskContext);
        } else {
            for (int8 idx : m_prePhysicsTaskIndices) ExecuteOne(idx);
        }
    } else {
        ExecuteTasks();                            // no physics: just run everything now
    }
}

bool TaskSystem::AddTaskChainToPrePhysicsList(int8 taskIdx)   // recursive topo-add
{
    for (int8 dep : m_tasks[taskIdx]->GetDependencyIndices())
        if (!AddTaskChainToPrePhysicsList(dep)) return false;
    if (m_tasks[taskIdx]->GetRequiredUpdateStage() == PostPhysics) return false; // can't depend on physics pre-physics
    if (VectorContains(m_prePhysicsTaskIndices, taskIdx)) return false;          // codependency
    m_prePhysicsTaskIndices.push_back(taskIdx);
    return true;
}

void TaskSystem::UpdatePostPhysics()
{
    m_taskContext.m_updateStage = PostPhysics;
    if (m_hasCodependentPhysicsTasks) return;
    if (m_hasPhysicsDependency) ExecuteTasks();        // run the remaining (post-physics) tasks

    // Produce the final pose buffer from the LAST task.
    if (!m_tasks.empty()) {
        Task* pFinal = m_tasks.back();
        const PoseBuffer* pResult = m_posePool.GetBuffer(pFinal->GetResultBufferIndex());
        if (pResult->IsAdditive())                     // graph output was additive → flatten it
            for (each pose) Blender::ApplyAdditiveToReferencePose(LOD, &pResult->pose, 1.0f, nullptr, &m_finalPoseBuffer.pose);
        else
            m_finalPoseBuffer.CopyFrom(pResult);
        m_finalPoseBuffer.CalculateModelSpaceTransforms();   // ready for skinning
        m_posePool.ReleasePoseBuffer(pFinal->GetResultBufferIndex());
    } else {
        m_finalPoseBuffer.Release(Pose::Type::ReferencePose, true);
    }
}

void TaskSystem::ExecuteTasks()    // simple in-order pass; deps always precede dependents
{
    for (int8 i=0; i<m_tasks.size(); ++i)
        if (!m_tasks[i]->IsComplete()) ExecuteOne(i);
    m_needsUpdate = false;
}
```
Because tasks are registered bottom-up (a dependency is always registered before
the task that needs it), a **plain forward pass** respects the DAG — no separate
topological sort needed except for the pre-physics partition.

> **Threading.** Esoterica executes tasks single-threaded for determinism. The DAG
> makes parallelisation *possible* (independent sibling chains), but don't bother
> until profiling demands it; correctness first.

### 7.5 Cached poses (forced transitions)

A separate cached-pose pool, keyed by a small `CachedPoseID`, survives across
frames. Used when a transition must blend *from a frozen snapshot* (interrupted
transitions, "snap to 50% of clip" state changes):
- `CachedPoseWriteTask(srcTaskIdx, id)` copies a pose into cache slot `id`.
- `CachedPoseReadTask(id)` produces a pose buffer from the cache (no dependency).
`TaskSystem` owns `CreateCachedPose()/ResetCachedPose()/DestroyCachedPose()`.

### 7.6 Concrete task inventory (implement in this order)

| Task | Deps | Stage | Serializable | Notes |
|---|---|---|---|---|
| `SampleTask` | 0 | Any | ✓ | samples a clip at a time → buffer (primary + secondary skeletons, LOD-aware) |
| `ReferencePoseTask` / `ZeroPoseTask` | 0 | Any | ✓ | bind pose / additive-identity |
| `BlendTask` | 2 | Any | ✓ | transfer dep0, borrow dep1, parent-space blend, optional bone mask |
| `OverlayBlendTask` | 2 | Any | ✓ | overlay (ignores unset secondary layer poses) |
| `AdditiveBlendTask` | 2 | Any | ✓ | additive layer |
| `GlobalBlendTask` | 2 | Any | ✓ | model-space blend; transfers dep1 |
| `CachedPoseReadTask` / `CachedPoseWriteTask` | 0 / 1 | Any | ✓ | cached pose storage |
| `ChainSolverTask` (two-bone IK) | 1 | Any | ✓ | analytic 2-bone solve |
| `IKRigTask` | 1 | Any | ✓ | multi-effector rig solve |
| `RagdollSetPoseTask` | 1 | **PrePhysics** | ✗ | push anim pose to physics bodies |
| `RagdollGetPoseTask` | 0/1 | **PostPhysics** | ✗ | read simulated pose, blend with anim |

### 7.7 Task gotchas

1. Task indices are `int8` → **max 255 tasks/frame**. Plenty, but watch deep graphs.
2. A task may be registered once but consumed by one parent — buffers are
   single-consumer; that's what makes Transfer safe.
3. `AllowsSerialization()` must be `false` for ragdoll tasks (external physics state).
4. Codependent physics tasks (a task needed both pre- and post-physics) are detected
   and the frame falls back to a reference pose with a warning — design graphs to
   avoid this.
5. The final pose may be **additive** (e.g. graph root is an additive layer); always
   flatten via `ApplyAdditiveToReferencePose` before handing to skinning.

---

## 8. Deep Dive D — Graph compilation & serialization

> Milestone M5 (+ networking in M9). This is how an authored node graph becomes the
> compact runtime asset, and how it is (de)serialized — including over the network.

### 8.1 Runtime `GraphDefinition` layout

```cpp
class GraphDefinition : public IResource
{
    StringID                m_variationID;       // one definition per variation (see §10.5)
    TResourcePtr<Skeleton>  m_skeleton;

    Vector<int16>           m_persistentNodeIndices;     // nodes Initialize()d at construction
    Vector<uint32>          m_instanceNodeStartOffsets;  // byte offset of each node in the instance block
    uint32                  m_instanceRequiredMemory;
    uint32                  m_instanceRequiredAlignment;
    int16                   m_rootNodeIdx;

    Vector<StringID>        m_controlParameterIDs;
    Vector<StringID>        m_virtualParameterIDs;
    Vector<int16>           m_virtualParameterNodeIndices;
    HashMap<StringID,int16> m_parameterLookupMap;        // rebuilt at load

    Vector<ReferencedGraphSlot> m_referencedGraphSlots;
    Vector<ExternalGraphSlot>   m_externalGraphSlots;

    Vector<GraphNode::Definition*> m_nodeDefinitions;    // polymorphic, serialized by type
    Vector<ResourcePtr>            m_resources;           // clips, masks, child graphs, ...
    ResourceLUT                    m_resourceLUT;         // flattened incl. child graphs
};
```
The key idea: **immutable shared Definition objects** describe the graph; **per-instance
state** lives in a separate memory block addressed by the precomputed offsets.

### 8.2 The Definition → Node placement-new pattern

```cpp
template<class T>
T* GraphNode::Definition::CreateNode(const InstantiationContext& ctx, InstantiationOptions o) const
{
    T* pNode = reinterpret_cast<T*>(ctx.m_nodePtrs[m_nodeIdx]);   // precomputed slot
    if (o == InstantiationOptions::CreateNode) {
        new (pNode) T();                  // placement-new at the slot
        pNode->m_pDefinition = this;      // link to immutable settings
    }
    return pNode;
}

// Example with children:
void AndNode::Definition::InstantiateNode(const InstantiationContext& ctx, InstantiationOptions o) const
{
    auto* pNode = CreateNode<AndNode>(ctx, o);
    pNode->m_conditionNodes.reserve(m_conditionNodeIndices.size());
    for (int16 idx : m_conditionNodeIndices)
        ctx.SetNodePtrFromIndex(idx, pNode->m_conditionNodes.emplace_back(nullptr)); // wire by index
}
```
Pointers are wired **by index** so the graph structure survives serialization. The
serialized form has no pointers at all.

### 8.3 The compilation pipeline

A `GraphCompilationContext` accumulates the runtime definition while tools nodes
compile themselves top-down from the result node:

```cpp
class GraphCompilationContext
{
    HashMap<UUID,int16>            m_nodeIDToIndexMap;   // memoization: compile each node once
    Vector<GraphNode::Definition*> m_nodeDefinitions;
    Vector<uint32>                 m_nodeMemoryOffsets;
    uint32                         m_currentNodeMemoryOffset = 0;
    uint32                         m_graphInstanceRequiredAlignment = alignof(bool);
    Vector<int16>                  m_persistentNodeIndices;
    Vector<ResourceID>             m_registeredResources;
    // ... referenced/external slot registries ...

    template<class T>
    NodeCompilationState GetDefinition(const BaseNode* pNode, typename T::Definition*& out)
    {
        if (auto it = m_nodeIDToIndexMap.find(pNode->GetID()); it != end) {   // already compiled
            out = (typename T::Definition*)m_nodeDefinitions[it->second];
            return AlreadyCompiled;
        }
        out = New<typename T::Definition>();
        out->m_nodeIdx = int16(m_nodeDefinitions.size());
        m_nodeDefinitions.push_back(out);
        m_nodeIDToIndexMap[pNode->GetID()] = out->m_nodeIdx;
        TryAddPersistentNode(pNode, out);
        // memory layout: pad to alignof(T), record offset, advance by sizeof(T)
        uint32 pad = CalculatePaddingForAlignment(m_currentNodeMemoryOffset, alignof(T));
        m_nodeMemoryOffsets.push_back(m_currentNodeMemoryOffset + pad);
        m_currentNodeMemoryOffset += sizeof(T) + pad;
        m_graphInstanceRequiredAlignment = Max(m_graphInstanceRequiredAlignment, alignof(T));
        return NeedCompilation;
    }
    int16 RegisterResource(const ResourceID& id);   // dedup → index
};
```

Each tools node implements `Compile()`:
```cpp
int16 AnimationClipToolsNode::Compile(GraphCompilationContext& ctx) const
{
    AnimationClipNode::Definition* pDef = nullptr;
    if (ctx.GetDefinition<AnimationClipNode>(this, pDef) == NeedCompilation) {
        if (auto* pReverse = GetConnectedInputNode(0))           // recurse into children
            pDef->m_playInReverseValueNodeIdx = pReverse->Compile(ctx);
        auto* data = GetResolvedVariationData(ctx);              // variation-aware data
        pDef->m_dataSlotIdx     = ctx.RegisterResource(data->m_animClip.GetResourceID());
        pDef->m_speedMultiplier = data->m_speedMultiplier;
        pDef->m_sampleRootMotion= m_sampleRootMotion;
    }
    return pDef->m_nodeIdx;     // shared nodes return the same index
}
```

Driver:
```cpp
bool CompileGraph(const ToolsGraphDefinition& tools, StringID variationID)
{
    // 1) control parameters first (they become the first nodes)
    // 2) virtual parameters
    // 3) root result node → recursively compiles the whole reachable graph
    int16 rootIdx = resultNode->Compile(ctx);
    // 4) copy ctx → runtime GraphDefinition (offsets, sizes, alignment, slots, resources)
    return rootIdx != InvalidIndex;
}
```

### 8.4 Instance construction

```cpp
GraphInstance::GraphInstance(const GraphDefinition* def, uint64 ownerID,
                             TaskSystem* sharedTS, SampledEventsBuffer* sharedEvents)
{
    m_isStandalone = (sharedTS == nullptr);
    if (m_isStandalone) { m_pTaskSystem = New<TaskSystem>(def->GetSkeleton()); /* + events, rmdbg */ }

    // ONE aligned allocation for all node state
    m_pMemory = (uint8*)Alloc(def->m_instanceRequiredMemory, def->m_instanceRequiredAlignment);
    for (uint32 off : def->m_instanceNodeStartOffsets)
        m_nodes.push_back((GraphNode*)(m_pMemory + off));

    // create referenced child graphs (share our task system + event buffer)
    // ...

    InstantiationContext ic{ ..., m_nodes, ..., def->m_skeleton.Get(), def->m_parameterLookupMap, &def->m_resources, ownerID };
    for (int16 i=0; i<m_nodes.size(); ++i) {
        ic.m_currentNodeIdx = i;
        def->m_nodeDefinitions[i]->InstantiateNode(ic, CreateNode);   // placement-new each node
    }

    for (int16 idx : def->m_persistentNodeIndices) m_nodes[idx]->Initialize(m_graphContext);
    m_pRootNode = (PoseNode*)m_nodes[def->m_rootNodeIdx];
}
```
Destruction reverses it: shutdown persistent nodes, call each node's destructor
explicitly (placement-new ⇒ manual `~GraphNode()`), free the one block.

`EvaluateGraph` is then just:
```cpp
GraphPoseNodeResult GraphInstance::EvaluateGraph(Seconds dt, const XForm& world,
        PhysicsScene* phys, const SyncTrackTimeRange* range, bool reset)
{
    if (m_isStandalone) { m_pTaskSystem->Reset(); m_pSampledEventsBuffer->Clear(); }
    m_graphContext.Update(dt, world, phys);              // bumps m_updateID
    if (reset || !m_pRootNode->IsInitialized()) ResetGraphState();
    return m_pRootNode->Update(m_graphContext, range);
}
void ExecutePrePhysicsPoseTasks(const XForm& finalWorld){ m_pTaskSystem->UpdatePrePhysics(dt, finalWorld, finalWorld.GetInverse()); }
void ExecutePostPhysicsPoseTasks(){ m_pTaskSystem->UpdatePostPhysics(); }
```

### 8.5 Node serialization

The macro generates `Load`/`Save` that chain to the base then (de)serialize the
node's own fields:
```cpp
#define SK_SERIALIZE_NODEDEF(Base, ...) \
    void Load(BinaryArchiveReader& a) override { Base::Load(a); a.Serialize(__VA_ARGS__); } \
    void Save(BinaryArchiveWriter& a) const override { Base::Save(a); a.Serialize(__VA_ARGS__); }

struct AnimationClipNode::Definition : PoseNode::Definition {
    SK_SERIALIZE_NODEDEF(PoseNode::Definition, m_playInReverseValueNodeIdx, m_resetTimeValueNodeIdx,
                         m_speedMultiplier, m_startSyncEventOffset, m_sampleRootMotion,
                         m_allowLooping, m_dataSlotIdx);
    int16 m_dataSlotIdx = InvalidIndex; /* ...etc... */
};
```
The loader reconstructs the **polymorphic** definition array by type id (Skore's
reflection `TypeDescriptorCollection` equivalent), then calls `Load` on each, then
rebuilds `m_parameterLookupMap`, and during *install* resolves resource handles
(skeleton, clips, child graphs) and flattens the resource LUT (pulling child graph
resources up so the root LUT can resolve everything by path id).

### 8.6 Task serialization (networking / replay)

Tasks are bit-packed into a fixed buffer. The trick is a **`ResourceMappings`**
table so a clip pointer becomes a tiny index, and field widths are computed from
counts:
```cpp
class TaskSerializer : public BitArchive<1280>   // fixed 1280-byte buffer
{
    TaskSerializer(const Skeleton* s, const ResourceMappings& rm, uint8 numTasks) {
        m_maxBitsForDependencies = BitsForValue(numTasks);            // ≤8
        m_maxBitsForBoneMask     = BitsForValue(s->GetNumBoneMasks());
        m_maxBitsForResourceIDs  = BitsForValue(rm.GetNumMappings());
        m_maxBitsForBoneIndices  = BitsForValue(s->GetNumBones());
        WriteUInt(m_maxBitsForDependencies, 4);
        WriteUInt(numTasks, m_maxBitsForDependencies);
    }
    void WriteRotation(const Quat& q){ EncodedQuaternion e(q); WriteUInt(e.d0,16); WriteUInt(e.d1,16); WriteUInt(e.d2,16); }
    void WriteNormalizedFloat8Bit(float f);   // weights in 8 bits
    template<class T> void WriteResourcePtr(const T* p){ WriteUInt(m_resourceMappings.GetIndex(p->GetResourceID()), m_maxBitsForResourceIDs); }
};
```
A `BlendTask` serializes as: 2 dependency indices + an 8-bit weight + an optional
bone-mask task list. Each `Task::AllowsSerialization()` gates whether the frame can
be sent (ragdoll frames can't). On the receiver, `DeserializeTasks` rebuilds the
list and a normal `Execute` pass reproduces the exact pose.

### 8.7 Compile/serialize gotchas

1. Node indices are `int16` → **max ~32k nodes** per compiled graph; split huge
   graphs into referenced sub-graphs.
2. Compute and honor **per-node alignment**; SIMD-heavy nodes need 16/32-byte
   alignment, and the instance block alignment is the max over all nodes.
3. **Memoize** shared nodes by editor UUID so a node referenced twice compiles once
   (safe because Definitions are stateless).
4. **One `GraphDefinition` per variation** — client/server must agree on variation
   for task serialization to line up.
5. Placement-new ⇒ you **must** call destructors manually and free exactly one block.
6. Rebuild lookup maps and resolve resource handles at load/install, not compile.

---

## 9. The node catalogue to implement

Implement roughly in this order; each tier unlocks real authoring power. (Names
mirror Esoterica; group your files the same way.)

**Tier 1 — sources & values (M4):** `AnimationClipNode`, `ZeroPoseNode`,
`ReferencePoseNode`, `AnimationPoseNode` (pose-at-time); `ControlParameter{Bool,
ID,Float,Vector,Target}Node`; `Const{Bool,ID,Float,Vector,Target}Node`;
`Virtual{...}Node`.

**Tier 2 — math/logic (M4):** Float (remap, clamp, abs, ease, curve, math,
comparison, range-comparison, switch, angle-math, selector); Bool (and/or/not);
ID (comparison, id→float); Vector (info/create/negate); Target (is-set, info,
point, offset); `Cached{...}Node` (snapshot on state entry/exit).

**Tier 3 — blends & selectors (M4/M8):** `Blend1DNode`, `VelocityBlendNode`,
`Blend2DNode` (precomputed triangulation: store points + triangle indices + hull
indices; resolve a sample to 3 sources with barycentric weights),
`SelectorNode`/`ParameterizedSelectorNode` (+ clip-restricted variants).

**Tier 4 — state machines (M6):** `StateMachineNode`, `StateNode`,
`TransitionNode`, `StateCompletedConditionNode`, `TimeConditionNode`, and the
event-condition value nodes (ID/foot/sync/transition events).

**Tier 5 — layering & control (M8):** `LayerBlendNode` (base + up to N layers,
each with weight/bone-mask/root-motion-weight/blend-mode/sync), bone-mask nodes
(`BoneMaskNode`, `FixedWeightBoneMaskNode`, `BoneMaskBlendNode`,
`BoneMaskSelectorNode`), `SpeedScaleNode`/`DurationScaleNode`/
`VelocityBasedSpeedScaleNode`, `RootMotionOverrideNode`, event nodes
(`IDEventNode`, `IDEventConditionNode`, ...).

**Tier 6 — warps, IK, physics, sub-graphs (M8/M9):** `OrientationWarpNode`,
`TargetWarpNode`, `TwoBoneIKNode`, `IKRigNode`, `PoweredRagdollNode`,
`SimulatedRagdollNode`, `ReferencedGraphNode`, `ExternalGraphNode`,
`PassthroughNode` (base for property-modifying pose nodes).

---

## 10. Advanced features

### 10.1 Layers & bone masks
`LayerBlendNode` updates a base pose, then for each layer sets up a
`GraphLayerContext` (layer weight, root-motion weight, a `BoneMaskTaskList`) and
updates the layer node inside that context. Blend modes: **Overlay**
(`OverlayBlendTask`), **Additive** (`AdditiveBlendTask`), **ModelSpace**
(`GlobalBlendTask`). Bone masks are *deferred task lists* (Generate/Mask/Scale/
Combine/Blend) materialized only when a blend task needs them — pool the temporaries.

### 10.2 Motion warping
- **OrientationWarp**: rotates the character toward a target direction while the
  clip plays, distributing the rotation across the clip per authored warp events.
- **TargetWarp**: warps the root-motion *trajectory* to land on a moving target;
  analyses the clip into fixed/warpable sections and rewrites the path with
  Lerp/Hermite/Bezier (feature-preserving) curves. Driven by `TargetWarpEvent`s on
  the clip and a `Target` control parameter.

### 10.3 Root-motion override
`RootMotionOverrideNode` replaces the clip's root delta with a gameplay-desired
velocity/direction per-axis (X/Y/Z + facing), with min/max clamps and event-gated
blending. This is how you get responsive control without losing the pose.

### 10.4 IK & ragdoll
- **Two-bone IK** = analytic solve (`ChainSolverTask`): pivot, stretch, stiffness.
- **IK rig** (`IKRigTask`) = multi-effector solver over an authored rig
  (per-link joint limits). Esoterica wraps a third-party solver (RKIK); you can
  drop in your own FABRIK or use Jolt's. Up to ~6 effectors.
- **Ragdoll**: `RagdollSetPoseTask` (PrePhysics) pushes the anim pose into Jolt
  bodies; `RagdollGetPoseTask` (PostPhysics) reads simulated transforms and blends
  with animation by weight (powered ragdoll) or fully (simulated). Drive blend
  weight from `RagdollEvent` curves on the clip.

### 10.5 Variations (a standout feature)
One authored graph targets multiple skeletons via a **variation hierarchy**
(inheriting parent→child). Per-variation **data nodes** override skeleton-dependent
resources (which clip, which IK rig, which bone mask). **Each variation compiles to
its own `GraphDefinition`** bound to its skeleton. Implement the hierarchy + the
`GetResolvedVariationData()` lookup (walk up to find an override, else inherit).

### 10.6 Referenced vs external graphs
- **Referenced**: embedded at compile time, shares the parent's task system &
  event buffer, skeleton must match. Auto parameter mapping.
- **External**: a runtime-pluggable slot (`ConnectExternalGraph(slotID, def)`),
  e.g. swap weapon/vehicle behavior. Managed by an `ExternalGraphController`.

### 10.7 Recording & debugging
- **GraphRecorder**: serialize per-frame control params + sync range + task blob +
  node state, recursively for child graphs → deterministic frame-stepping replay.
- **RootMotionDebugger**: ring buffer of expected vs actual root transforms with a
  dependency graph of sample/modify/blend actions.
Gate all of this behind a `SK_DEVELOPMENT_TOOLS` flag like Esoterica does.

---

## 11. Editor / authoring

You already have `Skore::GraphEditor` (`Editor/Source/Skore/ImGui/GraphEditor.hpp`)
with Blueprint + StateMachine node kinds and Flow/Value pins — reuse it directly.

Build, in `Editor/Source/Skore/Animation/`:
1. A **tools-graph data model** (`ToolsGraphDefinition`) — authored nodes with
   typed pins (Pose/Bool/Float/Vector/ID/Target/BoneMask), parented sub-graphs
   (flow graphs + state-machine graphs + transition conduits), variation hierarchy,
   control/virtual parameter lists.
2. A **base tools node** with `Compile(ctx)` and `GetCompiledNodeIndex()` per §8.3,
   one tools node per runtime node.
3. The **flow-graph** and **state-machine-graph** editor surfaces on `GraphEditor`.
4. A **parameter panel** (create/rename/group control & virtual params; live values).
5. A **variation editor** (hierarchy tree, per-variation skeleton + data overrides).
6. **Live debugging** modes: *Direct Preview* (in-editor entity), *Live Debug*
   (attach to a running graph component), *Review Recording* (frame-step). Highlight
   active nodes, show value-node readouts, transition progress, pose-node debug info.
7. The **resource compiler** that runs `CompileGraph` per variation → the runtime
   `GraphDefinition` asset.

Event authoring lives on the **clip** asset: a multi-track timeline (ID, foot,
transition, snap-to-frame, orientation/target warp, root-motion, ragdoll tracks),
with sync-track marking. Bake the `SyncTrack` from the sync-marked track at compile.

---

## 12. ECS integration & the per-frame update

Add a `GraphComponent` (Skore `Component`) holding a `RID` graph asset + a
`GraphInstance*`, plus a simpler `ClipPlayerComponent` for non-graph playback.

Drive updates from an animation world/entity system across two stages (Skore's
`Tickable` / `FixedTickable`, or an explicit pre/post-physics hook):

```cpp
// PRE-PHYSICS
for (GraphComponent* c : animComponents) {
    c->GetController()->PreGraphUpdate(dt);                       // gameplay sets control params
    auto result = c->m_pInstance->EvaluateGraph(dt, c->WorldXForm(), physScene, nullptr, false);
    c->m_rootMotionDelta = result.m_rootMotionDelta;             // entity movement consumes this
    // move the entity by root motion here (or hand to character controller)
    c->m_pInstance->ExecutePrePhysicsPoseTasks(c->FinalWorldXForm());
}
// [ PHYSICS TICK ]  — ragdoll bodies simulate
// POST-PHYSICS
for (GraphComponent* c : animComponents) {
    c->m_pInstance->ExecutePostPhysicsPoseTasks();
    c->GetController()->PostGraphUpdate(dt);                      // gameplay reads sampled events
    c->m_pSkeletalMesh->SetPose(c->m_pInstance->GetPrimaryPose());
}
```

If you have no ragdoll/IK yet, you can collapse this to a single stage (the task
system runs everything in `UpdatePrePhysics` when there's no physics dependency).

---

## 13. Testing & validation strategy

Test each layer in isolation before stacking the next.

- **M0/M4 — Sync track unit tests.** Round-trip `GetTime`↔`GetPercentageThrough`
  for many percentages and offsets; verify LCM duration scaling against the worked
  example (walk+run→1.1s); verify `clampToTrack` modulo on negatives and overflows.
- **M1 — Blender golden tests.** Blend a pose with itself at every weight (must be
  identity); blend ref↔pose at 0/0.5/1; additive of zero-pose is identity; bone-mask
  0/1 fast-paths match the general path. Snapshot a few known poses numerically.
- **M2 — Task DAG.** Construct a Sample→Sample→Blend graph by hand; assert buffer
  transfer reuses indices (no growth in pool high-water mark); assert the forward
  pass executes deps first.
- **M4 vertical slice.** One clip plays and loops with correct root motion; then a
  walk↔run `Blend1D` on a float param with **no foot sliding** (the real proof that
  sync works end-to-end). Eyeball with a foot-locked debug draw.
- **M6.** A 3-state locomotion SM (idle→walk→run) with synchronized transitions;
  verify events from the outgoing branch are marked inactive (no double-fire).
- **M5/M9.** Compile→serialize→load round-trip yields a byte-identical definition;
  task serialize→deserialize reproduces the same final pose (diff the buffers).

A continuously useful tool: a **debug HUD** showing the active task tree, each
node's current time / sync event, and the root-motion ring buffer — port
Esoterica's `DebugView_Animation` early; it pays for itself.

---

## 14. Gotchas checklist

Pin this near your monitor.

**Blending / sync**
- [ ] Same `SyncTrackTimeRange` pointer to all synchronized children, never mutated.
- [ ] Clear model-space cache after every parent-space mutation.
- [ ] Additive result is additive only if **both** inputs are additive.
- [ ] Bone-mask weight multiplies the global weight.
- [ ] Snap weight≈1.0 to exactly 1.0 to hit copy fast-paths.
- [ ] Root-motion loop wrap composes `post * pre`.
- [ ] Single-frame clips force prev=cur=1.0; reverse inverts the event query range.

**Graph / tasks**
- [ ] Value nodes evaluate once per frame via `m_updateID` (`WasUpdated`).
- [ ] Buffers are single-consumer; Transfer steals, Access borrows + must Release.
- [ ] Watch which dependency each task transfers (BlendTask: dep0; GlobalBlendTask: dep1).
- [ ] Flatten an additive final pose with `ApplyAdditiveToReferencePose`.
- [ ] Avoid codependent physics tasks (pre+post on the same chain).

**Compile / instance**
- [ ] Wire node pointers by index; serialized form has no pointers.
- [ ] Honor per-node alignment; instance alignment = max over nodes.
- [ ] Memoize shared nodes by UUID; one Definition per node, one per variation.
- [ ] Placement-new ⇒ manual destructors + single free.

**State machine**
- [ ] Mark outgoing-branch events as inactive branch.
- [ ] Only forceable transitions may interrupt an in-progress one.
- [ ] Surface the active child's sync track/duration up to the SM node's parents.

---

### Appendix — recommended file layout in Skore

```
Runtime/Source/Skore/Animation/
  AnimXForm.hpp  Pose.hpp/.cpp  Skeleton.hpp/.cpp  AnimationClip.hpp/.cpp
  SyncTrack.hpp/.cpp  Blender.hpp/.cpp  BoneMask.hpp/.cpp  RootMotion.hpp/.cpp
  Target.hpp  AnimationEvent.hpp  Events/...
  Graph/  RuntimeGraph_Context|Definition|Instance|Node|ValueTypes|SampledEvents.*
  Graph/Nodes/  RuntimeGraphNode_*.{hpp,cpp}
  TaskSystem/  Task.* TaskSystem.* TaskPosePool.* TaskSerializer.*  Tasks/Task_*.*
  IK/  IKRig.*  IKChainSolver.*
  Components/  Component_AnimationGraph.*  Component_ClipPlayer.*
  Systems/  WorldSystem_Animation.*

Editor/Source/Skore/Animation/
  ToolsGraph/  ToolsGraph_Definition|Compilation|Variations.*
  ToolsGraph/Nodes/  ToolsGraphNode_*.*
  ResourceEditors/  ResourceEditor_AnimationGraph.*  (on Skore::GraphEditor)
  ResourceCompilers/  ResourceCompiler_AnimationGraph.*  ResourceCompiler_AnimationClip.*
```

*End of guide. Build it bottom-up, test each layer, and treat §4 (sync + blending)
as the make-or-break milestone.*
