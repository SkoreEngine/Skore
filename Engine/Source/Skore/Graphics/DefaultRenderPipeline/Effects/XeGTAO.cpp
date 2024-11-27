#include "XeGTAO.hpp"

#include "Skore/Graphics/Graphics.hpp"
#include "Shaders/Effects/XeGTAO/XeGTAO.h"


namespace Skore
{
    static const char* depthMipNames[XE_GTAO_DEPTH_MIP_LEVELS] = {
        "g_outWorkingDepthMIP0",
        "g_outWorkingDepthMIP1",
        "g_outWorkingDepthMIP2",
        "g_outWorkingDepthMIP3",
        "g_outWorkingDepthMIP4"
    };

    struct XeGTASetupPass : RenderGraphPassHandler
    {
        RenderGraphResource* constantBuffers;

        XeGTASetupPass(RenderGraphResource* constantBuffers) : constantBuffers(constantBuffers) {}

        void Render(RenderCommands& cmd) override
        {
            Extent viewport = rg->GetViewportExtent();

            //TODO: get it from some config
            XeGTAO::GTAOSettings  settings{};
            settings.Radius = 3.0f;
            XeGTAO::GTAOConstants gtaoConstants{};
            GTAOUpdateConstants(gtaoConstants, viewport.width, viewport.height, settings, rg->GetCameraData().projection.a, false, 0);
            MemCopy(gtaoConstants.View.m, rg->GetCameraData().view.a, sizeof(float[16]));

            Graphics::UpdateBufferData(BufferDataInfo{
                .buffer = constantBuffers->buffer,
                .data = &gtaoConstants,
                .size = sizeof(XeGTAO::GTAOConstants)
            });
        }
    };


    struct XeGTADenoisePass : RenderGraphPassHandler
    {
        //TODO: get it from some config
        XeGTAO::GTAOSettings  settings{};


        RenderGraphResource* constantBuffers;
        RenderGraphResource* workingAOTerm;
        RenderGraphResource* workingAOTermPong;
        RenderGraphResource* workingEdges;
        RenderGraphResource* sampler;
        RenderGraphResource* aoOutput;

        PipelineState denoisePass{};
        BindingSet* denoisePassBs{};
        PipelineState denoiseLastPass{};
        BindingSet* denoiseLastPassBs{};

        XeGTADenoisePass(RenderGraphResource* constantBuffers,
                         RenderGraphResource* workingAoTerm,
                         RenderGraphResource* workingAOTermPong,
                         RenderGraphResource* workingEdges,
                         RenderGraphResource* sampler,
                         RenderGraphResource* aoOutput)
            : constantBuffers(constantBuffers),
              workingAOTerm(workingAoTerm),
              workingAOTermPong(workingAOTermPong),
              workingEdges(workingEdges),
              sampler(sampler),
              aoOutput(aoOutput) {}


        void Init() override
        {
            ShaderState* csDenoisePass = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Effects/XeGTAO/vaGTAO.comp")->GetState("CSDenoisePass");
            ShaderState* csDenoiseLastPass = Assets::LoadByPath<ShaderAsset>("Skore://Shaders/Effects/XeGTAO/vaGTAO.comp")->GetState("CSDenoiseLastPass");

            denoisePass = Graphics::CreateComputePipelineState({
                .shaderState = csDenoisePass
            });

            denoisePassBs = Graphics::CreateBindingSet(csDenoisePass);

            denoiseLastPass = Graphics::CreateComputePipelineState({
                .shaderState = csDenoiseLastPass
            });

            denoiseLastPassBs = Graphics::CreateBindingSet(csDenoiseLastPass);
        }

        void Render(RenderCommands& cmd) override
        {
            denoisePassBs->GetVar("g_srcWorkingAOTerm")->SetTexture(workingAOTerm->texture);
            denoisePassBs->GetVar("g_srcWorkingEdges")->SetTexture(workingEdges->texture);
            denoisePassBs->GetVar("g_samplerPointClamp")->SetSampler(sampler->sampler);
            denoisePassBs->GetVar("g_GTAOConsts")->SetBuffer(constantBuffers->buffer);
            denoisePassBs->GetVar("g_outFinalAOTerm")->SetTexture(workingAOTermPong->texture);

            denoiseLastPassBs->GetVar("g_srcWorkingAOTerm")->SetTexture(workingAOTerm->texture);
            denoiseLastPassBs->GetVar("g_srcWorkingEdges")->SetTexture(workingEdges->texture);
            denoiseLastPassBs->GetVar("g_samplerPointClamp")->SetSampler(sampler->sampler);
            denoiseLastPassBs->GetVar("g_GTAOConsts")->SetBuffer(constantBuffers->buffer);
            denoiseLastPassBs->GetVar("g_outFinalAOTerm")->SetTexture(aoOutput->texture);

            const i32 passCount = Math::Max(1, settings.DenoisePasses);
            for (i32 i = 0; i < passCount; ++i)
            {
                const bool lastPass = i == passCount - 1;
                if (!lastPass)
                {
                    Extent3D size = workingAOTermPong->textureCreation.extent;
                    cmd.BindPipelineState(denoisePass);
                    cmd.BindBindingSet(denoisePass, denoisePassBs);
                    cmd.Dispatch((size.width + (XE_GTAO_NUMTHREADS_X * 2) - 1) / XE_GTAO_NUMTHREADS_X, (size.height + (XE_GTAO_NUMTHREADS_Y * 2) - 1) / XE_GTAO_NUMTHREADS_Y, 1);
                }
                else
                {
                    cmd.ResourceBarrier({
                        .texture = aoOutput->texture,
                        .oldLayout = ResourceLayout::ShaderReadOnly,
                        .newLayout = ResourceLayout::General
                    });


                    Extent3D size = aoOutput->textureCreation.extent;
                    cmd.BindPipelineState(denoiseLastPass);
                    cmd.BindBindingSet(denoiseLastPass, denoiseLastPassBs);
                    cmd.Dispatch((size.width + (XE_GTAO_NUMTHREADS_X * 2) - 1) / XE_GTAO_NUMTHREADS_X, (size.height + (XE_GTAO_NUMTHREADS_Y * 2) - 1) / XE_GTAO_NUMTHREADS_Y, 1);


                    cmd.ResourceBarrier({
                        .texture = aoOutput->texture,
                        .oldLayout = ResourceLayout::General,
                        .newLayout = ResourceLayout::ShaderReadOnly,
                    });
                }
            }
        }

