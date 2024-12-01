#include "imgui.h"
#include "Skore/Engine.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Core/SharedPtr.hpp"
#include "Skore/Core/Sinks.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/IO/Asset.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Scene/Scene.hpp"

using namespace Skore;


struct BinaryAssetLoader : AssetLoader
{
    Asset* LoadAsset() override
    {
        if (TypeHandler* typeHandler = Registry::FindTypeByName(typeName))
        {
            Asset* asset = typeHandler->Cast<Asset>(typeHandler->NewInstance());
            asset->SetTypeHandler(typeHandler);

            FileHandler file = FileSystem::OpenFile(pakFile, AccessMode::ReadOnly);

            String temp;
            temp.Resize(assetSize);
            usize read = FileSystem::ReadFileAt(file, temp.begin(), assetSize, assetOffset);
            SK_ASSERT(read, "file cannot be read");

            FileSystem::CloseFile(file);

            JsonArchiveReader reader(temp, true);
            Serialization::Deserialize(typeHandler, reader, reader.GetRoot(), asset);

            return asset;
        }

        return nullptr;
    }

    Array<u8> LoadStream(usize offset, usize size) override
    {
        FileHandler file = FileSystem::OpenFile(pakFile, AccessMode::ReadOnly);

        Array<u8> temp;
        temp.Resize(size);

        FileSystem::ReadFileAt(file, temp.begin(), size, streamOffset + offset);
        FileSystem::CloseFile(file);

        return temp;
    }

    StringView GetName() override
    {
        return name;
    }

    String name;
    String typeName;
    String pakFile;

    u64 assetOffset{};
    u64 assetSize{};
    u64 streamOffset{};
    u64 streamSize{};
};

Array<SharedPtr<BinaryAssetLoader>> assets;

Logger&         logger = Logger::GetLogger("Skore::Player", LogLevel::Debug);
Scene*          scene;
RenderPipeline* renderPipeline{};
RenderGraph*    renderGraph;
Extent          resolution;

void InitPlayer()
{
    resolution = Engine::GetViewportExtent();

    //load assets
    String assetDir = Path::Join(FileSystem::CurrentDir(), "Assets");
    for (const String& entry : DirectoryEntries{assetDir})
    {
        if (Path::Extension(entry) == ".assets")
        {
            String pakFile = Path::Join(assetDir, Path::Name(entry), ".pak");

            JsonArchiveReader reader(FileSystem::ReadFileAsString(entry));
            ArchiveValue      arr = reader.GetRoot();
            usize             size = reader.ArraySize(arr);

            ArchiveValue item = {};
            for (int i = 0; i < size; ++i)
            {
                item = reader.ArrayNext(arr, item);

                BinaryAssetLoader* loader = assets.EmplaceBack(MakeShared<BinaryAssetLoader>()).Get();
                loader->pakFile = pakFile;
                loader->name = reader.StringValue(reader.GetObjectValue(item, "name"));
                loader->typeName = reader.StringValue(reader.GetObjectValue(item, "type"));
                loader->assetOffset = reader.UIntValue(reader.GetObjectValue(item, "assetOffset"));
                loader->assetSize = reader.UIntValue(reader.GetObjectValue(item, "assetSize"));
                loader->streamOffset = reader.UIntValue(reader.GetObjectValue(item, "streamOffset"));
                loader->streamSize = reader.UIntValue(reader.GetObjectValue(item, "streamSize"));

                UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(item, "uuid")));
                String path = reader.StringValue(reader.GetObjectValue(item, "path"));

                Assets::Create(uuid, loader);
                Assets::SetPath(uuid, path);

               //logger.Debug("asset {} created with path {} ", loader->name, path);
            }
        }
    }

    //load scene;
    scene = Assets::Load<Scene>(Skore::UUID::FromString("ec4936c5-29b5-7842-10f3-d483a125aaf7"));
    if (scene)
    {
        scene->Start();
    }

    TypeHandler* type = Registry::FindTypeByName("Skore::DefaultRenderPipeline");
    renderPipeline = type->Cast<RenderPipeline>(type->NewInstance());

    renderGraph = Alloc<RenderGraph>(RenderGraphCreation{
        .drawToSwapChain = true,
        .updateCamera = true
    });

    renderPipeline->BuildRenderGraph(*renderGraph);
    renderGraph->Create(scene, resolution);
}

void UpdatePlayer(f64 deltaTime)
{
    if (scene)
    {
        scene->Update();
    }

    // ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    // ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
    // if (ImGui::Begin("FPS", 0, window_flags))
    // {
    //     auto& io = ImGui::GetIO();
    //     ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    //     ImGui::End();
    // }

}

void ShutdownPlayer()
{
    DestroyAndFree(renderGraph);
    DestroyAndFree(renderPipeline);
}

int main(i32 argc, char** argv)
{
    StdOutSink stdOutSink{};
    Logger::RegisterSink(stdOutSink);

    Engine::Init(argc, argv);

    Event::Bind<OnInit, &InitPlayer>();
    Event::Bind<OnUpdate, &UpdatePlayer>();
    Event::Bind<OnShutdown, &ShutdownPlayer>();

    EngineContextCreation contextCreation{
        .title = "Skore Engine",
        .resolution = {1920, 1080},
        .maximize = true,
        .headless = false,
    };

    Engine::CreateContext(contextCreation);

    Engine::Run();
    Engine::Destroy();

    return 0;
}
