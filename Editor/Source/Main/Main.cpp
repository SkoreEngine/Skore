#include "Skore/Main.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/EditorSettings.hpp"
#include "Skore/Project/ProjectManager.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Sinks.hpp"

namespace Skore
{
	SK_API AppResult EditorInit(StringView project, const AppConfig& appConfig);
	SK_API void EditorTypeRegister();
	SK_API void RegisterProjectManagerTypes();
	ConsoleSink& GetConsoleSink();
	StdOutSink stdOutSink{};

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
		bool saveInitialProject = false;
		bool openProjectManager = argParser.Has("project-manager") || argParser.Has("new-project");
		ProjectManagerTab projectManagerTab = argParser.Has("new-project") ? ProjectManagerTab::NewProject : ProjectManagerTab::RecentProjects;

		if (argParser.Has("project"))
		{
			initialProject = argParser.Get("project");
			saveInitialProject = true;
			appConfig.maximized = true;
		}
		else if (!openProjectManager && ShouldLoadPreviousProjectOnStartup())
		{
			initialProject = ProjectManager::LoadLastOpenedProject();
			if (!initialProject.Empty())
			{
				appConfig.maximized = true;
			}
		}

		AppResult initResult = AppResult::Failure;
		if (!initialProject.Empty())
		{
			if (saveInitialProject)
			{
				ProjectManager::SaveLastOpenedProject(initialProject);
			}
			initResult = EditorInit(initialProject, appConfig);
		}
		else
		{
			initResult = ProjectManager::Init(projectManagerTab, appConfig);
		}

		if (initResult != AppResult::Continue)
		{
			return initResult == AppResult::Success ? 0 : 1;
		}

		if (App::Run() == AppResult::Failure)
		{
			return 1;
		}

		return 0;
	}
}
