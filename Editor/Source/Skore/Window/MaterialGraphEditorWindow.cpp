#include "Skore/Window/MaterialGraphEditorWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/MaterialGraph/MaterialGraphCompiler.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/Resources.hpp"
#include "imgui.h"

namespace Skore
{
	MenuItemContext MaterialGraphEditorWindow::s_menu;

	namespace
	{
		ImColor PinColor(MaterialDataType type)
		{
			switch (type)
			{
				case MaterialDataType::Float: return ImColor(160, 160, 160);
				case MaterialDataType::Vec2:  return ImColor(220, 220, 90);
				case MaterialDataType::Vec3:  return ImColor(120, 160, 230);
				case MaterialDataType::Vec4:  return ImColor(200, 120, 220);
			}
			return ImColor(150, 200, 150);
		}
	}

	const char* MaterialGraphEditorWindow::GetTitle() const
	{
		return ICON_FA_DIAGRAM_PROJECT " Material Graph";
	}

	void MaterialGraphEditorWindow::Init(VoidPtr userData)
	{
		if (userData)
		{
			m_graph = *static_cast<RID*>(userData);
		}
	}

	void MaterialGraphEditorWindow::Draw(bool& open)
	{
		ImGui::SetNextWindowSize(ImVec2(960, 600), ImGuiCond_FirstUseEver);
		if (!ImGuiBegin(this, &open))
		{
			ImGui::End();
			return;
		}

		if (!m_graph)
		{
			ImGui::TextDisabled("No material graph open.");
			ImGui::End();
			return;
		}

		DrawToolbar();
		ImGui::Separator();

		if (m_showCode)
		{
			ImVec2 avail = ImGui::GetContentRegionAvail();
			ImGui::BeginChild("##mg_graph", ImVec2(avail.x * 0.6f, 0.0f), false);
			DrawGraph();
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("##mg_code", ImVec2(0.0f, 0.0f), true);
			DrawCodePanel();
			ImGui::EndChild();
		}
		else
		{
			DrawGraph();
		}

		ImGui::End();
	}

	void MaterialGraphEditorWindow::DrawToolbar()
	{
		if (ImGui::Button(ICON_FA_HAMMER " Build"))
		{
			Build();
		}
		ImGui::SameLine();
		ImGui::Checkbox(ICON_FA_CODE " Show HLSL", &m_showCode);

		const Array<u64>& selected = m_editor.GetSelectedNodes();
		if (selected.Size() == 1)
		{
			RID node = RID{selected[0]};
			if (ResourceObject nodeObj = Resources::Read(node))
			{
				MaterialNode* def = MaterialNodeRegistry::Find(nodeObj.GetString(MaterialGraphNodeResource::Type));
				if (def && def->GetValueKind() != MaterialNodeValueKind::None)
				{
					ImGui::SameLine();
					ImGui::TextUnformatted(def->GetDisplayName().CStr());
					ImGui::SameLine();
					DrawValueInspector(node, def);
				}
			}
		}
	}

	void MaterialGraphEditorWindow::DrawValueInspector(RID node, MaterialNode* def)
	{
		ResourceObject nodeObj = Resources::Read(node);
		if (!nodeObj)
		{
			return;
		}

		Vec4 v = (m_editing && m_editNode == node) ? m_editValue : nodeObj.GetVec4(MaterialGraphNodeResource::Value);

		bool changed = false;
		ImGui::SetNextItemWidth(220.0f);
		switch (def->GetValueKind())
		{
			case MaterialNodeValueKind::Float:
				changed = ImGui::DragFloat("##mg_value", &v.x, 0.01f);
				break;
			case MaterialNodeValueKind::Vec2:
				changed = ImGui::DragFloat2("##mg_value", &v.x, 0.01f);
				break;
			case MaterialNodeValueKind::Color:
				changed = ImGui::ColorEdit3("##mg_value", &v.x);
				break;
			default:
				return;
		}

		if (changed)
		{
			m_editing = true;
			m_editNode = node;
			m_editValue = v;
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Edit Node Value");
			ResourceObject write = Resources::Write(node);
			write.SetVec4(MaterialGraphNodeResource::Value, v);
			write.Commit(scope);
			m_editing = false;
		}
	}

