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
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/World/WorldCommon.hpp"
#include "Skore/World/Components/StaticMeshRender.hpp"

namespace Skore
{

	struct FBXImportCache
	{
		Array<RID> textures;
		HashMap<ufbx_material*, RID> materials;
		HashMap<ufbx_mesh*, RID> meshes;
	};


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

	RID ProcessTextures(const FBXImportSettings& settings, StringView basePath, ufbx_texture* texture, ufbx_scene* scene, UndoRedoScope* scope)
	{
		if (!texture || !texture->filename.data)
		{
			return {};
		}

		String textureName = texture->name.data ? texture->name.data : "Texture";
		String texturePath = Path::Join(basePath, texture->filename.data);

		if (texture->content.data != nullptr && texture->content.size > 0)
		{
			int a = 0;
		}

		// if (textureFile == nullptr)
		// {
		// 	if (texture->relative_filename.data)
		// 	{
		// 		texturePath = Path::Join(basePath, texture->relative_filename.data);
		// 		textureFile = AssetEditor::GetFileByAbsolutePath(texturePath);
		// 	}
		//
		// 	if (textureFile == nullptr)
		// 	{
		// 		logger.Warn("texture file not found {} ", texture->filename.data);
		// 		parentAsset->SetStatus(AssetStatus::Warning);
		// 		parentAsset->AddMissingFile(texture->filename.data);
		// 		return nullptr;
		// 	}
		// }

		return {};
	}


