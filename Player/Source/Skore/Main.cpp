#include "Skore/App.hpp"
#include "Skore/Main.hpp"

#include "Skore/Events.hpp"
#include "Skore/OpenXRManager.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneManager.hpp"

namespace Skore
{
	struct SwapchainRenderPipelineModule;
	struct DefaultRenderPipeline;
	struct ProfilerOverlayPipelineModule;


	StdOutSink stdOutSink{};

	static RenderPipelineContext* pipelineContext;

	Logger& logger = Logger::GetLogger("Skore::Player");

	void OnPlayerOnRecordRenderCommands(GPUCommandBuffer* cmd)
	{
		if (Scene* scene = SceneManager::GetActiveScene())
		{
			pipelineContext->Execute(cmd, scene);
		}
	}

	void OnPlayerShutdown()
	{
		pipelineContext->Destroy();
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

		String assetsPath = Path::Join(currentDir, "Assets");

		//------------------- step 2 -- Resource Loading
		for (const String& file : DirectoryEntries(assetsPath))
		{
			logger.Debug("found file {} ", file);
			if (Path::Extension(file) == SK_RESOURCE_EXT)
			{
				resourceLoaded = true;
				Resources::LoadResources(file);
			}
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

		Array<TypeID> extraModules;
		extraModules.EmplaceBack(TypeInfo<SwapchainRenderPipelineModule>::ID());
		extraModules.EmplaceBack(TypeInfo<ProfilerOverlayPipelineModule>::ID());

		RenderPipelineContextSettings contextSettings;
		pipelineContext = RenderPipeline::CreateContext(TypeInfo<DefaultRenderPipeline>::ID(), extraModules, contextSettings);

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
