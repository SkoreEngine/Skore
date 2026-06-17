#include "Skore/Main.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Project/ProjectManager.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Utils/StaticContent.hpp"

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

		if (AppResult result = App::CreateContext(appConfig); result != AppResult::Continue)
		{
			return result == AppResult::Success ? 0 : 1;
		}

		{
			StaticContent::Image icon = StaticContent::GetImage("Content/Images/LogoSmall.jpeg");
			Platform::SetWindowIcon(Graphics::GetWindow(), icon.pixels.Data(), icon.width, icon.height);
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