#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/ImGui/GraphEditor.hpp"

namespace Skore
{
	struct MenuItemEventData;

	struct GraphNodeWidgetState
	{
		f32    floatValue = 0.0f;
		bool   boolValue = false;
		i32    comboIndex = 0;
		char   textBuffer[128] = {};
		String comboOptions{};  // semicolon-separated
	};

	struct GraphPin
	{
		String           name{};
		GraphPinType     type = GraphPinType::Value;
		ImColor          color = ImColor(150, 200, 150);
		GraphWidgetType  widgetType = GraphWidgetType::None;
	};

	struct GraphNode
	{
		u64              id{};
		GraphNodeType    nodeType = GraphNodeType::Blueprint;
		String           name{};
		Vec2             position{};
		ImColor          color = ImColor(41, 41, 43, 230);
		ImColor          headerColor = ImColor(128, 195, 248);
		f32              rounding = -1.0f;
		Array<GraphPin>              inputs{};
		Array<GraphPin>              outputs{};
		Array<GraphNodeWidgetState>  inputWidgetStates{};
	};

	struct GraphLink
	{
		u64     id{};
		u64     outputNodeId{};
		u32     outputPinIndex{};
		u64     inputNodeId{};
		u32     inputPinIndex{};
		ImColor color = ImColor(200, 200, 200);
	};

	class GraphEditorWindow : public EditorWindow
	{
	public:
		SK_CLASS(GraphEditorWindow, EditorWindow);

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void RegisterType(NativeReflectType<GraphEditorWindow>& type);
		static void OpenGraphEditorWindow(const MenuItemEventData& eventData);

	private:
		u64 AllocateId();

		GraphEditor      m_editor{};
		Array<GraphNode>  m_nodes{};
		Array<GraphLink>  m_links{};
		u64               m_nextId = 1;
	};
}
