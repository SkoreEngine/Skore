#include "Skore/Graphics/RenderTools.hpp"

#include <stb_image_write.h>

#include "Skore/Graphics/Graphics.hpp"
#include "mikktspace.h"
#include "xatlas.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"

#define A_CPU
#include "FidelityFX/ffx_a.h"
#include "FidelityFX/ffx_spd.h"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::RenderTools");
		GPUPipeline* textureResizePipeline = nullptr;


		struct MikkTSpaceSoAData
		{
			Span<Vec3> positions;
			Span<Vec3> normals;
			Span<Vec2> uvs;
			Span<Vec4> tangents;
			Span<u32>  indices;
		};

		static i32 MikkGetNumFaces(const SMikkTSpaceContext* pContext)
		{
			MikkTSpaceSoAData& data = *static_cast<MikkTSpaceSoAData*>(pContext->m_pUserData);
			return (i32)data.indices.Size() / 3;
		}

		static i32 MikkGetNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace)
		{
			return 3;
		}

		static u32 MikkGetVertexIndex(const SMikkTSpaceContext* context, i32 iFace, i32 iVert)
		{
			MikkTSpaceSoAData& data = *static_cast<MikkTSpaceSoAData*>(context->m_pUserData);
			return data.indices[iFace * 3 + iVert];
		}

		static void MikkGetPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
		{
			MikkTSpaceSoAData& data = *static_cast<MikkTSpaceSoAData*>(pContext->m_pUserData);
			Vec3& v = data.positions[MikkGetVertexIndex(pContext, iFace, iVert)];
			fvPosOut[0] = v.x;
			fvPosOut[1] = v.y;
			fvPosOut[2] = v.z;
		}

		static void MikkGetNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
		{
			MikkTSpaceSoAData& data = *static_cast<MikkTSpaceSoAData*>(pContext->m_pUserData);
			Vec3& v = data.normals[MikkGetVertexIndex(pContext, iFace, iVert)];
			fvNormOut[0] = v.x;
			fvNormOut[1] = v.y;
			fvNormOut[2] = v.z;
		}

		static void MikkGetTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
		{
			MikkTSpaceSoAData& data = *static_cast<MikkTSpaceSoAData*>(pContext->m_pUserData);
			Vec2& v = data.uvs[MikkGetVertexIndex(pContext, iFace, iVert)];
			fvTexcOut[0] = v.x;
			fvTexcOut[1] = v.y;
		}

		static void MikkSetTangentSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
		{
			MikkTSpaceSoAData& data = *static_cast<MikkTSpaceSoAData*>(pContext->m_pUserData);
			Vec4& t = data.tangents[MikkGetVertexIndex(pContext, iFace, iVert)];
			t.x = fvTangent[0];
			t.y = fvTangent[1];
			t.z = fvTangent[2];
			t.w = -fSign;
		}
	}


	void MeshTools::CalcNormals(Span<Vec3> positions, Span<Vec3> normals, Span<u32> indices)
	{
		for (usize i = 0; i < normals.Size(); i++)
		{
			normals[i] = Vec3{0.0f, 0.0f, 0.0f};
		}

		for (usize i = 0; i < indices.Size(); i += 3)
		{
			u32 idx0 = indices[i + 0];
			u32 idx1 = indices[i + 1];
			u32 idx2 = indices[i + 2];

			Vec3& v0 = positions[idx0];
			Vec3& v1 = positions[idx1];
			Vec3& v2 = positions[idx2];

			Vec3 edge1 = v1 - v0;
			Vec3 edge2 = v2 - v0;
			Vec3 normal = Vec3{
				edge1.y * edge2.z - edge1.z * edge2.y,
				edge1.z * edge2.x - edge1.x * edge2.z,
				edge1.x * edge2.y - edge1.y * edge2.x
			};

			normals[idx0] = normals[idx0] + normal;
			normals[idx1] = normals[idx1] + normal;
			normals[idx2] = normals[idx2] + normal;
		}

		for (usize i = 0; i < normals.Size(); i++)
		{
			Vec3& n = normals[i];
			float length = sqrt(n.x * n.x + n.y * n.y + n.z * n.z);

			if (length > 0.00001f)
			{
				float invLength = 1.0f / length;
				n.x *= invLength;
				n.y *= invLength;
				n.z *= invLength;
			}
			else
			{
				n = Vec3{0.0f, 1.0f, 0.0f};
			}
		}
	}

	void MeshTools::CalcTangents(Span<Vec3> positions, Span<Vec3> normals, Span<Vec2> uvs, Span<Vec4> tangents, Span<u32> indices)
	{
		MikkTSpaceSoAData userData{
			.positions = positions,
			.normals = normals,
			.uvs = uvs,
			.tangents = tangents,
			.indices = indices
		};

		SMikkTSpaceInterface anInterface{
			.m_getNumFaces = MikkGetNumFaces,
			.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace,
			.m_getPosition = MikkGetPosition,
			.m_getNormal = MikkGetNormal,
			.m_getTexCoord = MikkGetTexCoord,
			.m_setTSpaceBasic = MikkSetTangentSpaceBasic
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

	void RenderTools::TextureResize(GPUCommandBuffer* cmd, GPUTexture* srcTexture, GPUTexture* dstTexture)
	{
		if (!textureResizePipeline)
		{
			textureResizePipeline = Graphics::CreateComputePipeline(ComputePipelineDesc{
				.shader = Resources::FindByPath("Skore://Shaders/TextureResize.comp"),
				.allowImmediateSet = true,
				.debugName = "TextureResizePipeline",
			});
		}
		cmd->BindPipeline(textureResizePipeline);

		Extent3D extent = dstTexture->GetDesc().extent;

		struct PushConstants
		{
			Extent outputSize;
			Extent pad0;
		};

		PushConstants pushConstants;
		pushConstants.outputSize = {extent.width, extent.height};
		cmd->PushConstants(textureResizePipeline, ShaderStage::Compute, 0, sizeof(PushConstants), &pushConstants);

		cmd->SetTexture(textureResizePipeline, 0, 0, srcTexture, 0);
		cmd->SetTexture(textureResizePipeline, 0, 1, dstTexture, 0);
		cmd->SetSampler(textureResizePipeline, 0, 2, Graphics::GetLinearSampler());

		cmd->ResourceBarrier(dstTexture, ResourceState::ShaderReadOnly, ResourceState::General, 0, 0);
		cmd->Dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);
		cmd->ResourceBarrier(dstTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 0);
	}

	void SinglePassDownsampler::Init(GPUTexture* inputTexture)
	{
		m_texture = inputTexture;

		const TextureDesc& textureDesc = inputTexture->GetDesc();
		SK_ASSERT(textureDesc.mipLevels > 1, "texture require mips");

		ComputePipelineDesc desc;
		desc.shader = Resources::FindByPath("Skore://Shaders/FidelityFX/SPD.shader");

		if (textureDesc.format == TextureFormat::R8G8B8A8_UNORM)
		{
			desc.variant = "VariantRGBA";
		}
		else if (textureDesc.format == TextureFormat::R32G32B32A32_FLOAT)
		{
			desc.variant = "VariantRGBA32F";
		}
		else if (textureDesc.format == TextureFormat::R16G16B16A16_FLOAT)
		{
			desc.variant = "VariantRGBA16F";
		}
		else
		{
			SK_ASSERT(false, "unsupported texture format");
		}

		desc.debugName = "SinglePassDownsampler";
		m_pipeline = Graphics::CreateComputePipeline(desc);
		m_descriptorSet = Graphics::CreateDescriptorSet(desc.shader, desc.variant, 0);

		m_buffer = Graphics::CreateBuffer(BufferDesc{
			.size = sizeof(u32) * textureDesc.arrayLayers,
			.usage = ResourceUsage::UnorderedAccess,
			.hostVisible = true,
			.debugName = "SinglePassDownsamplerBuffer"
		});

		VoidPtr mapped = m_buffer->Map();
		for (u32 i = 0; i < textureDesc.arrayLayers; i++)
		{
			static_cast<u32*>(mapped)[i] = 0;
		}
		m_buffer->Unmap();

		m_descriptorSet->UpdateBuffer(2, m_buffer, 0, sizeof(u32) * textureDesc.arrayLayers);

		for (u32 mip = 0; mip < 13; mip++)
		{
			TextureViewDesc textureViewDesc = {};
			textureViewDesc.type = TextureViewType::Type2DArray;
			textureViewDesc.texture = inputTexture;
			textureViewDesc.baseMipLevel = std::min(textureDesc.mipLevels - 1, mip);
			m_textureViews[mip] = Graphics::CreateTextureView(textureViewDesc);
			m_descriptorSet->UpdateTextureView(0, m_textureViews[mip], mip);
		}
		m_descriptorSet->UpdateTextureView(1, m_textureViews[6], 0);
	}

	void SinglePassDownsampler::Execute(GPUCommandBuffer* cmd)
	{
		SK_ASSERT(m_texture, "SinglePassDownsampler is not initialized");

		const TextureDesc& textureDesc = m_texture->GetDesc();

		varAU2(dispatchThreadGroupCountXY);
		varAU2(workGroupOffset); // needed if Left and Top are not 0,0
		varAU2(numWorkGroupsAndMips);
		varAU4(rectInfo) = initAU4(0, 0, textureDesc.extent.width, textureDesc.extent.height); // left, top, width, height
		SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

		SpdConstants data;
		data.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
		data.mips = numWorkGroupsAndMips[1];
		data.workGroupOffset[0] = workGroupOffset[0];
		data.workGroupOffset[1] = workGroupOffset[1];

		u32 dispatchX = dispatchThreadGroupCountXY[0];
		u32 dispatchY = dispatchThreadGroupCountXY[1];
		u32 dispatchZ = textureDesc.arrayLayers;

		cmd->BindPipeline(m_pipeline);
		cmd->PushConstants(m_pipeline, ShaderStage::Compute, 0, sizeof(SpdConstants), &data);
		cmd->BindDescriptorSet(m_pipeline, 0, m_descriptorSet, {});

		cmd->ResourceBarrier(m_texture, ResourceState::ShaderReadOnly, ResourceState::General, 0, textureDesc.mipLevels, 0, textureDesc.arrayLayers);
		cmd->Dispatch(dispatchX, dispatchY, dispatchZ);
		cmd->ResourceBarrier(m_texture, ResourceState::General, ResourceState::ShaderReadOnly, 0, textureDesc.mipLevels, 0, textureDesc.arrayLayers);
	}

	SinglePassDownsampler::~SinglePassDownsampler()
	{
		if (m_buffer) m_buffer->Destroy();
		if (m_pipeline) m_pipeline->Destroy();
		if (m_descriptorSet) m_descriptorSet->Destroy();

		for (auto& textureView : m_textureViews)
		{
			if (textureView)
			{
				textureView->Destroy();
			}
		}
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
		desc.shader = Resources::FindByPath("Skore://Shaders/GenBRDFLUT.comp");
		desc.debugName = "BRDFLUTGen";

		GPUPipeline*      computePipeline = Graphics::CreateComputePipeline(desc);
		GPUDescriptorSet* descriptorSet = Graphics::CreateDescriptorSet(desc.shader, desc.variant, 0);

		descriptorSet->UpdateTexture(0, m_texture);

		Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
		{
			cmd->ResourceBarrier(m_texture, ResourceState::Undefined, ResourceState::General, 0, 0);

			cmd->BindPipeline(computePipeline);
			cmd->BindDescriptorSet(computePipeline, 0, descriptorSet, {});

			cmd->Dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);

			cmd->ResourceBarrier(m_texture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 0);
		});

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
		TextureDesc cubemapDesc = cubeMapTexture->GetDesc();

		cmd->BindPipeline(m_pipeline);
		cmd->BindDescriptorSet(m_pipeline, 0, m_descriptorSet, {});
		cmd->ResourceBarrier(cubeMapTexture, ResourceState::Undefined, ResourceState::General, 0, cubemapDesc.mipLevels, 0, cubemapDesc.arrayLayers);
		cmd->Dispatch((textureDesc.extent.width + 31) / 32, (textureDesc.extent.height + 31) / 32, 6.0f);
		cmd->ResourceBarrier(cubeMapTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, cubemapDesc.mipLevels, 0, cubemapDesc.arrayLayers);
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
		cmd->Dispatch((textureDesc.extent.width + 7) / 8, (textureDesc.extent.height + 7) / 8, 6.0f);
		cmd->ResourceBarrier(irradianceTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, 1, 0, 6);
	}

	void DiffuseIrradianceGenerator::Destroy()
	{
		if (m_pipeline) m_pipeline->Destroy();
		if (m_descriptorSet) m_descriptorSet->Destroy();
		if (m_irradianceTextureView) m_irradianceTextureView->Destroy();
	}

	SpecularMapGenerator::~SpecularMapGenerator()
	{
		if (m_pipeline) m_pipeline->Destroy();
		for (GPUTextureView* view : m_views)
		{
			view->Destroy();
		}
	}

	void SpecularMapGenerator::Init(GPUTexture* cubemapTexture, GPUTexture* specularMapTexture)
	{
		ComputePipelineDesc desc;
		desc.shader = Resources::FindByPath("Skore://Shaders/SpecularMap.comp");
		desc.allowImmediateSet = true;
		m_pipeline = Graphics::CreateComputePipeline(desc);

		m_cubemapTexture = cubemapTexture;
		m_specularMapTexture = specularMapTexture;

		TextureDesc specularDesc = m_specularMapTexture->GetDesc();

		m_views.Resize(specularDesc.mipLevels);

		for (u32 mip = 0; mip < specularDesc.mipLevels; ++mip)
		{
			m_views[mip] = Graphics::CreateTextureView(TextureViewDesc{
				.texture = m_specularMapTexture,
				.type = TextureViewType::Type2DArray,
				.baseMipLevel = mip
			});
		}
	}

	void SpecularMapGenerator::Execute(GPUCommandBuffer* cmd) const
	{
		cmd->BindPipeline(m_pipeline);
		TextureDesc specularDesc = m_specularMapTexture->GetDesc();

		cmd->ResourceBarrier(m_specularMapTexture, ResourceState::Undefined, ResourceState::General, 0, specularDesc.mipLevels, 0, specularDesc.arrayLayers);

		for (u32 mip = 0; mip < specularDesc.mipLevels; ++mip)
		{
			u32 mipWidth = specularDesc.extent.width * std::pow(0.5, mip);
			u32 mipHeight = specularDesc.extent.height * std::pow(0.5, mip);

			SpecularMapFilterSettings settings{
				.roughness = Max(static_cast<float>(mip) / static_cast<float>(specularDesc.mipLevels - 1), 0.01f)
			};

			cmd->PushConstants(m_pipeline, ShaderStage::Compute, 0, sizeof(settings), &settings);

			cmd->SetTexture(m_pipeline, 0, 0, m_cubemapTexture, 0);
			cmd->SetTextureView(m_pipeline, 0, 1, m_views[mip], 0);
			cmd->SetSampler(m_pipeline, 0, 2, Graphics::GetLinearSampler());

			cmd->Dispatch(std::ceil(mipWidth / 32.f), std::ceil(mipHeight / 32.f), 6);
		}
		cmd->ResourceBarrier(m_specularMapTexture, ResourceState::General, ResourceState::ShaderReadOnly, 0, specularDesc.mipLevels, 0, specularDesc.arrayLayers);
	}


	void MeshTools::GenerateLightmapUV(f32 lightMapTexelSize, Array<Vec3>& positions, Array<Vec3>& normals, Array<Vec2>& uvs, Array<Vec2>& uv2s,
		Array<Vec3>& colors, Array<Vec4>& tangents, Array<u32>& indices, Array<MeshPrimitive>& primitives, const Vec3& scale, Vec2& lightmapSizeHint)
	{
		if (positions.Empty() || indices.Empty() || primitives.Empty()) return;

		Array<Vec3> posCopy;
		posCopy.Resize(positions.Size());

		for (u32 p = 0; p < posCopy.Size(); p++)
		{
			posCopy[p] = positions[p] * scale;
		}

		xatlas::Atlas* atlas = xatlas::Create();

		// Add each primitive as a separate mesh so xatlas preserves primitive boundaries
		for (u32 p = 0; p < primitives.Size(); p++)
		{
			MeshPrimitive& prim = primitives[p];

			xatlas::MeshDecl meshDecl{};
			meshDecl.vertexCount = static_cast<u32>(posCopy.Size());
			meshDecl.vertexPositionData = posCopy.Data();
			meshDecl.vertexPositionStride = sizeof(Vec3);
			meshDecl.vertexNormalData = normals.Data();
			meshDecl.vertexNormalStride = sizeof(Vec3);
			meshDecl.indexCount = prim.indexCount;
			meshDecl.indexData = &indices[prim.firstIndex];
			meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

			xatlas::AddMeshError error = xatlas::AddMesh(atlas, meshDecl);
			if (error != xatlas::AddMeshError::Success)
			{
				logger.Error("xatlas::AddMesh failed for primitive {}: {}", p, static_cast<i32>(error));
				xatlas::Destroy(atlas);
				return;
			}
		}

		xatlas::ChartOptions chartOptions;
		chartOptions.fixWinding = true;

		xatlas::PackOptions packOptions;
		packOptions.padding = 1;
		packOptions.maxChartSize = 4094;
		packOptions.blockAlign = true;
		packOptions.texelsPerUnit = 1.0f / lightMapTexelSize;

		xatlas::Generate(atlas, chartOptions, packOptions);

		f32 atlasWidth = static_cast<f32>(atlas->width);
		f32 atlasHeight = static_cast<f32>(atlas->height);

		lightmapSizeHint = Vec2{atlasWidth, atlasHeight};

		logger.Info("Atlas size: {}x{}", atlasWidth, atlasHeight);

		Array<Vec3> newPositions;
		Array<Vec3> newNormals;
		Array<Vec2> newUvs;
		Array<Vec2> newUv2s;
		Array<Vec3> newColors;
		Array<Vec4> newTangents;
		Array<u32>  newIndices;

		for (u32 p = 0; p < primitives.Size(); p++)
		{
			MeshPrimitive& prim = primitives[p];
			const xatlas::Mesh& outputMesh = atlas->meshes[p];

			u32 vertexOffset = static_cast<u32>(newPositions.Size());

			prim.firstIndex = static_cast<u32>(newIndices.Size());
			prim.indexCount = outputMesh.indexCount;

			for (u32 i = 0; i < outputMesh.vertexCount; i++)
			{
				const xatlas::Vertex& xvert = outputMesh.vertexArray[i];
				u32 ref = xvert.xref;

				newPositions.EmplaceBack(positions[ref]);
				newNormals.EmplaceBack(normals[ref]);
				newUvs.EmplaceBack(uvs.Empty() ? Vec2(0, 0) : uvs[ref]);
				if (!colors.Empty()) newColors.EmplaceBack(colors[ref]);
				if (!tangents.Empty()) newTangents.EmplaceBack(tangents[ref]);

				if (atlasWidth > 0.0f && atlasHeight > 0.0f)
				{
					newUv2s.EmplaceBack(Vec2{xvert.uv[0] / atlasWidth, xvert.uv[1] / atlasHeight});
				}
				else
				{
					newUv2s.EmplaceBack(Vec2{0, 0});
				}
			}

			for (u32 i = 0; i < outputMesh.indexCount; i++)
			{
				newIndices.EmplaceBack(outputMesh.indexArray[i] + vertexOffset);
			}
		}

		xatlas::Destroy(atlas);

		positions = std::move(newPositions);
		normals = std::move(newNormals);
		uvs = std::move(newUvs);
		uv2s = std::move(newUv2s);
		colors = std::move(newColors);
		tangents = std::move(newTangents);
		indices = std::move(newIndices);
	}

	namespace
	{
		bool IsHDRFormat(TextureFormat format)
		{
			switch (format)
			{
				case TextureFormat::R16_FLOAT:
				case TextureFormat::R16G16_FLOAT:
				case TextureFormat::R16G16B16_FLOAT:
				case TextureFormat::R16G16B16A16_FLOAT:
				case TextureFormat::R32_FLOAT:
				case TextureFormat::R32G32_FLOAT:
				case TextureFormat::R32G32B32_FLOAT:
				case TextureFormat::R32G32B32A32_FLOAT:
					return true;
				default:
					return false;
			}
		}

		bool IsR16FloatFormat(TextureFormat format)
		{
			switch (format)
			{
				case TextureFormat::R16_FLOAT:
				case TextureFormat::R16G16_FLOAT:
				case TextureFormat::R16G16B16_FLOAT:
				case TextureFormat::R16G16B16A16_FLOAT:
					return true;
				default:
					return false;
			}
		}

		f32 HalfToFloat(u16 h)
		{
			u32 sign = (h >> 15) & 0x1;
			u32 exponent = (h >> 10) & 0x1F;
			u32 mantissa = h & 0x3FF;

			u32 f;
			if (exponent == 0)
			{
				if (mantissa == 0)
				{
					f = sign << 31;
				}
				else
				{
					exponent = 1;
					while (!(mantissa & 0x400))
					{
						mantissa <<= 1;
						exponent--;
					}
					mantissa &= 0x3FF;
					f = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
				}
			}
			else if (exponent == 31)
			{
				f = (sign << 31) | 0x7F800000 | (mantissa << 13);
			}
			else
			{
				f = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
			}

			f32 result;
			memcpy(&result, &f, sizeof(f32));
			return result;
		}
	}

	void RenderTools::SaveTextureToDisk(GPUTexture* texture, ResourceState currentState, const StringView& directory, const StringView& name)
	{
		if (!texture) return;

		const TextureDesc& desc = texture->GetDesc();
		u32 layers = desc.cubemap ? desc.arrayLayers : 1;
		u32 formatSize = GetTextureFormatSize(desc.format);
		i8 channels = GetTextureFormatNumChannels(desc.format);
		bool isHDR = IsHDRFormat(desc.format);
		bool isR16 = IsR16FloatFormat(desc.format);

		usize maxLayerSize = static_cast<usize>(desc.extent.width) * desc.extent.height * formatSize;

		GPUBuffer* stagingBuffer = Graphics::CreateBuffer(BufferDesc{
			.size = maxLayerSize,
			.usage = ResourceUsage::CopyDest,
			.hostVisible = true,
			.persistentMapped = true
		});

		for (u32 layer = 0; layer < layers; ++layer)
		{
			u32 mipWidth = desc.extent.width;
			u32 mipHeight = desc.extent.height;

			for (u32 mip = 0; mip < desc.mipLevels; ++mip)
			{
				Graphics::SubmitGPUWork(QueueType::Graphics, [&](GPUCommandBuffer* cmd)
				{
					cmd->ResourceBarrier(texture, currentState, ResourceState::CopySource, mip, layer);
					cmd->CopyTextureToBuffer({
						.buffer = stagingBuffer,
						.texture = texture,
						.extent = Extent3D(mipWidth, mipHeight, 1),
						.mipLevel = mip,
						.arrayLayer = layer,
					});
					cmd->ResourceBarrier(texture, ResourceState::CopySource, currentState, mip, layer);
				});

				String fileName{name};
				if (layers > 1)
				{
					char suffix[16];
					snprintf(suffix, sizeof(suffix), "_%u", layer);
					fileName += suffix;
				}
				if (desc.mipLevels > 1)
				{
					char suffix[16];
					snprintf(suffix, sizeof(suffix), "_mip%u", mip);
					fileName += suffix;
				}

				VoidPtr data = stagingBuffer->GetMappedData();

				if (isHDR)
				{
					String filePath = Path::Join(directory, fileName + ".hdr");

					if (isR16)
					{
						u32 pixelCount = mipWidth * mipHeight * channels;
						Array<f32> floatData;
						floatData.Resize(pixelCount);
						const u16* halfData = static_cast<const u16*>(data);
						for (u32 i = 0; i < pixelCount; ++i)
						{
							floatData[i] = HalfToFloat(halfData[i]);
						}
						stbi_write_hdr(filePath.CStr(), mipWidth, mipHeight, channels, floatData.Data());
					}
					else
					{
						stbi_write_hdr(filePath.CStr(), mipWidth, mipHeight, channels, static_cast<const float*>(data));
					}
				}
				else
				{
					String filePath = Path::Join(directory, fileName + ".bmp");
					stbi_write_bmp(filePath.CStr(), mipWidth, mipHeight, channels, data);
				}

				if (mipWidth > 1) mipWidth /= 2;
				if (mipHeight > 1) mipHeight /= 2;
			}
		}

		stagingBuffer->Destroy();
	}

	void RenderToolsInit()
	{
		//TODO
	}

	void RenderToolsShutdown()
	{
		if (textureResizePipeline)
		{
			textureResizePipeline->Destroy();
			textureResizePipeline = nullptr;
		}
	}
}