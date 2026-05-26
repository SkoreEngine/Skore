#include "Skore/Resource/Importers/MeshImporter.hpp"

#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"

#include "meshoptimizer.h"

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
			Array<RID>     meshLODs;
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

			// Compute mesh AABB (positions are still untouched here)
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

			// === meshoptimizer: cache + overdraw + vertex fetch on LOD 0 ===
			// Position lives at offset 0 in every layout, so we can read it back from the
			// interleaved buffer as `(const float*)vertexBuffer.Data()` with `stride`.
			if (settings.optimizeMesh)
			{
				for (const MeshPrimitive& p : primitives)
				{
					u32* idx = indices.Data() + p.firstIndex;
					meshopt_optimizeVertexCache(idx, idx, p.indexCount, vertexCount);
					meshopt_optimizeOverdraw(idx, idx, p.indexCount,
					                         reinterpret_cast<const float*>(vertexBuffer.Data()),
					                         vertexCount, stride, settings.overdrawThreshold);
				}

				ByteBuffer reordered;
				reordered.Resize(vertexBuffer.Size());
				size_t newVertexCount = meshopt_optimizeVertexFetch(
					reordered.Data(),
					indices.Data(), indices.Size(),
					vertexBuffer.Data(), vertexCount, stride);

				vertexBuffer = std::move(reordered);
				vertexCount = newVertexCount;
				vertexBuffer.Resize(vertexCount * stride);
			}

			// Helper to read a vertex position straight out of the interleaved buffer.
			auto readPos = [&](u32 vIdx) -> Vec3
			{
				Vec3 p;
				memcpy(&p, vertexBuffer.Data() + static_cast<u64>(vIdx) * stride, sizeof(Vec3));
				return p;
			};

			auto computePrimAABBs = [&](Array<u32>& idx, Array<MeshPrimitive>& prims)
			{
				for (MeshPrimitive& p : prims)
				{
					if (p.indexCount == 0)
					{
						// Leave a valid (degenerate) AABB so culling doesn't reject the instance
						// due to default {F32_MAX, F32_LOW} bounds.
						p.aabb = AABB(Vec3(0, 0, 0), Vec3(0, 0, 0));
						continue;
					}
					Vec3 mn = readPos(idx[p.firstIndex]);
					Vec3 mx = mn;
					for (u32 i = p.firstIndex + 1; i < p.firstIndex + p.indexCount; i++)
					{
						Vec3 v = readPos(idx[i]);
						mn = Vec3::Min(mn, v);
						mx = Vec3::Max(mx, v);
					}
					p.aabb = AABB(mn, mx);
				}
			};

			computePrimAABBs(indices, primitives);

			// === Build LODs ===
			// Reserve up front so subsequent EmplaceBacks don't reallocate the outer
			// storage and invalidate references into lodIndices[0] / lodPrims[0].
			u32 maxLods = (settings.generateLODs && !hasBones) ? Math::Max(settings.lodCount, 1u) : 1u;

			Array<Array<u32>>           lodIndices;
			Array<Array<MeshPrimitive>> lodPrims;
			lodIndices.Reserve(maxLods);
			lodPrims.Reserve(maxLods);
			lodIndices.EmplaceBack(std::move(indices));
			lodPrims.EmplaceBack(std::move(primitives));

			if (settings.generateLODs && !hasBones && settings.lodCount > 1)
			{
				for (u32 lod = 1; lod < settings.lodCount; lod++)
				{
					const Array<u32>&           srcIdx   = lodIndices[0];
					const Array<MeshPrimitive>& srcPrims = lodPrims[0];

					float reduction = std::pow(settings.lodReduction, static_cast<float>(lod));

					Array<u32>           newIdx;
					Array<MeshPrimitive> newPrims;
					newIdx.Reserve(static_cast<u64>(srcIdx.Size() * reduction) + srcPrims.Size() * 3);

					for (const MeshPrimitive& p : srcPrims)
					{
						if (p.indexCount < 6)
						{
							// too small to simplify — carry the primitive as-is
							MeshPrimitive np = p;
							np.firstIndex = newIdx.Size();
							for (u32 i = 0; i < p.indexCount; i++) newIdx.EmplaceBack(srcIdx[p.firstIndex + i]);
							newPrims.EmplaceBack(np);
							continue;
						}

						size_t target = static_cast<size_t>(p.indexCount * reduction);
						target -= target % 3;
						// Allow simplification down to a single triangle so the most
						// aggressive LODs (e.g. lod 7 at 0.5^7 ≈ 0.78%) can actually shrink
						// small primitives. The got<3 fallback below guarantees at least
						// one triangle is emitted.
						if (target < 3) target = 3;

						Array<u32> tmp;
						tmp.Resize(p.indexCount);

						// Pass normal (3 floats) + uv0 (2 floats) as attribute metric so the
						// simplifier penalises collapses across UV seams and hard normal edges.
						// These attributes are stored contiguously in the interleaved buffer
						// starting at offset sizeof(Vec3) (position is always first).
						const float attribWeights[5] = {0.5f, 0.5f, 0.5f, 1.0f, 1.0f};

						// meshopt_SimplifyLockBorder: never move topological-border vertices.
						// Essential for scenes like Sponza where one primitive contains many
						// disconnected components — without this, edge vertices of each piece
						// collapse inward and the mesh tears apart. Godot uses the same flag.
						const unsigned int simplifyOptions = meshopt_SimplifyLockBorder;

						// Scale error budget per LOD (Godot-style). LOD 1 uses half the
						// configured base error; each subsequent LOD doubles. This lets later
						// LODs actually reach their reduction targets — a fixed budget makes
						// the simplifier give up early on aggressive LODs.
						float effectiveError = settings.lodTargetError * std::pow(2.0f, static_cast<float>(lod) - 1.0f);

						float  resultError = 0.0f;
						size_t got = meshopt_simplifyWithAttributes(
							tmp.Data(),
							srcIdx.Data() + p.firstIndex, p.indexCount,
							reinterpret_cast<const float*>(vertexBuffer.Data()),
							vertexCount, stride,
							reinterpret_cast<const float*>(vertexBuffer.Data() + sizeof(Vec3)),
							stride,
							attribWeights, 5,
							nullptr,
							target, effectiveError, simplifyOptions, &resultError);

						// Intentionally no meshopt_simplifySloppy fallback: it doesn't respect
						// attributes or borders and will mangle disconnected geometry. If the
						// quality simplifier can't reach the target index count, we just keep
						// fewer simplifications — correctness over reduction ratio.

						// Always emit a primitive per LOD so runtime can index by primitive.
						// Fall back to the first triangle from the source if simplification collapsed.
						if (got < 3)
						{
							got = 3;
							tmp[0] = srcIdx[p.firstIndex + 0];
							tmp[1] = srcIdx[p.firstIndex + 1];
							tmp[2] = srcIdx[p.firstIndex + 2];
						}

						MeshPrimitive np = p;
						np.firstIndex = newIdx.Size();
						np.indexCount = static_cast<u32>(got);

						for (size_t i = 0; i < got; i++) newIdx.EmplaceBack(tmp[i]);

						meshopt_optimizeVertexCache(
							newIdx.Data() + np.firstIndex,
							newIdx.Data() + np.firstIndex,
							got, vertexCount);

						newPrims.EmplaceBack(np);
					}

					if (newPrims.Empty()) break;

					computePrimAABBs(newIdx, newPrims);

					// Stop once a level barely shrinks the previous one.
					u64 prevTotal = 0;
					for (const auto& p : lodPrims.Back()) prevTotal += p.indexCount;
					if (newIdx.Size() >= prevTotal * 95 / 100) break;

					lodIndices.EmplaceBack(std::move(newIdx));
					lodPrims.EmplaceBack(std::move(newPrims));
				}
			}

			// === Write buffer: [vertices][lod0 idx][lod0 prims][lod1 idx][lod1 prims]... ===
			u64 vertexBufferSize = static_cast<u64>(vertexCount) * stride;

			Array<u64> idxOffsets;
			Array<u64> primOffsets;
			idxOffsets.Reserve(lodIndices.Size());
			primOffsets.Reserve(lodIndices.Size());

			u64 cursor = vertexBufferSize;
			for (u64 i = 0; i < lodIndices.Size(); i++)
			{
				idxOffsets.EmplaceBack(cursor);
				cursor += lodIndices[i].Size() * sizeof(u32);
				primOffsets.EmplaceBack(cursor);
				cursor += lodPrims[i].Size() * sizeof(MeshPrimitive);
			}

			ResourceBuffer resourceBuffer = ResourceAssets::CreateTempBuffer();
			FileHandler    bufferFile = resourceBuffer.OpenFile(AccessMode::WriteOnly);

			FileSystem::WriteFile(bufferFile, vertexBuffer.Data(), vertexBufferSize);
			for (u64 i = 0; i < lodIndices.Size(); i++)
			{
				FileSystem::WriteFile(bufferFile, lodIndices[i].Data(), lodIndices[i].Size() * sizeof(u32));
				FileSystem::WriteFile(bufferFile, lodPrims[i].Data(), lodPrims[i].Size() * sizeof(MeshPrimitive));
			}
			FileSystem::CloseFile(bufferFile);

			Array<RID> meshLODs;
			meshLODs.Reserve(lodIndices.Size());
			for (u64 i = 0; i < lodIndices.Size(); i++)
			{
				RID            lodRID = Resources::Create<MeshLodResource>(UUID::RandomUUID(), scope);
				ResourceObject lodObj = Resources::Write(lodRID);
				lodObj.SetUInt(MeshLodResource::LodNumber, static_cast<u32>(i));
				lodObj.SetUInt(MeshLodResource::VerticesOffset, 0);
				lodObj.SetUInt(MeshLodResource::VerticesCount, vertexCount);
				lodObj.SetUInt(MeshLodResource::IndicesOffset, idxOffsets[i]);
				lodObj.SetUInt(MeshLodResource::IndicesCount, lodIndices[i].Size());
				lodObj.SetUInt(MeshLodResource::PrimitiveOffset, primOffsets[i]);
				lodObj.SetUInt(MeshLodResource::PrimitiveCount, lodPrims[i].Size());
				// Distance-factor threshold: switch to LOD k when (camera-distance /
				// instance-radius) >= lodSwitchDistance * 2^(k-1). With the default
				// lodSwitchDistance=8, LOD 1 activates at 8×radius and each subsequent
				// LOD doubles (16, 32, 64, …). LOD 0 is the fallback — its stored
				// value is irrelevant since the cull never compares against it. The
				// field is still called "ScreenSize" in the resource for compatibility,
				// but the cull shader treats it as a distance/radius ratio.
				float threshold = settings.lodSwitchDistance * std::pow(2.0f, static_cast<float>(i) - 1.0f);
				lodObj.SetFloat(MeshLodResource::ScreenSize, threshold);
				lodObj.Commit(scope);
				meshLODs.EmplaceBack(lodRID);
			}

			return MeshGeometryResult{
				.aabbMin = minBounds,
				.aabbMax = maxBounds,
				.lightmapSizeHint = lightmapSizeHint,
				.vertexLayout = vertexLayoutRID,
				.meshLODs = std::move(meshLODs),
				.buffer = resourceBuffer,
			};
		}

		void WriteGeometryToMesh(ResourceObject& meshObject, const MeshGeometryResult& result)
		{
			meshObject.SetVec3(MeshResource::AABBMin, result.aabbMin);
			meshObject.SetVec3(MeshResource::AABBMax, result.aabbMax);
			meshObject.SetVec2(MeshResource::LightmapSizeHint, result.lightmapSizeHint);
			meshObject.SetSubObject(MeshResource::VertexLayout, result.vertexLayout);

			Array<RID> oldLods(meshObject.GetSubObjectList(MeshResource::MeshLODs));
			for (RID oldLod : oldLods)
			{
				meshObject.RemoveFromSubObjectList(MeshResource::MeshLODs, oldLod);
			}
			meshObject.AddToSubObjectList(MeshResource::MeshLODs, result.meshLODs);

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
