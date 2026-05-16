#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/IO/InputTypes.hpp"

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Skore
{
	enum class WindowFlags : i32
	{
		None           = 0,
		Maximized      = 1 << 0,
		Fullscreen     = 1 << 1,
		SubscribeInput = 1 << 2,
	};

	ENUM_FLAGS(WindowFlags, i32)


	enum class MessageBoxType
	{
		Info,
		Warning,
		Error,
	};

	struct FileFilter
	{
		const char* name{};
		const char* spec{};
	};

	SK_HANDLER(Window);

	namespace Platform
	{
		//window
		SK_API Window CreateWindow(StringView title, u32 width, u32 height, WindowFlags flags);
		SK_API void   DestroyWindow(Window window);
		SK_API f32    GetWindowDPI(Window window);
		SK_API Extent GetWindowSize(Window window);
		SK_API bool   IsWindowMinimized(Window window);
		SK_API void   SetWindowCursorLockMode(Window window, CursorLockMode cursorLockMode);
		SK_API void   MaximizeWindow(Window window);
		SK_API VoidPtr GetNativeWindowHandle(Window window);

		SK_API bool ShowSimpleMessageBox(MessageBoxType type, StringView title, StringView message, Window window = {});

		SK_API void SaveDialog(std::function<void(StringView path)>&& func, Span<FileFilter> filters, StringView defaultPath, StringView fileName, Window window = {});
		SK_API void OpenDialog(std::function<void(StringView path)>&& func, Span<FileFilter> filters, StringView defaultPath, Window window = {});
		SK_API void OpenDialogMultiple(std::function<void(Span<StringView> path)>&& func, Span<FileFilter> filters, StringView defaultPath, Window window = {});
		SK_API void PickFolder(std::function<void(StringView path)>&& func, StringView defaultPath, Window window = {});


		SK_API VoidPtr LoadObject(StringView library);
		SK_API void    UnloadObject(VoidPtr library);
		SK_API VoidPtr LoadFunction(VoidPtr library, StringView functionName);

		SK_API void   SetClipboardText(StringView text);
		SK_API String GetClipboardText();

		SK_API void OpenURL(StringView url);

		SK_API VoidPtr CreateProcess(const char * const *args, bool pipeStdio);
		SK_API void DestroyProcess(VoidPtr process);

		SK_API u64 GetTime();
	}
}
