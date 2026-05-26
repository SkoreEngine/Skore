#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"


namespace Skore
{
	struct UndoRedoScope;

	struct MeshImportSettings
	{
		bool  regenerateNormals = false;
		bool  recalculateTangents = true;
		bool  generateUV1s = false;
		float lightMapTexelSize = 1.0f;

		// meshoptimizer
		bool  optimizeMesh = true;          // vertex cache / overdraw / fetch
		bool  generateLODs = true;          // produce additional simplified LODs (skipped for skinned meshes)
		u32   lodCount = 5;                 // total LOD count including LOD 0
		float lodReduction = 0.5f;          // each LOD targets prev * lodReduction triangles
		float lodTargetError = 0.08f;       // meshopt_simplify error threshold (relative)
		float lodSwitchDistance = 8.0f;     // dist/radius factor at which LOD 1 activates; doubles per LOD step
		float overdrawThreshold = 1.05f;    // overdraw vs vertex-cache trade-off
	};

	struct BoneInfluence
	{
		u32  indices[4] = {};
		Vec4 weights = {};
	};

	struct MeshVertexImportData
	{
		Span<Vec3> positions;          // Required
		Span<Vec3> normals;            // Optional
		Span<Vec2> uv;                 // Optional
		Span<Vec2> uv2;                // Optional
		Span<Vec3> colors;             // Optional
		Span<Vec4> tangents;           // Optional
		Span<BoneInfluence> bones;     // Optional, empty for static meshes
	};

	SK_API RID ImportMesh(RID directory, const MeshImportSettings& settings, StringView name, Span<RID> materials, Span<MeshPrimitive> primitives,
		const MeshVertexImportData& vertexData, Span<u32> indices, RID skin, Vec3 scale, UndoRedoScope* scope);

	SK_API void ReimportMesh(RID meshRID, const MeshImportSettings& settings, UndoRedoScope* scope);
}
