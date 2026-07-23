#include "PipelineCommon.hpp"

#include "Skore/Core/Algorithm.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderGraph.hpp"
#include "Skore/Graphics/RenderPipeline.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	namespace
	{
		constexpr u32 kCascadeCullPlaneCount = 5;
		constexpr u32 kMaxShadowPipelines = MaxShadowCullSlots / MaxNumCascades;

		constexpr const char* ShadowCountsName = "ShadowIndirectCounts";
		constexpr const char* ShadowOffsetsName = "ShadowCullOffsets";
		constexpr const char* ShadowCullDataName = "ShadowCullData";

		struct ShadowCullDataCB
		{
			Vec4 cascadeFrustumPlanes[MaxNumCascades * kCascadeCullPlaneCount];
			u32  shadowPipelineCount;
			u32  pad0[3];
		};

		struct ShadowCascadeUniform
		{
			Mat4 viewProj;
			u32  disableMask;
			u32  pad[3];
		};
	}

	struct CascadeShadowPass : DefaultPipelinePass
	{
		SK_CLASS(CascadeShadowPass, DefaultPipelinePass);

		u32 shadowMapSize = 2048;

		Vec3 lastFrustumCenter[MaxNumCascades] = {};
		Vec3 lastLightDir[MaxNumCascades] = {};
		u64  frameCounter = 0;
		u32  maxUpdatePeriod = 8;
		f32  centerMoveThreshold = 0.1f;
		f32  lightRotationDotThreshold = 0.999f;

		GPUSampler*       shadowSampler = nullptr;
		GPUBuffer*        cascadeUniformBuffer = nullptr;
		u64               cascadeUniformAlignedSize = 0;
		GPUDescriptorSet* cascadeDescriptorSets[SK_FRAMES_IN_FLIGHT][MaxNumCascades] = {};

		Array<GPUPipeline*> shadowPipelines;
		Scene*              cachedScene = nullptr;

		u32    drawsGeneration = 0;
		u64    drawsCapacity = 0;
		String drawsName;

		ShadowCullDataCB cullDataCB = {};
		u32              slotOffsets[MaxShadowCullSlots] = {};
		u32              slotMaxDraws[MaxShadowCullSlots] = {};
		u32              pipesThisFrame = 0;

		bool instanceDataInitialized = false;

		~CascadeShadowPass() override
		{
			if (shadowSampler) shadowSampler->Destroy();
			if (cascadeUniformBuffer) cascadeUniformBuffer->Destroy();
			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				for (u32 i = 0; i < MaxNumCascades; ++i)
				{
					if (cascadeDescriptorSets[f][i]) cascadeDescriptorSets[f][i]->Destroy();
				}
			}
			CleanupPipelines();
		}

		void CleanupPipelines()
		{
			for (GPUPipeline* pipeline : shadowPipelines)
			{
				if (pipeline) pipeline->Destroy();
			}
			shadowPipelines.Clear();
		}

		void EnsureResources()
		{
			if (cascadeUniformBuffer != nullptr) return;

			shadowSampler = Graphics::CreateSampler(SamplerDesc{
				.minFilter = FilterMode::Linear,
				.magFilter = FilterMode::Linear,
				.addressModeU = AddressMode::ClampToEdge,
				.addressModeV = AddressMode::ClampToEdge,
				.addressModeW = AddressMode::ClampToEdge,
				.compareEnable = true,
				.compareOp = CompareOp::LessEqual,
				.borderColor = BorderColor::OpaqueWhite,
			});

			u64 uboAlignment = Graphics::GetDevice()->GetProperties().limits.minUniformBufferOffsetAlignment;
			cascadeUniformAlignedSize = AlignedSize(static_cast<u64>(sizeof(ShadowCascadeUniform)), uboAlignment);

			cascadeUniformBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = cascadeUniformAlignedSize * MaxNumCascades * SK_FRAMES_IN_FLIGHT,
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "ShadowCascadeUniformBuffer"
			});

			for (u32 f = 0; f < SK_FRAMES_IN_FLIGHT; ++f)
			{
				for (u32 i = 0; i < MaxNumCascades; ++i)
				{
					cascadeDescriptorSets[f][i] = Graphics::CreateDescriptorSet(DescriptorSetDesc{
						.bindings = {
							DescriptorSetLayoutBinding{
								.binding = 0,
								.descriptorType = DescriptorType::UniformBuffer
							}
						},
						.debugName = "ShadowCascadeDescriptorSet"
					});

					DescriptorUpdate update;
					update.type = DescriptorType::UniformBuffer;
					update.binding = 0;
					update.buffer = cascadeUniformBuffer;
					update.bufferOffset = cascadeUniformAlignedSize * (f * MaxNumCascades + i);
					update.bufferRange = sizeof(ShadowCascadeUniform);
					cascadeDescriptorSets[f][i]->Update(update);
				}
			}
		}

		void EnsureShadowPipelines(RenderSceneObjects* objects, GPURenderPass* renderPass, GPUDescriptorSet* pipelineSceneSet, GPUDescriptorSet* globalSet, GPUDescriptorSet* skinningSet)
		{
			RID shadowShader = Resources::FindByPath("Skore://Shaders/ShadowMapIndirect.shader");
			if (!shadowShader) return;

			u32 count = Math::Min(static_cast<u32>(objects->shadowPipelines.Size()), kMaxShadowPipelines);
			while (shadowPipelines.Size() < count)
			{
				const DrawPipelineDesc& desc = objects->shadowPipelines[shadowPipelines.Size()].desc;

				Array<String> macros;
				if (desc.masked) macros.EmplaceBack("HAS_MASK");
				String variant = ShaderResource::GetVariantName(macros);

				GraphicsPipelineDesc gpuDesc;
				gpuDesc.shader = shadowShader;
				gpuDesc.variant = variant;
				gpuDesc.rasterizerState.cullMode = desc.cullMode;
				gpuDesc.rasterizerState.depthClampEnable = true;
				gpuDesc.rasterizerState.depthBiasEnable = true;
				gpuDesc.rasterizerState.depthBiasConstantFactor = 1.0f;
				gpuDesc.rasterizerState.depthBiasClamp = 0.0f;
				gpuDesc.rasterizerState.depthBiasSlopeFactor = 1.5f;
				gpuDesc.depthStencilState.depthTestEnable = true;
				gpuDesc.depthStencilState.depthWriteEnable = true;
				gpuDesc.depthStencilState.depthCompareOp = CompareOp::LessEqual;
				gpuDesc.blendStates = {BlendStateDesc{}};
				gpuDesc.renderPass = renderPass;
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 1, .descriptorSet = pipelineSceneSet});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 2, .descriptorSet = globalSet});
				gpuDesc.descriptorSetsOverride.EmplaceBack(DescriptorSetOverride{.set = 3, .descriptorSet = skinningSet});

				shadowPipelines.EmplaceBack(Graphics::CreateGraphicsPipeline(gpuDesc));
			}
		}

		void BuildRenderGraph(RenderGraph& renderGraph) override
		{
			ShadowMapInstanceData* shadowData = renderGraph.CreateInstance<ShadowMapInstanceData>(ShadowMapInstanceDataName);
			if (!instanceDataInitialized)
			{
				*shadowData = ShadowMapInstanceData{};
				instanceDataInitialized = true;
			}

			EnsureResources();

			shadowData->shadowMapSize = shadowMapSize;
			shadowData->numCascades = MaxNumCascades;
			shadowData->shadowSampler = shadowSampler;
			shadowData->hasShadowLight = false;

			//usage declared explicitly: the texture can be created on a frame where the cascade
			//passes are skipped (no scene set yet), and inferred usage would then miss DepthStencil
			renderGraph.Create(ShadowMapTextureName, RenderGraphTextureDesc{
				.format = Format::D32_FLOAT,
				.extent = Extent{shadowMapSize, shadowMapSize},
				.arrayLayers = MaxNumCascades,
				.usage = ResourceUsage::DepthStencil | ResourceUsage::ShaderResource,
				.clearDepth = 1.0f,
				.persistent = true
			});

			String cascadeViewNames[MaxNumCascades];
			for (u32 i = 0; i < MaxNumCascades; ++i)
			{
				cascadeViewNames[i] = String("ShadowMapCascade_") + ToString(i);
				renderGraph.CreateView(cascadeViewNames[i], RenderGraphViewDesc{
					.texture = ShadowMapTextureName,
					.viewType = TextureViewType::Type2DArray,
					.baseMipLevel = 0,
					.mipLevelCount = 1,
					.baseArrayLayer = i,
					.arrayLayerCount = 1
				});
			}

			Scene*            scene = renderGraph.GetScene();
			GPUDescriptorSet* sceneSet = renderGraph.GetSceneDescriptorSet();
			GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
			if (scene == nullptr || sceneSet == nullptr || globalSet == nullptr)
			{
				++frameCounter;
				return;
			}

			if (cachedScene != nullptr && cachedScene != scene)
			{
				CleanupPipelines();
			}
			cachedScene = scene;

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

			RenderSceneObjects* objects = &scene->renderObjects;

			if (!foundDirectionalShadow || objects->instanceDataCount == 0)
			{
				++frameCounter;
				return;
			}

			shadowData->hasShadowLight = true;

			float cascadeSplits[MaxNumCascades];

			float nearClip = renderGraph.camera.nearClip;
			float farClip = Math::Max(lightMaxShadowDistance, nearClip * 2.0f);
			float clipRange = farClip - nearClip;

			float minZ = nearClip;
			float maxZ = nearClip + clipRange;

			float range = maxZ - minZ;
			float ratio = maxZ / minZ;

			for (u32 i = 0; i < MaxNumCascades; i++)
			{
				float p = (i + 1) / static_cast<float>(MaxNumCascades);
				float log = minZ * std::pow(ratio, p);
				float uniform = minZ + range * p;
				float d = lightSplitLambda * (log - uniform) + uniform;
				cascadeSplits[i] = (d - nearClip) / clipRange;
			}

			Mat4 shadowProj = renderGraph.camera.projectionNoJitter;
			shadowProj[2][2] = farClip / (nearClip - farClip);
			shadowProj[3][2] = -(farClip * nearClip) / (farClip - nearClip);
			Mat4 invCam = Mat4::Inverse(shadowProj * renderGraph.camera.view);

			u32 frame = renderGraph.GetCurrentFrame();

			bool  cascadeUpdated[MaxNumCascades] = {};
			u32   updatedCount = 0;
			float lastSplitDist = 0.0f;

			for (u32 i = 0; i < MaxNumCascades; i++)
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

				for (u32 j = 0; j < 8; j++)
				{
					Vec4 invCorner = invCam * Vec4(frustumCorners[j], 1.0f);
					frustumCorners[j] = Vec3::Make(invCorner / invCorner.w);
				}

				for (u32 j = 0; j < 4; j++)
				{
					Vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
					frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
					frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
				}

				Vec3 frustumCenter = Vec3();
				for (u32 j = 0; j < 8; j++)
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
					u32  period = Math::Min(1u << i, maxUpdatePeriod);
					bool periodReady = (frameCounter % period) == 0;
					bool largeMove = Vec3::Length(frustumCenter - lastFrustumCenter[i]) > radius * centerMoveThreshold;
					bool lightRotated = Vec3::Dot(lightDir, lastLightDir[i]) < lightRotationDotThreshold;

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

				f32 sMapSize = static_cast<f32>(shadowMapSize);
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

				shadowData->cascadeSplits[i] = (nearClip + splitDist * clipRange) * -1.0f;
				shadowData->cascadeViewProjMat[i] = lightOrthoMatrix * lightViewMatrix;

				Frustum cascadeFrustum = Math::CreateFrustumFromCamera(shadowData->cascadeViewProjMat[i]);

				auto packPlane = [](const Plane& pl)
				{
					return Vec4{pl.normal.x, pl.normal.y, pl.normal.z, pl.distance};
				};
				cullDataCB.cascadeFrustumPlanes[i * kCascadeCullPlaneCount + 0] = packPlane(cascadeFrustum.planes[FRUSTUM_SIDE_LEFT]);
				cullDataCB.cascadeFrustumPlanes[i * kCascadeCullPlaneCount + 1] = packPlane(cascadeFrustum.planes[FRUSTUM_SIDE_RIGHT]);
				cullDataCB.cascadeFrustumPlanes[i * kCascadeCullPlaneCount + 2] = packPlane(cascadeFrustum.planes[FRUSTUM_SIDE_BOTTOM]);
				cullDataCB.cascadeFrustumPlanes[i * kCascadeCullPlaneCount + 3] = packPlane(cascadeFrustum.planes[FRUSTUM_SIDE_TOP]);
				cullDataCB.cascadeFrustumPlanes[i * kCascadeCullPlaneCount + 4] = packPlane(cascadeFrustum.planes[FRUSTUM_SIDE_FAR]);

				lastFrustumCenter[i] = frustumCenter;
				lastLightDir[i] = lightDir;

				char* memory = static_cast<char*>(cascadeUniformBuffer->GetMappedData()) + cascadeUniformAlignedSize * (frame * MaxNumCascades + i);
				ShadowCascadeUniform* cascadeUniform = new(memory) ShadowCascadeUniform{};
				cascadeUniform->viewProj = shadowData->cascadeViewProjMat[i];
				cascadeUniform->disableMask = (lightOpaqueShadowCascades > 0 && i + lightOpaqueShadowCascades >= MaxNumCascades) ? 1u : 0u;

				cascadeUpdated[i] = true;
				++updatedCount;
			}

			const u32 numShadowPipes = Math::Min(static_cast<u32>(objects->shadowPipelines.Size()), kMaxShadowPipelines);
			const u32 totalSlots = numShadowPipes * MaxNumCascades;

			if (updatedCount == 0 || totalSlots == 0)
			{
				++frameCounter;
				return;
			}

			pipesThisFrame = numShadowPipes;
			cullDataCB.shadowPipelineCount = numShadowPipes;

			const u32 perSlotMaxDraws = objects->instanceDataCount;

			u32 totalDraws = 0;
			for (u32 slot = 0; slot < totalSlots; ++slot)
			{
				slotOffsets[slot] = totalDraws;
				slotMaxDraws[slot] = perSlotMaxDraws;
				totalDraws += perSlotMaxDraws;
			}

			u64 requiredSize = static_cast<u64>(totalDraws) * sizeof(DrawIndexedIndirectArguments);
			if (requiredSize > drawsCapacity)
			{
				drawsCapacity = Math::Max(requiredSize * 2, static_cast<u64>(sizeof(DrawIndexedIndirectArguments)) * InitialInstanceNumber);
				++drawsGeneration;
				drawsName = String("ShadowIndirectDraws_") + ToString(drawsGeneration);
			}

			renderGraph.Create(drawsName, RenderGraphBufferDesc{
				.size = drawsCapacity,
				.usage = ResourceUsage::IndirectBuffer,
				.hostVisible = false,
				.perFrame = true
			});

			renderGraph.Create(ShadowCountsName, RenderGraphBufferDesc{
				.size = sizeof(u32) * MaxShadowCullSlots,
				.usage = ResourceUsage::IndirectBuffer,
				.hostVisible = false,
				.perFrame = true
			});

			renderGraph.Create(ShadowOffsetsName, RenderGraphBufferDesc{
				.size = sizeof(u32) * MaxShadowCullSlots,
				.hostVisible = true,
				.persistentMapped = true,
				.perFrame = true
			});

			renderGraph.Create(ShadowCullDataName, RenderGraphBufferDesc{
				.size = sizeof(ShadowCullDataCB),
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.perFrame = true
			});

			renderGraph
				.AddPass("ShadowCullingPrepare")
				.Stage(RenderStage::ShadowPass)
				.Write(ShadowCountsName)
				.Render([this](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
				{
					RenderGraph& rg = *pass.GetGraph();

					GPUBuffer* counts = rg.GetBuffer(ShadowCountsName);
					GPUBuffer* offsets = rg.GetBuffer(ShadowOffsetsName);
					GPUBuffer* cullData = rg.GetBuffer(ShadowCullDataName);
					if (counts == nullptr || offsets == nullptr || cullData == nullptr) return;

					memcpy(offsets->GetMappedData(), slotOffsets, sizeof(u32) * MaxShadowCullSlots);
					memcpy(cullData->GetMappedData(), &cullDataCB, sizeof(ShadowCullDataCB));
					cmd->FillBuffer(counts, 0, sizeof(u32) * MaxShadowCullSlots, 0);
				});

			renderGraph
				.AddComputePass("ShadowCulling", "Skore://Shaders/CullingShadowPass.comp")
				.Stage(RenderStage::ShadowPass)
				.Write(drawsName)
				.WriteRead(ShadowCountsName)
				.Read(ShadowOffsetsName)
				.Read(ShadowCullDataName)
				.DescriptorSet(1, sceneSet)
				.DescriptorSet(2, globalSet)
				.Constants<i32>([](RenderGraph&, i32& forcedLod)
				{
					forcedLod = RenderDebug::ForcedLod();
				})
				.Dispatch(objects->instanceDataCount, 1, 1);

			for (u32 i = 0; i < MaxNumCascades; ++i)
			{
				if (!cascadeUpdated[i]) continue;

				renderGraph
					.AddGraphicsPass(String("ShadowCascade_") + ToString(i))
					.Stage(RenderStage::ShadowPass)
					.Read(drawsName)
					.Read(ShadowCountsName)
					.Write(cascadeViewNames[i])
					.Render([this, i](RenderGraphPass& pass, Scene* scene, GPUCommandBuffer* cmd)
					{
						if (scene == nullptr || pass.GetRenderPass() == nullptr) return;

						RenderGraph& rg = *pass.GetGraph();

						GPUDescriptorSet* sceneSet = rg.GetSceneDescriptorSet();
						GPUDescriptorSet* globalSet = RenderResourceCache::GetGlobalDescriptorSet();
						GPUBuffer*        draws = rg.GetBuffer(drawsName);
						GPUBuffer*        counts = rg.GetBuffer(ShadowCountsName);
						if (sceneSet == nullptr || globalSet == nullptr || draws == nullptr || counts == nullptr) return;

						RenderSceneObjects* objects = &scene->renderObjects;
						GPUDescriptorSet*   skinningSet = objects->GetSkinningDescriptorSet();

						EnsureShadowPipelines(objects, pass.GetRenderPass(), rg.GetSceneDescriptorSet(0), globalSet, skinningSet);

						u32 frame = rg.GetCurrentFrame();

						cmd->BindIndexBuffer(RenderResourceCache::GetMeshDataBuffer(), 0, IndexType::Uint32);

						for (u32 sp = 0; sp < pipesThisFrame && sp < shadowPipelines.Size(); ++sp)
						{
							GPUPipeline* pipeline = shadowPipelines[sp];
							if (pipeline == nullptr) continue;

							cmd->BindPipeline(pipeline);
							cmd->BindDescriptorSet(pipeline, 0, cascadeDescriptorSets[frame][i]);
							cmd->BindDescriptorSet(pipeline, 1, sceneSet);
							cmd->BindDescriptorSet(pipeline, 2, globalSet);
							cmd->BindDescriptorSet(pipeline, 3, skinningSet);

							u32 slot = i * pipesThisFrame + sp;
							cmd->DrawIndexedIndirectCount(draws,
							                              slotOffsets[slot] * sizeof(DrawIndexedIndirectArguments),
							                              counts,
							                              slot * sizeof(u32),
							                              slotMaxDraws[slot],
							                              sizeof(DrawIndexedIndirectArguments));
						}
					});
			}

			++frameCounter;
		}
	};

	void RegisterCascadeShadowPass()
	{
		Reflection::Type<CascadeShadowPass>();
	}
}
