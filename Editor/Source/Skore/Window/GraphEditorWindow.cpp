#include "Skore/Window/GraphEditorWindow.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "imgui.h"

namespace Skore
{
	const char* GraphEditorWindow::GetTitle() const
	{
		return ICON_FA_DIAGRAM_PROJECT " Graph Editor";
	}

	void GraphEditorWindow::Init(VoidPtr userData)
	{
		// Add some test nodes
		{
			GraphNode node{};
			node.id = AllocateId();
			node.name = "InputAction Fire";
			node.position = {50.0f, 100.0f};
			node.headerColor = ImColor(255, 128, 128);
			node.outputs.EmplaceBack(GraphPin{"Pressed", GraphPinType::Flow});
			node.outputs.EmplaceBack(GraphPin{"Released", GraphPinType::Flow});
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			node.name = "For";
			node.position = {300.0f, 80.0f};
			node.headerColor = ImColor(128, 195, 248);
			node.inputs.EmplaceBack(GraphPin{"", GraphPinType::Flow});

			node.outputs.EmplaceBack(GraphPin{"End", GraphPinType::Flow});
			node.outputs.EmplaceBack(GraphPin{"Loop", GraphPinType::Flow});
			node.outputs.EmplaceBack(GraphPin{"Index", GraphPinType::Value, ImColor(147, 226, 74)});

			node.inputs.EmplaceBack(GraphPin{"Start", GraphPinType::Value, ImColor(147, 226, 74), GraphWidgetType::InputFloat});
			node.inputs.EmplaceBack(GraphPin{"End", GraphPinType::Value, ImColor(147, 226, 74), GraphWidgetType::InputFloat});
			node.inputs.EmplaceBack(GraphPin{"Step", GraphPinType::Value, ImColor(147, 226, 74), GraphWidgetType::InputFloat});

			m_nodes.EmplaceBack(Traits::Move(node));
			m_nodes.Back().inputWidgetStates.Resize(4);
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			node.name = "Print String";
			node.position = {550.0f, 60.0f};
			node.headerColor = ImColor(128, 195, 248);
			node.inputs.EmplaceBack(GraphPin{"", GraphPinType::Flow});
			node.inputs.EmplaceBack(GraphPin{"In String", GraphPinType::Value, ImColor(124, 21, 153), GraphWidgetType::InputText});
			node.outputs.EmplaceBack(GraphPin{"", GraphPinType::Flow});
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			node.name = "Set Timer";
			node.position = {550.0f, 250.0f};
			node.headerColor = ImColor(128, 195, 248);
			node.inputs.EmplaceBack(GraphPin{"", GraphPinType::Flow});
			node.inputs.EmplaceBack(GraphPin{"Object", GraphPinType::Value, ImColor(51, 150, 215), GraphWidgetType::Combo});
			node.inputs.EmplaceBack(GraphPin{"Time", GraphPinType::Value, ImColor(147, 226, 74), GraphWidgetType::InputFloat});
			node.inputs.EmplaceBack(GraphPin{"Looping", GraphPinType::Value, ImColor(220, 48, 48), GraphWidgetType::Checkbox});
			node.outputs.EmplaceBack(GraphPin{"", GraphPinType::Flow});
			m_nodes.EmplaceBack(Traits::Move(node));

			m_nodes.Back().inputWidgetStates.Resize(4);
			auto& comboState = m_nodes.Back().inputWidgetStates[1];
			comboState.comboOptions = "Self;Player;Enemy;World";
			comboState.comboIndex = 0;

			m_nodes.Back().inputWidgetStates[2].floatValue = 2.0f;
		}

		// State machine test nodes
		u64 idleId, runId, jumpId, fallId, landId, deathId;
		{
			GraphNode node{};
			node.id = AllocateId();
			idleId = node.id;
			node.nodeType = GraphNodeType::StateMachine;
			node.name = "Idle";
			node.position = {850.0f, 150.0f};
			node.color = ImColor(60, 80, 120, 230);
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			runId = node.id;
			node.nodeType = GraphNodeType::StateMachine;
			node.name = "Run";
			node.position = {1100.0f, 150.0f};
			node.color = ImColor(80, 120, 60, 230);
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			jumpId = node.id;
			node.nodeType = GraphNodeType::StateMachine;
			node.name = "Jump";
			node.position = {1350.0f, 150.0f};
			node.color = ImColor(120, 100, 50, 230);
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			fallId = node.id;
			node.nodeType = GraphNodeType::StateMachine;
			node.name = "Fall";
			node.position = {1350.0f, 320.0f};
			node.color = ImColor(100, 60, 100, 230);
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			landId = node.id;
			node.nodeType = GraphNodeType::StateMachine;
			node.name = "Land";
			node.position = {1100.0f, 320.0f};
			node.color = ImColor(70, 100, 100, 230);
			m_nodes.EmplaceBack(Traits::Move(node));
		}
		{
			GraphNode node{};
			node.id = AllocateId();
			deathId = node.id;
			node.nodeType = GraphNodeType::StateMachine;
			node.name = "Death";
			node.position = {975.0f, 500.0f};
			node.color = ImColor(140, 50, 50, 230);
			m_nodes.EmplaceBack(Traits::Move(node));
		}

		// State machine links
		auto addSmLink = [&](u64 from, u64 to)
		{
			GraphLink link{};
			link.id = AllocateId();
			link.outputNodeId = from;
			link.outputPinIndex = 0;
			link.inputNodeId = to;
			link.inputPinIndex = 0;
			link.color = ImColor(200, 200, 200);
			m_links.EmplaceBack(link);
		};

		addSmLink(idleId, runId);
		addSmLink(runId, idleId);
		addSmLink(runId, jumpId);
		addSmLink(idleId, jumpId);
		addSmLink(jumpId, fallId);
		addSmLink(fallId, landId);
		addSmLink(landId, idleId);
		addSmLink(landId, runId);
		addSmLink(runId, landId);
		addSmLink(idleId, deathId);
		addSmLink(runId, deathId);
	}

