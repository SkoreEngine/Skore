#include "Skore/Main.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Project/ProjectManager.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Sinks.hpp"

namespace Skore
{
	SK_API void EditorInit(StringView project);
	SK_API void EditorTypeRegister();
	SK_API void RegisterProjectManagerTypes();
	ConsoleSink& GetConsoleSink();
	StdOutSink stdOutSink{};

	void ImGuiInit();
	void ImGuiNewFrame();

	i32 Main(int argc, char** argv)
	{
		Logger::RegisterSink(stdOutSink);
		Logger::RegisterSink(GetConsoleSink());

		App::Init(argc, argv, []
		{
			GroupScope scope("Editor");
			EditorTypeRegister();
			RegisterProjectManagerTypes();
		});

		AppConfig appConfig;
		appConfig.title = "Skore Editor";
		appConfig.fullscreen = false;
		appConfig.maximized = false;
		appConfig.width = 1280;
		appConfig.height = 720;
		appConfig.enableReload = true;

		ArgParser argParser;
		argParser.Parse(argc, argv);
		String initialProject;

		if (argParser.Has("project"))
		{
			initialProject = argParser.Get("project");
			appConfig.maximized = true;
		}

		if (App::CreateContext(appConfig) != AppResult::Continue)
		{
			return 1;
		}

		ImGuiInit();

		if (!initialProject.Empty())
		{
			EditorInit(initialProject);
		}
		else
		{
			ProjectManager::Init();
		}

		if (App::Run() == AppResult::Failure)
		{
			return 1;
		}

		return 0;
	}
}