# Porting the Esoterica Animation **Editor** onto Skore

> Companion to `AnimationSystemPort.md` (the runtime guide). This document maps
> Esoterica's animation **graph editor** (authoring/tools side) onto Skore's
> existing editor infrastructure: the `GraphEditor` ImGui canvas, the
> resource model (`RID` / `ResourceObject` / `UndoRedoScope`), and the
> already-scaffolded `AnimatorEditor`.
>
> The headline conclusion: **don't port Esoterica's bespoke `NodeGraph`
> framework. Evolve Skore's existing resource-backed animator and reuse
> `GraphEditor`.** Keep Esoterica's *compilation* logic (it's the valuable part);
> replace its *retained-mode authoring framework* with Skore-native resources.

---

## Table of Contents

1. [The two editor models, side by side](#1-the-two-editor-models-side-by-side)
2. [Strategy: evolve, don't port the framework](#2-strategy-evolve-dont-port-the-framework)
3. [What Skore already has (and its gap)](#3-what-skore-already-has-and-its-gap)
4. [The animation-graph resource schema](#4-the-animation-graph-resource-schema)
5. [The node-type registry](#5-the-node-type-registry)
6. [Immediate-mode reconciliation: GraphEditor ↔ resources](#6-immediate-mode-reconciliation)
7. [Navigation: drilling into states & sub-graphs](#7-navigation-drilling-into-states--sub-graphs)
8. [Panel-by-panel mapping](#8-panel-by-panel-mapping)
9. [Compilation, preview & live debug](#9-compilation-preview--live-debug)
10. [Editor milestones](#10-editor-milestones)
11. [Gotchas](#11-gotchas)

---

## 1. The two editor models, side by side

| Concern | Esoterica | Skore (today) |
|---|---|---|
| Authoring data | Retained C++ object tree (`BaseGraph`/`BaseNode`/`FlowGraph`), reflected, serialized to JSON/binary | **Resource graph**: `RID` + `ResourceObject` with enum-indexed fields, sub-objects, references |
| Node canvas | Custom `NodeGraph::GraphView` (retained, owns view state) | **`GraphEditor`** immediate-mode widget (per-frame node/link accumulation, returns a diff) |
| Undo/redo | Graph modification events + serialize snapshots | **`UndoRedoScope`** threaded through every resource mutation (built-in) |
| State machine | `StateMachineGraph` + `StateNode` + `TransitionConduitNode` | `AnimatorGraphWindow` draws SM via `GraphEditor` (`GraphNodeType::StateMachine`) |
| Node settings | `EE_REFLECT()` fields on the tools node | `ResourceObject` fields / a reflected settings sub-object |
| Compile → runtime | `GraphDefinitionCompiler::CompileGraph(toolsGraph, variation)` | *(to be added — port this part wholesale)* |
| Variations | `VariationHierarchy` + per-node `Data` overrides | `Avatars` sub-object list (placeholder; evolve into variations) |
| Live preview | `GraphInstance` on a preview entity + `GraphRecorder` | `PlayMode` + (to-be-added) `GraphComponent` |

The shapes differ most in **authoring storage** (retained objects vs resources)
and **canvas** (retained vs immediate). Everything below is about bridging those
two differences while keeping Esoterica's compile/runtime intact.

---

## 2. Strategy: evolve, don't port the framework

Esoterica's `NodeGraph` framework (`BaseNode`/`BaseGraph`/`FlowGraph`/pins/
connections + `GraphView`) is ~5k lines of retained-mode UI/data machinery that
**duplicates what Skore already provides**: Skore's `ResourceObject` graph *is*
your retained model (with free undo/redo and serialization), and `GraphEditor`
*is* your canvas. Porting the framework would mean running two parallel object
models and two undo systems. Don't.

**Adopt this split:**

| Esoterica layer | Action in Skore |
|---|---|
| `NodeGraph::BaseGraph/BaseNode/FlowGraph/Pin/Connection` | **Replace** with resource schema (§4) + `GraphEditor` |
| `FlowToolsNode` (per-node authoring class) | **Replace** with a **node-type descriptor** in a registry (§5) |
| `GraphView` (canvas) | **Reuse** `GraphEditor`; reconcile via its result diff (§6) |
| Node `Compile(GraphCompilationContext&)` | **Port nearly verbatim** — drive it from the resource graph instead of the object tree |
| `GraphDefinitionCompiler`, `GraphCompilationContext` | **Port verbatim** (see runtime guide §8) |
| `VariationHierarchy`, per-node `Data` overrides | **Port the concept** onto resource sub-objects (§4.5) |
| Editor tool panels | **Build as `EditorWindow`s** in the Animator workspace (§8) |
| `GraphInstance` preview, `GraphRecorder` | **Reuse** via a `GraphComponent` on a preview entity (§9) |

The one place you keep Esoterica's code almost line-for-line is the **compiler**:
each node-type's "compile" turns authored settings + input connections into a
runtime `GraphNode::Definition` and returns an `int16` index. That logic is engine
agnostic — feed it from `ResourceObject` reads instead of `EE_REFLECT` fields.

---

## 3. What Skore already has (and its gap)

### 3.1 The reusable `GraphEditor` canvas
`Editor/Source/Skore/ImGui/GraphEditor.hpp` — an **immediate-mode** node canvas:
```cpp
editor.Begin("graph_id");
editor.BeginNode(nodeId, "Blend1D", pos, desc);          // desc: node visual type + colors
  editor.InputPin("Pose A", GraphPinType::Value, color);
  editor.PinWidgetDragFloat(&someValue);                 // optional inline widget on a pin
  editor.OutputPin("Result", GraphPinType::Value);
editor.EndNode();
editor.Link(linkId, outNodeId, outPinIdx, inNodeId, inPinIdx);
GraphEditorResult r = editor.End();
// r.createdLinks / r.deletedNodeIds / r.deletedLinkIds / r.movedNodes / r.selectionChanged
```
Visual node kinds: `GraphNodeType::Blueprint` (pinned dataflow) and
`GraphNodeType::StateMachine` (rounded state with edge-routed transition links).
Pin kinds: `Flow` vs `Value`. Inline pin widgets: DragFloat / InputText /
Checkbox / Combo. It tracks selection, box-select, link creation, pan/zoom — you
just feed it nodes each frame and apply the returned diff.

### 3.2 The resource model
Assets are `RID`s whose contents are `ResourceObject`s with **enum-indexed
fields**, each annotated by type. Real example
(`Runtime/.../Graphics/GraphicsResources.hpp`):
```cpp
struct AnimationControllerResource {
    enum { Name /*String*/, PreviewEntity /*Reference*/,
           Layers /*SubObjectList*/, Parameters /*SubObjectList*/, Avatars /*SubObjectList*/ };
};
```
Mutation is transactional with undo built in:
```cpp
ResourceObject o = Resources::Write(rid);
o.SetString(AnimationLayerResource::Name, "New Layer");
o.AddToSubObjectList(AnimationLayerResource::States, stateRid);
o.Commit(scope);                                   // scope = UndoRedoScope*
```
Field types available (from existing schemas): `String, Uint, Float, Vec2, Vec3,
Quat, Enum, Blob, Reference, ReferenceArray, SubObject, SubObjectList`. You react
to external edits via resource change callbacks (e.g. `AnimatorEditor::OnStateChange`).

### 3.3 The existing animator (the gap)
Skore's `AnimatorEditor` + windows already model a **Unity-Mecanim** controller:
```
AnimationControllerResource { Name, PreviewEntity, Layers[], Parameters[], Avatars[] }
AnimationLayerResource      { Name, Weight, States[], Transitions[], DefaultState→ }
AnimationStateResource      { Position(Vec2), Speed(Float), Animation→ }   // ONE clip per state
AnimationTransitionResource { From→, To→ }                                 // no conditions
AnimationParameterResource  { Name, Type(Enum) }
AnimationAvatarResource     { Name, ... }
```
This is the skeleton you'll grow. The **gaps vs Esoterica** are exactly the
powerful bits:
- a state plays one clip → must become a **blend tree** (a node-graph);
- transitions have no **conditions / duration / sync mode**;
- parameters lack **control-vs-virtual**, preview range, expected values;
- avatars are placeholders → become **variations** (skeleton + per-node overrides);
- there's no **value-node graph**, no **layers-with-bone-masks**, no **compile step**.

The `GraphEditorWindow` (a generic blueprint editor with an in-memory
`GraphNode/GraphLink` model and `m_nextId`) is a useful *visual* reference but its
data lives in memory, not resources — for the animation graph you want the
resource-backed approach so you get undo + serialization for free.

---

## 4. The animation-graph resource schema

Define the authoring model as resources. This *is* your "tools graph". Each node
is a sub-object; connections are stored on the graph. (Field annotations follow
the Skore convention.)

### 4.1 Top-level graph asset
```cpp
struct AnimGraphResource {                 // .animgraph
    enum { Name,                 //String
           Skeleton,             //Reference (default variation skeleton)
           RootGraph,            //SubObject  → AnimNodeGraphResource (the root blend tree)
           ControlParameters,    //SubObjectList → AnimParameterResource
           VirtualParameters,    //SubObjectList → AnimParameterResource
           Variations,           //SubObject  → AnimVariationHierarchyResource
           PreviewEntity };      //Reference
};
```

### 4.2 A node-graph (blend tree OR value tree OR state machine)
One schema serves all graph kinds; a `GraphKind` enum disambiguates.
```cpp
struct AnimNodeGraphResource {
    enum class GraphKind { BlendTree, ValueTree, StateMachine, TransitionConduit };
    enum { Kind,          //Enum  GraphKind
           Nodes,         //SubObjectList → AnimNodeResource
           Connections,   //SubObjectList → AnimConnectionResource (blend/value trees)
           EntryState,    //Reference (state machines only)
           ViewOffset,    //Vec2  (persist canvas pan)
           ViewScale };   //Float
};

struct AnimConnectionResource {
    enum { FromNode,   //Reference → AnimNodeResource
           FromPin,    //Uint  (output pin index)
           ToNode,     //Reference
           ToPin };    //Uint  (input pin index)
};
```

### 4.3 A node
```cpp
struct AnimNodeResource {
    enum { TypeId,        //Uint  (node-type descriptor id, see §5)
           Name,          //String (user label / state name)
           Position,      //Vec2  (canvas position)
           Settings,      //SubObject (a reflected per-node settings object; type chosen by TypeId)
           DynamicPins,   //SubObjectList (for Blend1D/Layer/IKRig variable input pins)
           ChildGraph,    //SubObject → AnimNodeGraphResource (state's blend tree / SM node's SM)
           SecondaryGraph,//SubObject → AnimNodeGraphResource (transition's condition value-tree)
           VariationData };//SubObject → AnimVariationDataResource (per-variation overrides)
};
```
- `Settings` is the equivalent of Esoterica's `EE_REFLECT()` fields. Store it as a
  reflected sub-object whose concrete type is selected by `TypeId`; the property
  panel reflects it directly (§8.3).
- `ChildGraph` is how a **state** owns a blend tree and a **state-machine node**
  owns a state machine — exactly Esoterica's `CreateChildGraph<FlowGraph>()` /
  `CreateChildGraph<StateMachineGraph>()`.
- `SecondaryGraph` is how a **transition** owns its condition value-tree (Esoterica's
  conduit). The transition's own blend settings live in `Settings`.

### 4.4 Parameters
```cpp
struct AnimParameterResource {
    enum class ValueType { Bool, Float, Vector, ID, Target, BoneMask };
    enum { Name,            //String
           Group,           //String (UI grouping)
           Type,            //Enum ValueType
           IsVirtual,       //Bool  (virtual params have a SecondaryGraph computing them)
           PreviewBool,     //Bool
           PreviewFloat,    //Float
           PreviewMin,      //Float
           PreviewMax,      //Float
           PreviewVector,   //Vec3
           PreviewID,       //String
           ExpectedIDs,     //SubObjectList<String>  (autocomplete for ID params)
           ComputeGraph };  //SubObject → AnimNodeGraphResource (virtual params only)
};
```
This directly captures Esoterica's `Bool/Float/ID/...ControlParameterToolsNode`
preview fields and the control-vs-virtual split.

### 4.5 Variations (port of `VariationHierarchy`)
```cpp
struct AnimVariationHierarchyResource {
    enum { Variations };     //SubObjectList → AnimVariationResource
};
struct AnimVariationResource {
    enum { Id,        //String
           Parent,    //String  (inherits skeleton + overrides from parent; "" for default)
           Skeleton };//Reference
};
// On a node, per-variation data overrides:
struct AnimVariationDataResource {
    enum { DefaultData,  //SubObject (reflected Data, e.g. {clip, speed, syncOffset})
           Overrides };  //SubObjectList → { VariationId:String, Data:SubObject }
};
```
Resolution mirrors Esoterica's `GetResolvedVariationData`: walk from the active
variation up the `Parent` chain, first override wins, else `DefaultData`. The
compiler runs **once per variation**, producing one runtime `GraphDefinition` each
(runtime guide §8, §10.5).

> Practical note: you can either *rename* the existing `Avatars` list to
> `Variations`, or keep `Avatars` as the variation list and add a `Skeleton`
> reference to it. Either way it's a small migration of the existing schema.

---

## 5. The node-type registry

Esoterica encodes each node-type as a C++ class (`AnimationClipToolsNode`,
`Blend1DToolsNode`, …) with virtuals (`GetTypeName`, `GetCategory`,
`GetAllowedParentGraphTypes`, pin declarations, `Compile`). In Skore, model each
node-type once as a **descriptor** registered in a table, keyed by the `TypeId`
stored on `AnimNodeResource`:

```cpp
struct AnimNodePinDesc { String name; AnimValueType type; bool dynamic = false; };

struct AnimNodeTypeDesc {
    u32        typeId;
    String     name;                 // "Animation Clip"
    String     category;             // "Animation" (node-create menu grouping)
    ImColor    headerColor;          // GetTitleBarColor()
    GraphNodeVisual visual;          // Blueprint or StateMachine
    u8         allowedGraphKinds;    // bitflags: BlendTree | ValueTree | TransitionConduit
    AnimValueType outputType;        // pose/bool/float/...
    Array<AnimNodePinDesc> inputs;   // static input pins
    bool       supportsDynamicInputs = false;
    AnimValueType dynamicInputType = AnimValueType::Pose;
    bool       hasChildGraph = false;        // states, state-machine nodes
    AnimNodeGraphResource::GraphKind childKind;
    TypeID     settingsType;         // reflected settings struct for the property panel

    // The two behaviours that can't be pure data:
    Function<i16(GraphCompilationContext&, RID node)> compile;   // port of Tools::Compile()
    Function<void(GraphEditor&, RID node)>            drawExtra; // optional in-node controls
};

void RegisterAnimNodeType(const AnimNodeTypeDesc&);
Span<const AnimNodeTypeDesc> GetAnimNodeTypes();
```

Registration reads like a table:
```cpp
RegisterAnimNodeType({ .typeId = NodeType_Blend1D, .name = "Blend 1D",
    .category = "Blends", .visual = Blueprint, .allowedGraphKinds = BlendTree,
    .outputType = Pose,
    .inputs = { {"Parameter", Float} },          // + dynamic Pose pins
    .supportsDynamicInputs = true, .dynamicInputType = Pose,
    .settingsType = TypeInfo<Blend1DSettings>::ID(),
    .compile = &CompileBlend1D });
```

Two payoffs:
1. The **node-create context menu**, pin layout, colors, and graph-kind
   restrictions are pure data — the editor builds menus and draws pins from the
   table (replacing dozens of `GetTypeName/GetCategory/GetAllowedParentGraphTypes`
   overrides).
2. `compile` is the only meaningful per-node code, and it's a near-verbatim port
   of Esoterica's `Compile()` — it reads `Settings` + input connections from the
   resource and emits a runtime `Definition`.

**Pin type → color mapping** (port Esoterica's `GetPinTypeForValueType` + colors):
Pose, Bool, Float, Vector, ID, Target, BoneMask each get a distinct `ImColor` so
links read at a glance. Use `GraphPinType::Flow` for Pose pins (the "main" stream)
and `GraphPinType::Value` for the rest, or color-code all as `Value` — your call.

---

## 6. Immediate-mode reconciliation

`GraphEditor` is immediate-mode; your truth is in resources. Each frame: **emit
nodes/links from resources → apply the returned diff back to resources** (under an
`UndoRedoScope`). This replaces Esoterica's retained `GraphView` entirely.

```cpp
void AnimGraphWindow::Draw(bool& open)
{
    RID graph = m_editor->GetActiveGraph();           // current graph on the nav stack
    ResourceObject g = Resources::Read(graph);

    m_canvas.Begin("anim_graph");

    // 1) EMIT nodes
    g.IterateSubObjectList(AnimNodeGraphResource::Nodes, [&](RID nodeRid){
        ResourceObject n = Resources::Read(nodeRid);
        const AnimNodeTypeDesc& t = GetAnimNodeType(n.GetUInt(AnimNodeResource::TypeId));
        GraphNodeDesc d{ .nodeType = t.visual, .headerColor = t.headerColor };
        m_canvas.BeginNode(RidToId(nodeRid), n.GetString(AnimNodeResource::Name).CStr(),
                           n.GetVec2(AnimNodeResource::Position), d);
        for (auto& pin : t.inputs)  m_canvas.InputPin(pin.name.CStr(), PinVisual(pin.type), PinColor(pin.type));
        // dynamic pins from n.DynamicPins ...
        m_canvas.OutputPin("Out", PinVisual(t.outputType), PinColor(t.outputType));
        if (t.drawExtra) t.drawExtra(m_canvas, nodeRid);
        m_canvas.EndNode();
    });

    // 2) EMIT connections
    g.IterateSubObjectList(AnimNodeGraphResource::Connections, [&](RID linkRid){
        ResourceObject c = Resources::Read(linkRid);
        m_canvas.Link(RidToId(linkRid),
            RidToId(c.GetReference(AnimConnectionResource::FromNode)), c.GetUInt(AnimConnectionResource::FromPin),
            RidToId(c.GetReference(AnimConnectionResource::ToNode)),   c.GetUInt(AnimConnectionResource::ToPin));
    });

    GraphEditorResult r = m_canvas.End();

    // 3) APPLY the diff back to resources (one undo scope per user gesture)
    if (!r.createdLinks.Empty() || !r.deletedNodeIds.Empty() || !r.deletedLinkIds.Empty() || !r.movedNodes.Empty())
    {
        UndoRedoScope* scope = Editor::CreateUndoScope("Edit Anim Graph");
        for (auto& link : r.createdLinks)   if (IsValidConnection(link)) CreateConnectionResource(graph, link, scope);
        for (u64 id : r.deletedLinkIds)     RemoveConnectionResource(graph, IdToRid(id), scope);
        for (u64 id : r.deletedNodeIds)     RemoveNodeResource(graph, IdToRid(id), scope);
        for (auto& m : r.movedNodes)        SetNodePosition(IdToRid(m.nodeId), m.position, scope);
        scope->Commit();
    }

    // 4) Node-create context menu (right-click) — build from the type registry,
    //    filtered by this graph's GraphKind via allowedGraphKinds.
}
```

Key reconciliation rules:
- **Stable ids.** `GraphEditor` keys nodes/links by `u64`. Derive that id from the
  resource `RID` (a stable hash) so selection survives across frames — don't use a
  running counter like `GraphEditorWindow::m_nextId` for resource-backed graphs.
- **Validate connections** before committing (port Esoterica's `IsValidConnection`:
  pin types must match; a Pose input takes exactly one connection; respect
  `allowedGraphKinds`). Reject in step 3, don't draw the link.
- **One `UndoRedoScope` per gesture**, then `Commit()`. Because mutations go
  through `Resources::Write(...).Commit(scope)`, undo/redo is automatic — you do
  **not** need to port Esoterica's serialize-snapshot undo machinery.
- **Selection** comes from `r.selectionChanged` → drives the property panel
  (§8.3) and outliner highlight.

---

## 7. Navigation: drilling into states & sub-graphs

Esoterica keeps a **graph stack** (`m_loadedGraphStack` of `LoadedGraphData`) and
breadcrumbs; double-clicking a state or referenced-graph node pushes a new graph.
Reproduce this with a small stack of `RID`s:

```cpp
struct AnimGraphNav {
    Array<RID> stack;                 // [0] = root graph; back() = currently shown
    RID  Active() const { return stack.Back(); }
    void Push(RID childGraph) { stack.PushBack(childGraph); }
    void PopTo(u32 depth)     { stack.Resize(depth + 1); }
};
```
- Double-click a **state** → push its `ChildGraph` (a `BlendTree` graph). The graph
  view now edits that state's blend tree.
- Double-click a **state-machine node** → push its `ChildGraph` (a `StateMachine`
  graph).
- Double-click a **transition** → open its `SecondaryGraph` (a `TransitionConduit`
  value-tree) — Esoterica shows this in the **secondary** graph view beside the
  primary, which `GraphEditor` supports by simply running a second canvas instance.
- **Breadcrumbs** = render `stack` names as buttons; clicking one calls `PopTo`.
- **Referenced graphs** (another `.animgraph` asset embedded as a node): push a
  read-only view of that asset's root graph (Esoterica loads its
  `ToolsGraphDefinition` into the stack; you load the other `RID`).

This is the exact role of Esoterica's `GetNavigationTarget()` — encode it as
"which sub-object graph does double-clicking this node open."

---

## 8. Panel-by-panel mapping

Build these as `EditorWindow`s registered into the **Animator** workspace
(`WorkspaceTypes::Animator`), docked per Esoterica's layout. You already have two
of them scaffolded.

| Esoterica panel | Skore window | Status |
|---|---|---|
| Primary graph view + breadcrumb nav | `AnimatorGraphWindow` (extend) | exists (SM only) → add blend-tree mode + nav stack |
| Secondary graph view (transitions/curves) | second `GraphEditor` instance in the same window or a sibling window | new |
| Outliner (node tree + filter) | `AnimatorTreeViewWindow` (extend) | exists (layers/params/avatars) |
| Node property grid (settings + variation overrides) | `PropertiesWindow` driven by selection | reuse |
| Control-parameters panel (groups + live preview) | new `AnimParametersWindow` | new (model exists) |
| Variation editor (hierarchy + skeleton picker) | new `AnimVariationsWindow` | new (Avatars→Variations) |
| Compilation log (click-to-node) | new `AnimCompileLogWindow` (or reuse `ConsoleWindow`) | new |
| Clip browser (drag-drop) | reuse `ProjectBrowserWindow` filtered to clips | reuse |
| Viewport (3D preview + record controls) | reuse `SceneViewWindow` against a preview scene | reuse |

### 8.1 Graph view (`AnimatorGraphWindow`)
Already draws a layer's state machine via `GraphEditor` with
`GraphNodeType::StateMachine` and has context menus (New State / New Transition /
Set Default / Duplicate / Delete). Extend it to:
- switch between **StateMachine** rendering (states + transition edges) and
  **Blueprint** rendering (blend/value trees) based on the active graph's `Kind`;
- use the **reconciliation loop** of §6 instead of bespoke state/transition
  handling, so blend-tree editing comes "for free";
- host the **nav stack + breadcrumbs** of §7;
- build the right-click **node-create menu** from the type registry (§5), filtered
  by the active graph's `GraphKind`.

### 8.2 Outliner (`AnimatorTreeViewWindow`)
Today it lists Layers / Parameters / Avatars. Keep that, and add a per-graph
**node tree with a filter** (Esoterica's outliner): iterate the active graph's
`Nodes`, show by name, double-click to select/center. Selection syncs with the
graph view via the shared selection set.

### 8.3 Property panel
On selection change (`GraphEditorResult::selectionChanged`), reflect the selected
node's `Settings` sub-object into `PropertiesWindow`. Because `Settings` is a
reflected type chosen by `TypeId`, Skore's existing property grid renders it with
no per-node UI code — this replaces Esoterica's hand-rolled node-editor grid. For
**variation-data** nodes, show a second grid bound to the resolved
`AnimVariationDataResource` for the active variation (Esoterica shows exactly this
dual grid).

### 8.4 Parameters panel
Port Esoterica's control-parameter panel: a group tree (by `Group`) listing params
with **type-specific live editors** — checkbox (Bool), slider with
`PreviewMin/Max` (Float), combo from `ExpectedIDs` (ID), vector input (Vector),
transform gizmo (Target). In preview mode these write directly into the running
`GraphInstance` control parameters (§9). Creating/renaming/deleting params mutates
`AnimGraphResource::ControlParameters` under an undo scope (the existing
`AnimatorEditor::AddParameter/RemoveParameter/RenameParameter` is the template).

### 8.5 Variations panel
A tree over `AnimVariationHierarchyResource` with a **skeleton picker** per
variation and an **overrides** view for variation-data nodes. Changing the active
variation re-resolves variation data in the graph view and re-targets the compile.

---

## 9. Compilation, preview & live debug

This is where the runtime guide and this editor guide meet. **Port the compiler
verbatim** (runtime guide §8.3) but feed it from resources.

### 9.1 Compile trigger
On *Start Preview*, *Save*, or *Refresh*:
```cpp
GraphDefinitionCompiler compiler;
bool ok = compiler.CompileGraph(animGraphResource, activeVariationId);  // reads ResourceObjects
m_compileLog = compiler.GetLog();                 // NodeCompilationLogEntry list
m_uuidToRuntimeIdx = compiler.GetNodeMappings();  // for live-debug highlighting
if (!ok) { ShowErrorDialog(); return; }
```
Each node-type's registered `compile` callback (§5) does the work: read `Settings`
+ input connections from the resource, populate a runtime `GraphNode::Definition`,
register resources/parameters, return the `int16` node index. Memoize by node
`RID` so shared nodes compile once (runtime guide §8.3).

Surface `m_compileLog` in the log window with severity colors; clicking an entry
selects the offending node via `m_uuidToRuntimeIdx`'s inverse — same UX as
Esoterica's clickable log.

### 9.2 Live preview
Esoterica spins up a preview entity with a `GraphComponent` +
`SkeletalMeshComponent` + `GraphInstance`. In Skore, in the Animator workspace's
preview scene (you already store `PreviewEntity` on the controller/graph resource):
1. instantiate the compiled `GraphDefinition` → `GraphInstance` on a `GraphComponent`;
2. each frame, push the parameters panel values into the instance's control params;
3. run the two-phase update (runtime guide §12) and draw the skeletal mesh in
   `SceneViewWindow`;
4. drive it with Skore's `PlayMode` (`Editing`/`Paused`/`Simulating`).

### 9.3 Live debug & recording
Esoterica's `DebugMode` = `{ None, Preview, LiveDebug, ReviewRecording }`:
- **LiveDebug**: attach the editor to a *running game's* `GraphComponent` (find it
  by entity id) and visualize its live state — highlight active nodes (via the
  UUID↔runtime-index map), show value-node readouts, transition progress.
- **ReviewRecording**: port `GraphRecorder` (runtime guide §10.7) — record
  per-frame control params + sync range + serialized task blob + node state, then
  scrub frames. This needs the runtime recording support, so it lands in editor
  milestone E5 alongside runtime M9.

Gate all of this behind `SK_DEVELOPMENT_TOOLS` as Esoterica does.

---

## 10. Editor milestones

Sequence the editor work alongside the runtime milestones (runtime guide §2).

| Editor milestone | Delivers | Needs runtime |
|---|---|---|
| **E0 — Schema** | the resource schemas of §4; migrate existing Animator schema (Avatars→Variations; State gains `ChildGraph`) | — |
| **E1 — Node registry + blend-tree canvas** | §5 registry; §6 reconciliation; create/move/delete/link blend-tree nodes; property panel via reflected `Settings` | M3 |
| **E2 — Compile + preview** | port `GraphDefinitionCompiler` reading resources; compile log window; live preview entity + skeletal mesh | M4, M5 |
| **E3 — Parameters + state machines** | parameters panel with live preview; state-machine graph editing (states own blend trees; transitions own conduits); nav stack + breadcrumbs | M6 |
| **E4 — Variations + advanced nodes** | variation hierarchy panel + per-node overrides; clip browser drag-drop; layer/bone-mask/IK/warp node UIs | M8 |
| **E5 — Live debug + recording** | attach-to-game live debug; record/replay scrubbing | M9 |

A good first vertical slice (after **E2**): author a Blend1D of walk↔run driven by
a Float control parameter, hit Preview, drag the parameter slider, and watch the
character blend with correct foot-sync. That exercises registry → reconciliation →
compile → instance → preview end to end.

---

## 11. Gotchas

**Immediate-mode vs retained**
- Derive `GraphEditor` node/link `u64` ids from resource `RID`s (stable), not a
  running counter — otherwise selection/links flicker across frames.
- Emit every node and link **every frame** from the resource graph; the canvas
  holds no persistent model of its own for you.
- Apply the result diff **once per gesture** inside a single `UndoRedoScope`.

**Resources & undo**
- All mutations go through `Resources::Write(...).Commit(scope)`. Don't invent a
  parallel undo system — Skore's scopes already cover it (this deletes a whole
  Esoterica subsystem).
- Use **sub-objects** for owned children (a node's settings, a state's child
  graph) and **references** for cross-links (connection endpoints, default state,
  transition from/to). Deleting a node must also delete its incident connections
  (the existing `RemoveState` already does this for transitions — follow that
  pattern).

**Node model**
- Keep `compile` the *only* per-node-type code; everything else (pins, menu,
  colors, allowed graphs) is data in the descriptor.
- Enforce `allowedGraphKinds` when building the create-menu and when validating
  pastes/drops (Esoterica's `GetAllowedParentGraphTypes`).
- Dynamic-pin nodes (Blend1D, Layer, IKRig) must keep `DynamicPins` in sync with
  `Settings` (port `RefreshDynamicPins`); re-derive pin count before emitting.

**Navigation & compile**
- One compiled `GraphDefinition` **per variation**; recompile when the active
  variation changes.
- Map node `RID` ↔ runtime `int16` index at compile time and keep both directions
  — you need it for log-click-to-node and live-debug highlighting.
- Transitions store their blend settings in `Settings` and their *condition* in the
  `SecondaryGraph`; don't conflate them.

---

### Cross-references
- Runtime data model, node base classes, sync/blending, state machine, task
  system, and the **compiler/serialization** internals: see `AnimationSystemPort.md`.
- Reusable canvas API: `Editor/Source/Skore/ImGui/GraphEditor.hpp`.
- Existing animator model to evolve: `AnimatorEditor.{hpp,cpp}`,
  `AnimatorGraphWindow`, `AnimatorTreeViewWindow`, and the resource schemas in
  `Graphics/GraphicsResources.hpp`.

*End of editor port guide.*
