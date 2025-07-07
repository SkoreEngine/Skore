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
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		template <typename T>
		struct MikkTSpaceGenerator
		{
			struct MikkTSpaceUserData
			{
				Span<T>   vertices;
				Span<u32> indices;
			};

			static i32 GetNumFaces(const SMikkTSpaceContext* pContext)
			{
				MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
				return (i32)mesh.indices.Size() / 3;
			}

			static i32 GetNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace)
			{
				return 3;
			}

			static u32 GetVertexIndex(const SMikkTSpaceContext* context, i32 iFace, i32 iVert)
			{
				MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(context->m_pUserData);

				auto faceSize = GetNumVerticesOfFace(context, iFace);
				auto indicesIndex = (iFace * faceSize) + iVert;
				return mesh.indices[indicesIndex];
			}

			static void GetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
			{
				MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);

				T& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
				fvPosOut[0] = v.position.x;
				fvPosOut[1] = v.position.y;
				fvPosOut[2] = v.position.z;
			}

			static void GetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
			{
				MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);

				T& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
				fvNormOut[0] = v.normal.x;
				fvNormOut[1] = v.normal.y;
				fvNormOut[2] = v.normal.z;
			}

			static void GetTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
			{
				MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);

				T& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
				fvTexcOut[0] = v.texCoord.x;
				fvTexcOut[1] = v.texCoord.y;
			}

			static void SetTangentSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
			{
				MikkTSpaceUserData& mesh = *static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);

				T& v = mesh.vertices[GetVertexIndex(pContext, iFace, iVert)];
				v.tangent.x = fvTangent[0];
				v.tangent.y = fvTangent[1];
				v.tangent.z = fvTangent[2];
				v.tangent.w = -fSign;
			}
		};
	}


	void MeshTools::CalcNormals(Span<MeshStaticVertex> vertices, Span<u32> indices)
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

	void MeshTools::CalcTangents(Span<MeshStaticVertex> vertices, Span<u32> indices)
	{
		using MikkTSpaceGen = MikkTSpaceGenerator<MeshStaticVertex>;

		MikkTSpaceGen::MikkTSpaceUserData userData{
			.vertices = vertices,
			.indices = indices
		};

		SMikkTSpaceInterface anInterface{
			.m_getNumFaces = MikkTSpaceGen::GetNumFaces,
			.m_getNumVerticesOfFace = MikkTSpaceGen::GetNumVerticesOfFace,
			.m_getPosition = MikkTSpaceGen::GetPosition,
			.m_getNormal = MikkTSpaceGen::GetNormal,
			.m_getTexCoord = MikkTSpaceGen::GetTexCoord,
			.m_setTSpaceBasic = MikkTSpaceGen::SetTangentSpaceBasic
		};

		SMikkTSpaceContext mikkTSpaceContext{
			.m_pInterface = &anInterface,
			.m_pUserData = &userData
		};

		genTangSpaceDefault(&mikkTSpaceContext);
	}

	void MeshTools::CalcTangents(Span<MeshSkeletalVertex> vertices, Span<u32> indices)
	{
		using MikkTSpaceGen = MikkTSpaceGenerator<MeshSkeletalVertex>;

		MikkTSpaceGen::MikkTSpaceUserData userData{
			.vertices = vertices,
			.indices = indices
		};

		SMikkTSpaceInterface anInterface{
			.m_getNumFaces = MikkTSpaceGen::GetNumFaces,
			.m_getNumVerticesOfFace = MikkTSpaceGen::GetNumVerticesOfFace,
			.m_getPosition = MikkTSpaceGen::GetPosition,
			.m_getNormal = MikkTSpaceGen::GetNormal,
			.m_getTexCoord = MikkTSpaceGen::GetTexCoord,
			.m_setTSpaceBasic = MikkTSpaceGen::SetTangentSpaceBasic
		};

		SMikkTSpaceContext mikkTSpaceContext{
			.m_pInterface = &anInterface,
			.m_pUserData = &userData
		};

		genTangSpaceDefault(&mikkTSpaceContext);
	}

	u64 MeshTools::GenerateIndices(const Array<MeshStaticVertex>& allVertices, Array<u32>& newIndices, Array<MeshStaticVertex>& newVertices, bool checkForDuplicates)
	{
		newIndices.Reserve(allVertices.Size());
		newVertices.Reserve(allVertices.Size());

		u64 reduced = 0;

		if (checkForDuplicates)
		{
			HashMap<MeshStaticVertex, u32> uniqueVertices{};
			for (const MeshStaticVertex& vertexData : allVertices)
			{
				auto it = uniqueVertices.Find(vertexData);
				if (it == uniqueVertices.end())
				{
					it = uniqueVertices.Insert({vertexData, static_cast<u32>(newVertices.Size())}).first;
					newVertices.EmplaceBack(vertexData);
				}
				newIndices.EmplaceBack(it->second);
			}

			reduced = allVertices.Size() - newVertices.Size();
		}
		else
		{
			for (const MeshStaticVertex& vertexData : allVertices)
			{
				newIndices.EmplaceBack(static_cast<u32>(newVertices.Size()));
				newVertices.EmplaceBack(vertexData);
			}
		}

		newVertices.ShrinkToFit();

		return reduced;
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
