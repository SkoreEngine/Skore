#include "Skore/Window/AnimatorTreeViewWindow.hpp"

#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/AnimatorEditor.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceType.hpp"

namespace Skore
{
	MenuItemContext AnimatorTreeViewWindow::menuItemContext;

	struct AnimatorTreeWindowState
	{
		enum
		{
			SelectedItem
		};
	};

	const char* AnimatorTreeViewWindow::GetTitle() const
	{
		return ICON_FA_LIST " Animator Tree";
	}

	void AnimatorTreeViewWindow::Init(VoidPtr userData)
	{
		m_windowState = Resources::Create<AnimatorTreeWindowState>();
		Resources::FindType<AnimatorTreeWindowState>()->RegisterEvent(ResourceEventType::Changed, OnTreeSelectionChange, this);
	}

	AnimatorTreeViewWindow::~AnimatorTreeViewWindow()
	{
		Resources::FindType<AnimatorTreeWindowState>()->UnregisterEvent(ResourceEventType::Changed, OnTreeSelectionChange, this);
		Resources::Destroy(m_windowState);
	}

	void AnimatorTreeViewWindow::SelectItem(RID item, UndoRedoScope* scope)
	{
		if (GetSelectedItem() == item) return;
		if (!scope) scope = Editor::CreateUndoRedoScope("Select Tree Item");

		ResourceObject state = Resources::Write(m_windowState);
		state.SetReference(AnimatorTreeWindowState::SelectedItem, item);
		state.Commit(scope);
	}

	RID AnimatorTreeViewWindow::GetSelectedItem() const
	{
		ResourceObject state = Resources::Read(m_windowState);
		return state.GetReference(AnimatorTreeWindowState::SelectedItem);
	}

	bool AnimatorTreeViewWindow::IsSelected(RID item) const
	{
		return GetSelectedItem() == item;
	}

	RID AnimatorTreeViewWindow::GetActionTarget() const
	{
		return m_contextItem ? m_contextItem : GetSelectedItem();
	}

	static constexpr u64 LayersHeaderID = 0xFFFF0001;
	static constexpr u64 ParamsHeaderID = 0xFFFF0002;
	static constexpr u64 AvatarsHeaderID = 0xFFFF0003;

	bool AnimatorTreeViewWindow::IsItemLayer(RID item) const
	{
		if (!item) return false;
		if (item.id == LayersHeaderID) return true;
		if (item.id == ParamsHeaderID || item.id == AvatarsHeaderID) return false;
		ResourceType* type = Resources::GetType(item);
		return type && type->GetID() == TypeInfo<AnimationLayerResource>::ID();
	}

	bool AnimatorTreeViewWindow::IsItemParameter(RID item) const
	{
		if (!item) return false;
		if (item.id == ParamsHeaderID) return true;
		if (item.id == LayersHeaderID || item.id == AvatarsHeaderID) return false;
		ResourceType* type = Resources::GetType(item);
		return type && type->GetID() == TypeInfo<AnimationParameterResource>::ID();
	}

	bool AnimatorTreeViewWindow::IsItemAvatar(RID item) const
	{
		if (!item) return false;
		if (item.id == AvatarsHeaderID) return true;
		if (item.id == LayersHeaderID || item.id == ParamsHeaderID) return false;
		ResourceType* type = Resources::GetType(item);
		return type && type->GetID() == TypeInfo<AnimationAvatarResource>::ID();
	}

	void AnimatorTreeViewWindow::OnTreeSelectionChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(userData);
		if (!newValue) return;
		if (newValue.GetRID() != window->m_windowState) return;

		RID selectedItem = newValue.GetReference(AnimatorTreeWindowState::SelectedItem);

		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		RID controller = editor.GetController();
		if (controller)
		{
			if (ResourceObject controllerObj = Resources::Read(controller))
			{
				Span<RID> layers = controllerObj.GetSubObjectList(AnimationControllerResource::Layers);
				for (RID layer : layers)
				{
					if (layer == selectedItem && editor.GetSelectedLayer() != selectedItem)
					{
						editor.SelectLayer(selectedItem);
						return;
					}
				}
			}
		}

