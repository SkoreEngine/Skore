#include "Skore/EditorWorkspace.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "Skore/Editor.hpp"
#include "Skore/EditorLayout.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Window/PropertiesWindow.hpp"


namespace Skore
{
	struct WorkspaceResourceState
	{
		enum
		{
			SelectedAsset
		};
	};

	namespace
	{
		u32     workspaceCount = 0;
		Logger& logger = Logger::GetLogger("Skore::EditorWorkspace");

		EventHandler<OnAssetSelection>   onAssetSelectionHandler{};
	}

	EditorWorkspace::EditorWorkspace(u8 workspaceTypeId)
		: id(workspaceCount++)
		, m_workspaceTypeId(workspaceTypeId)
	{
		String typeName = "Workspace";
		for (const EditorWorkspaceTypeDesc& ws : GetEditorWorkspaceTypeStorages())
		{
			if (ws.id == workspaceTypeId)
			{
				typeName = ws.displayName;
				break;
			}
		}
		name = String(typeName).Append(" ").Append(ToString(id));

		m_dockSpaceId = 10000 + (id * 10000);
		m_centerSpaceId = m_dockSpaceId;

		Resources::FindType<WorkspaceResourceState>()->RegisterEvent(ResourceEventType::Changed,  WorkspaceStateChanged, this);

		state = Resources::Create<WorkspaceResourceState>();
	}

	EditorWorkspace::~EditorWorkspace()
	{
		Resources::FindType<WorkspaceResourceState>()->UnregisterEvent(ResourceEventType::Changed, WorkspaceStateChanged, this);
	}

	StringView EditorWorkspace::GetName() const
	{
		return name;
	}

	u32 EditorWorkspace::GetId() const
	{
		return id;
	}

	u8 EditorWorkspace::GetWorkspaceTypeId() const
	{
		return m_workspaceTypeId;
	}

	SceneEditor* EditorWorkspace::GetSceneEditor()
	{
		return &m_sceneEditor;
	}

	AnimatorEditor& EditorWorkspace::GetAnimatorEditor()
	{
		return m_animatorEditor;
	}

	void EditorWorkspace::OpenAsset(RID rid)
	{
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Open Asset");
		ResourceObject stateObject = Resources::Write(state);
		stateObject.SetReference(WorkspaceResourceState::SelectedAsset, rid);
		stateObject.Commit(scope);
	}

	void EditorWorkspace::OpenWindow(TypeID windowType, VoidPtr userData)
	{
		OpenWindowInternal(windowType, AllocateWindowId(), userData, true, true);
	}

	void EditorWorkspace::OpenWindowWithId(TypeID windowType, u32 windowId, VoidPtr userData)
	{
		OpenWindowInternal(windowType, windowId, userData, false, false);
	}

	void EditorWorkspace::OpenWindowInternal(TypeID windowType, u32 windowId, VoidPtr userData, bool dockToDefault, bool autoFocus)
	{
		for (const EditorWindowStorage& window : GetEditorWindowStorages())
		{
			if (window.typeId == windowType)
			{
				ReflectType* reflectType = Reflection::FindTypeById(window.typeId);

				OpenWindowStorage openWindowStorage = OpenWindowStorage{
					.id = windowId,
					.instance = static_cast<EditorWindow*>(reflectType->NewObject()),
					.reflectType = reflectType
				};

				openWindowStorage.instance->workspace = this;
				openWindowStorage.instance->id = openWindowStorage.id;
				openWindowStorage.instance->skipFocusOnFirstAppearance = !autoFocus;
				openWindowStorage.instance->Init(userData);

				m_openWindows.EmplaceBack(openWindowStorage);

				if (dockToDefault)
				{
					u32 p = GetDockId(window.dockPosition);
					if (p != U32_MAX)
					{
						ImGuiDockBuilderDockWindow(windowId, p);
					}
				}

				EditorLayout::OnWindowOpened(m_workspaceTypeId, windowType, windowId);
				break;
			}
		}
	}

	bool EditorWorkspace::IsWindowIdInUse(u32 windowId) const
	{
		for (const OpenWindowStorage& w : m_openWindows)
		{
			if (w.id == windowId) return true;
		}
		return false;
	}

