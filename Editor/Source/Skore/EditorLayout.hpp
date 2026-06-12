#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/TypeInfo.hpp"

namespace Skore
{
	struct SavedEditorWindow
	{
		TypeID typeId{};
		u32    id{};
		String state{}; //serialized internal window state (fields tagged with EditorSerialize)
	};

	namespace EditorLayout
	{
		//Invoked right before the layout is written to disk so the owner can refresh each
		//open window's serialized state through SaveWindowState.
		using FnCaptureWindowStates = void (*)(VoidPtr userData);

		SK_API void Init();
		SK_API void Shutdown();

		SK_API bool                    HasSavedWorkspace(u8 workspaceTypeId);
		SK_API Span<SavedEditorWindow> GetSavedWindows(u8 workspaceTypeId);

		SK_API void OnWindowOpened(u8 workspaceTypeId, TypeID windowType, u32 id);
		SK_API void OnWindowClosed(u8 workspaceTypeId, u32 id);

		SK_API void SetCaptureCallback(FnCaptureWindowStates callback, VoidPtr userData);
		SK_API void SaveWindowState(u8 workspaceTypeId, u32 id, StringView state);

		SK_API void Flush();
		SK_API void ResetAll();
	}
}
