#include "Skore/Graphics/Pipelines/Modules/CascadeShadowMapModule.hpp"

#include "json/value.h"
#include "Skore/Core/Algorithm.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Graphics/Pipelines/DefaultRenderPipeline/PipelineCommon.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	namespace
	{
		constexpr u32 kCascadeCullPlaneCount = 5;
		constexpr u32 kMaxShadowPipelines = 256;

		struct ShadowCullDataCB
		{
			Vec4 cascadeFrustumPlanes[MaxNumCascades * kCascadeCullPlaneCount];
			u32  shadowPipelineCount;
			u32  pad0[3];
		};

		struct IndexedIndirectCommand
		{
			u32 indexCount;
			u32 instanceCount;
			u32 firstIndex;
			i32 vertexOffset;
			u32 startInstanceLocation;
		};

		struct ShadowCascadeUniform
		{
			Mat4 viewProj;
			u32  disableMask;
			u32  pad[3];
		};
	}

	RenderPipelinePassSetup CascadeShadowPassBase::GetPassSetup()
	{
		RenderPipelinePassSetup setup;
		setup.type = RenderPipelinePassType::Other;
		setup.stage = PipelineRenderStage::ShadowPass;
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
		m_cascadeCullingFrustums.Resize(shadowMapData->numCascades);
		m_cascadeCullingPlanes.Resize(shadowMapData->numCascades);
		m_lastFrustumCenter.Resize(shadowMapData->numCascades);
		m_lastLightDir.Resize(shadowMapData->numCascades);

		m_shadowMapTextureViews.Resize(shadowMapData->numCascades);
		m_shadowMapRenderPass.Resize(shadowMapData->numCascades);
		m_shadowMapFramebuffers.Resize(shadowMapData->numCascades);

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			m_shadowMapDescriptorSets[f].Resize(shadowMapData->numCascades);
		}

		u64 uboAlignment = Graphics::GetDevice()->GetProperties().limits.minUniformBufferOffsetAlignment;
		m_uniformBufferAlignedSize = AlignedSize(static_cast<u64>(sizeof(ShadowCascadeUniform)), uboAlignment);

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
				.baseArrayLayer = i,
				.arrayLayerCount = 1
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
				descriptorUpdate.bufferRange = sizeof(ShadowCascadeUniform);
				m_shadowMapDescriptorSets[f][i]->Update(descriptorUpdate);
			}
		}

		m_shadowCullDataAlignedSize = AlignedSize(static_cast<u64>(sizeof(ShadowCullDataCB)), uboAlignment);
		m_shadowCullDataBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = m_shadowCullDataAlignedSize * SK_FRAMES_IN_FLIGHT,
			.usage = ResourceUsage::ConstantBuffer,
			.hostVisible = true,
			.persistentMapped = true,
			.debugName = "ShadowCullDataBuffer"
		});

		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			m_shadowCullDescriptorSet[f] = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{
						.binding = 0,
						.count = kMaxShadowPipelines,
						.descriptorType = DescriptorType::StorageBuffer,
						.renderType = RenderType::Array
					},
					DescriptorSetLayoutBinding{
						.binding = 1,
						.descriptorType = DescriptorType::StorageBuffer,
					},
					DescriptorSetLayoutBinding{
						.binding = 2,
						.descriptorType = DescriptorType::UniformBuffer,
					},
				}
			});

			DescriptorUpdate cullDataUpdate;
			cullDataUpdate.type = DescriptorType::UniformBuffer;
			cullDataUpdate.binding = 2;
			cullDataUpdate.buffer = m_shadowCullDataBuffer;
			cullDataUpdate.bufferOffset = m_shadowCullDataAlignedSize * f;
			cullDataUpdate.bufferRange = sizeof(ShadowCullDataCB);
			m_shadowCullDescriptorSet[f]->Update(cullDataUpdate);
		}

		m_shadowCullPipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
			.shader = Resources::FindByPath("Skore://Shaders/CullingShadowPass.comp"),
			.descriptorSetsOverride = {
				DescriptorSetOverride{
					.set = 0,
					.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
				},
				DescriptorSetOverride{
					.set = 1,
					.descriptorSet = context->GetSceneDescriptorSet(0)
				},
				DescriptorSetOverride{
					.set = 2,
					.descriptorSet = m_shadowCullDescriptorSet[0]
				}
			}
		});
	}

	void CascadeShadowPassBase::Render(Scene* scene, GPUCommandBuffer* cmd)
	{
		if (!scene) return;

		if (cachedPipelineObjects != nullptr && cachedPipelineObjects != scene)
		{
			for (GPUPipeline* p : shadowMapPipelines)
			{
				p->Destroy();
			}
			shadowMapPipelines.Clear();
		}

		Vec3 lightDir = Vec3(-1.0f, -1.0f, -1.0f);
		f32  lightMaxShadowDistance = 100.0f;
		f32  lightSplitLambda = 0.85f;
		bool lightInterleavedCascadeUpdates = true;
		u32  lightOpaqueShadowCascades = 0;
		bool foundDirectionalShadow = false;

		scene->Iterate<LightComponent>([&](LightComponent* light)
		{
			if (foundDirectionalShadow) return;
			if (!light->GetEnableShadows() || light->GetLightType() != LightType::Directional) return;

			const Mat4& transform = light->GetEntity()->GetWorldTransform();
			lightDir = Vec3::Normalize(-Vec3(transform[2]));
			lightMaxShadowDistance = light->GetMaxShadowDistance();
			lightSplitLambda = light->GetSplitLambda();
			lightInterleavedCascadeUpdates = light->GetInterleavedCascadeUpdates();
			lightOpaqueShadowCascades = light->GetOpaqueShadowCascades();
			foundDirectionalShadow = true;
		});

		if (!foundDirectionalShadow)
		{
			return;
		}

		float cascadeSplits[MaxNumCascades];

		float nearClip = context->camera.nearClip;
		float farClip  = Math::Max(lightMaxShadowDistance, nearClip * 2.0f);
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
			float d = lightSplitLambda * (log - uniform) + uniform;
			cascadeSplits[i] = (d - nearClip) / clipRange;
		}

		Mat4 shadowProj = context->camera.projectionNoJitter;
		shadowProj[2][2] = farClip / (nearClip - farClip);
		shadowProj[3][2] = -(farClip * nearClip) / (farClip - nearClip);
		Mat4 invCam = Mat4::Inverse(shadowProj * context->camera.view);

		u32 frame = context->GetCurrentFrame();

		cmd->BeginDebugMarker("Cascade shadow maps", Vec4{0, 0, 0, 1});

		bool  cascadeUpdated[MaxNumCascades] = {};
		u32   updatedCount   = 0;
		float lastSplitDist  = 0.0f;

		for (uint32_t i = 0; i < shadowMapData->numCascades; i++)
		{
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
			radius = std::round(radius * 16.0f) / 16.0f;

			lastSplitDist = cascadeSplits[i];

			if (lightInterleavedCascadeUpdates)
			{
				u32  period       = Math::Min(1u << i, m_maxUpdatePeriod);
				bool periodReady  = (m_frameCounter % period) == 0;
				bool largeMove    = Vec3::Length(frustumCenter - m_lastFrustumCenter[i]) > radius * m_centerMoveThreshold;
				bool lightRotated = Vec3::Dot(lightDir, m_lastLightDir[i]) < m_lightRotationDotThreshold;

				if (!(periodReady || largeMove || lightRotated))
				{
					continue;
				}
			}

			Vec3 maxExtents = Vec3{radius, radius, radius};
			Vec3 minExtents = -maxExtents;

			Vec3 lightUp = Math::Abs(Vec3::Dot(lightDir, Vec3{0.0f, 1.0f, 0.0f})) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
			Mat4 lightViewMatrix = Mat4::LookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, lightUp);
			Mat4 lightOrthoMatrix = Mat4::Ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

			f32 sMapSize  = static_cast<f32>(shadowMapData->shadowMapSize);
			f32 texelSize = 2.0f / sMapSize;

			Mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
			Vec3 shadowOrigin = Vec3(shadowMatrix[3].x, shadowMatrix[3].y, shadowMatrix[3].z);
			Vec3 roundedOrigin = Vec3(
				std::round(shadowOrigin.x / texelSize) * texelSize,
				std::round(shadowOrigin.y / texelSize) * texelSize,
				shadowOrigin.z
			);
			Vec3 offset = roundedOrigin - shadowOrigin;
			lightOrthoMatrix[3].x += offset.x;
			lightOrthoMatrix[3].y += offset.y;

			shadowMapData->cascadeSplits[i]      = (nearClip + splitDist * clipRange) * -1.0f;
			shadowMapData->cascadeViewProjMat[i] = lightOrthoMatrix * lightViewMatrix;

			m_cascadeCullingFrustums[i] = Math::CreateFrustumFromCamera(shadowMapData->cascadeViewProjMat[i]);

			auto packPlane = [](const Plane& pl) {
				return Vec4{pl.normal.x, pl.normal.y, pl.normal.z, pl.distance};
			};
			m_cascadeCullingPlanes[i][0] = packPlane(m_cascadeCullingFrustums[i].planes[FRUSTUM_SIDE_LEFT]);
			m_cascadeCullingPlanes[i][1] = packPlane(m_cascadeCullingFrustums[i].planes[FRUSTUM_SIDE_RIGHT]);
			m_cascadeCullingPlanes[i][2] = packPlane(m_cascadeCullingFrustums[i].planes[FRUSTUM_SIDE_BOTTOM]);
			m_cascadeCullingPlanes[i][3] = packPlane(m_cascadeCullingFrustums[i].planes[FRUSTUM_SIDE_TOP]);
			m_cascadeCullingPlanes[i][4] = packPlane(m_cascadeCullingFrustums[i].planes[FRUSTUM_SIDE_FAR]);

			m_lastFrustumCenter[i] = frustumCenter;
			m_lastLightDir[i]      = lightDir;

			char* memory = static_cast<char*>(shadowMapUniformBuffer->GetMappedData()) + m_uniformBufferAlignedSize * (frame * shadowMapData->numCascades + i);
			ShadowCascadeUniform* cascadeUniform = new(memory) ShadowCascadeUniform{};
			cascadeUniform->viewProj    = shadowMapData->cascadeViewProjMat[i];
			cascadeUniform->disableMask = (lightOpaqueShadowCascades > 0 && i + lightOpaqueShadowCascades >= shadowMapData->numCascades) ? 1u : 0u;

			cascadeUpdated[i] = true;
			++updatedCount;
		}

		if (updatedCount == 0)
		{
			cmd->EndDebugMarker();
			++m_frameCounter;
			cachedPipelineObjects = scene;
			return;
		}

		RenderSceneObjects* objects = &scene->renderObjects;
		while (shadowMapPipelines.Size() < objects->shadowPipelines.Size())
		{
			const DrawPipelineDesc& desc = objects->shadowPipelines[shadowMapPipelines.Size()].desc;
			RID shadowShader = Resources::FindByPath("Skore://Shaders/ShadowMapIndirect.shader");

			Array<String> macros;
			if (desc.hasBones) macros.EmplaceBack("HAS_BONES");
			if (desc.masked)   macros.EmplaceBack("HAS_MASK");

			GPUPipeline* p = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = shadowShader,
				.variant = ShaderResource::GetVariantName(macros),
				.rasterizerState = {
					.cullMode = desc.cullMode,
					.depthClampEnable = true,
				},
				.depthStencilState = {
					.depthTestEnable = true,
					.depthWriteEnable = true,
					.depthCompareOp = CompareOp::LessEqual
				},
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = m_shadowMapRenderPass[0],
				.descriptorSetsOverride = {
					DescriptorSetOverride{
						.set = 0,
						.descriptorSet = RenderResourceCache::GetGlobalDescriptorSet()
					},
					DescriptorSetOverride{
						.set = 1,
						.descriptorSet = context->GetSceneDescriptorSet(0)
					}
				}
			});
			shadowMapPipelines.EmplaceBack(p);
		}

		const u32 numShadowPipes = static_cast<u32>(objects->shadowPipelines.Size());
		const u32 totalSlots     = numShadowPipes * shadowMapData->numCascades;

		PrepareShadowCullBuffers(objects, frame, numShadowPipes, totalSlots);

		if (totalSlots > 0 && objects->instanceDataCount > 0)
		{
			for (u32 slot = 0; slot < totalSlots; ++slot)
			{
				DescriptorUpdate update = {};
				update.type         = DescriptorType::StorageBuffer;
				update.binding      = 0;
				update.arrayElement = slot;
				update.buffer       = m_shadowIndirectDrawBuffers[frame][slot];
				m_shadowCullDescriptorSet[frame]->Update(update);
			}
			m_shadowCullDescriptorSet[frame]->UpdateBuffer(1, m_shadowIndirectCountBuffer[frame], 0, 0);

			ShadowCullDataCB cb = {};
			cb.shadowPipelineCount = numShadowPipes;
			for (u32 c = 0; c < shadowMapData->numCascades; ++c)
			{
				for (u32 p = 0; p < kCascadeCullPlaneCount; ++p)
				{
					cb.cascadeFrustumPlanes[c * kCascadeCullPlaneCount + p] = m_cascadeCullingPlanes[c][p];
				}
			}
			char* cullMem = static_cast<char*>(m_shadowCullDataBuffer->GetMappedData()) + m_shadowCullDataAlignedSize * frame;
			memcpy(cullMem, &cb, sizeof(ShadowCullDataCB));

			cmd->FillBuffer(m_shadowIndirectCountBuffer[frame], 0, sizeof(u32) * totalSlots, 0);
			cmd->MemoryBarrier();

			cmd->BeginDebugMarker("Shadow cull dispatch", Vec4{0.2f, 0.5f, 0.8f, 1});
			cmd->BindPipeline(m_shadowCullPipeline);
			cmd->BindDescriptorSet(m_shadowCullPipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
			cmd->BindDescriptorSet(m_shadowCullPipeline, 1, context->GetSceneDescriptorSet());
			cmd->BindDescriptorSet(m_shadowCullPipeline, 2, m_shadowCullDescriptorSet[frame]);

			i32 forcedLod = RenderDebug::ForcedLod();
			cmd->PushConstants(m_shadowCullPipeline, ShaderStage::Compute, 0, sizeof(i32), &forcedLod);

			cmd->Dispatch((objects->instanceDataCount + 15) / 16, 1, 1);
			cmd->MemoryBarrier();
			cmd->EndDebugMarker();
		}

		cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

		for (u32 i = 0; i < shadowMapData->numCascades; ++i)
		{
			if (!cascadeUpdated[i]) continue;

			cmd->BeginDebugMarker("Cascade: " + ToString(i), Vec4{0, 0, 0, 1});

			ClearValues clearValues = {};

			BeginRenderPassInfo beginInfo;
			beginInfo.renderPass  = m_shadowMapRenderPass[i];
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
			cmd->SetScissor({0, 0}, {shadowMapData->shadowMapSize, shadowMapData->shadowMapSize});

			for (u32 sp = 0; sp < numShadowPipes; ++sp)
			{
				GPUPipeline* pipeline = shadowMapPipelines[sp];
				cmd->BindPipeline(pipeline);
				cmd->BindDescriptorSet(pipeline, 0, RenderResourceCache::GetGlobalDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 1, context->GetSceneDescriptorSet());
				cmd->BindDescriptorSet(pipeline, 2, m_shadowMapDescriptorSets[frame][i], {});

				u32 slot = i * numShadowPipes + sp;
				cmd->DrawIndexedIndirectCount(
					m_shadowIndirectDrawBuffers[frame][slot],
					0,
					m_shadowIndirectCountBuffer[frame],
					slot * sizeof(u32),
					objects->instanceDataCount,
					sizeof(IndexedIndirectCommand)
				);
			}

			cmd->EndRenderPass();
			cmd->ResourceBarrier(shadowMapData->shadowTexture, ResourceState::DepthStencilAttachment, ResourceState::DepthStencilReadOnly, 0, i);
			cmd->EndDebugMarker();
		}

		cmd->EndDebugMarker();

		++m_frameCounter;
		cachedPipelineObjects = scene;
	}

	void CascadeShadowPassBase::PrepareShadowCullBuffers(RenderSceneObjects* objects, u32 frame, u32 numShadowPipes, u32 totalSlots)
	{
		const u64 perSlotInstances = Math::Max(static_cast<u32>(InitialInstanceNumber), objects->instanceDataCount);
		const u64 indirectSize     = perSlotInstances * sizeof(IndexedIndirectCommand) * 2;

		if (m_shadowIndirectDrawBuffers[frame].Size() < totalSlots)
		{
			m_shadowIndirectDrawBuffers[frame].Resize(totalSlots);
		}

		for (u32 slot = 0; slot < totalSlots; ++slot)
		{
			GPUBuffer*& buf = m_shadowIndirectDrawBuffers[frame][slot];
			if (buf == nullptr || buf->GetDesc().size < perSlotInstances * sizeof(IndexedIndirectCommand))
			{
				if (buf) buf->Destroy();
				buf = Graphics::CreateBuffer(BufferDesc{
					.size = indirectSize,
					.usage = ResourceUsage::IndirectBuffer | ResourceUsage::UnorderedAccess,
					.hostVisible = false,
					.persistentMapped = false,
					.debugName = "ShadowIndirectDrawBuffer"
				});
			}
		}

		const u64 countSize = sizeof(u32) * Math::Max(totalSlots, 1u);
		if (m_shadowIndirectCountBuffer[frame] == nullptr || m_shadowIndirectCountBuffer[frame]->GetDesc().size < countSize)
		{
			if (m_shadowIndirectCountBuffer[frame]) m_shadowIndirectCountBuffer[frame]->Destroy();
			m_shadowIndirectCountBuffer[frame] = Graphics::CreateBuffer(BufferDesc{
				.size = countSize,
				.usage = ResourceUsage::IndirectBuffer | ResourceUsage::UnorderedAccess | ResourceUsage::CopyDest,
				.hostVisible = false,
				.persistentMapped = false,
				.debugName = "ShadowIndirectCountBuffer"
			});
		}

		m_shadowIndirectPipelineCount = numShadowPipes;
	}

	bool CascadeShadowPassBase::IsAABBVisibleInCascade(const Frustum& cascadeFrustum, const AABB& aabb)
	{
		for (u32 i = 0; i < 6; ++i)
		{
			if (i == FRUSTUM_SIDE_NEAR) continue;

			const Plane& plane = cascadeFrustum.planes[i];
			Vec3 positiveVertex = aabb.min;
			if (plane.normal.x >= 0) positiveVertex.x = aabb.max.x;
			if (plane.normal.y >= 0) positiveVertex.y = aabb.max.y;
			if (plane.normal.z >= 0) positiveVertex.z = aabb.max.z;

			if (plane.GetDistanceToPoint(positiveVertex) < 0)
			{
				return false;
			}
		}
		return true;
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

		if (m_shadowCullPipeline) m_shadowCullPipeline->Destroy();
		if (m_shadowCullDataBuffer) m_shadowCullDataBuffer->Destroy();
		for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
		{
			if (m_shadowCullDescriptorSet[f]) m_shadowCullDescriptorSet[f]->Destroy();
			if (m_shadowIndirectCountBuffer[f]) m_shadowIndirectCountBuffer[f]->Destroy();
			for (GPUBuffer* buf : m_shadowIndirectDrawBuffers[f])
			{
				if (buf) buf->Destroy();
			}
			m_shadowIndirectDrawBuffers[f].Clear();
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