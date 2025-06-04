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

#include "Skore/Asset/AssetFileOld.hpp"
#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"
#include "Skore/Asset/AssetEditor.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Scene/Components/MeshRenderComponent.hpp"
#include "Skore/IO/FileSystem.hpp"

#include <ufbx.h>

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

	TextureAsset* ProcessTexture(AssetFileOld* parentAsset, ufbx_texture* texture, StringView basePath)
	{
		if (!texture || !texture->filename.data)
		{
			return nullptr;
		}

		String textureName = texture->name.data ? texture->name.data : "Texture";
		String texturePath = Path::Join(basePath, texture->filename.data);

		AssetFileOld* textureFile = AssetEditor::GetFileByAbsolutePath(texturePath);
		if (textureFile == nullptr)
		{
			if (texture->relative_filename.data)
			{
				texturePath = Path::Join(basePath, texture->relative_filename.data);
				textureFile = AssetEditor::GetFileByAbsolutePath(texturePath);
			}

			if (textureFile == nullptr)
			{
				logger.Warn("texture file not found {} ", texture->filename.data);
				parentAsset->SetStatus(AssetStatus::Warning);
				parentAsset->AddMissingFile(texture->filename.data);
				return nullptr;
			}
		}

		if (TextureAsset* textureAsset = textureFile->GetInstance()->SafeCast<TextureAsset>())
		{
			return textureAsset;
		}

		return nullptr;
	}

	MaterialAsset* ProcessMaterial(AssetFileOld* parentAsset, ufbx_material* material, Array<TextureAsset*>& textures, const ufbx_scene* scene)
	{
		if (!material)
		{
			return nullptr;
		}

		AssetFileOld* materialAssetFile = AssetEditor::FindOrCreateAsset(parentAsset, TypeInfo<MaterialAsset>::ID(), material->name.data ? material->name.data : "Material");
		if (!materialAssetFile)
		{
			return nullptr;
		}

		MaterialAsset* materialAsset = materialAssetFile->GetInstance()->SafeCast<MaterialAsset>();
		if (!materialAsset)
		{
			return nullptr;
		}

		// Default to opaque
		materialAsset->alphaMode = MaterialAsset::AlphaMode::Opaque;
		materialAsset->alphaCutoff = 0.5f;

		// Process diffuse/base color
		// Base color/diffuse
		if (const ufbx_prop* baseColorProp = ufbx_find_prop(&material->props, "base_color"))
		{
			if (baseColorProp->type == UFBX_PROP_COLOR)
			{
				materialAsset->baseColor = Color::FromVec3(ToVec3(baseColorProp->value_vec3), 1.0f);
			}
		}
		else if (const ufbx_prop* diffuseColorProp = ufbx_find_prop(&material->props, "DiffuseColor"))
		{
			if (diffuseColorProp->type == UFBX_PROP_COLOR)
			{
				materialAsset->baseColor = Color::FromVec3(ToVec3(diffuseColorProp->value_vec3), 1.0f);
			}
		}

		// Base color/diffuse texture
		if (const ufbx_texture* baseColorTex = ufbx_find_prop_texture(material, "base_color"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == baseColorTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->baseColorTexture = textures[textureIndex];
			}
		}
		else if (const ufbx_texture* diffuseTex = ufbx_find_prop_texture(material, "DiffuseColor"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == diffuseTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->baseColorTexture = textures[textureIndex];
			}
		}

		// Metallic
		if (const ufbx_prop* metallicProp = ufbx_find_prop(&material->props, "metalness"))
		{
			if (metallicProp->type == UFBX_PROP_NUMBER)
			{
				materialAsset->metallic = (float)metallicProp->value_real;
			}
		}

		// Metallic texture
		if (const ufbx_texture* metallicTex = ufbx_find_prop_texture(material, "metalness"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == metallicTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->metallicTexture = textures[textureIndex];
				materialAsset->metallicTextureChannel = TextureChannel::Red;
			}
		}

		// Roughness
		if (const ufbx_prop* roughnessProp = ufbx_find_prop(&material->props, "roughness"))
		{
			if (roughnessProp->type == UFBX_PROP_NUMBER)
			{
				materialAsset->roughness = (float)roughnessProp->value_real;
			}
		}
		else if (const ufbx_prop* shininessProp = ufbx_find_prop(&material->props, "Shininess"))
		{
			if (shininessProp->type == UFBX_PROP_NUMBER)
			{
				// Convert from shininess to roughness (approximate)
				materialAsset->roughness = 1.0f - Math::Clamp((float)shininessProp->value_real / 100.0f, 0.0f, 1.0f);
			}
		}

		// Roughness texture
		if (const ufbx_texture* roughnessTex = ufbx_find_prop_texture(material, "roughness"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == roughnessTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->roughnessTexture = textures[textureIndex];
				materialAsset->roughnessTextureChannel = TextureChannel::Green;
			}
		}

		// Normal map
		if (const ufbx_texture* normalTex = ufbx_find_prop_texture(material, "normal_map"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == normalTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->normalTexture = textures[textureIndex];
				materialAsset->normalMultiplier = 1.0f;
			}
		}
		else if (const ufbx_texture* normalTex = ufbx_find_prop_texture(material, "NormalMap"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == normalTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->normalTexture = textures[textureIndex];
				materialAsset->normalMultiplier = 1.0f;
			}
		}

		// Occlusion texture
		if (const ufbx_texture* occlusionTex = ufbx_find_prop_texture(material, "occlusion"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == occlusionTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->occlusionTexture = textures[textureIndex];
				materialAsset->occlusionStrength = 1.0f;
				materialAsset->occlusionTextureChannel = TextureChannel::Red;
			}
		}

		// Emission
		if (const ufbx_prop* emissiveProp = ufbx_find_prop(&material->props, "emission_color"))
		{
			if (emissiveProp->type == UFBX_PROP_COLOR)
			{
				materialAsset->emissiveFactor = ToVec3(emissiveProp->value_vec3);
			}
		}
		else if (const ufbx_prop* emissiveProp = ufbx_find_prop(&material->props, "EmissiveColor"))
		{
			if (emissiveProp->type == UFBX_PROP_COLOR)
			{
				materialAsset->emissiveFactor = ToVec3(emissiveProp->value_vec3);
			}
		}

		// Emission texture
		if (const ufbx_texture* emissiveTex = ufbx_find_prop_texture(material, "emission_color"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == emissiveTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->emissiveTexture = textures[textureIndex];
			}
		}
		else if (const ufbx_texture* emissiveTex = ufbx_find_prop_texture(material, "EmissiveColor"))
		{
			i32 textureIndex = -1;
			for (i32 i = 0; i < (i32)scene->textures.count; i++)
			{
				if (scene->textures.data[i] == emissiveTex)
				{
					textureIndex = i;
					break;
				}
			}

			if (textureIndex >= 0 && textureIndex < textures.Size() && textures[textureIndex] != nullptr)
			{
				materialAsset->emissiveTexture = textures[textureIndex];
			}
		}

		// Check transparency factor
		if (const ufbx_prop* opacityProp = ufbx_find_prop(&material->props, "Opacity"))
		{
			if (opacityProp->type == UFBX_PROP_NUMBER && opacityProp->value_real < 1.0f)
			{
				materialAsset->alphaMode = MaterialAsset::AlphaMode::Blend;
				//materialAsset->baseColor.a = (float)opacityProp->value_real;
			}
		}
		else if (const ufbx_prop* transparencyProp = ufbx_find_prop(&material->props, "TransparencyFactor"))
		{
			if (transparencyProp->type == UFBX_PROP_NUMBER && transparencyProp->value_real > 0.0f)
			{
				materialAsset->alphaMode = MaterialAsset::AlphaMode::Blend;
				//materialAsset->baseColor.a = 1.0f - (float)transparencyProp->value_real;
			}
		}

		// Mark the asset file as dirty to save changes
		materialAssetFile->MarkDirty();

		return materialAsset;
	}

	MeshAsset* ProcessMesh(AssetFileOld* parentAsset, ufbx_mesh* mesh, Array<MaterialAsset*>& materials, const ufbx_scene* scene)
	{
		if (!mesh)
			return nullptr;

		// Create a mesh asset as a child of the parent asset
		AssetFileOld* meshAssetFile = AssetEditor::FindOrCreateAsset(parentAsset, TypeInfo<MeshAsset>::ID(), mesh->name.data ? mesh->name.data : "Mesh");
		if (!meshAssetFile)
			return nullptr;

		// Get the mesh instance
		MeshAsset* meshAsset = meshAssetFile->GetInstance()->SafeCast<MeshAsset>();
		if (!meshAsset)
			return nullptr;

		// Get total vertex and index counts
		size_t totalVertices = mesh->num_vertices;
		size_t totalIndices = mesh->num_indices;

		Array<MeshAsset::Vertex>    vertices;
		Array<u32>                  indices;
		Array<MeshAsset::Primitive> primitives;
		Array<MaterialAsset*>       materialAssets;

		vertices.Reserve(totalVertices);
		indices.Reserve(totalIndices);
		primitives.Reserve(mesh->materials.count);
		materialAssets.Reserve(mesh->materials.count);

		// Material mapping
		for (u32 i = 0; i < mesh->materials.count; i++)
		{
			const ufbx_material* material = mesh->materials.data[i];
			if (material)
			{
				for (u32 j = 0; j < scene->materials.count; j++)
				{
					if (scene->materials.data[j] == material && j < materials.Size() && materials[j] != nullptr)
					{
						materialAssets.EmplaceBack(materials[j]);
						break;
					}
				}
			}
		}

		// If no materials were found, add a default one
		if (materialAssets.Empty())
		{
			materialAssets.EmplaceBack(nullptr);
		}

		// Check for available vertex attributes
		bool hasNormals = mesh->vertex_normal.exists;
		bool hasTangents = mesh->vertex_tangent.exists;
		bool hasTexCoords = mesh->vertex_uv.exists;
		bool hasColors = mesh->vertex_color.exists;

		// Extract vertices
		for (u32 i = 0; i < mesh->num_vertices; i++)
		{
			MeshAsset::Vertex vertex;

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

		// Face groups by material
		for (u32 i = 0; i < mesh->face_material.count; i++)
		{
			ufbx_face face = mesh->faces.data[i];
			u32 currentMaterialIndex = mesh->face_material.data[i];

			if (i == 0)
			{
				materialIndex = currentMaterialIndex;
				materialStartFace = 0;
			}

			// If material changes or this is the last face, create a primitive
			if (currentMaterialIndex != materialIndex || i == mesh->face_material.count - 1)
			{
				// If this is the last face, include it in the count
				if (i == mesh->face_material.count - 1)
				{
					materialFaceCount++;
				}

				// Add the primitive for the previous material
				MeshAsset::Primitive primitive;
				primitive.firstIndex = firstIndex;
				primitive.indexCount = (u32)(materialFaceCount * 3); // Triangulated faces
				primitive.materialIndex = materialIndex < materialAssets.Size() ? materialIndex : 0;
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
			MeshAsset::Primitive primitive;
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

		meshAsset->SetVertices(vertices);
		meshAsset->SetIndices(indices);
		meshAsset->SetPrimitives(primitives);
		meshAsset->SetMaterials(materialAssets);

		if (!hasNormals)
		{
			meshAsset->CalcNormals();
		}

		const bool recalculateTangents = true;
		if (recalculateTangents || !hasTangents)
		{
			meshAsset->CalcTangents(true);
		}

		// Mark the asset file as dirty to save changes
		meshAssetFile->MarkDirty();

		return meshAsset;
	}

	void ProcessNode(Scene* scene, Entity* parent, const ufbx_node* node, Array<MeshAsset*>& meshes, const ufbx_scene* fbxScene)
	{
		if (!node)
		{
			return;
		}

		//ignore camera and lights for now.
		if (node->camera || node->light)
		{
			return;
		}

		Entity* entity = Alloc<Entity>();
		entity->SetParent(parent);
		entity->SetUUID(UUID::RandomUUID());
		entity->SetName(node->name.data ? node->name.data : "Node");

		// Extract transform
		Transform transform;

		// Get local transform (position, rotation, scale)
		ufbx_transform localTransform = node->local_transform;

		transform.position = ToVec3(localTransform.translation);

		// Convert ufbx quaternion to our Quat format
		transform.rotation = Quat{
			localTransform.rotation.x,
			localTransform.rotation.y,
			localTransform.rotation.z,
			localTransform.rotation.w
		};

		transform.scale = ToVec3(localTransform.scale);
		entity->SetTransform(transform);

		// Handle mesh
		if (node->mesh)
		{
			for (u32 i = 0; i < fbxScene->meshes.count; i++)
			{
				if (fbxScene->meshes.data[i] == node->mesh && i < meshes.Size() && meshes[i] != nullptr)
				{
					MeshRenderComponent* renderComponent = entity->AddComponent<MeshRenderComponent>();
					renderComponent->SetMesh(meshes[i]);
					break;
				}
			}
		}

		// Process children
		for (u32 i = 0; i < node->children.count; i++)
		{
			ProcessNode(scene, entity, node->children.data[i], meshes, fbxScene);
		}
	}

	struct FBXImporter : AssetImporter
	{
		SK_CLASS(FBXImporter, AssetImporter);

		Array<String> ImportExtensions() override
		{
			return {".fbx"};
		}

		bool ImportAsset(AssetFileOld* assetFile, StringView path) override
		{
			// Setup ufbx load options
			ufbx_load_opts opts = {};

			// opts.target_axes = {
			// 	{1.0f, 0.0f, 0.0f},  // +X
			// 	{0.0f, 1.0f, 0.0f},  // +Y
			// 	{0.0f, 0.0f, 1.0f},  // +Z
			// };

			// Load FBX file
			ufbx_error error;
			ufbx_scene* scene = ufbx_load_file(path.Data(), &opts, &error);

			if (!scene)
			{
				logger.Error("Error on import file {}: {}", path, error.description.data);
				assetFile->SetStatus(AssetStatus::Error);
				return false;
			}

			// Get the scene instance from the asset file
			Scene* engineScene = assetFile->GetInstance()->SafeCast<Scene>();
			if (!engineScene)
			{
				ufbx_free_scene(scene);
				return false;
			}

			String basePath = Path::Parent(path);
			String name = Path::Name(path);

			// Process textures
			Array<TextureAsset*> textures;
			textures.Reserve(scene->textures.count);

			for (u32 i = 0; i < scene->textures.count; i++)
			{
				if (TextureAsset* texture = ProcessTexture(assetFile, scene->textures.data[i], basePath))
				{
					textures.PushBack(texture);
				}
				else
				{
					textures.PushBack(nullptr);
				}
			}

			// Process materials
			Array<MaterialAsset*> materials;
			materials.Reserve(scene->materials.count);

			for (u32 i = 0; i < scene->materials.count; i++)
			{
				MaterialAsset* material = ProcessMaterial(assetFile, scene->materials.data[i], textures, scene);
				materials.PushBack(material);
			}

			// Process meshes
			Array<MeshAsset*> meshes;
			meshes.Reserve(scene->meshes.count);

			for (u32 i = 0; i < scene->meshes.count; i++)
			{
				MeshAsset* mesh = ProcessMesh(assetFile, scene->meshes.data[i], materials, scene);
				meshes.PushBack(mesh);
			}

			// Create root entity
			Entity* root = Alloc<Entity>();
			engineScene->SetRootEntity(root);
			root->SetName(name);
			root->SetUUID(UUID::RandomUUID());

			// Process nodes
			for (u32 i = 0; i < scene->nodes.count; i++)
			{
				const ufbx_node* node = scene->nodes.data[i];
				if (node->parent == nullptr)
				{
					ProcessNode(engineScene, root, node, meshes, scene);
				}
			}

			// Free the ufbx scene
			ufbx_free_scene(scene);

			// Mark the asset file as dirty to save changes
			assetFile->MarkDirty();

			return true;
		}

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<Scene>::ID();
		}
	};

	void RegisterFBXImporter()
	{
		Reflection::Type<FBXImporter>();
	}
}