	void MaterialGraphEditorWindow::DrawGraph()
	{
		m_editor.Begin("material_graph");

		if (ResourceObject graphObj = Resources::Read(m_graph))
		{
			graphObj.IterateSubObjectList(MaterialGraphResource::Nodes, [&](RID nodeRid)
			{
				ResourceObject nodeObj = Resources::Read(nodeRid);
				if (!nodeObj)
				{
					return;
				}

				MaterialNode* def = MaterialNodeRegistry::Find(nodeObj.GetString(MaterialGraphNodeResource::Type));
				if (!def)
				{
					return;
				}

				MaterialNodeColor color = def->GetHeaderColor();
				Vec2              position = nodeObj.GetVec2(MaterialGraphNodeResource::Position);
				m_editor.BeginNode(nodeRid.id, def->GetDisplayName().CStr(), position, GraphNodeDesc{
					.headerColor = ImColor(color.r, color.g, color.b)
				});

				for (const MaterialNodePin& pin : def->GetInputs())
				{
					m_editor.InputPin(pin.name.CStr(), GraphPinType::Value, PinColor(pin.type));
				}
				for (const MaterialNodePin& pin : def->GetOutputs())
				{
					m_editor.OutputPin(pin.name.CStr(), GraphPinType::Value, PinColor(pin.type));
				}

				m_editor.EndNode();
			});

			graphObj.IterateSubObjectList(MaterialGraphResource::Connections, [&](RID connRid)
			{
				ResourceObject connObj = Resources::Read(connRid);
				if (!connObj)
				{
					return;
				}

				RID outNode = connObj.GetReference(MaterialGraphConnectionResource::OutputNode);
				RID inNode = connObj.GetReference(MaterialGraphConnectionResource::InputNode);
				if (outNode && inNode)
				{
					m_editor.Link(connRid.id,
					              outNode.id, static_cast<u32>(connObj.GetUInt(MaterialGraphConnectionResource::OutputPin)),
					              inNode.id, static_cast<u32>(connObj.GetUInt(MaterialGraphConnectionResource::InputPin)));
				}
			});
		}

		GraphEditorResult result = m_editor.End();

		for (const GraphEditorNewLink& newLink : result.createdLinks)
		{
			AddConnection(RID{newLink.outputNodeId}, newLink.outputPinIndex, RID{newLink.inputNodeId}, newLink.inputPinIndex);
		}

		if (!result.movedNodes.Empty())
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move Node");
			for (const GraphEditorNodeMoved& moved : result.movedNodes)
			{
				ResourceObject nodeObj = Resources::Write(RID{moved.nodeId});
				nodeObj.SetVec2(MaterialGraphNodeResource::Position, moved.position);
				nodeObj.Commit(scope);
			}
		}

		for (u64 linkId : result.deletedLinkIds)
		{
			RemoveConnection(RID{linkId});
		}

		for (u64 nodeId : result.deletedNodeIds)
		{
			DeleteNode(RID{nodeId});
		}

