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
		bool  generateLightmapUVs = false;
		float lightMapTexelSize = 1.0f;

		bool  optimizeMesh = true;
		bool  generateLODs = true;
	};

	struct MeshImportOptions
	{
		f32 scaleFactor = 1.0f;
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

	SK_API RID ImportMesh(RID directory, const MeshImportSettings& settings, const MeshImportOptions& options, StringView name, Span<RID> materials, Span<MeshPrimitive> primitives,
		const MeshVertexImportData& vertexData, Span<u32> indices, RID skin, Vec3 scale, UndoRedoScope* scope);

	SK_API void ReimportMesh(RID meshRID, const MeshImportSettings& settings, UndoRedoScope* scope);
}
