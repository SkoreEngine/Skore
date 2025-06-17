// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "BasicSceneRenderer.hpp"
#include "Graphics.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	struct CameraBuffer
	{
		Mat4 viewProjection;
		Mat4 view;
		Mat4 projection;
		Vec3 cameraPosition;
		f32 _pad;
	};

	struct LightData
	{
		u32  type;
		Vec3 position;
		Vec4 direction;
		Vec4 color;
		f32  intensity;
		f32  range;
		f32  innerConeAngle;
		f32  outerConeAngle;
	};

	constexpr i32 MAX_LIGHTS = 64;

	struct LightFlags
	{
		enum
		{
			None           = 0,
			HasEnvironment = 1 << 1
		};
	};

	struct LightBuffer
	{
		u32       lightCount;
		Vec3      ambientLight;
		u32       shadowLightIndex;
		u32       lightFlags;
		Vec2      _pad;
		Vec4      cascadeSplits;
		Mat4      cascadeViewProjMat[s_numCascades];
		LightData lights[MAX_LIGHTS];
	};

	SceneRendererViewport::~SceneRendererViewport()
	{
		Destroy();
		if (opaqueMaterialPipeline) opaqueMaterialPipeline->Destroy();
		if (skyboxMaterialPipeline) skyboxMaterialPipeline->Destroy();
		if (descriptorSet) descriptorSet->Destroy();
		if (uniformBuffer) uniformBuffer->Destroy();
		if (lightBuffer) lightBuffer->Destroy();
		if (lightDescriptorSet) lightDescriptorSet->Destroy();
		if (finalCompositeDescriptorSet) finalCompositeDescriptorSet->Destroy();
		if (finalCompositePipeline) finalCompositePipeline->Destroy();
		if (m_shadowMapDepthTexture) m_shadowMapDepthTexture->Destroy();
		if (m_shadowMapUniformBuffer) m_shadowMapUniformBuffer->Destroy();
		if (m_shadowMapPipeline) m_shadowMapPipeline->Destroy();
		if (m_shadowMapSampler) m_shadowMapSampler->Destroy();
		if (m_diffuseIrradianceTexture) m_diffuseIrradianceTexture->Destroy();

		for (u32 i = 0; i < s_numCascades; ++i)
		{
			if (m_shadowMapTextureViews[i]) m_shadowMapTextureViews[i]->Destroy();
			if (m_shadowMapRenderPass[i]) m_shadowMapRenderPass[i]->Destroy();
			if (m_shadowMapDescriptorSets[i]) m_shadowMapDescriptorSets[i]->Destroy();
		}

		m_brdflutTexture.Destroy();
	}

	void SceneRendererViewport::Init()
	{
		m_brdflutTexture.Init(Extent(512, 512));

		m_diffuseIrradianceTexture = Graphics::CreateTexture(TextureDesc{
			.extent = {64, 64, 1},
			.arrayLayers = 6,
			.format = TextureFormat::R16G16B16A16_FLOAT,
			.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
			.cubemap = true,
			.debugName = "SceneRendererViewport_irradianceTexture"
		});

		Graphics::SetTextureState(m_diffuseIrradianceTexture, ResourceState::Undefined, ResourceState::ShaderReadOnly);
	}

	void SceneRendererViewport::Resize(Extent extent)
	{
		Destroy();

		//shadow resource
		if (!m_shadowMapDepthTexture)
		{
			m_shadowMapDepthTexture = Graphics::CreateTexture(TextureDesc{
				.extent = {m_shadowMapSize, m_shadowMapSize, 1},
				.mipLevels = 1,
				.arrayLayers = s_numCascades,
				.format = TextureFormat::D32_FLOAT,
				.usage = ResourceUsage::DepthStencil | ResourceUsage::ShaderResource,
				.debugName = "ShadowMapDepthTexture"
			});

			m_shadowMapUniformBuffer = Graphics::CreateBuffer(BufferDesc{
				.size = sizeof(Mat4) * s_numCascades,
				.usage = ResourceUsage::ConstantBuffer,
				.hostVisible = true,
				.persistentMapped = true,
				.debugName = "ShadowMapUniformBuffer"
			});

			for (u32 i = 0; i < s_numCascades; ++i)
			{
				m_shadowMapTextureViews[i] = Graphics::CreateTextureView(TextureViewDesc{
					.texture = m_shadowMapDepthTexture,
					.type = TextureViewType::Type2DArray,
					.baseArrayLayer = i
				});

				m_shadowMapRenderPass[i] = Graphics::CreateRenderPass(RenderPassDesc{
					.attachments = {
						AttachmentDesc{
							.textureView = m_shadowMapTextureViews[i],
							.loadOp = AttachmentLoadOp::Clear,
							.storeOp = AttachmentStoreOp::Store,
						}
					},
					.debugName = "ShadowMapRenderPass_Cascade_" + ToString(i)
				});

				m_shadowMapDescriptorSets[i] = Graphics::CreateDescriptorSet(DescriptorSetDesc{
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
				descriptorUpdate.buffer = m_shadowMapUniformBuffer;
				descriptorUpdate.bufferOffset = sizeof(Mat4) * i;
				m_shadowMapDescriptorSets[i]->Update(descriptorUpdate);
			}

			m_shadowMapPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/ShadowMap.raster"),
				.rasterizerState =
				{
					.cullMode = CullMode::Front
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
				//.vertexInputStride = sizeof(MeshAsset::Vertex)
			});

			m_shadowMapSampler = Graphics::CreateSampler(SamplerDesc{
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

		TextureDesc attachmentTextureDesc;
		attachmentTextureDesc.format = TextureFormat::R16G16B16A16_FLOAT;
		attachmentTextureDesc.extent = {extent.width, extent.height, 1};
		attachmentTextureDesc.usage = ResourceUsage::RenderTarget | ResourceUsage::ShaderResource;
		attachmentTexture = Graphics::CreateTexture(attachmentTextureDesc);

		TextureDesc depthTextureDesc;
		depthTextureDesc.extent = {extent.width, extent.height, 1};
		depthTextureDesc.format = TextureFormat::D32_FLOAT;
		depthTextureDesc.usage = ResourceUsage::DepthStencil;
		depthTexture = Graphics::CreateTexture(depthTextureDesc);

		RenderPassDesc renderPassDesc;

		renderPassDesc.attachments.EmplaceBack(AttachmentDesc{
			.texture = attachmentTexture,
			.finalState = ResourceState::ColorAttachment
		});

		renderPassDesc.attachments.EmplaceBack(AttachmentDesc{
			.texture = depthTexture,
			.finalState = ResourceState::DepthStencilAttachment,
		});

		renderPass = Graphics::CreateRenderPass(renderPassDesc);

		if (!opaqueMaterialPipeline)
		{
			DepthStencilStateDesc depthStencilState;
			depthStencilState.depthTestEnable = true;
			depthStencilState.depthWriteEnable = true;
			depthStencilState.depthCompareOp = CompareOp::Less;

			opaqueMaterialPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/MeshRender.raster"),
				.rasterizerState =
				{
					.cullMode = CullMode::Back,
				},
				.depthStencilState = depthStencilState,
				.blendStates =
				{
					BlendStateDesc{}
				},
				.renderPass = renderPass,
			});
		}

		if (!skyboxMaterialPipeline)
		{
			DepthStencilStateDesc depthStencilState;
			depthStencilState.depthTestEnable = true;
			depthStencilState.depthCompareOp = CompareOp::LessEqual;

			skyboxMaterialPipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/SkyboxRender.raster"),
				.rasterizerState = {
					.cullMode = CullMode::Front
				},
				.depthStencilState = depthStencilState,
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = renderPass,
			});
		}

		if (!finalCompositeDescriptorSet)
		{
			finalCompositeDescriptorSet = Graphics::CreateDescriptorSet(DescriptorSetDesc{
				.bindings = {
					DescriptorSetLayoutBinding{
						.binding = 0,
						.descriptorType = DescriptorType::SampledImage
					},
					DescriptorSetLayoutBinding{
						.binding = 1,
						.descriptorType = DescriptorType::Sampler
					}
				}
			});
		}

		{
			DescriptorUpdate descriptorUpdate;
			descriptorUpdate.type = DescriptorType::SampledImage;
			descriptorUpdate.binding = 0;
			descriptorUpdate.texture = attachmentTexture;
			finalCompositeDescriptorSet->Update(descriptorUpdate);
		}

		{
			DescriptorUpdate descriptorUpdate;
			descriptorUpdate.type = DescriptorType::Sampler;
			descriptorUpdate.binding = 1;
			descriptorUpdate.sampler = Graphics::GetLinearSampler();
			finalCompositeDescriptorSet->Update(descriptorUpdate);
		}

		m_extent = extent;
	}

	Extent SceneRendererViewport::GetExtent() const
	{
		return m_extent;
	}

	void SceneRendererViewport::SetCamera(f32 nearClip, f32 farClip, const Mat4& view, const Mat4& projection, Vec3 cameraPosition)
	{
		m_view = view;
		m_projection = projection;
		m_nearClip = nearClip;
		m_farClip = farClip;
		m_cameraPosition = cameraPosition;
	}

	void SceneRendererViewport::Render(RenderStorage* storage, GPUCommandBuffer* cmd)
	{
		if (!uniformBuffer)
		{
			BufferDesc bufferDesc;
			bufferDesc.size = sizeof(CameraBuffer);
			bufferDesc.usage = ResourceUsage::ConstantBuffer;
			bufferDesc.hostVisible = true;
			bufferDesc.persistentMapped = true;
			uniformBuffer = Graphics::CreateBuffer(bufferDesc);
		}

		if (!lightBuffer)
		{
			BufferDesc bufferDesc;
			bufferDesc.size = sizeof(LightBuffer);
			bufferDesc.usage = ResourceUsage::ConstantBuffer;
			bufferDesc.hostVisible = true;
			bufferDesc.persistentMapped = true;
			lightBuffer = Graphics::CreateBuffer(bufferDesc);
		}

		if (!descriptorSet)
		{
			DescriptorSetDesc desc;
			desc.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::UniformBuffer
				}
			};
			desc.debugName = "SceneRendererViewport_descriptorSet";
			descriptorSet = Graphics::CreateDescriptorSet(desc);

			DescriptorUpdate descriptorUpdate;
			descriptorUpdate.type = DescriptorType::UniformBuffer;
			descriptorUpdate.binding = 0;
			descriptorUpdate.buffer = uniformBuffer;
			descriptorSet->Update(descriptorUpdate);
		}

		if (!lightDescriptorSet)
		{
			DescriptorSetDesc desc;
			desc.bindings = {
				DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = DescriptorType::UniformBuffer
				},
				DescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = DescriptorType::SampledImage
				},
				DescriptorSetLayoutBinding{
					.binding = 2,
					.descriptorType = DescriptorType::Sampler
				},
				DescriptorSetLayoutBinding{
					.binding = 3,
					.descriptorType = DescriptorType::SampledImage
				}

			};
			desc.debugName = "SceneRendererViewport_lightDescriptorSet";
			lightDescriptorSet = Graphics::CreateDescriptorSet(desc);

			DescriptorUpdate descriptorUpdate;
			descriptorUpdate.type = DescriptorType::UniformBuffer;
			descriptorUpdate.binding = 0;
			descriptorUpdate.buffer = lightBuffer;
			lightDescriptorSet->Update(descriptorUpdate);


			lightDescriptorSet->UpdateTexture(1, m_shadowMapDepthTexture);
			lightDescriptorSet->UpdateSampler(2, m_shadowMapSampler);
			lightDescriptorSet->UpdateTexture(3, m_diffuseIrradianceTexture);
		}

		Mat4 viewProjection = m_projection * m_view;

		new(uniformBuffer->GetMappedData()) CameraBuffer{
			.viewProjection = viewProjection,
			.view = m_view,
			.projection = m_projection,
			.cameraPosition = m_cameraPosition
		};

		if (storage)
		{
			PrepareEnvironment(storage, cmd);
			RenderShadows(storage, cmd);
		}

		LightBuffer lightBufferData;
		lightBufferData.lightCount = 0;
		lightBufferData.cascadeSplits = {m_cascadeSplits[0], m_cascadeSplits[1], m_cascadeSplits[2], m_cascadeSplits[3]};
		memcpy(lightBufferData.cascadeViewProjMat, m_cascadeViewProjMat, sizeof(Mat4) * s_numCascades);

		lightBufferData.ambientLight = Vec3(0.2);
		lightBufferData.shadowLightIndex = U32_MAX;
		lightBufferData.lightFlags = LightFlags::None;

		// if (m_skyMaterial)
		// {
		// 	lightBufferData.lightFlags |= LightFlags::HasEnvironment;
		// }

		for (i32 i = 0; i < MAX_LIGHTS; i++)
		{
			lightBufferData.lights[i] = LightData{};
		}

		if (storage && !storage->lights.empty())
		{
			u32 lightIndex = 0;
			for (auto& lightIt : storage->lights)
			{
				if (lightIndex >= MAX_LIGHTS)
				{
					break;
				}

				const LightRenderData& lightData = lightIt.second;
				if (!lightData.visible)
				{
					continue;
				}


				if (lightData.type == LightType::Directional &&
					lightData.enableShadows &&
					lightBufferData.shadowLightIndex == U32_MAX)
				{
					lightBufferData.shadowLightIndex = lightIndex;
				}

				LightData& shaderLight = lightBufferData.lights[lightIndex];
				shaderLight.type = static_cast<u32>(lightData.type);
				shaderLight.position = Math::GetTranslation(lightData.transform);

				Vec3 forward = Vec3(-lightData.transform[2][0], -lightData.transform[2][1], -lightData.transform[2][2]);
				shaderLight.direction = Vec4(Math::Normalize(forward), 0.0f);

				shaderLight.color = Vec4(lightData.color.ToVec3(), 0.0f);
				shaderLight.intensity = lightData.intensity;
				shaderLight.range = lightData.range;
				shaderLight.innerConeAngle = lightData.innerConeAngle;
				shaderLight.outerConeAngle = lightData.outerConeAngle;

				lightIndex++;
			}

			lightBufferData.lightCount = lightIndex;
		}

		new(lightBuffer->GetMappedData()) LightBuffer(lightBufferData);

		cmd->BeginDebugMarker("Main Pass", Vec4{0, 0, 0, 1});
		cmd->BeginRenderPass(renderPass, {0.0, 0.0, 0.0, 0.0}, 1.0f, 0);

		ViewportInfo viewportInfo{};
		viewportInfo.x = 0.;
		viewportInfo.y = 0.;
		viewportInfo.y = (f32)m_extent.height;
		viewportInfo.width = (f32)m_extent.width;
		viewportInfo.height = -(f32)m_extent.height;
		viewportInfo.minDepth = 0.;
		viewportInfo.maxDepth = 1.;

		cmd->SetViewport(viewportInfo);
		cmd->SetScissor({0, 0}, m_extent);

		if (storage)
		{
			//opaque render
			if (!storage->meshes.empty())
			{
				cmd->BindPipeline(opaqueMaterialPipeline);
				cmd->BindDescriptorSet(opaqueMaterialPipeline, 0, descriptorSet, {});
				cmd->BindDescriptorSet(opaqueMaterialPipeline, 2, lightDescriptorSet, {});

				for (auto& meshIt : storage->meshes)
				{
					MeshRenderData& meshRenderData = meshIt.second;

					if (meshRenderData.mesh && meshRenderData.visible)
					{
						if (!meshRenderData.mesh->vertexBuffer)
						{
							continue;
						}

						if (!meshRenderData.mesh->indexBuffer)
						{
							continue;
						}

						cmd->BindVertexBuffer(0, {meshRenderData.mesh->vertexBuffer}, {0});
						cmd->BindIndexBuffer(meshRenderData.mesh->indexBuffer, 0, IndexType::Uint32);
						cmd->PushConstants(opaqueMaterialPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &meshRenderData.transform);

						for (StaticMeshResource::Primitive& primitive : meshRenderData.mesh->primitives)
						{
							GPUDescriptorSet* materialDs = meshRenderData.mesh->materials[primitive.materialIndex].descriptorSet;

							if (materialDs == nullptr)
							{
								continue;
							}

							cmd->BindDescriptorSet(opaqueMaterialPipeline, 1, materialDs, {});
							cmd->DrawIndexed(primitive.indexCount, 1, primitive.firstIndex, 0, 0);
						}
					}
				}
			}

			// if (m_skyMaterial)
			// {
			// 	cmd->BindPipeline(skyboxMaterialPipeline);
			// 	Mat4 viewProj = m_projection * Mat4(Mat34(m_view));
			//
			// 	cmd->PushConstants(skyboxMaterialPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &viewProj);
			// 	cmd->BindDescriptorSet(skyboxMaterialPipeline, 0, m_skyMaterial->GetDescriptorSet(), {});
			// 	cmd->Draw(36, 1, 0, 0);
			// }
		}

		cmd->EndRenderPass();
		cmd->EndDebugMarker();
		cmd->ResourceBarrier(attachmentTexture, ResourceState::ColorAttachment, ResourceState::ShaderReadOnly, 0, 0);
	}

	void SceneRendererViewport::Blit(GPURenderPass* renderPass, GPUCommandBuffer* cmd)
	{
		if (!finalCompositePipeline)
		{
			finalCompositePipeline = Graphics::CreateGraphicsPipeline(GraphicsPipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/FinalComposite.raster"),
				.depthStencilState = {
					.depthTestEnable = false
				},
				.blendStates = {
					BlendStateDesc{}
				},
				.renderPass = renderPass
			});
		}

		cmd->BindPipeline(finalCompositePipeline);
		cmd->BindDescriptorSet(finalCompositePipeline, 0, finalCompositeDescriptorSet, {});
		cmd->Draw(3, 1, 0, 0);
	}

	void SceneRendererViewport::Destroy()
	{
		Graphics::WaitIdle();

		if (attachmentTexture) attachmentTexture->Destroy();
		if (depthTexture) depthTexture->Destroy();
		if (renderPass) renderPass->Destroy();

		renderPass = nullptr;
		attachmentTexture = nullptr;
		depthTexture = nullptr;
	}

	void SceneRendererViewport::PrepareEnvironment(RenderStorage* storage, GPUCommandBuffer* cmd)
	{
		// MaterialAsset* skyMaterial = nullptr;
		//
		// if (!storage->environments.empty())
		// {
		// 	cmd->BindPipeline(skyboxMaterialPipeline);
		//
		// 	for (auto& environmentIt : storage->environments)
		// 	{
		// 		EnvironmentRenderData& environmentRenderData = environmentIt.second;
		// 		if (environmentRenderData.skyboxMaterial && environmentRenderData.visible)
		// 		{
		// 			if (environmentRenderData.skyboxMaterial->type == MaterialAsset::MaterialType::SkyboxEquirectangular)
		// 			{
		// 				skyMaterial = environmentRenderData.skyboxMaterial;
		// 			}
		// 		}
		// 	}
		// }
		//
		// if (skyMaterial != nullptr && m_skyMaterial != skyMaterial)
		// {
		// 	GPUTexture* cubeMapTexture  = Graphics::CreateTexture(TextureDesc{
		// 		.extent = {256, 256, 1},
		// 		.mipLevels = 1,
		// 		.arrayLayers = 6,
		// 		.format = TextureFormat::R16G16B16A16_FLOAT,
		// 		.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
		// 		.cubemap = true,
		// 		.debugName = "SceneRendererViewport_CubemapTexture"
		// 	});
		//
		// 	GPUCommandBuffer* resourceCmd = Graphics::CreateCommandBuffer();
		// 	resourceCmd->Begin();
		//
		// 	EquirectangularToCubeMap equirectangularToCubemap;
		// 	equirectangularToCubemap.Init();
		// 	equirectangularToCubemap.Execute(resourceCmd, skyMaterial->sphericalTexture->GetTexture(), cubeMapTexture);
		//
		//
		// 	DiffuseIrradianceGenerator diffuseIrradianceGenerator;
		// 	diffuseIrradianceGenerator.Init();
		// 	diffuseIrradianceGenerator.Execute(resourceCmd, cubeMapTexture, m_diffuseIrradianceTexture);
		//
		// 	resourceCmd->End();
		// 	resourceCmd->SubmitAndWait();
		// 	resourceCmd->Destroy();
		//
		// 	equirectangularToCubemap.Destroy();
		// 	diffuseIrradianceGenerator.Destroy();
		// 	cubeMapTexture->Destroy();
		//
		//
		// 	//make prefiltered texture
		// }
		//
		// m_skyMaterial = skyMaterial;
	}

	void SceneRendererViewport::RenderShadows(RenderStorage* storage, GPUCommandBuffer* cmd)
	{
		//TODO: get a default value?
		Vec3 lightDir = Vec3(-1.0f, -1.0f, -1.0f);
		bool shadowsEnabled = false;

		u32 lightIndex = 0;
		for (auto& lightIt : storage->lights)
		{
			if (lightIndex >= MAX_LIGHTS)
			{
				break;
			}

			lightIndex++;

			const LightRenderData& lightData = lightIt.second;
			if (!lightData.visible)
			{
				continue;
			}

			if (!lightData.enableShadows || lightData.type != LightType::Directional)
			{
				continue;
			}

			shadowsEnabled = true;
			lightDir = Math::Normalize(-Vec3(lightData.transform[2]));
		}

		if (!shadowsEnabled)
		{
			cmd->BeginDebugMarker("Shadows", Vec4(0, 0, 0, 1));
			for (u32 i = 0; i < s_numCascades; i++)
			{
				cmd->BeginRenderPass(m_shadowMapRenderPass[i], {0.0, 0.0, 0.0, 0.0}, 1.0f, 0);
				cmd->EndRenderPass();
				cmd->ResourceBarrier(m_shadowMapDepthTexture, ResourceState::DepthStencilAttachment, ResourceState::DepthStencilReadOnly, 0, i);
			}
			cmd->EndDebugMarker();
			return;
		}

		float cascadeSplits[s_numCascades];

		//TODO make a settings
		float nearClip = m_nearClip;
		float farClip = m_farClip;
		float clipRange = farClip - nearClip;

		float minZ = nearClip;
		float maxZ = nearClip + clipRange;

		float range = maxZ - minZ;
		float ratio = maxZ / minZ;

		// Calculate split depths based on view camera frustum
		// Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
		for (u32 i = 0; i < s_numCascades; i++)
		{
			float p = (i + 1) / static_cast<float>(s_numCascades);
			float log = minZ * std::pow(ratio, p);
			float uniform = minZ + range * p;
			float d = m_cascadeSplitLambda * (log - uniform) + uniform;
			cascadeSplits[i] = (d - nearClip) / clipRange;
		}

		cmd->BeginDebugMarker("Cascade shadow maps", Vec4{0, 0, 0, 1});

		// Calculate orthographic projection matrix for each cascade
		float lastSplitDist = 0.0;
		for (uint32_t i = 0; i < s_numCascades; i++)
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
			Mat4 invCam = Math::Inverse(m_projection * m_view);
			for (uint32_t j = 0; j < 8; j++)
			{
				Vec4 invCorner = invCam * Vec4(frustumCorners[j], 1.0f);
				frustumCorners[j] = Math::MakeVec3(invCorner / invCorner.w);
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
				float distance = Math::Len(frustumCorner - frustumCenter);
				radius = Math::Max(radius, distance);
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			Vec3 maxExtents = Vec3{radius, radius, radius};
			Vec3 minExtents = -maxExtents;

			Mat4 lightViewMatrix = Math::LookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, Vec3{0.0f, 1.0f, 0.0f});
			Mat4 lightOrthoMatrix = Math::Ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

			// Store split distance and matrix in cascade
			m_cascadeSplits[i] = (nearClip + splitDist * clipRange) * -1.0f;
			m_cascadeViewProjMat[i] = lightOrthoMatrix * lightViewMatrix;

			lastSplitDist = cascadeSplits[i];

			cmd->BeginRenderPass(m_shadowMapRenderPass[i], {0.0, 0.0, 0.0, 0.0}, 1.0f, 0);
			cmd->SetViewport(ViewportInfo{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<f32>(m_shadowMapSize),
				.height = static_cast<f32>(m_shadowMapSize),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			});

			char* memory = static_cast<char*>(m_shadowMapUniformBuffer->GetMappedData()) + sizeof(Mat4) * i;
			new(memory) Mat4{m_cascadeViewProjMat[i]};

			cmd->SetScissor({0, 0}, {m_shadowMapSize, m_shadowMapSize});
			cmd->BindPipeline(m_shadowMapPipeline);
			cmd->BindDescriptorSet(m_shadowMapPipeline, 0, m_shadowMapDescriptorSets[i], {});

			for (auto& meshIt : storage->meshes)
			{
				MeshRenderData& meshRenderData = meshIt.second;

				if (meshRenderData.mesh && meshRenderData.visible && meshRenderData.castShadows)
				{
					// GPUBuffer* vertexBuffer = meshRenderData.mesh->GetVertexBuffer();
					// if (vertexBuffer == nullptr)
					// {
					// 	continue;
					// }
					//
					// GPUBuffer* indexBuffer = meshRenderData.mesh->GetIndexBuffer();
					// if (indexBuffer == nullptr)
					// {
					// 	continue;
					// }
					//
					// cmd->BindVertexBuffer(0, vertexBuffer, 0);
					// cmd->BindIndexBuffer(indexBuffer, 0, IndexType::Uint32);
					// cmd->PushConstants(m_shadowMapPipeline, ShaderStage::Vertex, 0, sizeof(Mat4), &meshRenderData.transform);

					// for (MeshAsset::Primitive& primitive : meshRenderData.mesh->GetPrimitives())
					// {
					// 	MaterialAsset* materialAsset = nullptr;
					//
					// 	if (meshRenderData.materials.Size() > primitive.materialIndex && meshRenderData.materials[primitive.materialIndex])
					// 	{
					// 		materialAsset = meshRenderData.materials[primitive.materialIndex];
					// 	}
					// 	else if (meshRenderData.mesh->GetMaterials().Size() > primitive.materialIndex && meshRenderData.mesh->GetMaterials()[primitive.materialIndex])
					// 	{
					// 		materialAsset = meshRenderData.mesh->GetMaterials()[primitive.materialIndex];
					// 	}
					//
					// 	if (materialAsset)
					// 	{
					// 		cmd->DrawIndexed(primitive.indexCount, 1, primitive.firstIndex, 0, 0);
					// 	}
					// }
				}
			}

			cmd->EndRenderPass();

			cmd->ResourceBarrier(m_shadowMapDepthTexture, ResourceState::DepthStencilAttachment, ResourceState::DepthStencilReadOnly, 0, i);
			cmd->EndDebugMarker();
		}
		cmd->EndDebugMarker();
	}
}