		// Right-click context menu (add / delete nodes)
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			const Array<u64>& selected = m_editor.GetSelectedNodes();
			m_contextNode = !selected.Empty() ? RID{selected[0]} : RID{};
			ImVec2 local = m_editor.GetCanvas().ToLocal(ImGui::GetMousePos());
			m_popupMousePos = Vec2{local.x, local.y};
			m_openPopup = true;
		}

		if (m_openPopup)
		{
			ImGui::OpenPopup("matgraph-popup");
			m_openPopup = false;
		}

		bool popupRes = ImGuiBeginPopupMenu("matgraph-popup");
		if (popupRes)
		{
			s_menu.Draw(this);
		}
		ImGuiEndPopupMenu(popupRes);
	}

	void MaterialGraphEditorWindow::DrawCodePanel()
	{
		ImGui::TextUnformatted("Generated HLSL");
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGuiInputTextMultiline(1001u, m_generatedHlsl, ImVec2(-1.0f, avail.y * 0.6f), ImGuiInputTextFlags_ReadOnly);

		ImGui::TextUnformatted("Build Log");
		ImGuiInputTextMultiline(1002u, m_buildLog, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
	}

	void MaterialGraphEditorWindow::Build()
	{
		MaterialGraphCompileResult result = MaterialGraphCompiler::Compile(m_graph);
		m_generatedHlsl = result.hlsl;
		m_buildLog = result.log;
		m_showCode = true;
	}

	void MaterialGraphEditorWindow::AddNode(MaterialNode* def, Vec2 position)
	{
		if (!def)
		{
			return;
		}

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Material Node");
		RID node = Resources::Create<MaterialGraphNodeResource>(UUID::RandomUUID(), scope);
		{
			ResourceObject nodeObj = Resources::Write(node);
			nodeObj.SetString(MaterialGraphNodeResource::Type, def->GetNodeTypeId());
			nodeObj.SetVec2(MaterialGraphNodeResource::Position, position);
			nodeObj.SetVec4(MaterialGraphNodeResource::Value, def->GetDefaultValue());
			nodeObj.Commit(scope);
		}

		ResourceObject graphObj = Resources::Write(m_graph);
		graphObj.AddToSubObjectList(MaterialGraphResource::Nodes, node);
		graphObj.Commit(scope);
	}

	void MaterialGraphEditorWindow::DeleteNode(RID node)
	{
		if (ResourceObject nodeObj = Resources::Read(node))
		{
			if (nodeObj.GetString(MaterialGraphNodeResource::Type) == MaterialNodeRegistry::OutputTypeId())
			{
				return; //the output node is permanent
			}
		}
		else
		{
			return;
		}

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Delete Material Node");

		Array<RID> staleConnections;
		if (ResourceObject graphObj = Resources::Read(m_graph))
		{
			graphObj.IterateSubObjectList(MaterialGraphResource::Connections, [&](RID conn)
			{
				ResourceObject connObj = Resources::Read(conn);
				if (connObj && (connObj.GetReference(MaterialGraphConnectionResource::OutputNode) == node
				             || connObj.GetReference(MaterialGraphConnectionResource::InputNode) == node))
				{
					staleConnections.EmplaceBack(conn);
				}
			});
		}

		ResourceObject graphObj = Resources::Write(m_graph);
		for (RID conn : staleConnections)
		{
			graphObj.RemoveFromSubObjectList(MaterialGraphResource::Connections, conn);
		}
		graphObj.RemoveFromSubObjectList(MaterialGraphResource::Nodes, node);
		graphObj.Commit(scope);
	}

	void MaterialGraphEditorWindow::AddConnection(RID outputNode, u32 outputPin, RID inputNode, u32 inputPin)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Connect Material Nodes");

		Array<RID> staleConnections;
		if (ResourceObject graphObj = Resources::Read(m_graph))
		{
			graphObj.IterateSubObjectList(MaterialGraphResource::Connections, [&](RID conn)
			{
				ResourceObject connObj = Resources::Read(conn);
				if (connObj
				    && connObj.GetReference(MaterialGraphConnectionResource::InputNode) == inputNode
				    && static_cast<u32>(connObj.GetUInt(MaterialGraphConnectionResource::InputPin)) == inputPin)
				{
					staleConnections.EmplaceBack(conn);
				}
			});
		}

		RID conn = Resources::Create<MaterialGraphConnectionResource>(UUID::RandomUUID(), scope);
		{
			ResourceObject connObj = Resources::Write(conn);
			connObj.SetReference(MaterialGraphConnectionResource::OutputNode, outputNode);
			connObj.SetUInt(MaterialGraphConnectionResource::OutputPin, outputPin);
			connObj.SetReference(MaterialGraphConnectionResource::InputNode, inputNode);
			connObj.SetUInt(MaterialGraphConnectionResource::InputPin, inputPin);
			connObj.Commit(scope);
		}

		ResourceObject graphObj = Resources::Write(m_graph);
		for (RID stale : staleConnections)
		{
			graphObj.RemoveFromSubObjectList(MaterialGraphResource::Connections, stale);
		}
		graphObj.AddToSubObjectList(MaterialGraphResource::Connections, conn);
		graphObj.Commit(scope);
	}

	void MaterialGraphEditorWindow::RemoveConnection(RID connection)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Disconnect Material Nodes");
		ResourceObject graphObj = Resources::Write(m_graph);
		graphObj.RemoveFromSubObjectList(MaterialGraphResource::Connections, connection);
		graphObj.Commit(scope);
	}

	void MaterialGraphEditorWindow::AddNodeAction(const MenuItemEventData& eventData)
	{
		MaterialGraphEditorWindow* window = static_cast<MaterialGraphEditorWindow*>(eventData.drawData);
		Span<MaterialNode*>        nodes = MaterialNodeRegistry::GetNodes();
		if (eventData.userData < nodes.Size())
		{
			window->AddNode(nodes[eventData.userData], window->m_popupMousePos);
		}
	}

	void MaterialGraphEditorWindow::DeleteNodeAction(const MenuItemEventData& eventData)
	{
		MaterialGraphEditorWindow* window = static_cast<MaterialGraphEditorWindow*>(eventData.drawData);
		if (window->m_contextNode)
		{
			window->DeleteNode(window->m_contextNode);
		}
	}

	bool MaterialGraphEditorWindow::HasContextNode(const MenuItemEventData& eventData)
	{
		MaterialGraphEditorWindow* window = static_cast<MaterialGraphEditorWindow*>(eventData.drawData);
		return window->m_contextNode.id != 0;
	}

	void MaterialGraphEditorWindow::RegisterType(NativeReflectType<MaterialGraphEditorWindow>& type)
	{
		Span<MaterialNode*> nodes = MaterialNodeRegistry::GetNodes();
		for (u32 i = 0; i < nodes.Size(); ++i)
		{
			MaterialNode* node = nodes[i];
			if (node->IsOutput())
			{
				continue;
			}

			StringView category = node->GetCategory();
			String     path = category.Empty()
				                  ? (String{"Add Node/"} + node->GetDisplayName())
				                  : (String{"Add Node/"} + category + "/" + node->GetDisplayName());

			s_menu.AddMenuItem(MenuItemCreation{
				.itemName = path,
				.icon = ICON_FA_PLUS,
				.priority = static_cast<i32>(i),
				.action = AddNodeAction,
				.userData = i
			});
		}

		s_menu.AddMenuItem(MenuItemCreation{
			.itemName = "Delete Node",
			.icon = ICON_FA_TRASH,
			.priority = 1000,
			.itemShortcut = {.presKey = Key::Delete},
			.action = DeleteNodeAction,
			.visible = HasContextNode
		});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::Center,
			.workspaceTypes = {WorkspaceTypes::All}
		});
	}
}