	RID ProcessMaterials(const FBXImportSettings& settings, FBXImportCache& cache, ufbx_material* material, ufbx_scene* scene, UndoRedoScope* scope)
	{
		RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), scope);

		Color baseColor = Color::FromVec3(ToVec3(material->pbr.base_color.value_vec3));

		ResourceObject materialObject = Resources::Write(materialResource);
		materialObject.SetString(MaterialResource::Name, !IsStrNullOrEmpty(material->name.data) ? material->name.data : "Material");
		materialObject.SetColor(MaterialResource::BaseColor, baseColor);
		materialObject.Commit(scope);

		return materialResource;
	}


	RID ProcessMesh(const FBXImportSettings& settings, FBXImportCache& cache, StringView name, ufbx_mesh* mesh, const ufbx_scene* scene, UndoRedoScope* scope)
	{
		// Get total vertex and index counts
		size_t totalVertices = mesh->num_vertices;
		size_t totalIndices = mesh->num_indices;

		Array<StaticMeshResource::Vertex>    vertices;
		Array<u32>                           indices;
		Array<StaticMeshResource::Primitive> primitives;
		Array<RID>                           meshMaterials;

		vertices.Reserve(totalVertices);
		indices.Reserve(totalIndices);
		primitives.Reserve(mesh->materials.count);

		meshMaterials.Reserve(mesh->materials.count);

		// Check for available vertex attributes
		bool hasNormals = mesh->vertex_normal.exists;
		bool hasTangents = mesh->vertex_tangent.exists;
		bool hasTexCoords = mesh->vertex_uv.exists;
		bool hasColors = mesh->vertex_color.exists;

		// Material mapping
		for (u32 i = 0; i < mesh->materials.count; i++)
		{
			if (ufbx_material* material = mesh->materials.data[i])
			{
				if (auto it = cache.materials.Find(material))
				{
					meshMaterials.EmplaceBack(it->second);
				}
			}
		}

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
			if (hasNormals && mesh->vertex_normal.values.count > i)
			{
				vertex.normal = ToVec3(mesh->vertex_normal.values[i]);
			}

			// UV coordinates
			if (hasTexCoords && mesh->vertex_uv.values.count > i)
			{
				vertex.texCoord.x = mesh->vertex_uv.values[i].x;
				vertex.texCoord.y = 1.0f - mesh->vertex_uv.values[i].y; // Flip Y for UV
			}

			// Vertex colors
			if (hasColors && mesh->vertex_color.values.count > i)
			{
				vertex.color = ToVec4(mesh->vertex_color.values[i]);
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
				primitive.materialIndex = materialIndex < meshMaterials.Size() ? materialIndex : 0;
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

		if (settings.generateNormals || !hasNormals)
		{
			MeshTools::CalcNormals(vertices, indices);
		}

		if (settings.recalculateTangents || !hasTangents)
		{
			MeshTools::CalcTangents(vertices, indices, true);
		}


		RID meshResource = Resources::Create<StaticMeshResource>(UUID::RandomUUID(), scope);

		ResourceObject meshObject = Resources::Write(meshResource);
		meshObject.SetString(StaticMeshResource::Name, !IsStrNullOrEmpty(mesh->name.data) ? mesh->name.data : name);
		meshObject.SetReferenceArray(StaticMeshResource::Materials, meshMaterials);
		meshObject.SetBlob(StaticMeshResource::Vertices, Span(reinterpret_cast<u8*>(vertices.Data()), vertices.Size() * sizeof(StaticMeshResource::Vertex)));
		meshObject.SetBlob(StaticMeshResource::Indices, Span(reinterpret_cast<u8*>(indices.Data()), indices.Size() * sizeof(u32)));
		meshObject.SetBlob(StaticMeshResource::Primitives, Span(reinterpret_cast<u8*>(primitives.Data()), primitives.Size() * sizeof(StaticMeshResource::Primitive)));
		meshObject.Commit(scope);

		return meshResource;
	}

	RID ProcessNode(const FBXImportSettings& settings, const ufbx_node* node, FBXImportCache& cache, const ufbx_scene* fbxScene, UndoRedoScope* scope)
	{
		if (!node)
		{
			return {};
		}

		//ignore camera and lights for now.
		if (node->camera || node->light)
		{
			return {};
		}

		RID entity = Resources::Create<EntityResource>(UUID::RandomUUID());
		ResourceObject entityObject = Resources::Write(entity);

		StringView nodeName = !IsStrNullOrEmpty(node->name.data) ? node->name.data : "Node";
		entityObject.SetString(EntityResource::Name, nodeName);


		// Extract transform
		Transform transform;
		transform.position = ToVec3(node->local_transform.translation);
		transform.rotation = Quat{node->local_transform.rotation.x, node->local_transform.rotation.y, node->local_transform.rotation.z, node->local_transform.rotation.w};
		transform.scale = ToVec3(node->local_transform.scale);

		RID transformRID = Resources::Create<Transform>(UUID::RandomUUID());
		Resources::ToResource(transformRID, &transform, scope);
		entityObject.SetSubObject(EntityResource::Transform, transformRID);

		// Handle mesh
		if (node->mesh)
		{

			auto meshIt = cache.meshes.Find(node->mesh);
			if (meshIt == cache.meshes.end())
			{
				if (RID meshRID = ProcessMesh(settings, cache, nodeName, node->mesh, fbxScene, scope))
				{
					meshIt = cache.meshes.Insert(node->mesh, meshRID).first;
				}
			}

			if (meshIt)
			{
				RID staticMeshRender = Resources::Create<StaticMeshRender>(UUID::RandomUUID());

				ResourceObject staticMeshRenderObject = Resources::Write(staticMeshRender);
				staticMeshRenderObject.SetReference(staticMeshRenderObject.GetIndex("mesh"), meshIt->second);
				staticMeshRenderObject.Commit(scope);

				entityObject.AddToSubObjectSet(EntityResource::Components, staticMeshRender);
			}

		}

		// Process children
		for (u32 i = 0; i < node->children.count; i++)
		{
			if (RID child = ProcessNode(settings, node->children.data[i], cache, fbxScene, scope))
			{
				entityObject.AddToSubObjectSet(EntityResource::Children, child);
			}
		}

		entityObject.Commit(scope);

		return entity;
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

			FBXImportCache cache;

			cache.textures.Reserve(scene->textures.count);

			String basePath = Path::Parent(path);
			String name = Path::Name(path);

			for (u32 i = 0; i < scene->textures.count; i++)
			{
				RID texture = ProcessTextures(settings, basePath, scene->textures.data[i], scene, scope);
				cache.textures.EmplaceBack(texture);
				if (texture)
				{
					dccAssetObject.AddToSubObjectSet(DCCAssetResource::Textures, texture);
				}
			}

			// Process Materials
			for (u32 i = 0; i < scene->materials.count; i++)
			{
				if (RID materialRID = ProcessMaterials(settings, cache, scene->materials.data[i], scene, scope))
				{
					cache.materials.Insert(scene->materials.data[i], materialRID);
					dccAssetObject.AddToSubObjectSet(DCCAssetResource::Materials, materialRID);
				}
			}

			// Process nodes
			for (u32 i = 0; i < scene->nodes.count; i++)
			{
				const ufbx_node* node = scene->nodes.data[i];
				if (node->parent == nullptr)
				{
					if (RID root = ProcessNode(settings, node, cache, scene, scope))
					{
						dccAssetObject.SetSubObject(DCCAssetResource::Entity, root);
					}
				}
			}

			//import missing meshes.
			for (u32 i = 0; i < scene->meshes.count; i++)
			{
				ufbx_mesh* mesh = scene->meshes.data[i];
				if (!cache.meshes.Has(mesh))
				{
					if (RID meshRID = ProcessMesh(settings, cache, String("Mesh").Append(i), mesh, scene, scope))
					{
						cache.meshes.Insert(mesh, meshRID);
					}
				}
			}

			for (const auto& it: cache.meshes)
			{
				dccAssetObject.AddToSubObjectSet(DCCAssetResource::Meshes, it.second);
			}

			ufbx_free_scene(scene);
		}

		dccAssetObject.Commit(scope);

		return true;
	}

	void RegisterFBXImporter()
	{
		Reflection::Type<FBXImporter>();
	}
}
