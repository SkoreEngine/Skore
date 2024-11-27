#pragma once

#include "Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Event.hpp"
#include "Graphics/GraphicsTypes.hpp"
#include "Platform/PlatformTypes.hpp"

namespace Skore
{
    class RenderCommands;

    using OnInit = EventType<"Skore::OnInit"_h, void()>;
    using OnBeginFrame = EventType<"Skore::OnBeginFrame"_h, void()>;
    using OnUpdate = EventType<"Skore::OnUpdate"_h, void(f64 deltaTime)>;
    using OnEndFrame = EventType<"Skore::OnEndFrame"_h, void()>;
    using OnShutdown = EventType<"Skore::OnShutdown"_h, void()>;
    using OnShutdownRequest = EventType<"Skore::OnShutdownRequest"_h, void(bool* canClose)>;
    using OnRecordRenderCommands = EventType<"Skore::OnRecordRenderCommands"_h, void(RenderCommands& renderCommands, f64 deltaTime)>;
    using OnSwapchainRender = EventType<"Skore::OnSwapchainRender"_h, void(RenderCommands& renderCommands)>;
    using OnSwapchainResize = EventType<"Skore::OnSwapchainResize"_h, void(Extent newExtend)>;

    struct EngineContextCreation
    {
        StringView title{};
        Extent     resolution{};
        bool       maximize{false};
        bool       fullscreen{false};
        bool       headless = false;
    };


    struct SK_API Engine
    {
        static void      Init();
        static void      Init(i32 argc, char** argv);
        static void      CreateContext(const EngineContextCreation& contextCreation);
        static void      Run();
        static void      Shutdown();
        static void      Destroy();
        static bool      IsRunning();
        static u64       GetFrame();
        static f64       DeltaTime();
        static Window    GetActiveWindow();
        static Swapchain GetSwapchain();
        static Extent    GetViewportExtent();

        static StringView GetArgByName(const StringView& name);
        static StringView GetArgByIndex(usize i);
        static bool       HasArgByName(const StringView& name);
    };
}
