#include "Skore/Resource/Importers/MeshImporter.hpp"

#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::MeshImporter");

		RID CreateVertexAttributeResource(StringView name, u32 offset, u32 size, UndoRedoScope* scope)
		{
			RID rid = Resources::Create<VertexAttributeResource>(UUID::RandomUUID(), scope);
			ResourceObject obj = Resources::Write(rid);
			obj.SetString(VertexAttributeResource::Name, name);
			obj.SetUInt(VertexAttributeResource::Offset, offset);
			obj.SetUInt(VertexAttributeResource::Size, size);
			obj.Commit(scope);
			return rid;
		}

		struct MeshGeometryResult
		{
			Vec3           aabbMin;
			Vec3           aabbMax;
			Vec2					 lightmapSizeHint;
			RID            vertexLayout;
			RID            meshLOD;
			ResourceBuffer buffer;
		};

		MeshGeometryResult ProcessMeshGeometry(const MeshImportSettings& settings,
		                                       Array<Vec3>& positions, Array<Vec3>& normals, Array<Vec2>& uvs, Array<Vec2>& uv2s,
		                                       Array<Vec3>& colors, Array<Vec4>& tangents, Array<BoneInfluence>& bones,
		                                       Array<u32>& indices, Array<MeshPrimitive>& primitives,
		                                       const Vec3& scale,
		                                       UndoRedoScope* scope)
		{
			u64 vertexCount = positions.Size();
			bool hasBones = !bones.Empty();

			// Normals: generate if empty or if settings say to regenerate
			if (settings.regenerateNormals || normals.Empty())
			{
				normals.Resize(vertexCount, Vec3(0, 1, 0));
				MeshTools::CalcNormals(positions, normals, indices);
			}

			// UV0: ensure present
			if (uvs.Empty())
			{
				uvs.Resize(vertexCount, Vec2(0, 0));
			}

			Vec2 lightmapSizeHint = {};

			// Generate UV2s (xatlas splits vertices, so update vertexCount)
			if (settings.generateUV1s && !hasBones)
			{
				MeshTools::GenerateLightmapUV(settings.lightMapTexelSize, positions, normals, uvs, uv2s, colors, tangents, indices, primitives, scale, lightmapSizeHint);
				vertexCount = positions.Size();
			}

			// Tangents: generate if empty or if settings say to recalculate
			if (settings.recalculateTangents || tangents.Empty())
			{
				tangents.Resize(vertexCount, Vec4(0, 0, 0, 0));
				MeshTools::CalcTangents(positions, normals, uvs, tangents, indices);
			}

			// Compute primitive AABBs
			for (MeshPrimitive& primitive : primitives)
			{
				Vec3 minBounds = positions[indices[primitive.firstIndex]];
				Vec3 maxBounds = positions[indices[primitive.firstIndex]];

				for (usize i = primitive.firstIndex; i < primitive.firstIndex + primitive.indexCount; i++)
				{
					minBounds = Vec3::Min(minBounds, positions[indices[i]]);
					maxBounds = Vec3::Max(maxBounds, positions[indices[i]]);
				}
				primitive.aabb = AABB(minBounds, maxBounds);
			}

			// Compute mesh AABB
			Vec3 minBounds = positions[0];
			Vec3 maxBounds = positions[0];

			for (const auto& pos : positions)
			{
				minBounds = Vec3::Min(minBounds, pos);
				maxBounds = Vec3::Max(maxBounds, pos);
			}

			// Build dynamic vertex layout — only pack attributes that are present
			u32 stride = 0;
			Array<RID> layoutAttributes;

			layoutAttributes.EmplaceBack(CreateVertexAttributeResource("position", stride, sizeof(Vec3), scope));
			stride += sizeof(Vec3);

			layoutAttributes.EmplaceBack(CreateVertexAttributeResource("normal", stride, sizeof(Vec3), scope));
			stride += sizeof(Vec3);

			layoutAttributes.EmplaceBack(CreateVertexAttributeResource("uv0", stride, sizeof(Vec2), scope));
			stride += sizeof(Vec2);

			bool hasUV2 = !uv2s.Empty();
			if (hasUV2)
			{
				layoutAttributes.EmplaceBack(CreateVertexAttributeResource("uv1", stride, sizeof(Vec2), scope));
				stride += sizeof(Vec2);
			}

			bool hasColors = !colors.Empty();
			if (hasColors)
			{
				layoutAttributes.EmplaceBack(CreateVertexAttributeResource("color", stride, sizeof(Vec3), scope));
				stride += sizeof(Vec3);
			}

			layoutAttributes.EmplaceBack(CreateVertexAttributeResource("tangent", stride, sizeof(Vec4), scope));
			stride += sizeof(Vec4);

			if (hasBones)
			{
				layoutAttributes.EmplaceBack(CreateVertexAttributeResource("boneIndices", stride, sizeof(u32) * 4, scope));
				stride += sizeof(u32) * 4;

				layoutAttributes.EmplaceBack(CreateVertexAttributeResource("boneWeights", stride, sizeof(Vec4), scope));
				stride += sizeof(Vec4);
			}

			RID vertexLayoutRID = Resources::Create<VertexLayoutResource>(UUID::RandomUUID(), scope);
			ResourceObject vertexLayoutObj = Resources::Write(vertexLayoutRID);
			vertexLayoutObj.AddToSubObjectList(VertexLayoutResource::Attributes, layoutAttributes);
			vertexLayoutObj.SetUInt(VertexLayoutResource::Stride, stride);
			vertexLayoutObj.Commit(scope);

			// Build interleaved vertex buffer in memory
			ByteBuffer vertexBuffer;
			vertexBuffer.Resize(vertexCount * stride);

			for (u64 i = 0; i < vertexCount; i++)
			{
				u8* dst = vertexBuffer.Data() + i * stride;
				u64 off = 0;

				memcpy(dst + off, &positions[i], sizeof(Vec3));  off += sizeof(Vec3);
				memcpy(dst + off, &normals[i], sizeof(Vec3));    off += sizeof(Vec3);
				memcpy(dst + off, &uvs[i], sizeof(Vec2));        off += sizeof(Vec2);

				if (hasUV2)
				{
					memcpy(dst + off, &uv2s[i], sizeof(Vec2));   off += sizeof(Vec2);
				}

				if (hasColors)
				{
					memcpy(dst + off, &colors[i], sizeof(Vec3)); off += sizeof(Vec3);
				}

				memcpy(dst + off, &tangents[i], sizeof(Vec4));   off += sizeof(Vec4);

				if (hasBones)
				{
					memcpy(dst + off, bones[i].indices, sizeof(u32) * 4); off += sizeof(u32) * 4;
					memcpy(dst + off, &bones[i].weights, sizeof(Vec4));   off += sizeof(Vec4);
				}
			}

			ResourceBuffer resourceBuffer = ResourceAssets::CreateTempBuffer();
			FileHandler bufferFile = resourceBuffer.OpenFile(AccessMode::WriteOnly);

			u64 vertexBufferSize = vertexCount * stride;
			u64 indexBufferSize = indices.Size() * sizeof(u32);
			u64 primitiveBufferSize = primitives.Size() * sizeof(MeshPrimitive);

			u64 verticeOffset = 0;
			u64 indicesOffset = vertexBufferSize;
			u64 primitiveOffset = vertexBufferSize + indexBufferSize;

			FileSystem::WriteFile(bufferFile, vertexBuffer.Data(), vertexBufferSize);
			FileSystem::WriteFile(bufferFile, indices.Data(), indexBufferSize);
			FileSystem::WriteFile(bufferFile, primitives.Data(), primitiveBufferSize);
			FileSystem::CloseFile(bufferFile);

			RID meshLOD = Resources::Create<MeshLodResource>(UUID::RandomUUID(), scope);
			ResourceObject meshLodObject = Resources::Write(meshLOD);
			meshLodObject.SetUInt(MeshLodResource::LodNumber, 0);
			meshLodObject.SetUInt(MeshLodResource::VerticesOffset, verticeOffset);
			meshLodObject.SetUInt(MeshLodResource::VerticesCount, vertexCount);
			meshLodObject.SetUInt(MeshLodResource::IndicesOffset, indicesOffset);
			meshLodObject.SetUInt(MeshLodResource::IndicesCount, indices.Size());
			meshLodObject.SetUInt(MeshLodResource::PrimitiveCount, primitives.Size());
			meshLodObject.SetUInt(MeshLodResource::PrimitiveOffset, primitiveOffset);
			meshLodObject.Commit(scope);

			return MeshGeometryResult{
				.aabbMin = minBounds,
				.aabbMax = maxBounds,
				.lightmapSizeHint = lightmapSizeHint,
				.vertexLayout = vertexLayoutRID,
				.meshLOD = meshLOD,
				.buffer = resourceBuffer,
			};
		}

		void WriteGeometryToMesh(ResourceObject& meshObject, const MeshGeometryResult& result)
		{
			meshObject.SetVec3(MeshResource::AABBMin, result.aabbMin);
			meshObject.SetVec3(MeshResource::AABBMax, result.aabbMax);
			meshObject.SetVec2(MeshResource::LightmapSizeHint, result.lightmapSizeHint);
			meshObject.SetSubObject(MeshResource::VertexLayout, result.vertexLayout);

			// Remove old LODs if any, then add the new one
			Array<RID> oldLods(meshObject.GetSubObjectList(MeshResource::MeshLODs));
			for (RID oldLod : oldLods)
			{
				meshObject.RemoveFromSubObjectList(MeshResource::MeshLODs, oldLod);
			}
			meshObject.AddToSubObjectList(MeshResource::MeshLODs, result.meshLOD);

			meshObject.SetBuffer(MeshResource::MeshData, result.buffer);
		}
	}

	RID ImportMesh(RID directory, const MeshImportSettings& settings, StringView name, Span<RID> materials, Span<MeshPrimitive> primitives,
	               const MeshVertexImportData& vertexData, Span<u32> indices, RID skin, Vec3 scale, UndoRedoScope* scope)
	{
		// Position is mandatory
		if (vertexData.positions.Empty())
		{
			logger.Error("ImportMesh '{}': positions are empty, rejecting mesh", name);
			return {};
		}

		// Make local copies since processing may modify or rebuild arrays
		Array<Vec3> positions(vertexData.positions);
		Array<Vec3> normals;
		Array<Vec2> uvs;
		Array<Vec2> uv2s;
		Array<Vec3> colors;
		Array<Vec4> tangents;
		Array<BoneInfluence> bones;
		Array<u32> localIndices(indices);
		Array<MeshPrimitive> localPrimitives(primitives);

		if (!vertexData.normals.Empty())
		{
			normals.Assign(vertexData.normals.begin(), vertexData.normals.end());
		}

		if (!vertexData.uv.Empty())
		{
			uvs.Assign(vertexData.uv.begin(), vertexData.uv.end());
		}

		if (!vertexData.uv2.Empty())
		{
			uv2s.Assign(vertexData.uv2.begin(), vertexData.uv2.end());
		}

		if (!vertexData.colors.Empty())
		{
			colors.Assign(vertexData.colors.begin(), vertexData.colors.end());
		}

		if (!settings.recalculateTangents && !vertexData.tangents.Empty())
		{
			tangents.Assign(vertexData.tangents.begin(), vertexData.tangents.end());
		}

		if (!vertexData.bones.Empty())
		{
			bones.Assign(vertexData.bones.begin(), vertexData.bones.end());
		}

		MeshGeometryResult result = ProcessMeshGeometry(settings, positions, normals, uvs, uv2s, colors, tangents, bones,
		                                                localIndices, localPrimitives, scale, scope);

		RID meshResource = Resources::Create<MeshResource>(UUID::RandomUUID(), scope);

		ResourceObject meshObject = Resources::Write(meshResource);
		meshObject.SetString(MeshResource::Name, name);
		meshObject.SetReferenceArray(MeshResource::Materials, materials);

		if (skin)
		{
			meshObject.SetSubObject(MeshResource::Skin, skin);
		}

		WriteGeometryToMesh(meshObject, result);
		meshObject.Commit(scope);

		return meshResource;
	}

	void ReimportMesh(RID meshRID, const MeshImportSettings& settings, UndoRedoScope* scope)
	{
		ResourceObject meshObject = Resources::Read(meshRID);
		if (!meshObject) return;

		String meshName = String(meshObject.GetString(MeshResource::Name));

		// Read vertex layout to get attribute offsets and stride
		RID vertexLayoutRID = meshObject.GetSubObject(MeshResource::VertexLayout);
		if (!vertexLayoutRID) return;

		ResourceObject layoutObject = Resources::Read(vertexLayoutRID);
		if (!layoutObject) return;

		u32 stride = layoutObject.GetUInt(VertexLayoutResource::Stride);

		u32 positionOffset    = U32_MAX;
		u32 normalOffset      = U32_MAX;
		u32 uvOffset          = U32_MAX;
		u32 uv1Offset         = U32_MAX;
		u32 colorOffset       = U32_MAX;
		u32 tangentOffset     = U32_MAX;
		u32 boneIndicesOffset = U32_MAX;
		u32 boneWeightsOffset = U32_MAX;

		Span<RID> attrs = layoutObject.GetSubObjectList(VertexLayoutResource::Attributes);
		for (RID attrRID : attrs)
		{
			ResourceObject attrObj = Resources::Read(attrRID);
			if (!attrObj) continue;

			StringView attrName = attrObj.GetString(VertexAttributeResource::Name);
			u32 attrOffset = attrObj.GetUInt(VertexAttributeResource::Offset);

			if (attrName == "position")          positionOffset = attrOffset;
			else if (attrName == "normal")       normalOffset = attrOffset;
			else if (attrName == "uv0")          uvOffset = attrOffset;
			else if (attrName == "uv1")          uv1Offset = attrOffset;
			else if (attrName == "color")        colorOffset = attrOffset;
			else if (attrName == "tangent")      tangentOffset = attrOffset;
			else if (attrName == "boneIndices")  boneIndicesOffset = attrOffset;
			else if (attrName == "boneWeights")  boneWeightsOffset = attrOffset;
		}

		if (positionOffset == U32_MAX)
		{
			logger.Warn("ReimportMesh '{}': missing position attribute, skipping", meshName);
			return;
		}

		// Read LOD 0 data
		Span<RID> lods = meshObject.GetSubObjectList(MeshResource::MeshLODs);
		if (lods.Empty()) return;

		ResourceObject lodObject = Resources::Read(lods[0]);
		if (!lodObject) return;

		u32 vertexCount     = lodObject.GetUInt(MeshLodResource::VerticesCount);
		u32 indexCount      = lodObject.GetUInt(MeshLodResource::IndicesCount);
		u64 vertexBufOffset = lodObject.GetUInt(MeshLodResource::VerticesOffset);
		u64 indexBufOffset  = lodObject.GetUInt(MeshLodResource::IndicesOffset);
		u64 primBufOffset   = lodObject.GetUInt(MeshLodResource::PrimitiveOffset);
		u64 primitiveCount  = lodObject.GetUInt(MeshLodResource::PrimitiveCount);

		ResourceBuffer buffer = meshObject.GetBuffer(MeshResource::MeshData);
		if (!buffer) return;

		// Read raw vertex data
		u64 vertexBufferSize = vertexCount * static_cast<u64>(stride);
		ByteBuffer vertexData;
		vertexData.Resize(vertexBufferSize);
		buffer.CopyData(vertexData.Data(), vertexBufferSize, vertexBufOffset);

		// Read indices
		Array<u32> indices;
		indices.Resize(indexCount);
		buffer.CopyData(indices.Data(), indexCount * sizeof(u32), indexBufOffset);

		// Read primitives
		Array<MeshPrimitive> primitives;
		primitives.Resize(primitiveCount);
		buffer.CopyData(primitives.Data(), primitiveCount * sizeof(MeshPrimitive), primBufOffset);

		// Decompose interleaved vertex buffer into separate arrays
		Array<Vec3> positions(vertexCount);
		Array<Vec3> normals;
		Array<Vec2> uvs;
		Array<Vec2> uv2s;
		Array<Vec3> colors;
		Array<Vec4> tangents;
		Array<BoneInfluence> bones;

		if (normalOffset != U32_MAX) normals.Resize(vertexCount);
		if (uvOffset != U32_MAX) uvs.Resize(vertexCount);
		if (uv1Offset != U32_MAX) uv2s.Resize(vertexCount);
		if (colorOffset != U32_MAX) colors.Resize(vertexCount);
		if (tangentOffset != U32_MAX) tangents.Resize(vertexCount);
		bool hasBones = boneIndicesOffset != U32_MAX && boneWeightsOffset != U32_MAX;
		if (hasBones) bones.Resize(vertexCount);

		for (u32 i = 0; i < vertexCount; i++)
		{
			const u8* src = vertexData.Data() + i * stride;

			memcpy(&positions[i], src + positionOffset, sizeof(Vec3));

			if (normalOffset != U32_MAX)
				memcpy(&normals[i], src + normalOffset, sizeof(Vec3));

			if (uvOffset != U32_MAX)
				memcpy(&uvs[i], src + uvOffset, sizeof(Vec2));

			if (uv1Offset != U32_MAX)
				memcpy(&uv2s[i], src + uv1Offset, sizeof(Vec2));

			if (colorOffset != U32_MAX)
				memcpy(&colors[i], src + colorOffset, sizeof(Vec3));

			if (tangentOffset != U32_MAX)
				memcpy(&tangents[i], src + tangentOffset, sizeof(Vec4));

			if (hasBones)
			{
				memcpy(bones[i].indices, src + boneIndicesOffset, sizeof(u32) * 4);
				memcpy(&bones[i].weights, src + boneWeightsOffset, sizeof(Vec4));
			}
		}

		logger.Info("ReimportMesh '{}': {} vertices, {} indices", meshName, vertexCount, indexCount);

		MeshGeometryResult result = ProcessMeshGeometry(settings, positions, normals, uvs, uv2s, colors, tangents, bones,
		                                                indices, primitives, Vec3(1.0), scope);

		// Single Write+Commit — preserves existing Name, Materials, Skin
		ResourceObject writeObj = Resources::Write(meshRID);
		WriteGeometryToMesh(writeObj, result);
		writeObj.Commit(scope);

		u64 newVertexCount = positions.Size();
		logger.Info("ReimportMesh '{}' done: {} -> {} vertices", meshName, vertexCount, newVertexCount);
	}
}
