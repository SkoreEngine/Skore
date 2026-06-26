#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
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
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void RegisterType(NativeReflectType<MaterialGraphEditorWindow>& type);

	private:
		GraphEditor m_editor{};
		RID         m_graph{};

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

		void DrawToolbar();
		void DrawGraph();
		void DrawValueInspector(RID node, const MaterialNodeDef& def);
		void DrawCodePanel();

		void Build();
		void AddNode(const MaterialNodeDef& def, Vec2 position);
		void DeleteNode(RID node);
		void AddConnection(RID outputNode, u32 outputPin, RID inputNode, u32 inputPin);
		void RemoveConnection(RID connection);

		static MenuItemContext s_menu;
		static void AddNodeAction(const MenuItemEventData& eventData);
		static void DeleteNodeAction(const MenuItemEventData& eventData);
		static bool HasContextNode(const MenuItemEventData& eventData);
	};
}
