#include "Engine.hpp"

#include <thread>

#include "Skore/Core/Logger.hpp"
#include "Skore/Platform/PlatformTypes.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "TypeRegister.hpp"
#include "Core/Attributes.hpp"
#include "Core/Registry.hpp"
#include "Core/SettingsManager.hpp"
#include "Skore/Core/StaticContent.hpp"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Graphics/RenderGraph.hpp"
#include "Graphics/RenderPipeline.hpp"

namespace Skore
{
    void            PlatformInit();
    void            PlatformShutdown();
    void            GraphicsInit();
    void            GraphicsCreateDevice(Adapter adapter);
    RenderCommands& GraphicsBeginFrame();
    void            GraphicsEndFrame(Swapchain swapchain);
    void            GraphicsShutdown();
    void            RegistryShutdown();
    void            EventShutdown();
    void            InputInit();
    void            AssetsShutdown();
    void            PhysicsInit();


    namespace
    {
        Logger&   logger = Logger::GetLogger("Skore::Engine");
        bool      running = false;
        Window    window{};
        Swapchain swapchain{};
        Vec4      clearColor = Vec4{0, 0, 0, 1};
        f64       lastTime{};
        f64       deltaTime{};
        u64       frame{0};
        ArgParser args{};

        EventHandler<OnInit>                 onInitHandler{};
        EventHandler<OnUpdate>               onUpdateHandler{};
        EventHandler<OnBeginFrame>           onBeginFrameHandler{};
        EventHandler<OnEndFrame>             onEndFrameHandler{};
        EventHandler<OnShutdown>             onShutdownHandler{};
        EventHandler<OnShutdownRequest>      onShutdownRequest{};
        EventHandler<OnRecordRenderCommands> onRecordRenderCommands{};
        EventHandler<OnSwapchainRender>      onSwapchainRender{};
    }

    void Engine::Init()
    {
        Init(0, nullptr);
    }

    void Engine::Init(i32 argc, char** argv)
    {
        args.Parse(argc, argv);

        TypeRegister();
        InputInit();
    }

    void Engine::CreateContext(const EngineContextCreation& contextCreation)
    {
        SettingsManager::Init(GetTypeID<ProjectSettings>());

        PlatformInit();

        WindowFlags windowFlags = WindowFlags::SubscriveInput;
        if (contextCreation.maximize)
        {
            windowFlags |= WindowFlags::Maximized;
        }

        if (contextCreation.fullscreen)
        {
            windowFlags |= WindowFlags::Fullscreen;
        }

        GraphicsInit();
        GraphicsCreateDevice(Adapter{});

        window = Platform::CreateWindow(contextCreation.title, contextCreation.resolution, windowFlags);
        Platform::SetWindowIcon(window, StaticContent::GetImageFile("Content/Images/Logo.jpeg"));

        swapchain = Graphics::CreateSwapchain(SwapchainCreation{
            .window = window,
            .vsync = true
        });

        ImGui::Init(window, swapchain);

        PhysicsInit();

        onInitHandler.Invoke();

        running = true;
    }

    void Engine::Run()
    {
        logger.Info("Skore Engine {} Initialized", SK_VERSION);

        while (running)
        {
            f64 currentTime = Platform::GetElapsedTime();
            deltaTime = currentTime - lastTime;
            lastTime = currentTime;

            onBeginFrameHandler.Invoke();
            Platform::ProcessEvents();

            ImGui::BeginFrame(window, deltaTime);

            if (Platform::UserRequestedClose(window))
            {
                Shutdown();
                if (running)
                {
                    Platform::SetWindowShouldClose(window, false);
                }
            }

            onUpdateHandler.Invoke(deltaTime);

            if (Extent extent = Platform::GetWindowExtent(window))
            {
                RenderCommands& cmd = GraphicsBeginFrame();
                cmd.Begin();

                RenderPass renderPass = Graphics::AcquireNextRenderPass(swapchain);

                onRecordRenderCommands.Invoke(cmd, deltaTime);

                cmd.BeginLabel("Swapchain", {0, 0, 0, 1});

                cmd.BeginRenderPass(BeginRenderPassInfo{
                    .renderPass = renderPass,
                    .clearValue = &clearColor
                });

                ViewportInfo viewportInfo{};
                viewportInfo.x = 0.;
                viewportInfo.y = 0.;
                viewportInfo.width = (f32)extent.width;
                viewportInfo.height = (f32)extent.height;
                viewportInfo.maxDepth = 0.;
                viewportInfo.minDepth = 1.;
                cmd.SetViewport(viewportInfo);
                cmd.SetScissor(Rect{.x = 0, .y = 0, .width = extent.width, .height = extent.height});

                onSwapchainRender.Invoke(cmd);

                cmd.BeginLabel("ImGui", Color::FromRGBA(41, 74, 122, 255));
                ImGui::Render(cmd);
                cmd.EndLabel();

                cmd.EndRenderPass();
                cmd.EndLabel();

                cmd.End();

                GraphicsEndFrame(swapchain);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                ImGui::EndFrame();
            }

            onEndFrameHandler.Invoke();

            frame++;
        }

        Graphics::WaitQueue();

        onShutdownHandler.Invoke();

        AssetsShutdown();

        Graphics::DestroySwapchain(swapchain);
        Platform::DestroyWindow(window);

        GraphicsShutdown();
        PlatformShutdown();

        ImGui::ImGuiShutdown();
    }

    void Engine::Shutdown()
    {
        bool canChose = true;
        onShutdownRequest.Invoke(&canChose);
        if (canChose)
        {
            running = false;
        }
    }

    Window Engine::GetActiveWindow()
    {
        return window;
    }

    Swapchain Engine::GetSwapchain()
    {
        return swapchain;
    }

    Extent Engine::GetViewportExtent()
    {
        return Platform::GetWindowExtent(window);
    }

    StringView Engine::GetArgByName(const StringView& name)
    {
        return args.Get(name);
    }

    StringView Engine::GetArgByIndex(usize i)
    {
        return args.Get(i);
    }

    bool Engine::HasArgByName(const StringView& name)
    {
        return args.Has(name);
    }

    bool Engine::IsRunning()
    {
        return running;
    }

    u64 Engine::GetFrame()
    {
        return frame;
    }

    f64 Engine::DeltaTime()
    {
        return deltaTime;
    }

    void Engine::Destroy()
    {
        RegistryShutdown();
        EventShutdown();
    }
}
