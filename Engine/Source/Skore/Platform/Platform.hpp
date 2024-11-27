#pragma once

#include "PlatformTypes.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Image.hpp"
#include "Skore/IO/InputTypes.hpp"

namespace Skore::Platform
{
    SK_API void         ProcessEvents();
    SK_API void         WaitEvents();

    SK_API Window       CreateWindow(const StringView& title, const Extent& extent, WindowFlags flags);
    SK_API Extent       GetWindowExtent(Window window);
    SK_API bool         UserRequestedClose(Window window);
    SK_API void         DestroyWindow(Window window);
    SK_API f32          GetWindowScale(Window window);
    SK_API void         SetWindowShouldClose(Window window, bool shouldClose);
    SK_API void         SetClipboardString(Window window, StringView string);
    SK_API void         SetCursor(Window window, MouseCursor mouseCursor);
    SK_API void         SetWindowIcon(Window window, const Image& image);

    SK_API f64          GetTime();
    SK_API f64          GetElapsedTime();

    SK_API void         ShowInExplorer(const StringView& path);

    SK_API DialogResult SaveDialog(String& path, const Span<FileFilter>& filters, const StringView& defaultPath, const StringView& fileName);
    SK_API DialogResult OpenDialog(String& path, const Span<FileFilter>& filters, const StringView& defaultPath);
    SK_API DialogResult OpenDialogMultiple(Array<String>& paths, const Span<FileFilter>& filters, const StringView& defaultPath);
    SK_API DialogResult PickFolder(String& path, const StringView& defaultPath);

    SK_API VoidPtr LoadDynamicLib(const StringView& library);
    SK_API void    FreeDynamicLib(VoidPtr library);
    SK_API VoidPtr GetFunctionAddress(VoidPtr library, const StringView& functionName);
}
