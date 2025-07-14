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

#include "MeshImporter.hpp"
#include "TextureImporter.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Components/SkinnedMeshRenderer.hpp"
#include "Skore/Scene/Components/StaticMeshRenderer.hpp"

namespace Skore
{

	struct BoneData
	{
		u64 index;
	};

	struct FBXImportData
	{
		ufbx_scene*                   scene = nullptr;
		UndoRedoScope*                scope = nullptr;
		FBXImportSettings             settings;
		HashMap<ufbx_texture*, RID>   textures;
		HashMap<ufbx_material*, RID>  materials;
		HashMap<ufbx_mesh*, RID>      meshes;
		HashMap<RID, RID>             meshRootBone;
		HashMap<ufbx_node*, RID>      entities;
	};

	RID ProcessNode(FBXImportData& fbxData, ufbx_node* node, StringView name);


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

	RID ProcessTextures(RID directory, FBXImportData& fbxData, StringView basePath, ufbx_texture* texture)
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
			textureRID = ImportTextureFromMemory(directory, textureImportSettings, name, data, fbxData.scope);
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
				absolutePath = Path::Join(basePath, StringView{texture->element.name.data, texture->element.name.length});
			}

			//if found, import the texture
			if (FileSystem::GetFileStatus(absolutePath).exists)
			{
				textureRID = ImportTexture(directory, textureImportSettings, absolutePath, fbxData.scope);
			}
		}

		if (textureRID)
		{
			fbxData.textures.Insert(texture, textureRID);

			if (!textureImportSettings.createAssetFile)
			{
				return textureRID;
			}
		}

		return {};
	}


	RID ProcessMaterials(FBXImportData& fbxData, ufbx_material* material)
	{
		RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), fbxData.scope);

		ResourceObject materialObject = Resources::Write(materialResource);
		materialObject.SetString(MaterialResource::Name, !IsStrNullOrEmpty(material->name.data) ? material->name.data : "Material");
		materialObject.SetColor(MaterialResource::BaseColor, Color::FromVec3(ToVec3(material->pbr.base_color.value_vec3)));

		if (auto it = fbxData.textures.Find(material->pbr.base_color.texture))
		{
			materialObject.SetReference(MaterialResource::BaseColorTexture, it->second);
		}

		if (auto it = fbxData.textures.Find(material->pbr.base_color.texture))
		{
			materialObject.SetReference(MaterialResource::BaseColorTexture, it->second);
		}

		if (auto it = fbxData.textures.Find(material->pbr.normal_map.texture))
		{
			materialObject.SetReference(MaterialResource::NormalTexture, it->second);
		}

		if (auto it = fbxData.textures.Find(material->pbr.metalness.texture))
		{
			materialObject.SetReference(MaterialResource::MetallicTexture, it->second);
		}

		if (auto it = fbxData.textures.Find(material->pbr.roughness.texture))
		{
			materialObject.SetReference(MaterialResource::RoughnessTexture, it->second);
		}

		if (auto it = fbxData.textures.Find(material->pbr.ambient_occlusion.texture))
		{
			materialObject.SetReference(MaterialResource::OcclusionTexture, it->second);
		}

		if (auto it = fbxData.textures.Find(material->pbr.emission_color.texture))
		{
			materialObject.SetReference(MaterialResource::EmissiveTexture, it->second);
		}

		materialObject.Commit(fbxData.scope);

		return materialResource;
	}

	template <typename T>
	RID ProcessMesh(FBXImportData& fbxData, ufbx_mesh* mesh, StringView name)
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

		RID rootBone = {};

		if (mesh->skin_deformers.count > 0)
		{
			ufbx_skin_deformer* skin = mesh->skin_deformers[0];


			//process root cluster
			rootBone = ProcessNode(fbxData, skin->clusters[0]->bone_node, "");


			for (u64 i = 0; i < skin->clusters.count; ++i)
			{
				ufbx_skin_cluster* cluster = skin->clusters[i];
				if (auto itBone = fbxData.entities.Find(cluster->bone_node))
				{
					ResourceObject entityObject = Resources::Write(itBone->second);
					entityObject.SetUInt(EntityResource::BoneIndex, i);
					entityObject.Commit(fbxData.scope);
				}
			}

		}

		Array<RID> meshMaterials;

		//temp buffers
		size_t     numTriIndices = mesh->max_face_triangles * 3;
		Array<u32> triIndices = {heapAllocator, numTriIndices};

		Array<T>   tempVertices = {heapAllocator, maxTriangles * 3};
		Array<u32> tempIndices = {heapAllocator, maxTriangles * 3};

		Array<T>   allVertices = {heapAllocator, totalTriangles * 3};
		Array<u32> allIndices = {heapAllocator, totalTriangles * 3};

		Array<MeshPrimitive> primitives = {heapAllocator, partCount};

		u64 totalIndicesCount = 0;
		u64 totalVerticesCount = 0;

		u32 partIndex = 0;

		for (usize pi = 0; pi < mesh->material_parts.count; pi++)
		{
			ufbx_mesh_part* mesh_part = &mesh->material_parts.data[pi];
			if (mesh_part->num_triangles == 0)
			{
				continue;
			}

			u64 numIndices = 0;

			for (usize fi = 0; fi < mesh_part->num_faces; fi++)
			{
				ufbx_face face = mesh->faces.data[mesh_part->face_indices.data[fi]];
				usize     numTris = ufbx_triangulate_face(triIndices.Data(), numTriIndices, mesh, face);

				ufbx_vec2 defaultUV = {0, 0};
				ufbx_vec4 defaultColor = {1, 1, 1, 1};

				// Iterate through every vertex of every triangle in the triangulated result
				for (size_t vi = 0; vi < numTris * 3; vi++)
				{
					u32 ix = triIndices[vi];

					T* vert = &tempVertices[numIndices];

					ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
					ufbx_vec3 normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
					ufbx_vec4 color = mesh->vertex_color.exists ? ufbx_get_vertex_vec4(&mesh->vertex_color, ix) : defaultColor;
					ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : defaultUV;

					uv.y = 1.0f - uv.y; //Flip Y for UV

					vert->position = ToVec3(pos);
					vert->normal = Math::Normalize(ToVec3(normal));
					vert->texCoord = ToVec2(uv);
					vert->color = Vec3(ToVec4(color));

					if constexpr (std::is_same_v<T, MeshSkeletalVertex>)
					{
						if (mesh->skin_deformers.count > 0)
						{
							ufbx_skin_deformer* skin = mesh->skin_deformers[0];
							ufbx_skin_vertex vertex_weights = skin->vertices.data[mesh->vertex_indices.data[ix]];

							size_t numWeights = 0;

							for (size_t wi = 0; wi < vertex_weights.num_weights; wi++)
							{
								if (numWeights >= 4) break;

								ufbx_skin_weight weight = skin->weights.data[vertex_weights.weight_begin + wi];

								vert->boneIndices[wi] = weight.cluster_index;
								vert->boneWeights[wi] = weight.weight;

								numWeights++;
							}
						}
					}
					numIndices++;
				}
			}

			ufbx_vertex_stream streams[2];
			size_t             numStreams = 1;

			streams[0].data = tempVertices.Data();
			streams[0].vertex_count = numIndices;
			streams[0].vertex_size = sizeof(T);

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
					ufbx_material* material = mesh->materials.data[mesh_part->index];
					if (auto it = fbxData.materials.Find(material))
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

			memcpy(reinterpret_cast<char*>(allVertices.begin()) + totalVerticesCount * sizeof(T), tempVertices.Data(), sizeof(T) * numVertices);
			memcpy(reinterpret_cast<char*>(allIndices.begin()) + totalIndicesCount * sizeof(u32), tempIndices.Data(), sizeof(u32) * numIndices);

			totalVerticesCount += numVertices;
			totalIndicesCount += numIndices;
		}

		allVertices.Resize(totalVerticesCount);
		allIndices.Resize(totalIndicesCount);

		MeshImportSettings meshImportSettings = {};


		String meshName;

		if (!name.Empty())
		{
			meshName = name;
		}
		else if (!IsStrNullOrEmpty(mesh->name.data))
		{
			meshName = mesh->name.data;
		}
		else
		{
			u32 index = mesh - fbxData.scene->meshes.data[0];
			meshName = String("Mesh").Append(ToString(index));
		}

		RID meshRID = ImportMesh(RID{}, meshImportSettings, meshName, meshMaterials, primitives, allVertices, allIndices, fbxData.scope);

		if (rootBone)
		{
			fbxData.meshRootBone.Insert(meshRID, rootBone);
		}

		return meshRID;
	}


	RID ProcessMesh(FBXImportData& fbxData, ufbx_mesh* mesh, StringView name)
	{
		if (mesh->skin_deformers.count > 0)
		{
			return ProcessMesh<MeshSkeletalVertex>(fbxData, mesh, name);
		}
		return ProcessMesh<MeshStaticVertex>(fbxData, mesh, name);
	}

	RID ProcessAnimation(FBXImportData& fbxData, ufbx_anim_stack* animStack)
	{
		RID animation = Resources::Create<AnimationClipResource>(UUID::RandomUUID());

		ResourceObject animationObject = Resources::Write(animation);
		animationObject.SetString(AnimationClipResource::Name, !IsStrNullOrEmpty(animStack->name.data) ? animStack->name.data : "Animation");
		animationObject.Commit(fbxData.scope);

		return animation;
	}

	RID ProcessNode(FBXImportData& fbxData, ufbx_node* node, StringView name)
	{
		if (!node)
		{
			return {};
		}

		if (auto it = fbxData.entities.Find(node))
		{
			return it->second;
		}

		//ignore camera or lights
		if ((node->camera || node->light) && node->children.count == 0)
		{
			return {};
		}

		RID entity = Resources::Create<EntityResource>(UUID::RandomUUID());
		ResourceObject entityObject = Resources::Write(entity);

		StringView nodeName = name.Empty() ? "Node" : name;

		if (!IsStrNullOrEmpty(node->name.data))
		{
			nodeName = node->name.data;
		}

		logger.Debug("processing node {} ", nodeName);

		entityObject.SetString(EntityResource::Name, nodeName);

		// Extract transform
		Transform transform;
		transform.position = ToVec3(node->local_transform.translation);
		transform.rotation = Quat{node->local_transform.rotation.x, node->local_transform.rotation.y, node->local_transform.rotation.z, node->local_transform.rotation.w};
		transform.scale = ToVec3(node->local_transform.scale);

		RID transformRID = Resources::Create<Transform>(UUID::RandomUUID());
		Resources::ToResource(transformRID, &transform, fbxData.scope);
		entityObject.SetSubObject(EntityResource::Transform, transformRID);

		// Handle mesh
		if (node->mesh)
		{
			auto meshIt = fbxData.meshes.Find(node->mesh);
			if (meshIt == fbxData.meshes.end())
			{
				if (RID meshRID = ProcessMesh(fbxData, node->mesh, nodeName))
				{
					meshIt = fbxData.meshes.Insert(node->mesh, meshRID).first;
				}
			}

			if (meshIt)
			{
				ResourceObject meshObject = Resources::Write(meshIt->second);
				if (meshObject.GetBool(MeshResource::Skinned))
				{
					RID meshRenderer = Resources::Create<SkinnedMeshRenderer>(UUID::RandomUUID());
					ResourceObject skinnedMeshRenderObject = Resources::Write(meshRenderer);

					if (auto rootIt = fbxData.meshRootBone.Find(meshIt->second))
					{
						skinnedMeshRenderObject.SetReference(skinnedMeshRenderObject.GetIndex("rootBone"), rootIt->second);
					}

					skinnedMeshRenderObject.SetReference(skinnedMeshRenderObject.GetIndex("mesh"), meshIt->second);
					skinnedMeshRenderObject.Commit(fbxData.scope);

					entityObject.AddToSubObjectList(EntityResource::Components, meshRenderer);
				}
				else
				{
					RID meshRenderer = Resources::Create<StaticMeshRenderer>(UUID::RandomUUID());

					ResourceObject staticMeshRenderObject = Resources::Write(meshRenderer);
					staticMeshRenderObject.SetReference(staticMeshRenderObject.GetIndex("mesh"), meshIt->second);
					staticMeshRenderObject.Commit(fbxData.scope);

					entityObject.AddToSubObjectList(EntityResource::Components, meshRenderer);
				}
			}
		}

		// Process children
		for (u32 i = 0; i < node->children.count; i++)
		{
			if (RID child = ProcessNode(fbxData, node->children.data[i], ""))
			{
				entityObject.AddToSubObjectList(EntityResource::Children, child);
			}
		}

		fbxData.entities.Insert(node, entity);

		entityObject.Commit(fbxData.scope);

		return entity;
	}

	bool ImportFBX(RID directory, const FBXImportSettings& settings, StringView path, UndoRedoScope* scope)
	{
		String fileName = Path::Name(path);
		RID    dccAsset = ResourceAssets::CreateImportedAsset(directory, TypeInfo<DCCAssetResource>::ID(), fileName, scope, path);

		ResourceObject dccAssetObject = Resources::Write(dccAsset);
		dccAssetObject.SetString(DCCAssetResource::Name, Path::Name(path));


		bool allowGeometryHelperNodes = false;

		{
			ufbx_load_opts opts = {};
			opts.target_axes = ufbx_axes_right_handed_y_up;
			opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
			if (!allowGeometryHelperNodes)
			{
				opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY_NO_FALLBACK;
				opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE_NO_FALLBACK;
			}
			else
			{
				opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
				opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
			}

			opts.target_unit_meters = 1.0;
			opts.ignore_missing_external_files = true;
			opts.evaluate_skinning = true;
			opts.connect_broken_elements = true;
			opts.pivot_handling = UFBX_PIVOT_HANDLING_ADJUST_TO_PIVOT;
			opts.geometry_transform_helper_name.data = "GeometryTransformHelper";
			opts.geometry_transform_helper_name.length = SIZE_MAX;
			opts.scale_helper_name.data = "ScaleHelper";
			opts.scale_helper_name.length = SIZE_MAX;
			opts.node_depth_limit = 512;
			opts.target_camera_axes = ufbx_axes_right_handed_y_up;
			opts.target_light_axes = ufbx_axes_right_handed_y_up;
			//opts.clean_skin_weights = true;
			opts.generate_missing_normals = true;

			// Load FBX file
			ufbx_error  error;
			ufbx_scene* scene = ufbx_load_file(path.Data(), &opts, &error);

			if (!scene)
			{
				logger.Error("Error on import file {}: {}", path, error.description.data);
				return false;
			}

			FBXImportData fbxData;
			fbxData.scene = scene;
			fbxData.scope = scope;

			String basePath = Path::Parent(path);
			String name = Path::Name(path);

			for (u32 i = 0; i < scene->textures.count; i++)
			{
				if (RID texture = ProcessTextures(directory, fbxData, basePath, scene->textures.data[i]))
				{
					dccAssetObject.AddToSubObjectList(DCCAssetResource::Textures, texture);
				}
			}

			// Process Materials
			for (u32 i = 0; i < scene->materials.count; i++)
			{
				if (RID materialRID = ProcessMaterials(fbxData, scene->materials.data[i]))
				{
					fbxData.materials.Insert(scene->materials.data[i], materialRID);
					dccAssetObject.AddToSubObjectList(DCCAssetResource::Materials, materialRID);
				}
			}

			for (u32 i = 0; i < scene->anim_stacks.count; i++)
			{
				if (RID animation = ProcessAnimation(fbxData, scene->anim_stacks.data[i]))
				{
					dccAssetObject.AddToSubObjectList(DCCAssetResource::Animations, animation);
				}
			}

			// Process nodes
			for (u32 i = 0; i < scene->nodes.count; i++)
			{
				ufbx_node* current = scene->nodes.data[i];
				if (current->parent == nullptr)
				{
					while (current != nullptr && !current->mesh && current->children.count == 1)
					{
						current = current->children.data[0];
					}

					if (current)
					{
						if (RID root = ProcessNode(fbxData, current, fileName))
						{
							dccAssetObject.SetSubObject(DCCAssetResource::Entity, root);
						}
					}
				}
			}



			//import missing meshes.
			for (u32 i = 0; i < scene->meshes.count; i++)
			{
				ufbx_mesh* mesh = scene->meshes.data[i];
				if (!fbxData.meshes.Has(mesh))
				{
					if (RID meshRID = ProcessMesh(fbxData, mesh, ""))
					{
						fbxData.meshes.Insert(mesh, meshRID);
					}
				}
			}

			for (const auto& it : fbxData.meshes)
			{
				dccAssetObject.AddToSubObjectList(DCCAssetResource::Meshes, it.second);
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