        void Destroy() override
        {
            Graphics::DestroyBindingSet(denoisePassBs);
            Graphics::DestroyBindingSet(denoiseLastPassBs);
            Graphics::DestroyComputePipelineState(denoisePass);
            Graphics::DestroyComputePipelineState(denoiseLastPass);
        }
    };

    void XeGTAOSetup(RenderGraph& rg, RenderGraphResource* depth, RenderGraphResource* normals, RenderGraphResource* aoOutput)
    {
        RenderGraphResource* constantBuffers = rg.Create(RenderGraphResourceCreation{
            .name = "g_GTAOConsts",
            .type = RenderGraphResourceType::Buffer,
            .bufferCreation = {
                .usage = BufferUsage::UniformBuffer,
                .size = sizeof(XeGTAO::GTAOConstants),
                .allocation = BufferAllocation::TransferToCPU
            }
        });

        RenderGraphResource* sampler = rg.Create(RenderGraphResourceCreation{
            .name = "g_samplerPointClamp",
            .type = RenderGraphResourceType::Sampler,
            .samplerCreation = {
                .filter = SamplerFilter::Nearest,
                .addressMode = TextureAddressMode::ClampToEdge,
                .comparedEnabled = true
            }
        });

        RenderGraphResource* outWorkingDepth = rg.Create(RenderGraphResourceCreation{
            .name = "g_srcWorkingDepth",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::R32F,
            .mipLevels = XE_GTAO_DEPTH_MIP_LEVELS
        });

        RenderGraphResource* workingDepthMips[XE_GTAO_DEPTH_MIP_LEVELS];

        for (u32 i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i)
        {
            workingDepthMips[i] = rg.Create(RenderGraphResourceCreation{
                .name = depthMipNames[i],
                .type = RenderGraphResourceType::TextureView,
                .textureViewCreation = {
                    .texture = outWorkingDepth,
                    .baseMipLevel = i,
                }
            });
        }

        RenderGraphResource* workingEdges = rg.Create(RenderGraphResourceCreation{
            .name = "xeGTAOWorkingEdges",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::R,
        });

        RenderGraphResourceCreation creation = RenderGraphResourceCreation{
            .name = "xeGTAOWorkingAOTerm",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::R8U,
        };
        RenderGraphResource* workingAOTerm = rg.Create(creation);

        creation.name = "xeGTAOWorkingAOTermPong";
        RenderGraphResource* workingAOTermPong = rg.Create(creation);

        rg.AddPass("XeGTASetupPass", RenderGraphPassType::Other)
          .Write(constantBuffers)
          .Handler<XeGTASetupPass>(constantBuffers);

        RenderPassBuilder& xeGTAOPrefilterPass =
            rg.AddPass("XeGTAOPrefilterPass", RenderGraphPassType::Compute)
              .Shader("Skore://Shaders/Effects/XeGTAO/vaGTAO.comp", "Prefilter")
              .Read(constantBuffers)
              .Read(sampler)
              .Read("g_srcRawDepth", depth)
              .Write(outWorkingDepth)
              .Dispatch(16, 16, 1);

        for (int i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i)
        {
            xeGTAOPrefilterPass.Write(depthMipNames[i], workingDepthMips[i]);
        }

        rg.AddPass("XeGTAOMainPass", RenderGraphPassType::Compute)
          .Shader("Skore://Shaders/Effects/XeGTAO/vaGTAO.comp", "CSGTAOUltra")
          .Read("g_srcWorkingDepth", outWorkingDepth)
          .Read("g_srcNormalmap", normals)
          .Read(sampler)
          .Read(constantBuffers)
          .Write("g_outWorkingAOTerm", workingAOTerm)
          .Write("g_outWorkingEdges", workingEdges)
          .Dispatch(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1);

        rg.AddPass("XeGTADenoisePass", RenderGraphPassType::Compute)
          .Read(workingAOTerm)
          .Read(workingEdges)
          .Write(aoOutput)
          .Handler<XeGTADenoisePass>(constantBuffers, workingAOTerm, workingAOTermPong, workingEdges, sampler, aoOutput);
    }
}