	void EditorWorkspace::InitDockSpace()
	{
		if (m_dockInitialized) return;
		m_dockInitialized = true;

		if (EditorLayout::HasSavedWorkspace(m_workspaceTypeId))
		{
			for (const SavedEditorWindow& saved : EditorLayout::GetSavedWindows(m_workspaceTypeId))
			{
				OpenWindowWithId(saved.typeId, saved.id, nullptr);
			}
			return;
		}

		ImGuiDockBuilderReset(m_dockSpaceId);

		m_centerSpaceId = m_dockSpaceId;
		m_rightTopDockId = ImGui::DockBuilderSplitNode(m_centerSpaceId, ImGuiDir_Right, 0.15f, nullptr, &m_centerSpaceId);
		m_rightBottomDockId = ImGui::DockBuilderSplitNode(m_rightTopDockId, ImGuiDir_Down, 0.50f, nullptr, &m_rightTopDockId);

		m_bottomLeftDockId = ImGui::DockBuilderSplitNode(m_centerSpaceId, ImGuiDir_Down, 0.20f, nullptr, &m_centerSpaceId);
		m_bottomRightDockId = ImGui::DockBuilderSplitNode(m_bottomLeftDockId, ImGuiDir_Right, 0.33f, nullptr, &m_bottomLeftDockId);

		m_leftDockId = ImGui::DockBuilderSplitNode(m_centerSpaceId, ImGuiDir_Left, 0.12f, nullptr, &m_centerSpaceId);

		for (const EditorWindowStorage& windowType : GetEditorWindowStorages())
		{
			for (u8 wsType : windowType.workspaceTypes)
			{
				if (wsType == WorkspaceTypes::All || wsType == m_workspaceTypeId)
				{
					OpenWindowInternal(windowType.typeId, AllocateWindowId(), nullptr, true, false);
					break;
				}
			}
		}
	}

	void EditorWorkspace::ResetLayout()
	{
		DestroyAllWindows();
		m_dockInitialized = false;
	}

	void EditorWorkspace::DrawWindows()
	{
		for (u32 i = 0; i < m_openWindows.Size(); ++i)
		{
			OpenWindowStorage& openWindowStorage = m_openWindows[i];

			bool open = true;

			openWindowStorage.instance->Draw(open);
			if (!open)
			{
				EditorLayout::OnWindowClosed(m_workspaceTypeId, openWindowStorage.id);
				openWindowStorage.reflectType->Destroy(openWindowStorage.instance);
				m_openWindows.Erase(m_openWindows.begin() + i, m_openWindows.begin() + i + 1);
			}
		}
	}

	void EditorWorkspace::IterateWindows(FnWindowIterationCallback callback, VoidPtr userData)
	{
		for (const OpenWindowStorage& openWindow : m_openWindows)
		{
			callback(openWindow.instance, userData);
		}
	}

	void EditorWorkspace::DestroyAllWindows()
	{
		for (OpenWindowStorage& openWindow : m_openWindows)
		{
			if (openWindow.instance)
			{
				openWindow.reflectType->Destroy(openWindow.instance);
			}
		}
		m_openWindows.Clear();
	}

	bool EditorWorkspace::AnyWindowOfTypeHovered(TypeID windowType) const
	{
		logger.Info("Hovered window on AnyWindowOfTypeHovered {}", ImGuiHoveredWindowId());

		for (const OpenWindowStorage& openWindow : m_openWindows)
		{
			if (openWindow.reflectType->GetProps().typeId == windowType)
			{
				if (openWindow.id == ImGuiHoveredWindowId())
				{
					return true;
				}
			}
		}
		return false;
	}

	u32 EditorWorkspace::GetDockSpaceId() const
	{
		return m_dockSpaceId;
	}

	void EditorWorkspace::DoUpdate()
	{
		m_sceneEditor.DoUpdate();
	}

	u32 EditorWorkspace::GetDockId(DockPosition dockPosition) const
	{
		switch (dockPosition)
		{
			case DockPosition::None: return U32_MAX;
			case DockPosition::Center: return m_centerSpaceId;
			case DockPosition::Left: return m_leftDockId;
			case DockPosition::RightTop: return m_rightTopDockId;
			case DockPosition::RightBottom: return m_rightBottomDockId;
			case DockPosition::BottomLeft: return m_bottomLeftDockId;
			case DockPosition::BottomRight: return m_bottomRightDockId;
		}
		return U32_MAX;
	}

	void EditorWorkspace::RegisterType(NativeReflectType<EditorWorkspace>& type)
	{
		Resources::Type<WorkspaceResourceState>()
			.Field<WorkspaceResourceState::SelectedAsset>(ResourceFieldType::Reference)
			.Build();
	}

	void EditorWorkspace::WorkspaceStateChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		EditorWorkspace* workspace = static_cast<EditorWorkspace*>(userData);
		if (newValue)
		{
			onAssetSelectionHandler.Invoke(workspace->id,
				newValue.GetReference(WorkspaceResourceState::SelectedAsset));
		}
	}
}
