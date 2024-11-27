#include "ReflectionProbe.hpp"

#include "TransformComponent.hpp"
#include "Fyrion/Core/Logger.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Core/StringUtils.hpp"
#include "Fyrion/Graphics/Graphics.hpp"
#include "Fyrion/Graphics/RenderGraph.hpp"
#include "Fyrion/Graphics/RenderPipeline.hpp"
#include "Fyrion/Graphics/RenderProxy.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "Fyrion/Scene/Scene.hpp"

namespace Fyrion
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Fyrion::ReflectionProbe");
    }

    void ReflectionProbe::RegisterType(NativeTypeHandler<ReflectionProbe>& type)
    {
        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }

    void ReflectionProbe::Bake()
    {
        u16 mips = 6;
        u32 size = 256;
        specularMapGenerator.Init({size, size}, 6);

        RenderProxy* renderProxy = gameObject->GetScene()->GetProxy<RenderProxy>();

        Texture cubemapTest = Graphics::CreateTexture({
            .extent = {size, size, 1},
            .format = Format::RGBA,
            //.mipLevels = mips,
            .arrayLayers = 6,
            .defaultView = ViewType::TypeCube,
            .name = "CubemapTest"
        });

        Graphics::UpdateTextureLayout(cubemapTest, ResourceLayout::Undefined, ResourceLayout::CopyDest);

        TransformComponent* transformComponent = gameObject->GetComponent<TransformComponent>();

        logger.Info("starting bake");
        TypeHandler* type = Registry::FindTypeByName("Fyrion::DefaultRenderPipeline");
        RenderPipeline* renderPipeline = type->Cast<RenderPipeline>(type->NewInstance());

        RenderGraph* renderGraph = Alloc<RenderGraph>(RenderGraphCreation{
            .drawToSwapChain = false,
            .updateCamera = false
        });

        renderPipeline->BuildRenderGraph(*renderGraph);
        renderGraph->Create(gameObject->GetScene(), Extent{size, size});

        Mat4 projection = Math::Perspective(Math::Radians(90.f), 1, 0.1, 200);

        for (u32 i = 0; i < 6; i++)
        {
            Mat4 view;

            if (i == 0)
            {
                view = Math::Inverse(Math::Translate(Mat4{1.0}, transformComponent->GetWorldPosition()) * Math::ToMatrix4(Quat(Vec3{0, Math::Radians(270.f), 0})));
            }
            else if (i == 1)
            {
                view = Math::Inverse(Math::Translate(Mat4{1.0}, transformComponent->GetWorldPosition()) * Math::ToMatrix4(Quat(Vec3{0, Math::Radians(90.f), 0})));
            }
            else if (i == 2)
            {
                view = Math::Inverse(Math::Translate(Mat4{1.0}, transformComponent->GetWorldPosition()) * Math::ToMatrix4(Quat(Vec3{Math::Radians(90.f), 0, 0})));
            }
            else if (i == 3)
            {
                view = Math::Inverse(Math::Translate(Mat4{1.0}, transformComponent->GetWorldPosition()) * Math::ToMatrix4(Quat(Vec3{Math::Radians(-90.f), 0, 0})));
            }
            else if (i == 4)
            {
                view = Math::Inverse(Math::Translate(Mat4{1.0}, transformComponent->GetWorldPosition()) * Math::ToMatrix4(Quat(Vec3{0, Math::Radians(180.f), 0})));
            }
            else
            {
                view = Math::Inverse(Math::Translate(Mat4{1.0}, transformComponent->GetWorldPosition()) * Math::ToMatrix4(Quat(Vec3{0, 0, 0})));
            }

            renderGraph->SetCameraData(CameraData{
                .view = view,
                .viewInverse = Math::Inverse(view),
                .projection = projection,
                .projectionInverse = Math::Inverse(projection),
                .projView = projection * view,
                .lastProjView = {},
                .viewPos = transformComponent->GetWorldPosition(),
                .fov = 90.f,
                .nearClip = 1.0,
                .farClip = 200
            });

            RenderCommands& cmd = Graphics::GetCmd();
            cmd.Begin();
            renderGraph->RecordCommands(cmd, 0);

            TextureCopy textureCopy{};
            textureCopy.extent = {size, size, 1};
            textureCopy.srcSubresource.textureAspect = TextureAspect::Color,
            textureCopy.dstSubresource.textureAspect = TextureAspect::Color;
            textureCopy.dstSubresource.baseArrayLayer = i;

            cmd.CopyTexture(renderGraph->GetColorOutput(), ResourceLayout::ShaderReadOnly,
                            cubemapTest, ResourceLayout::CopyDest,
                            {&textureCopy, 1});

            cmd.SubmitAndWait(Graphics::GetMainQueue());

            // Array<u8> textureData;
            //
            // Graphics::GetTextureData(TextureGetDataInfo{
            //                              .texture = renderGraph->GetColorOutput(),
            //                              .format = Format::RGBA,
            //                              .extent = {size, size},
            //                              .textureLayout = ResourceLayout::ShaderReadOnly
            //                          }, textureData);
            //
            // Image image(size, size, 4);
            // image.data = textureData;
            //
            // String str = String("C:\\dev\\Fyrion\\test").Append("_").Append(ToString(i)).Append(".png");
            // image.SaveAsPNG(str);
        }

        Graphics::UpdateTextureLayout(cubemapTest, ResourceLayout::CopyDest, ResourceLayout::ShaderReadOnly);
        //RenderUtils::GenerateCubemapMips(cubemapTest, {size, size}, mips);

        RenderCommands& cmd = Graphics::GetCmd();
        cmd.Begin();
        specularMapGenerator.Generate(cmd, cubemapTest);
        cmd.SubmitAndWait(Graphics::GetMainQueue());

        DestroyAndFree(renderGraph);
        DestroyAndFree(renderPipeline);

        renderProxy->cubemapTest = specularMapGenerator.GetTexture();
        logger.Info("bake finished");
    }
}
