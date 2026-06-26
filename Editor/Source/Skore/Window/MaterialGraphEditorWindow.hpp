#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/ImGui/GraphEditor.hpp"
#include "Skore/MaterialGraph/MaterialNode.hpp"

namespace Skore
{
	struct MenuItemEventData;

	class MaterialGraphEditorWindow : public EditorWindow
	{
	public:
		SK_CLASS(MaterialGraphEditorWindow, EditorWindow);

		const char* GetTitle() const override;
		void        Draw(bool& open) override;

		static void RegisterType(NativeReflectType<MaterialGraphEditorWindow>& type);

	private:
		GraphEditor m_editor{};

		RID CurrentGraph() const;

		Vec2 m_popupMousePos{};
		bool m_openPopup = false;
		RID  m_contextNode{};

		bool   m_showCode = false;
		String m_generatedHlsl{};
		String m_buildLog{};

		//transient state so an in-progress value drag isn't clobbered by per-frame resource reads
		bool m_editing = false;
		RID  m_editNode{};
		Vec4 m_editValue{};

		//backing store for the inline default-value widgets on unconnected input pins. Keyed by
		//PinKey(nodeId, pinIndex); the entry being dragged (m_activePinKey) keeps its live value
		//instead of being refreshed from the resource each frame.
		HashMap<u64, Vec4> m_pinValues{};
		u64                m_activePinKey = 0;

		static u64 PinKey(u64 nodeId, u32 pinIndex) { return (nodeId << 8) | pinIndex; }

		void DrawToolbar();
		void DrawGraph();
		void DrawValueInspector(RID node, MaterialNode* def);
		void DrawPinValueWidgets(RID node, MaterialNode* def, const HashSet<u64>& connectedPins);
		void CommitPinValue(RID node, u32 pinIndex, Vec4 value);
		void DrawCodePanel();

		void Build();
		void AddNode(MaterialNode* node, Vec2 position);
		void DeleteNode(RID node);
		void AddConnection(RID outputNode, u32 outputPin, RID inputNode, u32 inputPin);
		void RemoveConnection(RID connection);

		static MenuItemContext s_menu;
		static void AddNodeAction(const MenuItemEventData& eventData);
		static void DeleteNodeAction(const MenuItemEventData& eventData);
		static bool HasContextNode(const MenuItemEventData& eventData);
	};
}
