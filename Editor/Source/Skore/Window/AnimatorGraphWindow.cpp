#include "Skore/Window/AnimatorGraphWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/AnimatorEditor.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	MenuItemContext AnimatorGraphWindow::menuItemContext;

	const char* AnimatorGraphWindow::GetTitle() const
	{
		return ICON_FA_DIAGRAM_PROJECT " Animator Graph";
	}

	void AnimatorGraphWindow::Draw(bool& open)
	{
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (!ImGuiBegin(this, &open))
		{
			ImGui::End();
			return;
		}

		AnimatorEditor& editor = workspace->GetAnimatorEditor();
		RID layer = editor.GetSelectedLayer();

		// Sync graph editor selection from AnimatorEditor BEFORE Begin()
		// so the snapshot captures the authoritative state
		{
			Span<RID> selectedStates = editor.GetSelectedStates();
			Span<RID> selectedTransitions = editor.GetSelectedTransitions();

			Array<u64> nodes;
			Array<u64> links;
			for (RID s : selectedStates) nodes.EmplaceBack(s.id);
			for (RID t : selectedTransitions) links.EmplaceBack(t.id);
			m_editor.SetSelectedNodes(nodes);
			m_editor.SetSelectedLinks(links);
		}

		m_editor.Begin("animator_graph");

		bool hasLayer = false;

		if (layer)
		{
			if (ResourceObject layerObj = Resources::Read(layer))
			{
				hasLayer = true;

				RID defaultState = layerObj.GetReference(AnimationLayerResource::DefaultState);

				// Submit state nodes
				layerObj.IterateSubObjectList(AnimationLayerResource::States, [&](RID state)
				{
					ResourceObject stateObj = Resources::Read(state);
					if (!stateObj) return;

					RID animationRID = stateObj.GetReference(AnimationStateResource::Animation);
					Vec2 position = stateObj.GetVec2(AnimationStateResource::Position);
					String stateName = "Empty State";
					if (animationRID)
					{
						if (String assetName = ResourceAssets::GetAssetName(animationRID); !assetName.Empty())
						{
							stateName = assetName;
						}
					}

					bool isDefault = (state == defaultState);
					ImColor nodeColor = isDefault ? ImColor(200, 150, 50, 230) : ImColor(60, 80, 120, 230);

					m_editor.BeginNode(state.id, stateName.CStr(), position, GraphNodeDesc{
						.nodeType = GraphNodeType::StateMachine,
						.color = nodeColor
					});
					m_editor.EndNode();
				});

				// Submit transition links
				layerObj.IterateSubObjectList(AnimationLayerResource::Transitions, [&](RID transition)
				{
					ResourceObject transObj = Resources::Read(transition);
					if (!transObj) return;

					RID from = transObj.GetReference(AnimationTransitionResource::From);
					RID to = transObj.GetReference(AnimationTransitionResource::To);

					if (from && to)
					{
						m_editor.Link(transition.id, from.id, 0, to.id, 0, ImColor(200, 200, 200));
					}
				});
			}
		}

		GraphEditorResult result = m_editor.End();

		// Handle drag-drop of AnimationClipResource to create a new state
		if (hasLayer && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
			{
				if (AssetPayload* assetPayload = static_cast<AssetPayload*>(payload->Data))
				{
					if (assetPayload->asset && Resources::GetType(assetPayload->asset)->GetID() == TypeInfo<AnimationClipResource>::ID())
					{
						ImVec2 localPos = m_editor.GetCanvas().ToLocal(ImGui::GetMousePos());
						Vec2 dropPos = Vec2{localPos.x, localPos.y};

						UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Animation State");
						RID state = Resources::Create<AnimationStateResource>(UUID::RandomUUID(), scope);
						{
							ResourceObject stateObj = Resources::Write(state);
							stateObj.SetReference(AnimationStateResource::Animation, assetPayload->asset);
							stateObj.SetVec2(AnimationStateResource::Position, dropPos);
							stateObj.SetFloat(AnimationStateResource::Speed, 1.0f);
							stateObj.Commit(scope);
						}

						ResourceObject layerObj = Resources::Write(layer);
						layerObj.AddToSubObjectList(AnimationLayerResource::States, state);
						layerObj.Commit(scope);

						editor.SetSelection(Span<RID>(&state, 1), {}, scope);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (hasLayer)
		{
			// Handle new links created by the GraphEditor
			for (const auto& newLink : result.createdLinks)
			{
				RID fromState = {newLink.outputNodeId};
				RID toState = {newLink.inputNodeId};
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Transition");
				editor.AddTransition(layer, fromState, toState, scope);
			}

			// Handle moved nodes - batch into a single undo scope
			if (!result.movedNodes.Empty())
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move State");
				for (const auto& moved : result.movedNodes)
				{
					RID state = {moved.nodeId};
					ResourceObject stateObj = Resources::Write(state);
					stateObj.SetVec2(AnimationStateResource::Position, moved.position);
					stateObj.Commit(scope);
				}
			}

			// Handle deletions - batch into a single undo scope
			if (!result.deletedLinkIds.Empty() || !result.deletedNodeIds.Empty())
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Delete");
				editor.ClearSelection(scope);
				for (u64 linkId : result.deletedLinkIds)
				{
					RID transition = {linkId};
					editor.RemoveTransition(layer, transition, scope);
				}
				for (u64 nodeId : result.deletedNodeIds)
				{
					RID state = {nodeId};
					editor.RemoveState(layer, state, scope);
				}
			}

			// Track selection changes with undo/redo (skip if deletions already handled it)
			const Array<u64>& selectedNodes = m_editor.GetSelectedNodes();
			const Array<u64>& selectedLinks = m_editor.GetSelectedLinks();

			bool hadDeletions = !result.deletedLinkIds.Empty() || !result.deletedNodeIds.Empty();
			if (result.hasSelectionChanged && !hadDeletions)
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Selection Change");

				Array<RID> stateRIDs;
				Array<RID> transitionRIDs;
				for (u64 nodeId : selectedNodes) stateRIDs.EmplaceBack(RID{nodeId});
				for (u64 linkId : selectedLinks) transitionRIDs.EmplaceBack(RID{linkId});

				editor.SetSelection(stateRIDs, transitionRIDs, scope);
			}

			if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && result.deletedNodeIds.Empty())
			{
				menuItemContext.ExecuteHotKeys(this);
			}

			// Right-click popup
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
			{
				m_contextState = !selectedNodes.Empty() ? RID{selectedNodes[0]} : RID{};
				// Convert screen mouse position to canvas-local coordinates
				ImVec2 screenPos = ImGui::GetMousePos();
				ImVec2 localPos = m_editor.GetCanvas().ToLocal(screenPos);
				m_popupMousePos = Vec2{localPos.x, localPos.y};
				m_openPopup = true;
			}

			if (m_openPopup)
			{
				ImGui::OpenPopup("graph-popup");
				m_openPopup = false;
			}

			bool popupRes = ImGuiBeginPopupMenu("graph-popup");
			if (popupRes)
			{
				menuItemContext.Draw(this);
			}
			ImGuiEndPopupMenu(popupRes);
		}

		ImGui::End();
	}

	void AnimatorGraphWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	// --- Menu actions ---

	void AnimatorGraphWindow::NewEmptyState(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		RID layer = editor.GetSelectedLayer();
		if (layer)
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add State");
			editor.AddState(layer, window->m_popupMousePos, scope);
		}
	}

	void AnimatorGraphWindow::NewTransition(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		window->m_editor.StartLinkFromNode(window->m_contextState.id);
	}

	void AnimatorGraphWindow::SetAsDefaultState(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		RID layer = editor.GetSelectedLayer();
		if (layer)
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Set Default State");
			editor.SetDefaultState(layer, window->m_contextState, scope);
		}
	}

	void AnimatorGraphWindow::DuplicateState(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		RID layer = editor.GetSelectedLayer();
		if (layer)
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Duplicate State");
			editor.DuplicateState(layer, window->m_contextState, scope);
		}
	}

	void AnimatorGraphWindow::DeleteState(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		RID layer = editor.GetSelectedLayer();
		if (layer)
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Delete State");
			editor.RemoveState(layer, window->m_contextState, scope);
		}
	}

	bool AnimatorGraphWindow::HasContextState(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		return window->m_contextState.id != 0;
	}

	bool AnimatorGraphWindow::HasNoContextState(const MenuItemEventData& eventData)
	{
		AnimatorGraphWindow* window = static_cast<AnimatorGraphWindow*>(eventData.drawData);
		return window->m_contextState.id == 0;
	}

	void AnimatorGraphWindow::RegisterType(NativeReflectType<AnimatorGraphWindow>& type)
	{
		// State context menu (when right-clicking on a node)
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "New Transition", .icon = ICON_FA_ARROW_RIGHT, .priority = 0, .action = NewTransition, .visible = HasContextState});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "Set As Default State", .icon = ICON_FA_STAR, .priority = 50, .action = SetAsDefaultState, .visible = HasContextState});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "Duplicate State", .icon = ICON_FA_COPY, .priority = 100, .itemShortcut = {.ctrl = true, .presKey = Key::D}, .action = DuplicateState, .visible = HasContextState});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "Delete State", .priority = 200, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteState, .visible = HasContextState});

		// Empty space context menu (when right-clicking on empty area)
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "New Empty State", .icon = ICON_FA_PLUS, .priority = 0, .action = NewEmptyState, .visible = HasNoContextState});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::Center,
			.workspaceTypes = {WorkspaceTypes::Animator}
		});
	}
}
