#include "RenderGraph.hpp"

#include "Graphics.hpp"
#include "RenderProxy.hpp"
#include "Skore/Engine.hpp"
#include "Skore/Core/Graph.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::RenderGraph");
    }

    void RenderGraphResource::WriteIn(RenderGraphPass* pass)
    {
        if (!edges.Empty() && edges.Back().writePass == nullptr)
        {
            edges.Back().writePass = pass;
            return;
        }

        edges.EmplaceBack(ResourceEdges{.writePass = pass});
    }

    void RenderGraphResource::ReadIn(RenderGraphPass* pass)
    {
        if (edges.Empty())
        {
            edges.EmplaceBack(ResourceEdges{});
        }
        edges.Back().readPass.EmplaceBack(pass);
    }

    Extent3D RenderGraphResource::GetExtent() const
    {
        if (creation.type == RenderGraphResourceType::Texture)
        {
            return textureCreation.extent;
        }

        if (creation.type == RenderGraphResourceType::TextureView)
        {
            if (creation.textureViewCreation.texture != nullptr)
            {
                return creation.textureViewCreation.texture->textureCreation.extent;
            }
        }
        return {};
    }

    RenderGraphResource::~RenderGraphResource()
    {
        if (texture && (creation.type == RenderGraphResourceType::Texture || creation.type == RenderGraphResourceType::Attachment))
        {
            Graphics::DestroyTexture(texture);
        }

        if (buffer && creation.type == RenderGraphResourceType::Buffer)
        {
            Graphics::DestroyBuffer(buffer);
        }

        if (sampler && creation.type == RenderGraphResourceType::Sampler)
        {
            Graphics::DestroySampler(sampler);
        }

        if (textureView && creation.type == RenderGraphResourceType::TextureView)
        {
            Graphics::DestroyTextureView(textureView);
        }
    }

    RenderPass RenderGraphPass::GetRenderPass() const
    {
        return renderPass;
    }

    StringView RenderGraphPass::GetName() const
    {
        return name;
    }

    RenderGraphPass::~RenderGraphPass()
    {
        if (handler)
        {
            handler->Destroy();
        }

        if (ownInstanceInstance)
        {
            DestroyAndFree(handler);
        }

        if (renderPass)
        {
            Graphics::DestroyRenderPass(renderPass);
        }

        if (pipelineState)
        {
            if (type == RenderGraphPassType::Compute)
            {
                Graphics::DestroyComputePipelineState(pipelineState);
            }
        }

        if (bindingSet)
        {
            Graphics::DestroyBindingSet(bindingSet);
        }
    }

    void RenderGraphPass::CreateRenderPass()
    {
        if (type == RenderGraphPassType::Graphics)
        {
            LoadOp loadOp = clearDepth || clearValue ? LoadOp::Clear : LoadOp::Load;

            Array<AttachmentCreation> attachments{};
            for (const auto& output : outputs)
            {
                if (output.resource->creation.type == RenderGraphResourceType::Attachment)
                {
                    AttachmentCreation attachmentCreation = AttachmentCreation{
                        .texture = output.resource->texture,
                        .loadOp = loadOp
                    };

                    switch (loadOp)
                    {
                        case LoadOp::Load:
                        {
                            attachmentCreation.initialLayout = output.resource->creation.format != Format::Depth ? ResourceLayout::ColorAttachment : ResourceLayout::DepthStencilAttachment;
                            attachmentCreation.finalLayout = output.resource->creation.format != Format::Depth ? ResourceLayout::ColorAttachment : ResourceLayout::DepthStencilAttachment;
                            break;
                        }
                        case LoadOp::DontCare:
                        case LoadOp::Clear:
                        {
                            attachmentCreation.initialLayout = ResourceLayout::Undefined;
                            attachmentCreation.finalLayout = output.resource->creation.format != Format::Depth ? ResourceLayout::ColorAttachment : ResourceLayout::DepthStencilAttachment;
                            break;
                        }
                    }

                    attachments.EmplaceBack(attachmentCreation);

                    extent = output.resource->textureCreation.extent;
                }
            }

            RenderPassCreation renderPassCreation{
                .attachments = attachments
            };

            renderPass = Graphics::CreateRenderPass(renderPassCreation);
        }
    }

    void RenderGraphPass::CreatePipeline()
    {
        if (shaderState)
        {
            if (type == RenderGraphPassType::Compute)
            {
                pipelineState = Graphics::CreateComputePipelineState({
                    .shaderState = shaderState
                });

                if (handler)
                {
                    handler->pipelineState = pipelineState;
                }

                for (auto& output : outputs)
                {
                    extent = Math::Max(extent, output.resource->GetExtent());
                }
            }

            bindingSet = Graphics::CreateBindingSet(shaderState);
            if (handler)
            {
                handler->bindingSet = bindingSet;
            }
        }
    }

    RenderPassBuilder::RenderPassBuilder(RenderGraph* rg, RenderGraphPass* pass) : rg(rg), pass(pass) {}

    RenderPassBuilder& RenderPassBuilder::Read(RenderGraphResource* resource)
    {
        resource->ReadIn(pass);
        pass->inputs.EmplaceBack(resource, resource->creation.name);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::Read(StringView name, RenderGraphResource* resource)
    {
        resource->ReadIn(pass);
        pass->inputs.EmplaceBack(resource, name);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::Write(RenderGraphResource* resource)
    {
        resource->WriteIn(pass);
        pass->outputs.EmplaceBack(resource, resource->creation.name);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::Write(StringView name, RenderGraphResource* resource)
    {
        resource->WriteIn(pass);
        pass->outputs.EmplaceBack(resource, name);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::ClearColor(const Vec4& color)
    {
        pass->clearValue = color;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::ClearDepth(bool clear)
    {
        pass->clearDepth = clear;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::Shader(StringView path, StringView state)
    {
        pass->shaderState = Assets::LoadByPath<ShaderAsset>(path)->GetState(state);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::Dispatch(u32 x, u32 y, u32 z)
    {
        pass->dispatch = MakeOptional<Extent3D>(x, y, z);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::Handler(RenderGraphPassHandler* handler, bool ownInstance)
    {
        handler->pass = pass;
        handler->rg = rg;
        pass->handler = handler;
        pass->ownInstanceInstance = ownInstance;
        return *this;
    }


    RenderGraph::RenderGraph(const RenderGraphCreation& graphCreation) : renderGraphCreation(graphCreation)
    {
        if (renderGraphCreation.drawToSwapChain)
        {
            Event::Bind<OnSwapchainRender, &RenderGraph::SwapchainRender>(this);
            Event::Bind<OnSwapchainResize, &RenderGraph::SwapchainResize>(this);
            Event::Bind<OnRecordRenderCommands, &RenderGraph::RecordCommands>(this);
        }
    }

    RenderGraph::~RenderGraph()
    {
        Graphics::WaitQueue();

        if (renderGraphCreation.drawToSwapChain)
        {
            Graphics::DestroyGraphicsPipelineState(fullscreenPipeline);
            Graphics::DestroyBindingSet(bindingSet);

            Event::Unbind<OnSwapchainRender, &RenderGraph::SwapchainRender>(this);
            Event::Unbind<OnSwapchainResize, &RenderGraph::SwapchainResize>(this);
            Event::Unbind<OnRecordRenderCommands, &RenderGraph::RecordCommands>(this);
        }
    }

    RenderPassBuilder RenderGraph::AddPass(StringView name, RenderGraphPassType type)
    {
        SharedPtr<RenderGraphPass> pass = MakeShared<RenderGraphPass>();
        pass->id = passes.Size() + 1;
        pass->name = name;
        pass->type = type;
        passes.EmplaceBack(pass);
        return {this, pass.Get()};
    }

    RenderGraphResource* RenderGraph::Create(const RenderGraphResourceCreation& creation)
    {
        SharedPtr<RenderGraphResource> resource = MakeShared<RenderGraphResource>(creation);
        resources.EmplaceBack(resource);
        return resource.Get();
    }

    void RenderGraph::Resize(Extent extent)
    {
        this->viewportExtent = extent;
        Graphics::WaitQueue();

        for (auto& resource : resources)
        {
            if ((resource->creation.type == RenderGraphResourceType::Texture || resource->creation.type == RenderGraphResourceType::Attachment)
                && resource->creation.scale > 0.f)
            {
                Texture oldTexture = resource->texture;

                Extent size = Extent{viewportExtent.width, viewportExtent.height} * resource->creation.scale;
                resource->textureCreation.extent = {(size.width), (size.height), 1};
                resource->textureCreation.name = resource->creation.name;
                resource->texture = Graphics::CreateTexture(resource->textureCreation);

                //defer destroy to avoid getting the same pointer address for the next texture
                if (oldTexture)
                {
                    Graphics::DestroyTexture(oldTexture);
                }

                if (resource->creation.type == RenderGraphResourceType::Texture)
                {
                    Graphics::UpdateTextureLayout(resource->texture, ResourceLayout::Undefined, ResourceLayout::ShaderReadOnly);
                    resource->currentLayout = ResourceLayout::ShaderReadOnly;
                }
            }

            if (resource->creation.type == RenderGraphResourceType::TextureView)
            {
                if (resource->textureView)
                {
                    Graphics::DestroyTextureView(resource->textureView);
                }

                TextureViewCreation creation = resource->creation.textureViewCreation.ToTextureViewCreation();
                creation.texture = resource->creation.textureViewCreation.texture->texture;
                resource->textureView = Graphics::CreateTextureView(creation);
                resource->currentLayout = ResourceLayout::Undefined;
            }
        }

        for (auto& pass : passes)
        {
            if (pass->type == RenderGraphPassType::Graphics)
            {
                if (pass->renderPass)
                {
                    Graphics::DestroyRenderPass(pass->renderPass);
                }
                pass->CreateRenderPass();
            }
            else if (pass->type == RenderGraphPassType::Compute)
            {
                for (auto& output : pass->outputs)
                {
                    pass->extent = Math::Max(pass->extent, output.resource->GetExtent());
                }
            }

            if (pass->handler)
            {
                pass->handler->Resize(pass->extent);
            }
        }
    }

    void RenderGraph::Create(Scene* scene, Extent extent)
    {
        this->viewportExtent = extent;
        this->scene = scene;

        // SK_ASSERT(this->colorOutput, "color output must be provided");
        // SK_ASSERT(this->depthOutput, "depth output must be provided");

        CreateResources();

        Graph<u32, SharedPtr<RenderGraphPass>> graph{};
        for (auto& pass : passes)
        {
            graph.AddNode(pass->id, pass);
        }

        for (auto& resource : resources)
        {
            for (auto& edge : resource->edges)
            {
                for (auto& read : edge.readPass)
                {
                    if (edge.writePass)
                    {
                        graph.AddEdge(read->id, edge.writePass->id);
                    }
                }
            }
        }

        passes = graph.Sort();

        for (auto& pass : passes)
        {
            pass->CreateRenderPass();
            pass->CreatePipeline();

            if (pass->handler)
            {
                pass->handler->Init();
            }

            logger.Debug("pass {} created ", pass->name);
        }


        if (renderGraphCreation.drawToSwapChain)
        {
            auto format = Format::BGRA;
            GraphicsPipelineCreation creation = {
                .shaderState = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Fullscreen.raster")->GetDefaultState(),
                .attachments = {&format, 1},
            };

            fullscreenPipeline = Graphics::CreateGraphicsPipelineState(creation);
            bindingSet = Graphics::CreateBindingSet(creation.shaderState);

            bindingSet->GetVar("texture")->SetTexture(GetColorOutput());
        }
    }

    Extent RenderGraph::GetViewportExtent() const
    {
        return viewportExtent;
    }

    Scene* RenderGraph::GetScene() const
    {
        return scene;
    }

    void RenderGraph::SetCameraData(const CameraData& cameraData)
    {
        this->cameraData = cameraData;
    }

    const CameraData& RenderGraph::GetCameraData() const
    {
        return this->cameraData;
    }

    void RenderGraph::ColorOutput(RenderGraphResource* resource)
    {
        this->colorOutput = resource;
    }

    void RenderGraph::DepthOutput(RenderGraphResource* resource)
    {
        this->depthOutput = resource;
    }

    Texture RenderGraph::GetColorOutput() const
    {
        if(colorOutput)
        {
            return colorOutput->texture;
        }
        return {};
    }

    Texture RenderGraph::GetDepthOutput() const
    {
        if(depthOutput)
        {
            return depthOutput->texture;
        }
        return {};
    }

    void RenderGraph::RecordCommands(RenderCommands& cmd, f64 deltaTime)
    {
        if (renderGraphCreation.updateCamera)
        {
            RenderProxy* renderProxy = scene->GetProxy<RenderProxy>();
            if (renderProxy != nullptr && renderProxy->GetCamera() != nullptr)
            {
                const CameraData* gameCamera = renderProxy->GetCamera();

                cameraData.view = gameCamera->view;
                cameraData.projectionType = gameCamera->projectionType;
                cameraData.fov = gameCamera->fov;
                cameraData.viewPos = gameCamera->viewPos;
                cameraData.nearClip = gameCamera->nearClip;
                cameraData.farClip = gameCamera->farClip;
            }

            if (cameraData.projectionType == CameraProjection::Perspective)
            {
                cameraData.projection = Math::Perspective(Math::Radians(cameraData.fov),
                                                          static_cast<f32>(viewportExtent.width) / static_cast<f32>(viewportExtent.height),
                                                          cameraData.nearClip,
                                                          cameraData.farClip);
            }

            cameraData.lastProjView = cameraData.projView;
            cameraData.projView = cameraData.projection * cameraData.view;
            cameraData.viewInverse = Math::Inverse(cameraData.view);
            cameraData.projectionInverse = Math::Inverse(cameraData.projection);
        }

        for (auto& pass : passes)
        {
            if (pass->type != RenderGraphPassType::Other)
            {
                cmd.BeginLabel(pass->name, {0, 0, 0, 1});
            }

            if (pass->type == RenderGraphPassType::Compute)
            {
                for (const auto& input : pass->inputs)
                {
                    if (input.resource->creation.type == RenderGraphResourceType::Texture || input.resource->creation.type == RenderGraphResourceType::Attachment)
                    {
                        ResourceLayout newLayout = input.resource->creation.format != Format::Depth ? ResourceLayout::ShaderReadOnly : ResourceLayout::DepthStencilReadOnly;
                        if (input.resource->currentLayout != newLayout)
                        {
                            for (int m = 0; m < input.resource->textureCreation.mipLevels; ++m)
                            {
                                ResourceBarrierInfo resourceBarrierInfo{};
                                resourceBarrierInfo.texture = input.resource->texture;
                                resourceBarrierInfo.oldLayout = input.resource->currentLayout;
                                resourceBarrierInfo.newLayout = newLayout;
                                resourceBarrierInfo.mipLevel = m;
                                cmd.ResourceBarrier(resourceBarrierInfo);
                            }

                            input.resource->currentLayout = newLayout;
                        }
                    }
                }
            }

            for (const auto& output : pass->outputs)
            {
                if (output.resource->creation.type == RenderGraphResourceType::Texture)
                {
                    if (output.resource->currentLayout != ResourceLayout::General)
                    {
                        for (int m = 0; m < output.resource->textureCreation.mipLevels; ++m)
                        {
                            ResourceBarrierInfo resourceBarrierInfo{};
                            resourceBarrierInfo.texture = output.resource->texture;
                            resourceBarrierInfo.oldLayout = output.resource->currentLayout;
                            resourceBarrierInfo.newLayout = ResourceLayout::General;
                            resourceBarrierInfo.mipLevel = m;
                            cmd.ResourceBarrier(resourceBarrierInfo);
                        }
                        output.resource->currentLayout = ResourceLayout::General;
                    }
                }
            }

            if (pass->renderPass)
            {
                BeginRenderPassInfo renderPassInfo{};
                renderPassInfo.renderPass = pass->renderPass;

                if (pass->clearValue)
                {
                    renderPassInfo.clearValue = &*pass->clearValue;
                }

                ClearDepthStencilValue clearDepthStencilValue{};

                if (pass->clearDepth)
                {
                    renderPassInfo.depthStencil = &clearDepthStencilValue;
                }

                cmd.BeginRenderPass(renderPassInfo);

                ViewportInfo viewportInfo{};
                viewportInfo.x = 0.;
                viewportInfo.y = 0.;
                viewportInfo.y = (f32)pass->extent.height;
                viewportInfo.width = (f32)pass->extent.width;
                viewportInfo.height = -(f32)pass->extent.height;
                viewportInfo.minDepth = 0.;
                viewportInfo.maxDepth = 1.;

                cmd.SetViewport(viewportInfo);

                auto scissor = Rect{0, 0, pass->extent.width, pass->extent.height};
                cmd.SetScissor(scissor);
            }

            auto SetResource = [this](RenderGraphPass* pass, const RenderGraphPass::Resource& input)
            {
                if (input.resource->creation.type == RenderGraphResourceType::Texture ||
                    input.resource->creation.type == RenderGraphResourceType::Attachment)
                {
                    pass->bindingSet->GetVar(input.name)->SetTexture(input.resource->texture);
                }
                else if (input.resource->creation.type == RenderGraphResourceType::Buffer)
                {
                    pass->bindingSet->GetVar(input.name)->SetBuffer(input.resource->buffer);
                }
                else if (input.resource->creation.type == RenderGraphResourceType::TextureView)
                {
                    pass->bindingSet->GetVar(input.name)->SetTextureView(input.resource->textureView);
                }
            };

            if (pass->bindingSet)
            {
                for (auto& input : pass->inputs)
                {
                    SetResource(pass.Get(), input);
                }

                for (auto& output : pass->outputs)
                {
                    SetResource(pass.Get(), output);
                }
            }


            if (pass->dispatch && pass->pipelineState && pass->bindingSet)
            {
                cmd.BindPipelineState(pass->pipelineState);
                cmd.BindBindingSet(pass->pipelineState, pass->bindingSet);
                cmd.Dispatch((pass->extent.width + pass->dispatch->width - 1) / pass->dispatch->width,
                             (pass->extent.height + pass->dispatch->height - 1) / pass->dispatch->height,
                             pass->dispatch->depth);
            }

            if (pass->handler)
            {
                pass->handler->Render(cmd);
            }

            if (pass->renderPass)
            {
                cmd.EndRenderPass();

                for (auto output : pass->outputs)
                {
                    if (output.resource->creation.type == RenderGraphResourceType::Attachment)
                    {
                        output.resource->currentLayout = output.resource->textureCreation.format != Format::Depth ? ResourceLayout::ColorAttachment : ResourceLayout::DepthStencilAttachment;
                    }
                }
            }

            if (pass->type != RenderGraphPassType::Other)
            {
                cmd.EndLabel();
            }
        }

        if (colorOutput && colorOutput->currentLayout != ResourceLayout::ShaderReadOnly)
        {
            ResourceBarrierInfo resourceBarrierInfo{};
            resourceBarrierInfo.texture = colorOutput->texture;
            resourceBarrierInfo.oldLayout = colorOutput->currentLayout;
            resourceBarrierInfo.newLayout = ResourceLayout::ShaderReadOnly;
            cmd.ResourceBarrier(resourceBarrierInfo);

            colorOutput->currentLayout = ResourceLayout::ShaderReadOnly;
        }

        if (depthOutput && depthOutput->currentLayout != ResourceLayout::DepthStencilReadOnly)
        {
            ResourceBarrierInfo resourceBarrierInfo{};
            resourceBarrierInfo.texture = depthOutput->texture;
            resourceBarrierInfo.oldLayout = depthOutput->currentLayout;
            resourceBarrierInfo.newLayout = ResourceLayout::DepthStencilReadOnly;
            cmd.ResourceBarrier(resourceBarrierInfo);

            depthOutput->currentLayout = ResourceLayout::DepthStencilReadOnly;
        }
    }

    void RenderGraph::SwapchainRender(RenderCommands& cmd)
    {
        cmd.BindPipelineState(fullscreenPipeline);
        cmd.BindBindingSet(fullscreenPipeline, bindingSet);
        cmd.Draw(3, 1, 0, 0);
    }

    void RenderGraph::SwapchainResize(Extent extent)
    {
        this->Resize(extent);
        bindingSet->GetVar("texture")->SetTexture(GetColorOutput());
    }

    void RenderGraph::CreateResources()
    {
        for (auto& resource : resources)
        {
            switch (resource->creation.type)
            {
                case RenderGraphResourceType::None:
                    break;
                case RenderGraphResourceType::Buffer:
                {
                    if (resource->creation.bufferCreation.size > 0)
                    {
                        resource->buffer = Graphics::CreateBuffer(resource->creation.bufferCreation);
                    }
                    break;
                }
                case RenderGraphResourceType::Sampler:
                {
                    resource->sampler = Graphics::CreateSampler(resource->creation.samplerCreation);
                    break;
                }
                case RenderGraphResourceType::TextureView:
                {
                    SK_ASSERT(resource->creation.textureViewCreation.texture->texture, "something went wrong");

                    TextureViewCreation creation = resource->creation.textureViewCreation.ToTextureViewCreation();
                    creation.texture = resource->creation.textureViewCreation.texture->texture;
                    resource->textureView = Graphics::CreateTextureView(creation);
                    break;
                }
                case RenderGraphResourceType::Texture:
                case RenderGraphResourceType::Attachment:
                {
                    if (resource->creation.size > 0)
                    {
                        resource->textureCreation.extent = resource->creation.size;
                    }
                    else if (resource->creation.scale > 0.f)
                    {
                        Extent size = Extent{viewportExtent.width, viewportExtent.height} * resource->creation.scale;
                        resource->textureCreation.extent = {size.width, size.height, 1};
                    }
                    else
                    {
                        SK_ASSERT(false, "texture without size");
                    }

                    resource->textureCreation.name = resource->creation.name;
                    resource->textureCreation.format = resource->creation.format;
                    resource->textureCreation.mipLevels = resource->creation.mipLevels;

                    resource->textureCreation.usage = TextureUsage::ShaderResource | TextureUsage::TransferSrc;

                    if (resource->creation.type == RenderGraphResourceType::Attachment)
                    {
                        if (resource->textureCreation.format == Format::Depth)
                        {
                            resource->textureCreation.usage |= TextureUsage::DepthStencil;
                        }
                        else
                        {
                            resource->textureCreation.usage |= TextureUsage::RenderPass;
                        }
                    }
                    else if (resource->creation.type == RenderGraphResourceType::Texture)
                    {
                        resource->textureCreation.usage |= TextureUsage::Storage;
                    }

                    resource->texture = Graphics::CreateTexture(resource->textureCreation);

                    if (resource->creation.type == RenderGraphResourceType::Texture)
                    {
                        Graphics::UpdateTextureLayout(resource->texture, ResourceLayout::Undefined, ResourceLayout::ShaderReadOnly);
                        resource->currentLayout = ResourceLayout::ShaderReadOnly;
                    }
                    break;
                }
                case RenderGraphResourceType::Reference:
                {
                    break;
                }
            }
            logger.Debug("Created resource {} ", resource->creation.name);
        }
    }
}
