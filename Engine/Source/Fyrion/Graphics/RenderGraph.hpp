#pragma once

#include <functional>

#include "GraphicsTypes.hpp"
#include "Fyrion/Core/HashMap.hpp"
#include "Fyrion/Core/Optional.hpp"
#include "Fyrion/Core/SharedPtr.hpp"

namespace Fyrion
{
    class Scene;
    class RenderGraph;
    class RenderGraphPass;

    struct FY_API RenderGraphResource
    {
        RenderGraphResourceCreation creation;
        TextureCreation             textureCreation{};
        ResourceLayout              currentLayout = ResourceLayout::Undefined;

        Texture     texture = {};
        Buffer      buffer = {};
        Sampler     sampler = {};
        TextureView textureView = {};
        VoidPtr     reference = {};

        struct ResourceEdges
        {
            RenderGraphPass*        writePass;
            Array<RenderGraphPass*> readPass;
        };

        Array<ResourceEdges> edges;

        void WriteIn(RenderGraphPass* pass);
        void ReadIn(RenderGraphPass* pass);

        Extent3D GetExtent() const;

        ~RenderGraphResource();
    };


    struct FY_API RenderGraphPassHandler
    {
        virtual ~RenderGraphPassHandler() = default;

        RenderGraphPass* pass = nullptr;
        RenderGraph*     rg = nullptr;
        PipelineState    pipelineState = {};
        BindingSet*      bindingSet = nullptr;

        virtual void Init() {}
        virtual void Resize(Extent3D extent) {}
        virtual void Render(RenderCommands& cmd) {}
        virtual void Destroy() {}
    };

    class FY_API RenderGraphPass
    {
    public:
        RenderPass GetRenderPass() const;
        StringView GetName() const;

        friend class RenderGraph;
        friend class RenderPassBuilder;

        ~RenderGraphPass();

    private:
        struct Resource
        {
            RenderGraphResource* resource;
            String               name;
        };

        u32                     id{};
        Extent3D                extent{};
        String                  name{};
        RenderGraphPassType     type{};
        RenderPass              renderPass{};
        Array<Resource>         inputs;
        Array<Resource>         outputs;
        Optional<Vec4>          clearValue;
        Optional<Extent3D>      dispatch;
        bool                    clearDepth{};
        RenderGraphPassHandler* handler = nullptr;
        bool                    ownInstanceInstance = false;
        ShaderState*            shaderState = nullptr;
        PipelineState           pipelineState = {};
        BindingSet*             bindingSet = nullptr;

        void CreateRenderPass();
        void CreatePipeline();
    };

    class FY_API RenderPassBuilder
    {
    public:
        RenderPassBuilder(RenderGraph* rg, RenderGraphPass* pass);

        FY_NO_COPY_CONSTRUCTOR(RenderPassBuilder);

        RenderPassBuilder& Read(RenderGraphResource* resource);
        RenderPassBuilder& Read(StringView name, RenderGraphResource* resource);
        RenderPassBuilder& Write(RenderGraphResource* resource);
        RenderPassBuilder& Write(StringView name, RenderGraphResource* resource);
        RenderPassBuilder& ClearColor(const Vec4& color);
        RenderPassBuilder& ClearDepth(bool clear);
        RenderPassBuilder& Shader(StringView path, StringView state);
        RenderPassBuilder& Dispatch(u32 x, u32 y, u32 z);
        RenderPassBuilder& Handler(RenderGraphPassHandler* handler, bool ownInstance = false);


        template <typename Type, typename... Args>
        RenderPassBuilder& Handler(Args&&... args)
        {
            return Handler(Alloc<Type>(Traits::Forward<Args>(args)...), true);
        }

    private:
        RenderGraph*     rg;
        RenderGraphPass* pass;
    };

    struct RenderGraphCreation
    {
        bool drawToSwapChain = false;
        bool updateCamera = false;
    };

    class FY_API RenderGraph
    {
    public:
        RenderGraph(const RenderGraphCreation& graphCreation);
        ~RenderGraph();

        RenderPassBuilder    AddPass(StringView name, RenderGraphPassType type);
        RenderGraphResource* Create(const RenderGraphResourceCreation& creation);
        void                 Resize(Extent extent);
        void                 Create(Scene* scene, Extent extent);
        Extent               GetViewportExtent() const;
        Scene*               GetScene() const;
        void                 SetCameraData(const CameraData& cameraData);
        const CameraData&    GetCameraData() const;
        void                 ColorOutput(RenderGraphResource* resource);
        void                 DepthOutput(RenderGraphResource* resource);
        Texture              GetColorOutput() const;
        Texture              GetDepthOutput() const;
        void                 RecordCommands(RenderCommands& cmd, f64 deltaTime);

    private:
        RenderGraphCreation                   renderGraphCreation;
        Extent                                viewportExtent;
        Scene*                                scene = nullptr;
        Array<SharedPtr<RenderGraphResource>> resources;
        Array<SharedPtr<RenderGraphPass>>     passes;
        CameraData                            cameraData;
        RenderGraphResource*                  colorOutput = {};
        RenderGraphResource*                  depthOutput = {};


        PipelineState fullscreenPipeline{};
        BindingSet*   bindingSet{};

        void SwapchainRender(RenderCommands& cmd);
        void SwapchainResize(Extent extent);

        void CreateResources();
    };
}