		// Fire resource selection event for properties window (skip header IDs)
		RID resourceForProperties = selectedItem;
		if (selectedItem.id == LayersHeaderID || selectedItem.id == ParamsHeaderID || selectedItem.id == AvatarsHeaderID)
		{
			resourceForProperties = {};
		}
		EventHandler<OnResourceSelection>{}.Invoke(window->workspace->GetId(), resourceForProperties);
	}

	void AnimatorTreeViewWindow::Draw(bool& open)
	{
		if (!ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		AnimatorEditor& editor = workspace->GetAnimatorEditor();
		RID controller = editor.GetController();

		if (!controller)
		{
			ImGui::TextDisabled("No Animation Controller open");
			ImGui::End();
			return;
		}

		ResourceObject controllerObj = Resources::Read(controller);
		if (!controllerObj)
		{
			ImGui::End();
			return;
		}

		String controllerName = controllerObj.GetString(AnimationControllerResource::Name);

		ImGui::BeginChild("animator-tree-scroll", ImVec2(0, 0), false);

		m_newSelection = false;

		ImGuiBeginTreeNodeStyle();

		ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
		if (IsSelected(controller))
		{
			rootFlags |= ImGuiTreeNodeFlags_Selected;
		}

		bool rootOpen = ImGuiTreeNode(reinterpret_cast<VoidPtr>((usize)controller.id), (String(ICON_FA_DIAGRAM_PROJECT " ") + controllerName).CStr(), rootFlags);

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			SelectItem(controller);
			m_newSelection = true;
		}

		if (rootOpen)
		{
			DrawLayers(editor, controller);
			DrawParameters(editor, controller);
			DrawAvatars(editor, controller);
			ImGui::TreePop();
		}

		ImGuiEndTreeNodeStyle();

		// Single popup for the whole tree
		if (m_openPopup)
		{
			ImGui::OpenPopup("tree-popup");
			m_openPopup = false;
		}

		bool popupRes = ImGuiBeginPopupMenu("tree-popup");
		if (popupRes)
		{
			menuItemContext.Draw(this);
		}
		ImGuiEndPopupMenu(popupRes);

		// Deselect when clicking empty space — only when this window is hovered
		if (!m_newSelection && !popupRes && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			if (GetSelectedItem())
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Deselect");
				ResourceObject state = Resources::Write(m_windowState);
				state.SetReference(AnimatorTreeWindowState::SelectedItem, RID{});
				state.Commit(scope);
			}
		}

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		{
			menuItemContext.ExecuteHotKeys(this);
		}

		ImGui::EndChild();
		ImGui::End();
	}

	void AnimatorTreeViewWindow::DrawLayers(AnimatorEditor& editor, RID controller)
	{
		ResourceObject controllerObj = Resources::Read(controller);
		if (!controllerObj) return;

		ImGuiTreeNodeFlags layersFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;

		if (m_forceOpenLayers)
		{
			ImGui::SetNextItemOpen(true);
			m_forceOpenLayers = false;
		}

		RID layersHeaderId = {LayersHeaderID};
		if (IsSelected(layersHeaderId))
		{
			layersFlags |= ImGuiTreeNodeFlags_Selected;
		}

		bool layersOpen = ImGuiTreeNode(reinterpret_cast<VoidPtr>((usize)LayersHeaderID), ICON_FA_LAYER_GROUP " Layers", layersFlags);

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			SelectItem(layersHeaderId);
			m_newSelection = true;
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_contextItem = {};
			m_contextSection = ContextSection::Layer;
			m_openPopup = true;
		}

		if (layersOpen)
		{
			controllerObj.IterateSubObjectList(AnimationControllerResource::Layers, [&](RID layer)
			{
				ResourceObject layerObj = Resources::Read(layer);
				if (!layerObj) return;

				String layerName = layerObj.GetString(AnimationLayerResource::Name);

				// Renaming mode
				if (m_renamingItem && m_renamingItemRID == layer)
				{
					if (!m_renamingFocus)
					{
						m_renamingCache = layerName;
						ImGui::SetKeyboardFocusHere();
					}

					ImGui::Text(ICON_FA_LAYER_GROUP);
					ImGui::SameLine();
					ImGuiInputText(HashValue(layer.id + 0x10000), m_renamingCache);

					if (!ImGui::IsItemActive() && m_renamingFocus)
					{
						m_renamingItem = false;
						m_renamingFocus = false;
						UndoRedoScope* scope = Editor::CreateUndoRedoScope("Rename Layer");
						editor.RenameLayer(layer, m_renamingCache, scope);
						m_renamingItemRID = {};
					}

					if (!m_renamingFocus)
					{
						m_renamingFocus = true;
					}
					return;
				}

				ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
				if (IsSelected(layer))
				{
					nodeFlags |= ImGuiTreeNodeFlags_Selected;
				}

				ImGuiTreeLeaf(reinterpret_cast<VoidPtr>((usize)layer.id), (String(ICON_FA_LAYER_GROUP " ") + layerName).CStr(), nodeFlags);

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
				{
					SelectItem(layer);
					m_newSelection = true;
				}

				if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
				{
					m_contextItem = layer;
					m_contextSection = ContextSection::Layer;
					m_openPopup = true;
				}
			});

			ImGui::TreePop();
		}
	}

	void AnimatorTreeViewWindow::DrawParameters(AnimatorEditor& editor, RID controller)
	{
		ResourceObject controllerObj = Resources::Read(controller);
		if (!controllerObj) return;

		ImGuiTreeNodeFlags paramsFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;

		RID paramsHeaderId = {ParamsHeaderID};
		if (IsSelected(paramsHeaderId))
		{
			paramsFlags |= ImGuiTreeNodeFlags_Selected;
		}

		bool paramsOpen = ImGuiTreeNode(reinterpret_cast<VoidPtr>((usize)ParamsHeaderID), ICON_FA_SLIDERS " Parameters", paramsFlags);

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			SelectItem(paramsHeaderId);
			m_newSelection = true;
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_contextItem = {};
			m_contextSection = ContextSection::Parameter;
			m_openPopup = true;
		}

		if (paramsOpen)
		{
			controllerObj.IterateSubObjectList(AnimationControllerResource::Parameters, [&](RID parameter)
			{
				ResourceObject paramObj = Resources::Read(parameter);
				if (!paramObj) return;

				String paramName = paramObj.GetString(AnimationParameterResource::Name);

				// Renaming mode
				if (m_renamingItem && m_renamingItemRID == parameter)
				{
					if (!m_renamingFocus)
					{
						m_renamingCache = paramName;
						ImGui::SetKeyboardFocusHere();
					}

					ImGui::Text(ICON_FA_SLIDERS);
					ImGui::SameLine();
					ImGuiInputText(HashValue(parameter.id + 0x30000), m_renamingCache);

					if (!ImGui::IsItemActive() && m_renamingFocus)
					{
						m_renamingItem = false;
						m_renamingFocus = false;
						UndoRedoScope* scope = Editor::CreateUndoRedoScope("Rename Parameter");
						editor.RenameParameter(parameter, m_renamingCache, scope);
						m_renamingItemRID = {};
					}

					if (!m_renamingFocus)
					{
						m_renamingFocus = true;
					}
					return;
				}

				ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
				if (IsSelected(parameter))
				{
					nodeFlags |= ImGuiTreeNodeFlags_Selected;
				}

				ImGuiTreeLeaf(reinterpret_cast<VoidPtr>((usize)parameter.id), (String(ICON_FA_SLIDERS " ") + paramName).CStr(), nodeFlags);

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
				{
					SelectItem(parameter);
					m_newSelection = true;
				}

				if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
				{
					m_contextItem = parameter;
					m_contextSection = ContextSection::Parameter;
					m_openPopup = true;
				}
			});

			ImGui::TreePop();
		}
	}

	void AnimatorTreeViewWindow::DrawAvatars(AnimatorEditor& editor, RID controller)
	{
		ResourceObject controllerObj = Resources::Read(controller);
		if (!controllerObj) return;

		ImGuiTreeNodeFlags avatarsFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;

		if (m_forceOpenAvatars)
		{
			ImGui::SetNextItemOpen(true);
			m_forceOpenAvatars = false;
		}

		RID avatarsHeaderId = {AvatarsHeaderID};
		if (IsSelected(avatarsHeaderId))
		{
			avatarsFlags |= ImGuiTreeNodeFlags_Selected;
		}

		bool avatarsOpen = ImGuiTreeNode(reinterpret_cast<VoidPtr>((usize)AvatarsHeaderID), ICON_FA_PERSON " Avatars", avatarsFlags);

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			SelectItem(avatarsHeaderId);
			m_newSelection = true;
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_contextItem = {};
			m_contextSection = ContextSection::Avatar;
			m_openPopup = true;
		}

		if (avatarsOpen)
		{
			controllerObj.IterateSubObjectList(AnimationControllerResource::Avatars, [&](RID avatar)
			{
				ResourceObject avatarObj = Resources::Read(avatar);
				if (!avatarObj) return;

				String avatarName = avatarObj.GetString(AnimationAvatarResource::Name);

				// Renaming mode
				if (m_renamingItem && m_renamingItemRID == avatar)
				{
					if (!m_renamingFocus)
					{
						m_renamingCache = avatarName;
						ImGui::SetKeyboardFocusHere();
					}

					ImGui::Text(ICON_FA_PERSON);
					ImGui::SameLine();
					ImGuiInputText(HashValue(avatar.id + 0x50000), m_renamingCache);

					if (!ImGui::IsItemActive() && m_renamingFocus)
					{
						m_renamingItem = false;
						m_renamingFocus = false;
						UndoRedoScope* scope = Editor::CreateUndoRedoScope("Rename Avatar");
						editor.RenameAvatar(avatar, m_renamingCache, scope);
						m_renamingItemRID = {};
					}

					if (!m_renamingFocus)
					{
						m_renamingFocus = true;
					}
					return;
				}

				ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
				if (IsSelected(avatar))
				{
					nodeFlags |= ImGuiTreeNodeFlags_Selected;
				}

				ImGuiTreeLeaf(reinterpret_cast<VoidPtr>((usize)avatar.id), (String(ICON_FA_PERSON " ") + avatarName).CStr(), nodeFlags);

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
				{
					SelectItem(avatar);
					m_newSelection = true;
				}

				if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
				{
					m_contextItem = avatar;
					m_contextSection = ContextSection::Avatar;
					m_openPopup = true;
				}
			});

			ImGui::TreePop();
		}
	}

	void AnimatorTreeViewWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	// --- Menu actions ---

	void AnimatorTreeViewWindow::NewLayer(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Layer");
		RID layer = editor.AddLayer(scope);
		if (layer)
		{
			window->SelectItem(layer, scope);
		}
		window->m_forceOpenLayers = true;
	}

	void AnimatorTreeViewWindow::NewParameter(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Parameter");
		RID parameter = editor.AddParameter(scope);
		if (parameter)
		{
			window->SelectItem(parameter, scope);
		}
	}

	void AnimatorTreeViewWindow::NewAvatar(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Add Avatar");
		RID avatar = editor.AddAvatar(scope);
		if (avatar)
		{
			window->SelectItem(avatar, scope);
		}
		window->m_forceOpenAvatars = true;
	}

	void AnimatorTreeViewWindow::RenameItem(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		RID target = window->GetActionTarget();
		if (!target) return;
		if (!window->IsItemLayer(target) && !window->IsItemParameter(target) && !window->IsItemAvatar(target)) return;
		window->m_renamingItem = true;
		window->m_renamingFocus = false;
		window->m_renamingItemRID = target;
	}

	void AnimatorTreeViewWindow::DeleteItem(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		AnimatorEditor& editor = window->workspace->GetAnimatorEditor();
		RID target = window->GetActionTarget();
		if (!target) return;

		if (window->IsItemLayer(target))
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Remove Layer");
			editor.RemoveLayer(target, scope);
		}
		else if (window->IsItemParameter(target))
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Remove Parameter");
			editor.RemoveParameter(target, scope);
		}
		else if (window->IsItemAvatar(target))
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Remove Avatar");
			editor.RemoveAvatar(target, scope);
		}
	}

	bool AnimatorTreeViewWindow::HasContextItem(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		RID target = window->GetActionTarget();
		if (!target || target.id == LayersHeaderID || target.id == ParamsHeaderID || target.id == AvatarsHeaderID) return false;
		return window->IsItemLayer(target) || window->IsItemParameter(target) || window->IsItemAvatar(target);
	}

	bool AnimatorTreeViewWindow::IsLayerContext(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		if (window->m_contextItem)
		{
			return window->m_contextSection == ContextSection::Layer;
		}
		return window->IsItemLayer(window->GetSelectedItem());
	}

	bool AnimatorTreeViewWindow::IsParamContext(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		if (window->m_contextItem)
		{
			return window->m_contextSection == ContextSection::Parameter;
		}
		return window->IsItemParameter(window->GetSelectedItem());
	}

	bool AnimatorTreeViewWindow::IsAvatarContext(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		if (window->m_contextItem)
		{
			return window->m_contextSection == ContextSection::Avatar;
		}
		return window->IsItemAvatar(window->GetSelectedItem());
	}

	bool AnimatorTreeViewWindow::IsNoContext(const MenuItemEventData& eventData)
	{
		AnimatorTreeViewWindow* window = static_cast<AnimatorTreeViewWindow*>(eventData.drawData);
		RID target = window->GetActionTarget();
		return !target || (!window->IsItemLayer(target) && !window->IsItemParameter(target) && !window->IsItemAvatar(target));
	}

	void AnimatorTreeViewWindow::RegisterType(NativeReflectType<AnimatorTreeViewWindow>& type)
	{
		Resources::Type<AnimatorTreeWindowState>()
			.Field<AnimatorTreeWindowState::SelectedItem>(ResourceFieldType::Reference)
			.Build();

		// Context menu items
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "New Layer", .icon = ICON_FA_PLUS, .priority = 0, .action = NewLayer, .visible = IsLayerContext});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "New Parameter", .icon = ICON_FA_PLUS, .priority = 0, .action = NewParameter, .visible = IsParamContext});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "New Avatar", .icon = ICON_FA_PLUS, .priority = 0, .action = NewAvatar, .visible = IsAvatarContext});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "Rename", .priority = 100, .itemShortcut = {.presKey = Key::F2}, .action = RenameItem, .visible = HasContextItem});
		menuItemContext.AddMenuItem(MenuItemCreation{.itemName = "Delete", .priority = 200, .itemShortcut = {.presKey = Key::Delete}, .action = DeleteItem, .visible = HasContextItem});

		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::Left,
			.workspaceTypes = {WorkspaceTypes::Animator}
		});
	}
}
