#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class GPUTexture;
	class GPUCommandBuffer;
	class GPURenderPass;
	class GPUPipeline;
	class GPUDescriptorSet;

	class RenderInterfaceSkore : public Rml::RenderInterface
	{
	public:
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
		void                        RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
		void                        ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

		Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
		void               ReleaseTexture(Rml::TextureHandle texture) override;

		void EnableScissorRegion(bool enable) override;
		void SetScissorRegion(Rml::Rectanglei region) override;

		void SetTransform(const Rml::Matrix4f* transform) override;

		// Called by RmlUiRenderPass once per frame, around Rml::Context::Render(). The render pass
		// owns the command buffer/render pass; the interface only records into what it is handed.
		void BeginFrame(GPUCommandBuffer* cmd, GPURenderPass* renderPass, Extent viewport);
		void EndFrame();

		// Wraps an existing GPU texture (e.g. the engine's MSDF font atlas) as an RmlUi texture handle
		// tagged for MSDF rendering. The texture is borrowed, not owned. Used by FontEngineSkore.
		Rml::TextureHandle CreateMsdfTextureHandle(GPUTexture* atlas);

		void Destroy();

	private:
		GPUPipeline*      EnsurePipeline(bool msdf);
		GPUDescriptorSet* GetWhiteDescriptorSet();

		GPUCommandBuffer* currentCmd = nullptr;
		GPURenderPass*    frameRenderPass = nullptr;
		Extent            viewport = {};
		Mat4              projection = Mat4(1.0);

		GPUPipeline*   pipeline = nullptr;
		GPURenderPass* pipelineRenderPass = nullptr;

		GPUPipeline*   msdfPipeline = nullptr;
		GPURenderPass* msdfPipelineRenderPass = nullptr;

		GPUDescriptorSet* whiteDescriptorSet = nullptr;

		// CSS transform set via SetTransform; identity when none is active.
		Mat4 transform = Mat4(1.0);

		bool            scissorEnabled = false;
		Rml::Rectanglei scissorRegion = {};
	};
}
