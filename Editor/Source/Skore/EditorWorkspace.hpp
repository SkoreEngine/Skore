#pragma once

#include "AnimatorEditor.hpp"
#include "EditorCommon.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Scene/SceneEditor.hpp"


namespace Skore
{
	class SK_API EditorWorkspace
	{
	public:
		EditorWorkspace(u8 workspaceTypeId);
		~EditorWorkspace();
		StringView      GetName() const;
		u32             GetId() const;
		u8              GetWorkspaceTypeId() const;
		SceneEditor*    GetSceneEditor();
		AnimatorEditor& GetAnimatorEditor();
		void            OpenAsset(RID rid, RID preview = {});

		// Window management
		void OpenWindow(TypeID windowType, VoidPtr userData = nullptr);
		void OpenWindowWithId(TypeID windowType, u32 windowId, VoidPtr userData = nullptr);

		template <typename T>
		void OpenWindow(VoidPtr userData = nullptr)
		{
			OpenWindow(TypeInfo<T>::ID(), userData);
		}
		void InitDockSpace();
		void ResetLayout();
		void DrawWindows();
		void IterateWindows(FnWindowIterationCallback callback, VoidPtr userData = nullptr);
		void DestroyAllWindows();
		bool AnyWindowOfTypeHovered(TypeID windowType) const;
		bool IsWindowIdInUse(u32 windowId) const;
		u32  GetDockSpaceId() const;
		void DoUpdate();

		static void RegisterType(NativeReflectType<EditorWorkspace>& type);

	private:
		u32            id;
		String         name;
		u8             m_workspaceTypeId{};
		SceneEditor    m_sceneEditor{*this};
		AnimatorEditor m_animatorEditor{*this};

		RID state = {};

		// Window state
		Array<OpenWindowStorage> m_openWindows;
		bool m_dockInitialized = false;
		u32  m_dockSpaceId{};
		u32  m_centerSpaceId{};
		u32  m_rightTopDockId{};
		u32  m_rightBottomDockId{};
		u32  m_bottomLeftDockId{};
		u32  m_bottomRightDockId{};
		u32  m_leftDockId{};

		u32 GetDockId(DockPosition dockPosition) const;
		void OpenWindowInternal(TypeID windowType, u32 windowId, VoidPtr userData, bool dockToDefault, bool autoFocus);

		static void WorkspaceStateChanged(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData);
	};
}
