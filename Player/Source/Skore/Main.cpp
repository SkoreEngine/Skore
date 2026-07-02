#include "Skore/App.hpp"
#include "Skore/Main.hpp"

#include "Skore/Events.hpp"
#include "Skore/OpenXRManager.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/Pipeline/PipelineCommon.hpp"
#include "Skore/Graphics/Pipeline/ProfilerOverlayPass.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneManager.hpp"

namespace Skore
{
	//default pipeline plus the F5 profiler overlay, rendering straight into the swapchain
	struct PlayerRenderPipeline : DefaultRenderPipeline
	{
		SK_CLASS(PlayerRenderPipeline, DefaultRenderPipeline);

		ProfilerOverlayPass profilerOverlay;

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			DefaultRenderPipeline::BuildRenderGraph(renderGraph);
			profilerOverlay.BuildRenderGraph(renderGraph);
		}
	};

	StdOutSink stdOutSink{};

	static RenderPipelineContext* pipelineContext = nullptr;
	static GPUSwapchain*             swapchain = nullptr;
	static bool                      renderingDisabled = false;

	Logger& logger = Logger::GetLogger("Skore::Player");

	void CreatePlayerSwapchain()
	{
		SwapchainDesc swapchainDesc;
		swapchainDesc.desiredFormat = Format::BGRA8_UNORM;
		swapchainDesc.vsync = true;
		swapchainDesc.window = Graphics::GetWindow();
		swapchainDesc.debugName = "Swapchain";

		swapchain = Graphics::CreateSwapchain(swapchainDesc);

		RenderGraph& renderGraph = pipelineContext->GetRenderGraph();
		renderGraph.SetOutputAttachments(PostProcessOutputName, swapchain->GetTextures(), ResourceState::Present);
		renderGraph.SetOutputSize(Platform::GetWindowSize(swapchainDesc.window));
	}

	void TryResizeSwapchain(bool force)
	{
		Extent extent = Platform::GetWindowSize(Graphics::GetWindow());

		if (extent.width == 0 || extent.height == 0)
		{
			renderingDisabled = true;
			return;
		}

		renderingDisabled = false;

		Extent outputSize = pipelineContext->GetRenderGraph().GetOutputSize();
		if (!force && outputSize == extent)
		{
			return;
		}

		Graphics::WaitIdle();

		swapchain->Destroy();
		CreatePlayerSwapchain();
	}

	void OnPlayerWindowResized(VoidPtr windowHandle)
	{
		TryResizeSwapchain(false);
	}

	void OnPlayerWindowMinimized(VoidPtr windowHandle)
	{
		renderingDisabled = true;
	}

	void OnPlayerWindowRestored(VoidPtr windowHandle)
	{
		renderingDisabled = false;
		TryResizeSwapchain(true);
	}

	void OnPlayerOnRecordRenderCommands(GPUCommandBuffer* cmd)
	{
		if (renderingDisabled || swapchain == nullptr)
		{
			return;
		}

		if (swapchain->AcquireNextImage() == DeviceResult::SwapchainOutOfDate)
		{
			TryResizeSwapchain(true);
			if (renderingDisabled || swapchain->AcquireNextImage() != DeviceResult::Success)
			{
				return;
			}
		}

		pipelineContext->GetRenderGraph().SetCurrentOutputIndex(swapchain->GetCurrentImageIndex());

		if (Scene* scene = SceneManager::GetActiveScene())
		{
			pipelineContext->Execute(cmd, scene);
		}
	}

	void OnPlayerShutdown()
	{
		Graphics::WaitIdle();

		if (swapchain)
		{
			swapchain->Destroy();
			swapchain = nullptr;
		}

		if (pipelineContext)
		{
			DestroyAndFree(pipelineContext);
			pipelineContext = nullptr;
		}
	}

	void SK_API InitResourceLoaders();

	i32 Main(int argc, char** argv)
	{
		Logger::RegisterSink(stdOutSink);

		App::Init(argc, argv);

		ArgParser& args = App::GetArgs();
		String  currentDir = args.Get("current-path");
		currentDir = !currentDir.Empty() ? currentDir : FileSystem::CurrentDir();
		logger.Debug("current dir {} ", currentDir);

		//------------------- step 1 -- Load Plugins
		for (const String& file : DirectoryEntries(Path::Join(currentDir, "Plugins")))
		{
			App::LoadPlugin(file);
		}

		InitResourceLoaders();

		bool resourceLoaded = false;
		bool projectSettingsLoaded = false;

		String assetsPath = Path::Join(currentDir, "Assets");

		//------------------- step 2 -- Resource Loading
		for (const String& file : DirectoryEntries(assetsPath))
		{
			logger.Debug("found file {} ", file);
			if (Path::Extension(file) == SK_RESOURCE_EXT)
			{
				resourceLoaded = true;
				RID loadedProjectSettings = Resources::LoadResources(file);
				projectSettingsLoaded = loadedProjectSettings || projectSettingsLoaded;
			}
		}

		if (!projectSettingsLoaded)
		{
			logger.Warn("Project settings were not found in resources; loading default project settings before creating the app context.");

			YamlArchiveWriter writer;
			Settings::CreateDefault(writer, TypeInfo<ProjectSettings>::ID());

			YamlArchiveReader reader(writer.EmitAsString());
			RID defaultProjectSettings = Settings::Load(reader, TypeInfo<ProjectSettings>::ID());
			projectSettingsLoaded = defaultProjectSettings || projectSettingsLoaded;
		}

		if (!projectSettingsLoaded)
		{
			logger.Error("Failed to load project settings before creating the app context.");
			return 1;
		}

#ifdef SK_ENABLE_ALPHA_FEATURES
		OpenXRManager::Init();
#endif

		AppConfig appConfig;
		appConfig.fullscreen = false;
		appConfig.maximized = true;
		appConfig.width = 1920;
		appConfig.height = 1080;
		appConfig.title = "Skore Player";

		RID appSettings = Settings::Get<ProjectSettings, AppSettings>();
		if (ResourceObject appSettingsObject = Resources::Read(appSettings))
		{
			appConfig.title = appSettingsObject.GetString(AppSettings::Title);
			appConfig.width = appSettingsObject.GetVec2(AppSettings::Size).x;
			appConfig.height = appSettingsObject.GetVec2(AppSettings::Size).y;
			appConfig.fullscreen = appSettingsObject.GetBool(AppSettings::Fullscreen);
			appConfig.maximized = appSettingsObject.GetBool(AppSettings::Maximized);
		}

		if (AppResult result = App::CreateContext(appConfig); result != AppResult::Continue)
		{
			return result == AppResult::Success ? 0 : 1;
		}

		Reflection::Type<PlayerRenderPipeline>();

		pipelineContext = RenderPipelineContext::Create(sktypeid(PlayerRenderPipeline));
		RenderPipelineContext::SetMainContext(pipelineContext);
		CreatePlayerSwapchain();

		Event::Bind<OnWindowResized, &OnPlayerWindowResized>();
		Event::Bind<OnWindowMinimized, &OnPlayerWindowMinimized>();
		Event::Bind<OnWindowRestored, &OnPlayerWindowRestored>();
		Event::Bind<OnRecordRenderCommands, &OnPlayerOnRecordRenderCommands>();
		Event::Bind<OnShutdown, &OnPlayerShutdown>();


		if (resourceLoaded)
		{
			//------------step3 - Main scene load------------

			RID sceneSettings = Settings::Get<ProjectSettings, SceneSettings>();
			if (ResourceObject sceneSettingsObject = Resources::Read(sceneSettings))
			{
				if (RID defaultScene = sceneSettingsObject.GetReference(SceneSettings::DefaultScene))
				{
					Scene* scene = Alloc<Scene>(defaultScene);
					SceneManager::SetActiveScene(scene);
				}
			}
		}

		App::Run();

		return 0;
	}
}
