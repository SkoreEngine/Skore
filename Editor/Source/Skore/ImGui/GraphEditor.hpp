#pragma once

#include "imgui.h"
#include "imgui_canvas.h"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	enum class GraphNodeType
	{
		Blueprint,
		StateMachine
	};

	enum class GraphPinKind
	{
		Input,
		Output
	};

	enum class GraphPinType
	{
		Flow,
		Value
	};

	enum class GraphWidgetType
	{
		None,
		InputFloat,
		DragFloatN,
		Color,
		InputText,
		Checkbox,
		Combo
	};

	struct GraphNodeDesc
	{
		GraphNodeType nodeType = GraphNodeType::Blueprint;
		ImColor       color = ImColor(41, 41, 43, 230);
		ImColor       headerColor = ImColor(128, 195, 248);
		f32           rounding = -1.0f;
	};

	struct GraphEditorNewLink
	{
		u64 outputNodeId = 0;
		u32 outputPinIndex = 0;
		u64 inputNodeId = 0;
		u32 inputPinIndex = 0;
	};

	struct GraphEditorNodeMoved
	{
		u64  nodeId = 0;
		Vec2 oldPosition{};
		Vec2 position{};
	};

	struct GraphEditorPinEdit
	{
		u64 nodeId = 0;
		u32 pinIndex = 0;
	};

	struct GraphEditorSelectionChanged
	{
		Array<u64> oldSelectedNodes{};
		Array<u64> oldSelectedLinks{};
		Array<u64> newSelectedNodes{};
		Array<u64> newSelectedLinks{};
	};

	struct GraphEditorResult
	{
		Array<GraphEditorNewLink>   createdLinks{};
		Array<u64>                  deletedNodeIds{};
		Array<u64>                  deletedLinkIds{};
		Array<GraphEditorNodeMoved> movedNodes{};
		GraphEditorSelectionChanged selectionChanged{};
		bool                        hasSelectionChanged = false;

		//pin value widgets (PinWidgetDragFloatN / PinWidgetColor): which one is being dragged this
		//frame, and which finished an edit (so the caller can persist it).
		bool                      pinValueActive = false;
		GraphEditorPinEdit        activePinValue{};
		Array<GraphEditorPinEdit> committedPinValues{};

		//node currently under the mouse this frame (0 if none) and its world-space rect, so callers can
		//implement drop targets / overlays over a specific node. Valid after End().
		u64  hoveredNodeId = 0;
		Vec2 hoveredNodeMin{};
		Vec2 hoveredNodeMax{};
	};

	class GraphEditor
	{
	public:
		void Begin(const char* id);

		void BeginNode(u64 id, const char* name, Vec2 position, const GraphNodeDesc& desc = {});
		void InputPin(const char* name, GraphPinType type = GraphPinType::Value, ImColor color = ImColor(150, 200, 150));
		void OutputPin(const char* name, GraphPinType type = GraphPinType::Value, ImColor color = ImColor(150, 200, 150));

		// Attach a preview image rendered at the bottom of the current node. Call between BeginNode and EndNode.
		void NodeThumbnail(ImTextureID texture);

		void EndNode();

		// Pin widget functions — call right after InputPin to attach a widget to it
		void PinWidgetDragFloat(f32* value, f32 speed = 0.1f);
		void PinWidgetDragFloatN(f32* values, u32 count, f32 speed = 0.1f);
		void PinWidgetColor(f32* rgb);
		void PinWidgetInputText(char* buffer, u32 bufferSize);
		void PinWidgetCheckbox(bool* value);
		void PinWidgetCombo(i32* selectedIndex, const char* options);

		void Link(u64 id, u64 outputNodeId, u32 outputPinIndex, u64 inputNodeId, u32 inputPinIndex, ImColor color = ImColor(200, 200, 200));

		GraphEditorResult End();

		bool IsNodeSelected(u64 nodeId) const;
		bool IsLinkSelected(u64 linkId) const;
		void ClearSelection();
		void SetSelectedNodes(const Array<u64>& nodes);
		void SetSelectedLinks(const Array<u64>& links);

		void StartLinkFromNode(u64 nodeId);
		bool IsCreatingLink() const { return m_creatingLink; }

		const Array<u64>& GetSelectedNodes() const { return m_selectedNodes; }
		const Array<u64>& GetSelectedLinks() const { return m_selectedLinks; }

		const ImGuiEx::Canvas& GetCanvas() const { return m_canvas; }
		f32 GetZoom() const { return m_zoom; }
		f32 GetScale() const { return m_scale; }

		// Pan the canvas so the given node is centered in the viewport on the next End().
		void CenterOnNode(u64 nodeId) { m_pendingCenterNodeId = nodeId; m_hasPendingCenter = true; }

	private:
		struct FramePin
		{
			String					name = "";
			GraphPinType    type = GraphPinType::Value;
			ImColor         color = ImColor(150, 200, 150);
			GraphWidgetType widgetType = GraphWidgetType::None;
			f32*            floatPtr = nullptr;
			u32             floatCount = 1;
			f32             dragSpeed = 0.1f;
			bool*           boolPtr = nullptr;
			i32*            comboIndexPtr = nullptr;
			String          textBuffer = "";
			u32             textBufferSize = 0;
			const char*     comboOptions = nullptr;
		};

		struct FrameNode
		{
			u64           id = 0;
			String        name = "";
			Vec2          position{};
			Vec2          inputPosition{};
			Vec2          size{};
			GraphNodeDesc desc{};
			u32           inputStart = 0;
			u32           inputCount = 0;
			u32           outputStart = 0;
			u32           outputCount = 0;
			ImTextureID   thumbnail = 0;
		};

		struct FrameLink
		{
			u64     id = 0;
			u64     outputNodeId = 0;
			u32     outputPinIndex = 0;
			u64     inputNodeId = 0;
			u32     inputPinIndex = 0;
			ImColor color = ImColor(200, 200, 200);
		};

		FrameNode* FindNodeById(u64 id);
		const FrameNode* FindNodeById(u64 id) const;
		Vec2 GetPinPos(const FrameNode& node, GraphPinKind kind, u32 pinIndex) const;
		bool FindHoveredPin(u64& outNodeId, GraphPinKind& outKind, u32& outPinIndex) const;
		i32  FindHoveredNode() const;
		i32  FindHoveredLink() const;

		void ComputeNodeSize(FrameNode& node);
		void DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize) const;
		void DrawNode(ImDrawList* drawList, FrameNode& node);
		void DrawNodeText(ImDrawList* drawList, const FrameNode& node) const;
		void DrawNodeWidgets(FrameNode& node);
		void DrawLink(ImDrawList* drawList, const FrameLink& link, bool selected) const;
		void HandleInteraction(GraphEditorResult& result);
		f32  DistanceToLink(const FrameLink& link, const Vec2& point) const;

		void DrawStateMachineNode(ImDrawList* drawList, FrameNode& node);
		void DrawStateMachineNodeText(ImDrawList* drawList, const FrameNode& node) const;
		void DrawStateMachineLink(ImDrawList* drawList, const FrameLink& link, bool selected) const;
		Vec2 GetRectEdgePoint(const FrameNode& node, const Vec2& target) const;
		i32  CountLinksBetween(u64 nodeA, u64 nodeB) const;
		i32  GetLinkIndexBetween(const FrameLink& link, u64 nodeA, u64 nodeB) const;

		// Canvas
		ImGuiEx::Canvas m_canvas;
		Vec2  m_scrollOffset{0.0f, 0.0f};
		f32   m_zoom = 1.0f;
		f32   m_scale = 1.0f;
		f32   m_rowSpacing = 24.0f;

		// Per-frame accumulated data (cleared every Begin)
		Array<FrameNode> m_frameNodes{};
		Array<FramePin>  m_framePins{};
		Array<FrameLink> m_frameLinks{};
		i32              m_currentNodeIndex = -1;

		// Selection
		Array<u64> m_selectedNodes{};
		Array<u64> m_selectedLinks{};

		// Interaction state
		i32          m_draggedNode = -1;
		HashMap<u64, Vec2> m_dragOffsets{};
		HashMap<u64, Vec2> m_dragStartPositions{};

		// Selection change tracking
		Array<u64> m_prevSelectedNodes{};
		Array<u64> m_prevSelectedLinks{};

		// Box selection
		bool  m_boxSelecting = false;
		Vec2  m_boxSelectStart{};
		Vec2  m_boxSelectEnd{};

		// Link creation
		bool  m_creatingLink = false;
		u64   m_linkStartNodeId{};
		u32   m_linkStartPinIndex{};
		GraphPinKind m_linkStartKind{};

		// Canvas drag
		bool  m_draggingCanvas = false;
		Vec2  m_canvasDragStart{};

		// Deferred request to center a node in the viewport (applied in End once node sizes are known)
		bool  m_hasPendingCenter = false;
		u64   m_pendingCenterNodeId = 0;

		// Widget interaction tracking
		bool  m_nodeWidgetActive = false;
		bool  m_nodeWidgetHovered = false;

		// Pin value widget edit tracking (filled by DrawNodeWidgets, surfaced in GraphEditorResult)
		bool                      m_hasActiveValuePin = false;
		GraphEditorPinEdit        m_activeValuePin{};
		Array<GraphEditorPinEdit> m_committedPinValues{};

		// Constants
		static constexpr f32 PinRadius = 4.0f;
		static constexpr f32 NodeHeaderHeight = 22.0f;
		static constexpr f32 NodePadding = 10.0f;
		static constexpr f32 NodeRounding = 4.0f;
		static constexpr f32 NodeMinWidth = 200.0f;
		static constexpr f32 NodeThumbnailSize = 154.0f;
		static constexpr f32 LinkThickness = 2.5f;
		static constexpr f32 GridSize = 32.0f;
		static constexpr f32 ZoomMin = 0.1f;
		static constexpr f32 ZoomMax = 8.0f;
		static constexpr f32 LinkHitDistance = 8.0f;
		static constexpr f32 StateMachineRounding = 12.0f;
		static constexpr f32 StateMachineMinWidth = 120.0f;
		static constexpr f32 StateMachineMinHeight = 40.0f;
		static constexpr f32 ArrowSize = 8.0f;
		static constexpr f32 MultiLinkOffset = 12.0f;
	};
}
