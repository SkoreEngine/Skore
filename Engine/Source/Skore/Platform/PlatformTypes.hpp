#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/Event.hpp"

namespace Skore
{
    SK_HANDLER(Window);

    using OnDropFileCallback = EventType<"Skore::OnDropFileCallback"_h, void(Window window, const StringView& path)>;

    enum class WindowFlags : i32
    {
        None           = 0,
        Maximized      = 1 << 0,
        Fullscreen     = 1 << 1,
        SubscriveInput = 1 << 2,
    };

    ENUM_FLAGS(WindowFlags, i32)

    enum class DialogResult
    {
        OK     = 0,
        Error  = 1,
        Cancel = 2
    };

    struct FileFilter
    {
        const char* name{};
        const char* spec{};
    };
}
