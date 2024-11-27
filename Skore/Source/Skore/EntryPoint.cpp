#include "Skore/Engine.hpp"
#include "Skore/Core/ArgParser.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Editor/Editor.hpp"
#include "Skore/Editor/Launcher/Launcher.hpp"

using namespace Skore;

int main(i32 argc, char** argv)
{
    StdOutSink stdOutSink{};
    Logger::RegisterSink(stdOutSink);

    ArgParser args{};
    args.Parse(argc, argv);

    String projectPath = args.Get("projectPath");
    //
    if (projectPath.Empty())
    {
        Engine::Init(argc, argv);
        Launcher::Init();
        Engine::Run();
        projectPath = Launcher::GetProject();
        Launcher::Shutdown();
        Engine::Destroy();
    }

    if (!projectPath.Empty())
    {
        Engine::Init(argc, argv);
        Editor::Init(projectPath);

        EngineContextCreation contextCreation{
            .title = "Skore Engine",
            .resolution = {1920, 1080},
            .maximize = true,
            .headless = false,
        };

        Engine::CreateContext(contextCreation);

        Engine::Run();
        Engine::Destroy();
    }

    return 0;
}