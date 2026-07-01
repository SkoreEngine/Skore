#pragma once

#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
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

		//backing store for the inline default-value widgets on unconnected input pins. Keyed by
		//PinKey(nodeId, pinIndex); the entry being dragged (m_activePinKey) keeps its live value
		//instead of being refreshed from the resource each frame.
		HashMap<u64, Vec4> m_pinValues{};
		u64                m_activePinKey = 0;

		//keeps the GPU texture caches for node thumbnails alive across frames (the global cache holds
		//only weak references). Keyed by texture RID; pruned each frame to what is still referenced.
		HashMap<RID, TextureResourceCachePtr> m_thumbnailCaches{};

		//mirrors the editor's single-node selection so a redundant event isn't fired every frame
		RID m_selectedNode{};

		//tracks the graph the view was last centered on, so the output node is centered once when a graph opens
		RID m_centeredGraph{};

		void CenterOnOutputNode();

		static u64 PinKey(u64 nodeId, u32 pinIndex) { return (nodeId << 8) | pinIndex; }

		void        DrawToolbar();
		void        DrawGraph();
		static bool IsOutputPinDisabled(MaterialNode* def, u32 pinIndex, MaterialGraphResource::GraphAlphaMode alphaMode, MaterialGraphResource::GraphShadingModel shadingModel);
		void        DrawPinValueWidgets(RID node, MaterialNode* def, const HashSet<u64>& connectedPins, MaterialGraphResource::GraphAlphaMode alphaMode, MaterialGraphResource::GraphShadingModel shadingModel);
		void        CommitPinValue(RID node, u32 pinIndex, Vec4 value);
		ImTextureID ResolveThumbnail(RID node, MaterialNode* def, HashSet<u64>& usedTextures);
		bool        NodeAcceptsTexture(RID node);
		void        SetNodeTexture(RID node, RID texture);
		void        HandleTextureDrop(const GraphEditorResult& result);
		void        DrawCodePanel();

		void Build();
		void AddNode(MaterialNode* node, Vec2 position);
		void AddTextureNode(Vec2 position, RID texture);
		void DeleteNode(RID node);
		void AddConnection(RID outputNode, u32 outputPin, RID inputNode, u32 inputPin);
		void RemoveConnection(RID connection);

		static MenuItemContext s_menu;
		static void AddNodeAction(const MenuItemEventData& eventData);
		static void DeleteNodeAction(const MenuItemEventData& eventData);
		static bool HasContextNode(const MenuItemEventData& eventData);
	};
}
