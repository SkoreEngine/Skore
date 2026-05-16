#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"

#include "json/value.h"
#include "Skore/Core/Algorithm.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/Pipelines/PipelineCommon.hpp"

namespace Skore
{
	RenderPipelinePassSetup CascadeShadowPassBase::GetPassSetup()
	{
		RenderPipelinePassSetup setup;
		setup.type = RenderPipelinePassType::Other;
		setup.stage = DefaultPipelineRenderStage::ShadowPass;
		setup.dependencies.EmplaceBack(RenderPipelinePassDependency{.name = ShadowMapInstanceDataName, .access = RenderPipelineTextureAccess::Write});
		return setup;
	}

	void CascadeShadowPassBase::Init()
	{
		shadowMapData = context->GetInstanceData<ShadowMapInstanceData>(ShadowMapInstanceDataName);

		shadowMapData->cascadeSplits.Resize(shadowMapData->numCascades);
		shadowMapData->cascadeViewProjMat.Resize(shadowMapData->numCascades);
		m_cascadeOffsets.Resize(shadowMapData->numCascades);
		m_cascadeScales.Resize(shadowMapData->numCascades);

		m_shadowMapTextureViews.Resize(shadowMapData->numCascades);
		m_shadowMapRenderPass.Resize(shadowMapData->numCascades);
		m_shadowMapFramebuffers.Resize(shadowMapData->numCascades);

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			m_shadowMapDescriptorSets[f].Resize(shadowMapData->numCascades);
		}

		u64 uboAlignment = Graphics::GetDevice()->GetProperties().limits.minUniformBufferOffsetAlignment;
		m_uniformBufferAlignedSize = AlignedSize(static_cast<u64>(sizeof(Mat4)), uboAlignment);

		shadowMapUniformBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = m_uniformBufferAlignedSize * shadowMapData->numCascades * SK_FRAMES_IN_FLIGHT,
			.usage = ResourceUsage::ConstantBuffer,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = "ShadowMapUniformBuffer"
		});

		for (u32 i = 0; i < shadowMapData->numCascades; ++i)
		{
			m_shadowMapTextureViews[i] = Graphics::CreateTextureView(TextureViewDesc{
				.texture = shadowMapData->shadowTexture,
				.type = TextureViewType::Type2DArray,
				.baseArrayLayer = i
			});

			m_shadowMapRenderPass[i] = Graphics::CreateRenderPass(RenderPassDesc{
				.attachments = {
					AttachmentDesc{
						.finalState = ResourceState::DepthStencilAttachment,
						.loadOp = AttachmentLoadOp::Clear,
						.storeOp = AttachmentStoreOp::Store,
						.format = TextureFormat::D32_FLOAT,
					}
				},
				.debugName = "ShadowMapRenderPass_Cascade_" + ToString(i)
			});

			m_shadowMapFramebuffers[i] = Graphics::CreateFramebuffer(FramebufferDesc{
				.renderPass = m_shadowMapRenderPass[i],
				.attachments = {
					m_shadowMapTextureViews[i]
				},
				.debugName = "ShadowMapFramebuffer_Cascade_" + ToString(i)
			});

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				m_shadowMapDescriptorSets[f][i] = Graphics::CreateDescriptorSet(DescriptorSetDesc{
					.bindings = {
						DescriptorSetLayoutBinding{
							.binding = 0,
							.descriptorType = DescriptorType::UniformBuffer
						}
					}
				});

				DescriptorUpdate descriptorUpdate;
				descriptorUpdate.type = DescriptorType::UniformBuffer;
				descriptorUpdate.binding = 0;
				descriptorUpdate.buffer = shadowMapUniformBuffer;
				descriptorUpdate.bufferOffset = m_uniformBufferAlignedSize * (f * shadowMapData->numCascades + i);
				descriptorUpdate.bufferRange = sizeof(Mat4);
				m_shadowMapDescriptorSets[f][i]->Update(descriptorUpdate);
			}
		}

	}

	void CascadeShadowPassBase::Render(RenderSceneObjects* objects, GPUCommandBuffer* cmd)
	{
		if (!objects) return;

		//reset pipelines in case the scene change and the pipeline order is different.
		if (cachedPipelineObjects != nullptr && cachedPipelineObjects != objects)
		{
			for (GPUPipeline* p : shadowMapPipelines)
			{
				p->Destroy();
			}
			shadowMapPipelines.Clear();
		}

		Vec3 lightDir = {};
		u32  lightIndex = {};

		if (!objects->GetDirectionalLightShadowData(lightIndex, lightDir))
		{
			return;
		}

		float cascadeSplits[MaxNumCascades];

		float nearClip = context->camera.nearClip;
		float farClip = context->camera.farClip;
		float clipRange = farClip - nearClip;

		float minZ = nearClip;
		float maxZ = nearClip + clipRange;

		float range = maxZ - minZ;
		float ratio = maxZ / minZ;

		for (u32 i = 0; i < shadowMapData->numCascades; i++)
		{
			float p = (i + 1) / static_cast<float>(shadowMapData->numCascades);
			float log = minZ * std::pow(ratio, p);
			float uniform = minZ + range * p;
			float d = m_lambda * (log - uniform) + uniform;
			cascadeSplits[i] = (d - nearClip) / clipRange;
		}

		Mat4 invCam = Mat4::Inverse(context->camera.viewProjectionNoJitter);

		u32 frame = context->GetCurrentFrame();

		cmd->BeginDebugMarker("Cascade shadow maps", Vec4{0, 0, 0, 1});

		// Calculate orthographic projection matrix for each cascade
		float lastSplitDist = 0.0;
		for (uint32_t i = 0; i < shadowMapData->numCascades; i++)
		{
			cmd->BeginDebugMarker("Cascade: " + ToString(i), Vec4{0, 0, 0, 1});

			float splitDist = cascadeSplits[i];

			Vec3 frustumCorners[8] = {
				Vec3{-1.0f, 1.0f, 0.0f},
				Vec3{1.0f, 1.0f, 0.0f},
				Vec3{1.0f, -1.0f, 0.0f},
				Vec3{-1.0f, -1.0f, 0.0f},
				Vec3{-1.0f, 1.0f, 1.0f},
				Vec3{1.0f, 1.0f, 1.0f},
				Vec3{1.0f, -1.0f, 1.0f},
				Vec3{-1.0f, -1.0f, 1.0f},
			};

			// Project frustum corners into world space
			for (uint32_t j = 0; j < 8; j++)
			{
				Vec4 invCorner = invCam * Vec4(frustumCorners[j], 1.0f);
				frustumCorners[j] = Vec3::Make(invCorner / invCorner.w);
			}

			for (uint32_t j = 0; j < 4; j++)
			{
				Vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
				frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
				frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
			}

			// Get frustum center
			Vec3 frustumCenter = Vec3();
			for (uint32_t j = 0; j < 8; j++)
			{
				frustumCenter += frustumCorners[j];
			}
			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (auto frustumCorner : frustumCorners)
			{
				float distance = Vec3::Length(frustumCorner - frustumCenter);
				radius = Math::Max(radius, distance);
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			Vec3 maxExtents = Vec3{radius, radius, radius};
			Vec3 minExtents = -maxExtents;

			Mat4 lightViewMatrix = Mat4::LookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, Vec3{0.0f, 1.0f, 0.0f});
			Mat4 lightOrthoMatrix = Mat4::Ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

			f32 sMapSize = static_cast<f32>(shadowMapData->shadowMapSize);
			f32 texelSize = 2.0f / sMapSize;

			// Get the translation component of the shadow matrix
			Mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
			Vec3 shadowOrigin = Vec3(shadowMatrix[3].x, shadowMatrix[3].y, shadowMatrix[3].z);

			// Convert to texel space, round, then convert back
			Vec3 roundedOrigin = Vec3(
				std::round(shadowOrigin.x / texelSize) * texelSize,
				std::round(shadowOrigin.y / texelSize) * texelSize,
				shadowOrigin.z // Don't round Z
			);

			// Apply the correction to the orthographic matrix
			Vec3 offset = roundedOrigin - shadowOrigin;
			lightOrthoMatrix[3].x += offset.x;
			lightOrthoMatrix[3].y += offset.y;

			// Store split distance and matrix in cascade
			shadowMapData->cascadeSplits[i] = (nearClip + splitDist * clipRange) * -1.0f;
			shadowMapData->cascadeViewProjMat[i] = lightOrthoMatrix * lightViewMatrix;

			lastSplitDist = cascadeSplits[i];

			ClearValues clearValues = {};

			BeginRenderPassInfo beginInfo;
			beginInfo.renderPass = m_shadowMapRenderPass[i];
			beginInfo.framebuffer = m_shadowMapFramebuffers[i];
			beginInfo.clearValues = &clearValues;

			cmd->BeginRenderPass(beginInfo);
			cmd->SetViewport(ViewportInfo{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<f32>(shadowMapData->shadowMapSize),
				.height = static_cast<f32>(shadowMapData->shadowMapSize),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			});

			char* memory = static_cast<char*>(shadowMapUniformBuffer->GetMappedData()) + m_uniformBufferAlignedSize * (frame * shadowMapData->numCascades + i);
			new(memory) Mat4{shadowMapData->cascadeViewProjMat[i]};

			cmd->SetScissor({0, 0}, {shadowMapData->shadowMapSize, shadowMapData->shadowMapSize});

			RenderCascade(objects, cmd, shadowMapData->cascadeViewProjMat[i], i);

			cmd->EndRenderPass();

			cmd->ResourceBarrier(shadowMapData->shadowTexture, ResourceState::DepthStencilAttachment, ResourceState::DepthStencilReadOnly, 0, i);
			cmd->EndDebugMarker();
		}
		cmd->EndDebugMarker();

		cachedPipelineObjects = objects;
	}

	void CascadeShadowPassBase::Destroy()
	{
		if (shadowMapUniformBuffer) shadowMapUniformBuffer->Destroy();
		for (GPUPipeline* p : shadowMapPipelines)
		{
			if (p) p->Destroy();
		}

		for (u32 i = 0; i < shadowMapData->numCascades; ++i)
		{
			if (m_shadowMapTextureViews[i]) m_shadowMapTextureViews[i]->Destroy();
			if (m_shadowMapRenderPass[i]) m_shadowMapRenderPass[i]->Destroy();
			if (m_shadowMapFramebuffers[i]) m_shadowMapFramebuffers[i]->Destroy();
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				if (m_shadowMapDescriptorSets[f][i]) m_shadowMapDescriptorSets[f][i]->Destroy();
			}
		}
	}

	void CascadeShadowMapModuleBase::Init()
	{
		shadowMapData = context->GetInstanceData<ShadowMapInstanceData>(ShadowMapInstanceDataName);

		shadowMapData->shadowTexture = Graphics::CreateTexture(TextureDesc{
			.extent = {shadowMapData->shadowMapSize, shadowMapData->shadowMapSize, 1},
			.mipLevels = 1,
			.arrayLayers = shadowMapData->numCascades,
			.format = TextureFormat::D32_FLOAT,
			.usage = ResourceUsage::DepthStencil | ResourceUsage::ShaderResource,
			.debugName = "ShadowMapDepthTexture"
		});

		GPUCommandBuffer* cmd = Graphics::GetFreeCommandBuffer();
		cmd->Begin();
		cmd->ResourceBarrier(shadowMapData->shadowTexture, ResourceState::Undefined, ResourceState::DepthStencilReadOnly, 0, 1, 0, shadowMapData->numCascades);
		cmd->End();
		Graphics::SubmitGPUWork(cmd, true);
		Graphics::AddFreeCommandBuffer(cmd);

		shadowMapData->shadowSampler = Graphics::CreateSampler(SamplerDesc{
			.minFilter = FilterMode::Linear,
			.magFilter = FilterMode::Linear,
			.addressModeU = AddressMode::ClampToEdge,
			.addressModeV = AddressMode::ClampToEdge,
			.addressModeW = AddressMode::ClampToEdge,
			.compareEnable = true,
			.compareOp = CompareOp::LessEqual,
			.borderColor = BorderColor::OpaqueWhite,
		});
	}

	void CascadeShadowMapModuleBase::Destroy()
	{
		shadowMapData->shadowSampler->Destroy();
		shadowMapData->shadowTexture->Destroy();
	}

	Array<RenderPipelineResource> CascadeShadowMapModuleBase::GetResources()
	{
		Array<RenderPipelineResource> resources;
		resources.EmplaceBack(RenderPipelineResource{.name = ShadowMapInstanceDataName, .type = RenderPipelineResourceType::Instance, .instanceTypeId = sktypeid(ShadowMapInstanceData)});
		return resources;
	}

	RenderPipelineModuleSetup CascadeShadowMapModuleBase::GetSetup()
	{
		RenderPipelineModuleSetup setup;
		setup.passes.EmplaceBack(GetCascadeShadowPassTypeId());
		return setup;
	}

	void ShadowMapInstanceData::RegisterType(NativeReflectType<ShadowMapInstanceData>& type)
	{
		type.Field<&ShadowMapInstanceData::shadowMapSize>("shadowMapSize");
		type.Field<&ShadowMapInstanceData::numCascades>("numCascades");
		type.Field<&ShadowMapInstanceData::shadowSampler>("shadowSampler");
		type.Field<&ShadowMapInstanceData::shadowTexture>("shadowTexture");
		type.Field<&ShadowMapInstanceData::cascadeSplits>("cascadeSplits");
		type.Field<&ShadowMapInstanceData::cascadeViewProjMat>("cascadeViewProjMat");
	}

	void RegisterCascadeShadowMapModule()
	{
		Reflection::Type<ShadowMapInstanceData>();


		Reflection::Type<CascadeShadowPassBase>();
		Reflection::Type<CascadeShadowMapModuleBase>();
	}
}