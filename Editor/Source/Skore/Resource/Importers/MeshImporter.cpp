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

#include "MeshImporter.hpp"

#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/Resource/Resources.hpp"


namespace Skore
{
	namespace
	{
		template<typename T>
		RID ImportMesh(RID directory, const MeshImportSettings& settings, StringView name, Span<RID> materials, Span<MeshPrimitive> primitives, Span<T> vertices, Span<u32> indices,
		               UndoRedoScope* scope)
		{

			bool skinned = std::is_same_v<T, MeshSkeletalVertex>;

			if (settings.generateNormals)
			{
				//MeshTools::CalcNormals(allVertices, allIndices);
			}

			if (settings.recalculateTangents)
			{
				MeshTools::CalcTangents(vertices, indices);
			}

			//TODO - AABB, Center

			RID meshResource = Resources::Create<MeshResource>(UUID::RandomUUID(), scope);

			ResourceObject meshObject = Resources::Write(meshResource);
			meshObject.SetString(MeshResource::Name, name);
			meshObject.SetReferenceArray(MeshResource::Materials, materials);
			meshObject.SetBool(MeshResource::Skinned, skinned);
			meshObject.SetBlob(MeshResource::Vertices, Span((u8*)vertices.Data(), vertices.Size() * sizeof(T)));
			meshObject.SetBlob(MeshResource::Indices, Span((u8*)indices.Data(), indices.Size() * sizeof(u32)));
			meshObject.SetBlob(MeshResource::Primitives, Span((u8*)primitives.Data(), primitives.Size() * sizeof(MeshPrimitive)));
			meshObject.Commit(scope);

			return meshResource;
		}
	}

	RID ImportMesh(RID directory, const MeshImportSettings& settings, StringView name, Span<RID> materials, Span<MeshPrimitive> primitives, Span<MeshStaticVertex> vertices, Span<u32> indices,
	               UndoRedoScope* scope)
	{
		return ImportMesh<MeshStaticVertex>(directory, settings, name, materials, primitives, vertices, indices, scope);
	}

	RID ImportMesh(RID directory, const MeshImportSettings& settings, StringView name, Span<RID> materials, Span<MeshPrimitive> primitives, Span<MeshSkeletalVertex> vertices,
	               Span<u32> indices, UndoRedoScope* scope)
	{
		return ImportMesh<MeshSkeletalVertex>(directory, settings, name, materials, primitives, vertices, indices, scope);
	}
}
