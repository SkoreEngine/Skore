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

#include "TextureImporter.hpp"
#include "Skore/Core/ByteBuffer.hpp"
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
		HashMap<ufbx_texture*, RID>  textures;
		HashMap<ufbx_material*, RID> materials;
		HashMap<ufbx_mesh*, RID>     meshes;
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

	RID ProcessTextures(RID directory, FBXImportCache& cache, const FBXImportSettings& settings, StringView basePath, ufbx_texture* texture, ufbx_scene* scene, UndoRedoScope* scope)
	{

		RID textureRID = {};

		if (!texture)
		{
			return {};
		}

		TextureImportSettings textureImportSettings;
		textureImportSettings.filterMode = FilterMode::Linear;
		textureImportSettings.wrapMode = texture->wrap_u == UFBX_WRAP_CLAMP ? AddressMode::ClampToBorder : AddressMode::Repeat;
		textureImportSettings.createAssetFile = true;

		if (texture->content.data != nullptr && texture->content.size > 0)
		{
			textureImportSettings.createAssetFile = false;

			Span   data = Span((u8*)texture->content.data, texture->content.size);
			String name = Path::Name(StringView{texture->relative_filename.data, texture->relative_filename.length});
			textureRID = ImportTextureFromMemory(directory, textureImportSettings, name, data, scope);
		}
		else
		{
			String absolutePath;
			if (texture->filename.length > 0)
			{
				//try by texture->filename
				absolutePath = String{texture->filename.data, texture->filename.length};
			}

			if (!FileSystem::GetFileStatus(absolutePath).exists)
			{
				//it didn't work, try joining
				absolutePath = Path::Join(basePath, StringView{texture->relative_filename.data, texture->relative_filename.length});
			}

			//if found, import the texture
			//TODO: check if texture already exists in the path?
			if (FileSystem::GetFileStatus(absolutePath).exists)
			{
				textureRID =  ImportTexture(directory, textureImportSettings, absolutePath, scope);
			}
		}

		if (textureRID)
		{
			cache.textures.Insert(texture, textureRID);

			if (!textureImportSettings.createAssetFile)
			{
				return textureRID;
			}
		}

		return {};
	}


	RID ProcessMaterials(const FBXImportSettings& settings, FBXImportCache& cache, ufbx_material* material, UndoRedoScope* scope)
	{
		RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), scope);

		ResourceObject materialObject = Resources::Write(materialResource);
		materialObject.SetString(MaterialResource::Name, !IsStrNullOrEmpty(material->name.data) ? material->name.data : "Material");
		materialObject.SetColor(MaterialResource::BaseColor, Color::FromVec3(ToVec3(material->pbr.base_color.value_vec3)));

		if (auto it = cache.textures.Find(material->pbr.base_color.texture))
		{
			materialObject.SetReference(MaterialResource::BaseColorTexture, it->second);
		}

		if (auto it = cache.textures.Find(material->pbr.base_color.texture))
		{
			materialObject.SetReference(MaterialResource::BaseColorTexture, it->second);
		}

		if (auto it = cache.textures.Find(material->pbr.normal_map.texture))
		{
			materialObject.SetReference(MaterialResource::NormalTexture, it->second);
		}

		if (auto it = cache.textures.Find(material->pbr.metalness.texture))
		{
			materialObject.SetReference(MaterialResource::MetallicTexture, it->second);
		}

		if (auto it = cache.textures.Find(material->pbr.roughness.texture))
		{
			materialObject.SetReference(MaterialResource::RoughnessTexture, it->second);
		}

		if (auto it = cache.textures.Find(material->pbr.ambient_occlusion.texture))
		{
			materialObject.SetReference(MaterialResource::OcclusionTexture, it->second);
		}

		if (auto it = cache.textures.Find(material->pbr.emission_color.texture))
		{
			materialObject.SetReference(MaterialResource::EmissiveTexture, it->second);
		}

		materialObject.Commit(scope);

		return materialResource;
	}


	RID ProcessMesh(const FBXImportSettings& settings, FBXImportCache& cache, StringView name, ufbx_mesh* mesh, UndoRedoScope* scope)
	{

		if (!mesh) return {};

		Allocator* heapAllocator = MemoryGlobals::GetHeapAllocator();

		u64 partCount = 0;
		u64 maxTriangles = 0;
		u64 totalTriangles = 0;

		for (usize pi = 0; pi < mesh->material_parts.count; pi++)
		{
			ufbx_mesh_part* part = &mesh->material_parts.data[pi];
			if (part->num_triangles == 0)
			{
				continue;
			}
			partCount++;
			maxTriangles = Math::Max(maxTriangles, part->num_triangles);
			totalTriangles += part->num_triangles;
		}

		Array<RID> meshMaterials;

		//temp buffers
		size_t     numTriIndices = mesh->max_face_triangles * 3;
		Array<u32> triIndices = {heapAllocator, numTriIndices};

		Array<StaticMeshResource::Vertex> tempVertices = {heapAllocator, maxTriangles * 3};
		Array<u32> tempIndices = {heapAllocator, maxTriangles * 3};

		Array<StaticMeshResource::Vertex> allVertices = {heapAllocator, totalTriangles * 3};
		Array<u32> allIndices = {heapAllocator, totalTriangles * 3};

		Array<StaticMeshResource::Primitive> primitives = {heapAllocator, partCount};

		u64 totalIndicesCount = 0;
		u64 totalVerticesCount = 0;

		u32 partIndex = 0;

		for (usize pi = 0; pi < mesh->material_parts.count; pi++)
		{
			ufbx_mesh_part *mesh_part = &mesh->material_parts.data[pi];
			if (mesh_part->num_triangles == 0)
			{
				continue;
			}

			u64 numIndices = 0;

			for (usize fi = 0; fi < mesh_part->num_faces; fi++)
			{
				ufbx_face face = mesh->faces.data[mesh_part->face_indices.data[fi]];
				usize numTris = ufbx_triangulate_face(triIndices.Data(), numTriIndices, mesh, face);

				ufbx_vec2 defaultUV = {0, 0};
				ufbx_vec4 defaultColor = {1, 1, 1, 1};

				// Iterate through every vertex of every triangle in the triangulated result
				for (size_t vi = 0; vi < numTris * 3; vi++)
				{
					u32 ix = triIndices[vi];

					StaticMeshResource::Vertex *vert = &tempVertices[numIndices];

					ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
					ufbx_vec3 normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
					ufbx_vec4 color =  mesh->vertex_color.exists ? ufbx_get_vertex_vec4(&mesh->vertex_color, ix) : defaultColor;
					ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : defaultUV;

					vert->position = ToVec3(pos);
					vert->normal = Math::Normalize(ToVec3(normal));
					vert->texCoord = ToVec2(uv);
					vert->color = Vec3(ToVec4(color));

					//vert->f_vertex_index = (float)mesh->vertex_indices.data[ix];

					// The skinning vertex stream is pre-calculated above so we just need to
					// copy the right one by the vertex index.
					// if (skin) {
					// 	skin_vertices[num_indices] = mesh_skin_vertices[mesh->vertex_indices.data[ix]];
					// }

					numIndices++;
				}
			}

			ufbx_vertex_stream streams[2];
			size_t numStreams = 1;

			streams[0].data = tempVertices.Data();
			streams[0].vertex_count = numIndices;
			streams[0].vertex_size = sizeof(StaticMeshResource::Vertex);

			// if (skin) {
			// 	streams[1].data = skin_vertices;
			// 	streams[1].vertex_count = num_indices;
			// 	streams[1].vertex_size = sizeof(skin_vertex);
			// 	num_streams = 2;
			// }

			// Optimize the flat vertex buffer into an indexed one. `ufbx_generate_indices()`
			// compacts the vertex buffer and returns the number of used vertices.
			ufbx_error error;

			usize numVertices = ufbx_generate_indices(streams, numStreams, tempIndices.Data(), numIndices, NULL, &error);
			if (error.type != UFBX_ERROR_NONE)
			{
				logger.Error("Failed to generate index buffer");
				return {};
			}


			auto materialCheck = [&]() -> u32
			{
				if (mesh_part->index < mesh->materials.count)
				{
					ufbx_material *material =  mesh->materials.data[mesh_part->index];
					if (auto it = cache.materials.Find(material))
					{
						RID materialRID = it->second;

						usize index = FindFirstIndex(meshMaterials.begin(), meshMaterials.end(), materialRID);
						if (index == nPos)
						{
							index = meshMaterials.Size();
							meshMaterials.EmplaceBack(materialRID);
						}

						return index;
					}
				}
				return 0;
			};

			auto& primitive = primitives[partIndex++];
			primitive.firstIndex = totalIndicesCount;
			primitive.indexCount = numIndices;
			primitive.materialIndex = materialCheck();

			for (usize i = 0; i < numIndices; i++)
			{
				tempIndices[i] += totalVerticesCount;
			}

			memcpy(reinterpret_cast<char*>(allVertices.begin()) + totalVerticesCount * sizeof(StaticMeshResource::Vertex), tempVertices.Data(), sizeof(StaticMeshResource::Vertex) * numVertices);
			memcpy(reinterpret_cast<char*>(allIndices.begin()) + totalIndicesCount * sizeof(u32), tempIndices.Data(), sizeof(u32) * numIndices);

			totalVerticesCount += numVertices;
			totalIndicesCount += numIndices;
		}

		allVertices.Resize(totalVerticesCount);
		allIndices.Resize(totalIndicesCount);

		if (settings.generateNormals)
		{
			MeshTools::CalcNormals(allVertices, allIndices);
		}

		if (settings.recalculateTangents)
		{
			MeshTools::CalcTangents(allVertices, allIndices, true);
		}

		RID meshResource = Resources::Create<StaticMeshResource>(UUID::RandomUUID(), scope);

		ResourceObject meshObject = Resources::Write(meshResource);
		meshObject.SetString(StaticMeshResource::Name, !IsStrNullOrEmpty(mesh->name.data) ? mesh->name.data : name);
		meshObject.SetReferenceArray(StaticMeshResource::Materials, meshMaterials);
		meshObject.SetBlob(StaticMeshResource::Vertices, Span(reinterpret_cast<u8*>(allVertices.Data()), allVertices.Size() * sizeof(StaticMeshResource::Vertex)));
		meshObject.SetBlob(StaticMeshResource::Indices, Span(reinterpret_cast<u8*>(allIndices.Data()), allIndices.Size() * sizeof(u32)));
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
				if (RID meshRID = ProcessMesh(settings, cache, nodeName, node->mesh, scope))
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
			opts.target_unit_meters = 1.0;
			opts.ignore_missing_external_files = true;
			opts.evaluate_skinning = true;
			opts.connect_broken_elements = true;

			// Load FBX file
			ufbx_error  error;
			ufbx_scene* scene = ufbx_load_file(path.Data(), &opts, &error);

			if (!scene)
			{
				logger.Error("Error on import file {}: {}", path, error.description.data);
				return false;
			}

			FBXImportCache cache;

			String basePath = Path::Parent(path);
			String name = Path::Name(path);

			for (u32 i = 0; i < scene->textures.count; i++)
			{
				if (RID texture = ProcessTextures(directory, cache, settings, basePath, scene->textures.data[i], scene, scope))
				{
					dccAssetObject.AddToSubObjectSet(DCCAssetResource::Textures, texture);
				}
			}

			// Process Materials
			for (u32 i = 0; i < scene->materials.count; i++)
			{
				if (RID materialRID = ProcessMaterials(settings, cache, scene->materials.data[i], scope))
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
					if (RID meshRID = ProcessMesh(settings, cache, String("Mesh").Append(ToString(i)), mesh, scope))
					{
						cache.meshes.Insert(mesh, meshRID);
					}
				}
			}

			for (const auto& it : cache.meshes)
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
