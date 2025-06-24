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

#include "RenderTools.hpp"

#include "Graphics.hpp"
#include "mikktspace.h"
#include "Skore/Core/StaticContent.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		struct MikkTSpaceUserData
		{
			Span<MeshResource::Vertex> vertices;
			Span<u32>                        indices;
		};

		i32 GetNumFaces(const SMikkTSpaceContext* pContext)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			return (i32)mesh.indices.Size() / 3;
		}

		i32 GetNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace)
		{
			return 3;
		}

		u32 GetVertexIndex(const SMikkTSpaceContext* context, i32 iFace, i32 iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(context->m_pUserData);
			auto                faceSize = GetNumVerticesOfFace(context, iFace);
			auto                indicesIndex = (iFace * faceSize) + iVert;
			return mesh.indices[indicesIndex];
		}

		void GetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);

			MeshResource::Vertex& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			fvPosOut[0] = v.position.x;
			fvPosOut[1] = v.position.y;
			fvPosOut[2] = v.position.z;
		}

		void GetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			MeshResource::Vertex&  v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			fvNormOut[0] = v.normal.x;
			fvNormOut[1] = v.normal.y;
			fvNormOut[2] = v.normal.z;
		}

		void GetTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			MeshResource::Vertex&  v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			fvTexcOut[0] = v.texCoord.x;
			fvTexcOut[1] = v.texCoord.y;
		}

		void SetTangentSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
		{
			MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
			MeshResource::Vertex&  v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
			v.tangent.x = fvTangent[0];
			v.tangent.y = fvTangent[1];
			v.tangent.z = fvTangent[2];
			v.tangent.w = -fSign;
		}

		Vec3 CalculateTangent(const MeshResource::Vertex& v1, const MeshResource::Vertex& v2, const MeshResource::Vertex& v3)
		{
			Vec3 edge1 = v2.position - v1.position;
			Vec3 edge2 = v3.position - v1.position;
			Vec2 deltaUV1 = v2.texCoord - v1.texCoord;
			Vec2 deltaUV2 = v3.texCoord - v1.texCoord;

			Vec3 tangent{};

			float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
			tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
			tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
			tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

			return tangent;
		}

		void CalculateTangents(Span<MeshResource::Vertex> vertices, Span<u32> indices)
		{
			//Calculate tangents
			for (usize i = 0; i < indices.Size(); i += 3)
			{
				u32 idx0 = indices[i + 0];
				u32 idx1 = indices[i + 1];
				u32 idx2 = indices[i + 2];

				vertices[idx0].tangent = Vec4{CalculateTangent(vertices[idx0], vertices[idx1], vertices[idx2]), 1.0};
				vertices[idx1].tangent = Vec4{CalculateTangent(vertices[idx1], vertices[idx2], vertices[idx0]), 1.0};
				vertices[idx2].tangent = Vec4{CalculateTangent(vertices[idx2], vertices[idx0], vertices[idx1]), 1.0};
			}
		}


		void CalculateTangents(Span<MeshResource::Vertex> vertices)
		{
			//Calculate tangents
			for (usize i = 0; i < vertices.Size(); i += 3)
			{
				u32 idx0 = i + 0;
				u32 idx1 = i + 1;
				u32 idx2 = i + 2;
				vertices[idx0].tangent = Vec4{CalculateTangent(vertices[idx0], vertices[idx1], vertices[idx2]), 1.0};
				vertices[idx1].tangent = Vec4{CalculateTangent(vertices[idx1], vertices[idx2], vertices[idx0]), 1.0};
				vertices[idx2].tangent = Vec4{CalculateTangent(vertices[idx2], vertices[idx0], vertices[idx1]), 1.0};
			}
		}
	}


	void MeshTools::CalcNormals(Span<MeshResource::Vertex> vertices, Span<u32> indices)
	{
		for (auto& vertex : vertices)
		{
			vertex.normal = Vec3{0.0f, 0.0f, 0.0f};
		}

		for (usize i = 0; i < indices.Size(); i += 3)
		{
			u32 idx0 = indices[i + 0];
			u32 idx1 = indices[i + 1];
			u32 idx2 = indices[i + 2];

			Vec3& v0 = vertices[idx0].position;
			Vec3& v1 = vertices[idx1].position;
			Vec3& v2 = vertices[idx2].position;

			Vec3 edge1 = v1 - v0;
			Vec3 edge2 = v2 - v0;
			Vec3 normal = Vec3{
				edge1.y * edge2.z - edge1.z * edge2.y,
				edge1.z * edge2.x - edge1.x * edge2.z,
				edge1.x * edge2.y - edge1.y * edge2.x
			};

			vertices[idx0].normal = vertices[idx0].normal + normal;
			vertices[idx1].normal = vertices[idx1].normal + normal;
			vertices[idx2].normal = vertices[idx2].normal + normal;
		}

		for (auto& vertex : vertices)
		{
			float length = sqrt(vertex.normal.x * vertex.normal.x +
				vertex.normal.y * vertex.normal.y +
				vertex.normal.z * vertex.normal.z);

			if (length > 0.00001f)
			{
				float invLength = 1.0f / length;
				vertex.normal.x *= invLength;
				vertex.normal.y *= invLength;
				vertex.normal.z *= invLength;
			}
			else
			{
				vertex.normal = Vec3{0.0f, 1.0f, 0.0f};
			}
		}
	}

	void MeshTools::CalcTangents(Span<MeshResource::Vertex> vertices, Span<u32> indices, bool useMikktspace)
	{
		if (useMikktspace)
		{
			MikkTSpaceUserData userData{
				.vertices = vertices,
				.indices = indices
			};

			SMikkTSpaceInterface anInterface{
				.m_getNumFaces = GetNumFaces,
				.m_getNumVerticesOfFace = GetNumVerticesOfFace,
				.m_getPosition = GetPosition,
				.m_getNormal = GetNormal,
				.m_getTexCoord = GetTexCoord,
				.m_setTSpaceBasic = SetTangentSpaceBasic
			};

			SMikkTSpaceContext mikkTSpaceContext{
				.m_pInterface = &anInterface,
				.m_pUserData = &userData
			};

			genTangSpaceDefault(&mikkTSpaceContext);
		}
		else
		{
			CalculateTangents(vertices, indices);
		}
	}

	void SinglePassDownsampler::Init()
	{
		ComputePipelineDesc desc;
		desc.shader = Resources::FindByPath("Skore://Shaders/SPD.comp"),
			desc.debugName = "SinglePassDownsampler";
		m_pipeline = Graphics::CreateComputePipeline(desc);

		m_buffer = Graphics::CreateBuffer(BufferDesc{
			.size = sizeof(u32),
			.usage = ResourceUsage::UnorderedAccess,
			.debugName = "SinglePassDownsamplerBuffer"
		});
	}

	void SinglePassDownsampler::Downsample(GPUCommandBuffer* cmd, GPUTexture* inputTexture, GPUTexture* outputTexture)
	{
		const TextureDesc& textureDesc = inputTexture->GetDesc();

		u32 mipStart = 0;
		u32 outputMipCount = textureDesc.mipLevels - (mipStart + 1);
		u32 width = textureDesc.extent.width;
		u32 height = textureDesc.extent.height >> mipStart;
		u32 threadGroupX = (width + 63) >> 6;
		u32 threadGroupy = (height + 63) >> 6;
		SK_ASSERT(width <= 4096 && height <= 4096 && outputMipCount <= 12, "Cannot downscale this texture !");
		SK_ASSERT(mipStart < outputMipCount, "Mip start is incorrect");

		DownscaleData mipData;
		mipData.mipInfo.x = static_cast<f32>(outputMipCount);
		mipData.mipInfo.y = static_cast<f32>(threadGroupX * threadGroupy);
		mipData.mipInfo.z = static_cast<f32>(textureDesc.extent.width);
		mipData.mipInfo.w = static_cast<f32>(textureDesc.extent.height);

		cmd->BindPipeline(m_pipeline);
		cmd->PushConstants(m_pipeline, ShaderStage::Compute, 0, sizeof(DownscaleData), &mipData);
		// cmd->SetBuffer(m_pipeline, 0, 0, m_buffer, 0, 0);
		// cmd->SetTexture(m_pipeline, 0, 0, inputTexture, 0);
		// cmd->SetTexture(m_pipeline, 0, 0, outputTexture, 0);
		// cmd->SetSampler(m_pipeline, 0, 0, Graphics::GetLinearSampler());

		cmd->Dispatch(threadGroupX, threadGroupy, 1);
	}

	void SinglePassDownsampler::Destroy()
	{
		m_buffer->Destroy();
		m_pipeline->Destroy();

		m_buffer = nullptr;
		m_pipeline = nullptr;
	}


	void BRDFLUTTexture::Init(Extent extent)
	{
		if (extent.width == 0 || extent.height == 0) return;

		Destroy();

		m_texture = Graphics::CreateTexture(TextureDesc{
			.extent = {extent.width, extent.height, 1},
			.format = TextureFormat::R16G16_FLOAT,
			.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess,
			.debugName = "BRDFLUTTexture"
		});

		m_sampler = Graphics::CreateSampler(SamplerDesc{
			.addressModeU = AddressMode::ClampToEdge,
			.addressModeV = AddressMode::ClampToEdge,
			.addressModeW = AddressMode::ClampToEdge,
		});


		ComputePipelineDesc desc;
		desc.shader = Resources::FindByPath("Skore://Shaders/GenBRDFLUT.comp"),
			desc.debugName = "BRDFLUTGen";

		GPUPipeline*      computePipeline = Graphics::CreateComputePipeline(desc);
		GPUDescriptorSet* descriptorSet = Graphics::CreateDescriptorSet(desc.shader, desc.variant, 0);

		descriptorSet->UpdateTexture(0, m_texture);

		GPUCommandBuffer* cmd = Graphics::GetResourceCommandBuffer();
		cmd->Begin();

		cmd->ResourceBarrier(m_texture, ResourceState::Undefined, ResourceState::General, 0, 0);

		cmd->BindPipeline(computePipeline);
		cmd->BindDescriptorSet(computePipeline, 0, descriptorSet, {});

		cmd->Dispatch(Math::Ceil(extent.width / 32), Math::Ceil(extent.height / 32), 1);

		cmd->ResourceBarrier(m_texture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 0);

		cmd->End();
		cmd->SubmitAndWait();

		computePipeline->Destroy();
		descriptorSet->Destroy();
	}

	void BRDFLUTTexture::Destroy()
	{
		if (m_texture) m_texture->Destroy();
		if (m_sampler) m_sampler->Destroy();
		m_texture = nullptr;
		m_sampler = nullptr;
	}

	GPUTexture* BRDFLUTTexture::GetTexture() const
	{
		return m_texture;
	}

	GPUSampler* BRDFLUTTexture::GetSampler() const
	{
		return m_sampler;
	}

	void EquirectangularToCubeMap::Init()
	{
		ComputePipelineDesc desc;
		desc.shader = Resources::FindByPath("Skore://Shaders/EquirectangularToCubemap.comp");
		desc.debugName = "EquirectangularToCubemap";

		m_pipeline = Graphics::CreateComputePipeline(desc);
		m_descriptorSet = Graphics::CreateDescriptorSet(desc.shader, desc.variant, 0);
	}

	void EquirectangularToCubeMap::Execute(GPUCommandBuffer* cmd, GPUTexture* equirectangularTexture, GPUTexture* cubeMapTexture)
	{
		if (m_cubeMapTextureView && m_cubeMapTextureView->GetTexture() != cubeMapTexture)
		{
			m_cubeMapTextureView->Destroy();
			m_cubeMapTextureView = nullptr;
		}

		if (m_cubeMapTextureView == nullptr)
		{
			m_cubeMapTextureView = Graphics::CreateTextureView(TextureViewDesc{
				.texture = cubeMapTexture,
				.type = TextureViewType::Type2DArray,
			});

			m_descriptorSet->UpdateTexture(0, equirectangularTexture);
			m_descriptorSet->UpdateTextureView(1, m_cubeMapTextureView, 0);
			m_descriptorSet->UpdateSampler(2, Graphics::GetLinearSampler(), 0);
		}

		const TextureDesc& textureDesc = equirectangularTexture->GetDesc();

		cmd->BindPipeline(m_pipeline);
		cmd->BindDescriptorSet(m_pipeline, 0, m_descriptorSet, {});
		cmd->ResourceBarrier(cubeMapTexture, ResourceState::Undefined, ResourceState::General, 0, 1, 0, 6);
		cmd->Dispatch(Math::Ceil(textureDesc.extent.width / 32), Math::Ceil(textureDesc.extent.height / 32), 6.0f);
		cmd->ResourceBarrier(cubeMapTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 1, 0, 6);
	}

	void EquirectangularToCubeMap::Destroy()
	{
		if (m_pipeline) m_pipeline->Destroy();
		if (m_cubeMapTextureView) m_cubeMapTextureView->Destroy();
		if (m_descriptorSet) m_descriptorSet->Destroy();

		m_pipeline = nullptr;
		m_cubeMapTextureView = nullptr;
		m_descriptorSet = nullptr;
	}

	void DiffuseIrradianceGenerator::Init()
	{
		ComputePipelineDesc desc;
		desc.shader = Resources::FindByPath("Skore://Shaders/DiffuseIrradianceGenerator.comp"),
			desc.debugName = "DiffuseIrradianceGenerator";

		m_pipeline = Graphics::CreateComputePipeline(desc);
		m_descriptorSet = Graphics::CreateDescriptorSet(desc.shader, desc.variant, 0);
	}

	void DiffuseIrradianceGenerator::Execute(GPUCommandBuffer* cmd, GPUTexture* cubemapTexture, GPUTexture* irradianceTexture)
	{
		if (m_irradianceTextureView != nullptr && m_irradianceTextureView->GetTexture() != irradianceTexture)
		{
			m_irradianceTextureView->Destroy();
			m_irradianceTextureView = nullptr;
		}

		if (m_irradianceTextureView == nullptr)
		{
			m_irradianceTextureView = Graphics::CreateTextureView(TextureViewDesc{
				.texture = irradianceTexture,
				.type = TextureViewType::Type2DArray,
			});

			m_descriptorSet->UpdateTexture(0, cubemapTexture);
			m_descriptorSet->UpdateTextureView(1, m_irradianceTextureView);
			m_descriptorSet->UpdateSampler(2, Graphics::GetLinearSampler());
		}

		const TextureDesc& textureDesc = irradianceTexture->GetDesc();

		cmd->BindPipeline(m_pipeline);
		cmd->BindDescriptorSet(m_pipeline, 0, m_descriptorSet, {});
		cmd->ResourceBarrier(irradianceTexture, ResourceState::Undefined, ResourceState::General, 0, 1, 0, 6);
		cmd->Dispatch(Math::Ceil(textureDesc.extent.width / 8), Math::Ceil(textureDesc.extent.height / 8), 6.0f);
		cmd->ResourceBarrier(irradianceTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 1, 0, 6);
	}

	void DiffuseIrradianceGenerator::Destroy()
	{
		if (m_pipeline) m_pipeline->Destroy();
		if (m_descriptorSet) m_descriptorSet->Destroy();
		if (m_irradianceTextureView) m_irradianceTextureView->Destroy();
	}
}