	void GraphEditorWindow::Draw(bool& open)
	{
		ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (!ImGuiBegin(this, &open))
		{
			ImGui::End();
			return;
		}

		m_editor.Begin("graph_editor");

		// Submit nodes
		for (auto& node : m_nodes)
		{
			// Ensure widget states array is large enough
			while (node.inputWidgetStates.Size() < node.inputs.Size())
			{
				node.inputWidgetStates.EmplaceBack(GraphNodeWidgetState{});
			}

			m_editor.BeginNode(node.id, node.name.CStr(), node.position, GraphNodeDesc{
				.nodeType = node.nodeType,
				.color = node.color,
				.headerColor = node.headerColor,
				.rounding = node.rounding
			});

			for (u32 i = 0; i < node.inputs.Size(); i++)
			{
				const GraphPin& pin = node.inputs[i];
				m_editor.InputPin(pin.name.CStr(), pin.type, pin.color);

				GraphNodeWidgetState& state = node.inputWidgetStates[i];
				switch (pin.widgetType)
				{
					case GraphWidgetType::InputFloat:
						m_editor.PinWidgetDragFloat(&state.floatValue);
						break;
					case GraphWidgetType::InputText:
						m_editor.PinWidgetInputText(state.textBuffer, sizeof(state.textBuffer));
						break;
					case GraphWidgetType::Checkbox:
						m_editor.PinWidgetCheckbox(&state.boolValue);
						break;
					case GraphWidgetType::Combo:
						m_editor.PinWidgetCombo(&state.comboIndex, state.comboOptions.CStr());
						break;
					default:
						break;
				}
			}

			for (const auto& pin : node.outputs)
			{
				m_editor.OutputPin(pin.name.CStr(), pin.type, pin.color);
			}

			m_editor.EndNode();
		}

		// Submit links
		for (const auto& link : m_links)
		{
			m_editor.Link(link.id, link.outputNodeId, link.outputPinIndex, link.inputNodeId, link.inputPinIndex, link.color);
		}

		GraphEditorResult result = m_editor.End();

		// Handle created links
		for (const auto& newLink : result.createdLinks)
		{
			GraphLink link{};
			link.id = AllocateId();
			link.outputNodeId = newLink.outputNodeId;
			link.outputPinIndex = newLink.outputPinIndex;
			link.inputNodeId = newLink.inputNodeId;
			link.inputPinIndex = newLink.inputPinIndex;
			m_links.EmplaceBack(link);
		}

		// Handle moved nodes
		for (const auto& moved : result.movedNodes)
		{
			for (auto& node : m_nodes)
			{
				if (node.id == moved.nodeId)
				{
					node.position = moved.position;
					break;
				}
			}
		}

		// Handle deletions
		if (!result.deletedNodeIds.Empty() || !result.deletedLinkIds.Empty())
		{
			// Remove deleted links
			for (u64 linkId : result.deletedLinkIds)
			{
				for (i32 i = (i32)m_links.Size() - 1; i >= 0; i--)
				{
					if (m_links[i].id == linkId)
					{
						m_links.RemoveAt(i);
						break;
					}
				}
			}

			// Remove links connected to deleted nodes, then remove nodes
			for (u64 nodeId : result.deletedNodeIds)
			{
				for (i32 i = (i32)m_links.Size() - 1; i >= 0; i--)
				{
					if (m_links[i].outputNodeId == nodeId || m_links[i].inputNodeId == nodeId)
					{
						m_links.RemoveAt(i);
					}
				}
			}

			for (u64 nodeId : result.deletedNodeIds)
			{
				for (i32 i = (i32)m_nodes.Size() - 1; i >= 0; i--)
				{
					if (m_nodes[i].id == nodeId)
					{
						m_nodes.RemoveAt(i);
						break;
					}
				}
			}
		}

		ImGui::End();
	}

	u64 GraphEditorWindow::AllocateId()
	{
		return m_nextId++;
	}

	void GraphEditorWindow::RegisterType(NativeReflectType<GraphEditorWindow>& type)
	{
		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::Center,
			.order = 20,
			.workspaceTypes = {WorkspaceTypes::Graph}
		});
	}

	void GraphEditorWindow::OpenGraphEditorWindow(const MenuItemEventData& eventData)
	{
		Editor::GetActiveWorkspace()->OpenWindow<GraphEditorWindow>();
	}
}
