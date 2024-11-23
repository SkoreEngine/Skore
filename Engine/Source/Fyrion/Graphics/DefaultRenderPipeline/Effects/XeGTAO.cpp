#include "XeGTAO.hpp"

#include "Fyrion/Graphics/Graphics.hpp"
#include "Shaders/Effects/XeGTAO/XeGTAO.h"


namespace Fyrion
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

            XeGTAO::GTAOSettings  settings{};
            XeGTAO::GTAOConstants gtaoConstants{};
            GTAOUpdateConstants(gtaoConstants, viewport.width, viewport.height, settings, rg->GetCameraData().projection.a, false, 0);

            Graphics::UpdateBufferData(BufferDataInfo{
                .buffer = constantBuffers->buffer,
                .data = &gtaoConstants,
                .size = sizeof(XeGTAO::GTAOConstants)
            });
        }
    };

    void XeGTAOSetup(RenderGraph& rg, RenderGraphResource* depth)
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
            }
        });

        RenderGraphResource* outWorkingDepth = rg.Create(RenderGraphResourceCreation{
            .name = "g_srcWorkingDepth",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::Depth,
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

        RenderGraphResource* workingAOTerm = rg.Create(RenderGraphResourceCreation{
            .name = "XeGTAOWorkingAOTerm",
            .type = RenderGraphResourceType::Texture,
            .scale = {1, 1},
            .format = Format::R,
        });

        rg.AddPass("XeGTASetupPass", RenderGraphPassType::Other)
          .Write(constantBuffers)
          .Handler<XeGTASetupPass>(constantBuffers);

        RenderPassBuilder& xeGTAOPrefilterPass =
            rg.AddPass("XeGTAOPrefilterPass", RenderGraphPassType::Compute)
              .Shader("Fyrion://Shaders/Effects/XeGTAO/vaGTAO.comp", "Prefilter")
              .Read(constantBuffers)
              .Read(sampler)
              .Read("g_srcRawDepth", depth)
              .Dispatch(16, 16, 1);

        for (int i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i)
        {
            xeGTAOPrefilterPass.Write(depthMipNames[i], workingDepthMips[i]);
        }

        // rg.AddPass("XeGTAOMainPass", RenderGraphPassType::Compute)
        //   .Shader("Fyrion://Shaders/Effects/XeGTAO/vaGTAO.comp", "CSGTAOUltra")
        //   .Read("g_srcWorkingDepth", outWorkingDepth)
        //   .Read(sampler)
        //   .Write("g_outWorkingAOTerm", workingAOTerm)
        //   .Write("g_outWorkingEdges", workingEdges)
        //   .Dispatch(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1);

    }
}
