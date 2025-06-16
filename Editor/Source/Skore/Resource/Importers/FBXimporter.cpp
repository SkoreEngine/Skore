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

#include "FBXimporter.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

#include <ufbx.h>
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/FileSystem.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::FBXImporter");

	Vec2 ToVec2(const ufbx_vec2& vec)
	{
		return Vec2{vec.x, vec.y};
	}

	Vec3 ToVec3(const ufbx_vec3& vec)
	{
		return Vec3{vec.x, vec.y, vec.z};
	}

	Vec4 ToVec4(const ufbx_vec4& vec)
	{
		return Vec4{vec.x, vec.y, vec.z, vec.w};
	}

	struct FBXImporter : ResourceAssetImporter
	{
		SK_CLASS(FBXImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".fbx"};
		}

		bool ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) override
		{
			FBXImportSettings fbxImportSettings;
			return ImportFBX(directory, fbxImportSettings, path, scope);
		}
	};


	RID ProcessMesh(ufbx_mesh* mesh, ufbx_scene* scene, UndoRedoScope* scope)
	{
		// Material mapping
		for (u32 i = 0; i < mesh->materials.count; i++)
		{
			const ufbx_material* material = mesh->materials.data[i];
			if (material)
			{
				for (u32 j = 0; j < scene->materials.count; j++)
				{
					// if (scene->materials.data[j] == material && j < materials.Size() && materials[j] != nullptr)
					// {
					// 	materialAssets.EmplaceBack(materials[j]);
					// 	break;
					// }
				}
			}
		}

		// Get total vertex and index counts
		size_t totalVertices = mesh->num_vertices;
		size_t totalIndices = mesh->num_indices;

		Array<StaticMeshResource::Vertex>    vertices;
		Array<u32>                     indices;
		Array<StaticMeshResource::Primitive> primitives;

		//Array<MaterialAsset*>       materialAssets;

		vertices.Reserve(totalVertices);
		indices.Reserve(totalIndices);
		primitives.Reserve(mesh->materials.count);

		//materialAssets.Reserve(mesh->materials.count);

		// Check for available vertex attributes
		bool hasNormals = mesh->vertex_normal.exists;
		bool hasTangents = mesh->vertex_tangent.exists;
		bool hasTexCoords = mesh->vertex_uv.exists;
		bool hasColors = mesh->vertex_color.exists;

		// Extract vertices
		for (u32 i = 0; i < mesh->num_vertices; i++)
		{
			StaticMeshResource::Vertex vertex;

			// Default values
			vertex.position = Vec3{0, 0, 0};
			vertex.normal = Vec3{0, 1, 0};
			vertex.texCoord = Vec2{0, 0};
			vertex.color = Vec3{1, 1, 1};
			vertex.tangent = Vec4{1, 0, 0, 1};

			// Position
			vertex.position = ToVec3(mesh->vertex_position.values[i]);

			// Normal
			if (hasNormals)
			{
				vertex.normal.x = mesh->vertex_normal.values[i].x;
				vertex.normal.y = mesh->vertex_normal.values[i].y;
				vertex.normal.z = mesh->vertex_normal.values[i].z;
			}

			// UV coordinates
			if (hasTexCoords)
			{
				vertex.texCoord.x = mesh->vertex_uv.values[i].x;
				vertex.texCoord.y = 1.0f - mesh->vertex_uv.values[i].y; // Flip Y for UV
			}

			// Vertex colors
			if (hasColors)
			{
				vertex.color.x = mesh->vertex_color.values[i].x;
				vertex.color.y = mesh->vertex_color.values[i].y;
				vertex.color.z = mesh->vertex_color.values[i].z;
			}

			// Tangents
			if (hasTangents)
			{
				vertex.tangent.x = mesh->vertex_tangent.values[i].x;
				vertex.tangent.y = mesh->vertex_tangent.values[i].y;
				vertex.tangent.z = mesh->vertex_tangent.values[i].z;

				// Use bitangent to compute handedness (w component)
				if (mesh->vertex_bitangent.exists)
				{
					ufbx_vec3 normal = mesh->vertex_normal.values[i];
					ufbx_vec3 tangent = mesh->vertex_tangent.values[i];
					ufbx_vec3 bitangent = mesh->vertex_bitangent.values[i];

					// Cross product to determine handedness
					ufbx_vec3 crossProduct;
					crossProduct.x = normal.y * tangent.z - normal.z * tangent.y;
					crossProduct.y = normal.z * tangent.x - normal.x * tangent.z;
					crossProduct.z = normal.x * tangent.y - normal.y * tangent.x;

					// Dot product with bitangent
					float dot = crossProduct.x * bitangent.x + crossProduct.y * bitangent.y + crossProduct.z * bitangent.z;
					vertex.tangent.w = dot > 0.0f ? 1.0f : -1.0f;
				}
				else
				{
					vertex.tangent.w = 1.0f;
				}
			}

			vertices.EmplaceBack(vertex);
		}

		// Extract triangles
		u32 materialIndex = 0;
		u32 firstIndex = 0;
		size_t materialStartFace = 0;
		size_t materialFaceCount = 0;


		for (u32 i = 0; i < mesh->face_material.count; i++)
		{
			ufbx_face face = mesh->faces.data[i];
			u32 currentMaterialIndex = mesh->face_material.data[i];

			if (i == 0)
			{
				materialIndex = currentMaterialIndex;
				materialStartFace = 0;
			}

			if (currentMaterialIndex != materialIndex || i == mesh->face_material.count - 1)
			{
				if (i == mesh->face_material.count - 1)
				{
					materialFaceCount++;
				}

				StaticMeshResource::Primitive primitive;
				primitive.firstIndex = firstIndex;
				primitive.indexCount = (u32)(materialFaceCount * 3); // Triangulated faces
				//primitive.materialIndex = materialIndex < materialAssets.Size() ? materialIndex : 0;
				primitive.materialIndex = 0;
				primitives.EmplaceBack(primitive);

				// Start a new primitive with this material
				firstIndex += primitive.indexCount;
				materialIndex = currentMaterialIndex;
				materialStartFace = i;
				materialFaceCount = 0;
			}

			materialFaceCount++;
		}

		// If no primitives were created, create a single one for all indices
		if (primitives.Empty() && !vertices.Empty())
		{
			StaticMeshResource::Primitive primitive;
			primitive.firstIndex = 0;
			primitive.indexCount = (u32)indices.Size();
			primitive.materialIndex = 0;
			primitives.EmplaceBack(primitive);
		}

		// Now process and add the indices
		for (u32 i = 0; i < mesh->num_indices; i++)
		{
			indices.EmplaceBack(mesh->vertex_indices[i]);
		}

		RID meshResource = Resources::Create<StaticMeshResource>(UUID::RandomUUID(), scope);

		ResourceObject meshObject = Resources::Write(meshResource);


		meshObject.SetString(StaticMeshResource::Name, !IsStrNullOrEmpty(mesh->name.data) ? mesh->name.data : String("Mesh_").Append(ToString(mesh->element_id)));
		meshObject.SetBlob(StaticMeshResource::Vertices, Span(reinterpret_cast<u8*>(vertices.Data()), vertices.Size() * sizeof(StaticMeshResource::Vertex)));
		meshObject.SetBlob(StaticMeshResource::Primitives, Span(reinterpret_cast<u8*>(primitives.Data()), primitives.Size() * sizeof(StaticMeshResource::Primitives)));
		meshObject.SetBlob(StaticMeshResource::Indices, Span(reinterpret_cast<u8*>(indices.Data()), indices.Size() * sizeof(u32)));
		meshObject.Commit(scope);

		return meshResource;
	}

	bool ImportFBX(RID directory, const FBXImportSettings& settings, StringView path, UndoRedoScope* scope)
	{
		RID dccAsset = ResourceAssets::CreateImportedAsset(directory, TypeInfo<DCCAssetResource>::ID(), Path::Name(path), scope, path);

		ResourceObject dccAssetObject = Resources::Write(dccAsset);
		dccAssetObject.SetString(DCCAssetResource::Name, Path::Name(path));

		{
			ufbx_load_opts opts = {};

			// Load FBX file
			ufbx_error error;
			ufbx_scene* scene = ufbx_load_file(path.Data(), &opts, &error);

			if (!scene)
			{
				logger.Error("Error on import file {}: {}", path, error.description.data);
				return false;
			}

			for (u32 i = 0; i < scene->meshes.count; i++)
			{
				if (RID mesh =  ProcessMesh(scene->meshes.data[i], scene, scope))
				{
					dccAssetObject.AddToSubObjectSet(DCCAssetResource::Meshes, mesh);
				}
			}

		}

		dccAssetObject.Commit(scope);

		return true;
	}

	void RegisterFBXImporter()
	{
		Reflection::Type<FBXImporter>();
	}
}
