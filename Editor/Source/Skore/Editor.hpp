#pragma once

#include <functional>

#include "EditorCommon.hpp"
#include "MenuItem.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	struct UndoRedoScope;
	class EditorWorkspace;
	typedef void (*FnConfirmCallback)(VoidPtr userdata);

	struct SK_API Editor
	{
		static void              AddMenuItem(const MenuItemCreation& menuItem);
		static void              ShowConfirmDialog(StringView message, VoidPtr userData, FnConfirmCallback callback);
		static void              ShowErrorDialog(StringView message);
		static UndoRedoScope*    CreateUndoRedoScope(StringView name);
		static void              LockUndoRedo(bool lock);
		static RID               GetProject();
		static StringView        GetProjectPath();
		static StringView        GetProjectTempPath();
		static StringView        GetProjectLocalPath();
		static Span<RID>         GetPackages();
		static RID               LoadPackage(StringView name, StringView directory);

		static Span<String>      GetProjectPackages();
		static void              AddProjectPackage(StringView directory);
		static void              RemoveProjectPackage(StringView directory);
		static void              ExecuteOnMainThread(std::function<void()> func);
		static void              AddTask(std::function<void()> func, StringView name = "");

		static EditorWorkspace*  CreateWorkspace(u8 type);
		static EditorWorkspace*  GetActiveWorkspace();
		static EditorWorkspace*  GetWorkspaceOfType(u8 type);
		static u32               GetWorkspaceCount();
		static void              SetActiveWorkspace(u32 index);

		static bool DebugOptionsEnabled();
	};
}
