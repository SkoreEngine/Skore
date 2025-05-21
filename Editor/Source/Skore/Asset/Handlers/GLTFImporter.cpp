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

#include "Skore/Asset/AssetFile.hpp"
#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"
#include "Skore/Asset/AssetEditor.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Scene/Components/MeshRenderComponent.hpp"
#include "Skore/IO/FileSystem.hpp"

#include <cgltf.h>
#include <base64.hpp>


namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::GLTFImporter");

	template <typename T>
	void LoadBufferView(const cgltf_buffer_view* bufferView, Array<T>& outArray)
	{
		if (!bufferView || !bufferView->buffer || !bufferView->buffer->data)
			return;

		const u8* sourceData = static_cast<const u8*>(bufferView->buffer->data) + bufferView->offset;
		size_t    elementCount = bufferView->size / sizeof(T);

		outArray.Resize(elementCount);
		memcpy(outArray.Data(), sourceData, bufferView->size);
	}


	Vec2 ToVec2(const cgltf_float* vec)
	{
		return Vec2{vec[0], vec[1]};
	}

	Vec3 ToVec3(const cgltf_float* vec)
	{
		return Vec3{vec[0], vec[1], vec[2]};
	}

	Vec4 ToVec4(const cgltf_float* vec)
	{
		return Vec4{vec[0], vec[1], vec[2], vec[3]};
	}

	TextureAsset* ProcessTexture(AssetFile* parentAsset, const cgltf_options* options, cgltf_image* image, StringView basePath)
	{
		if (bool embedded = (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) || (image->uri && strncmp(image->uri, "data:", 5) == 0))
		{
			bool imported = false;

			String textureName = image->name ? image->name : (image->uri ? image->uri : "Texture");
			AssetFile*    textureAssetFile = AssetEditor::FindOrCreateAsset(parentAsset, TypeInfo<TextureAsset>::ID(), textureName);
			TextureAsset* textureAsset = textureAssetFile->GetInstance()->SafeCast<TextureAsset>();

			if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data)
			{
				const u8* bufferData = static_cast<const u8*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
				usize     bufferSize = image->buffer_view->size;

				imported = textureAsset->SetTextureDataFromFileInMemory(Span(bufferData, bufferSize), false, true, true);
			}
			else if (image->uri)
			{
				if (strncmp(image->uri, "data:", 5) == 0)
				{
					if (const char* base64Start = strstr(image->uri, ";base64,"))
					{
						base64Start += 8; // Move past ";base64,"
						usize size = strlen(base64Start);

						Array<u8> output = base64::decode_into<Array<u8>>(std::string_view(base64Start, size));
						imported = textureAsset->SetTextureDataFromFileInMemory(Span(static_cast<const u8*>(output.Data()), output.Size()), false, true, true);
					}
				}
			}

			if (imported)
			{
				textureAssetFile->MarkDirty();
				return textureAsset;
			}
		}
		else if (image->uri)
		{
			String texturePath = Path::Join(basePath, image->uri);
			AssetFile* textureFile = AssetEditor::GetFileByAbsolutePath(texturePath);
			if (textureFile == nullptr)
			{
				logger.Warn("texture file not found {} ", image->uri);
				parentAsset->SetStatus(AssetStatus::Warning);
				parentAsset->AddMissingFile(image->uri);
				return nullptr;
			}

			if (TextureAsset* textureAsset = textureFile->GetInstance()->SafeCast<TextureAsset>())
			{
				return textureAsset;
			}
		}
		return nullptr;
	}

	MaterialAsset* ProcessMaterial(AssetFile* parentAsset, cgltf_material* material, Array<TextureAsset*>& textures, cgltf_data* data)
	{
		if (!material)
		{
			return nullptr;
		}

		AssetFile* materialAssetFile = AssetEditor::FindOrCreateAsset(parentAsset, TypeInfo<MaterialAsset>::ID(), material->name ? material->name : "Material");
		if (!materialAssetFile)
		{
			return nullptr;
		}

		MaterialAsset* materialAsset = materialAssetFile->GetInstance()->SafeCast<MaterialAsset>();
		if (!materialAsset)
		{
			return nullptr;
		}

		switch (material->alpha_mode)
		{
			case cgltf_alpha_mode_opaque:
				materialAsset->alphaMode = MaterialAsset::AlphaMode::Opaque;
				break;
			case cgltf_alpha_mode_mask:
				materialAsset->alphaMode = MaterialAsset::AlphaMode::Mask;
				break;
			case cgltf_alpha_mode_blend:
				materialAsset->alphaMode = MaterialAsset::AlphaMode::Blend;
				break;
		}

		materialAsset->alphaCutoff = material->alpha_cutoff;

		// Process PBR metallic roughness
		if (material->has_pbr_metallic_roughness)
		{
			auto& pbr = material->pbr_metallic_roughness;
			materialAsset->baseColor = Color::FromVec4(Vec4{
				pbr.base_color_factor[0],
				pbr.base_color_factor[1],
				pbr.base_color_factor[2],
				pbr.base_color_factor[3]
			});

			materialAsset->metallic = pbr.metallic_factor;
			materialAsset->roughness = pbr.roughness_factor;

			if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
			{
				i32 imageIndex = (i32)(pbr.base_color_texture.texture->image - data->images);
				if (imageIndex >= 0 && imageIndex < textures.Size() && textures[imageIndex] != nullptr)
				{
					materialAsset->baseColorTexture = textures[imageIndex];}
			}

			if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image)
			{
				i32 imageIndex = (i32)(pbr.metallic_roughness_texture.texture->image - data->images);
				if (imageIndex >= 0 && imageIndex < textures.Size() && textures[imageIndex] != nullptr)
				{
					materialAsset->metallicTexture = textures[imageIndex];
					materialAsset->metallicTextureChannel = TextureChannel::Blue;
					materialAsset->roughnessTexture = textures[imageIndex];
					materialAsset->roughnessTextureChannel = TextureChannel::Green;
				}
			}
		}

		// Process normal texture
		if (material->normal_texture.texture && material->normal_texture.texture->image)
		{
			i32 imageIndex = (i32)(material->normal_texture.texture->image - data->images);
			if (imageIndex >= 0 && imageIndex < textures.Size() && textures[imageIndex] != nullptr)
			{
				materialAsset->normalTexture = textures[imageIndex];
				materialAsset->normalMultiplier = material->normal_texture.scale;
			}
		}
		
		// Process occlusion texture
		if (material->occlusion_texture.texture && material->occlusion_texture.texture->image)
		{
			i32 imageIndex = (i32)(material->occlusion_texture.texture->image - data->images);
			if (imageIndex >= 0 && imageIndex < textures.Size() && textures[imageIndex] != nullptr)
			{
				materialAsset->occlusionTexture = textures[imageIndex];
				materialAsset->occlusionStrength = material->occlusion_texture.scale;
				materialAsset->occlusionTextureChannel = TextureChannel::Red;
			}
		}
		
		// Process emissive texture and factor
		if (material->emissive_texture.texture && material->emissive_texture.texture->image)
		{
			i32 imageIndex = (i32)(material->emissive_texture.texture->image - data->images);
			if (imageIndex >= 0 && imageIndex < textures.Size() && textures[imageIndex] != nullptr)
			{
				materialAsset->emissiveTexture = textures[imageIndex];
			}
		}
		
		// Set emissive factor
		materialAsset->emissiveFactor = Vec3{
			material->emissive_factor[0],
			material->emissive_factor[1],
			material->emissive_factor[2]
		};

		// Mark the asset file as dirty to save changes
		materialAssetFile->MarkDirty();

		return materialAsset;
	}


	MeshAsset* ProcessMesh(AssetFile* parentAsset, cgltf_mesh* mesh, Array<MaterialAsset*>& materials, cgltf_data* data)
	{
		if (!mesh)
			return nullptr;

		// Create a mesh asset as a child of the parent asset
		AssetFile* meshAssetFile = AssetEditor::FindOrCreateAsset(parentAsset, TypeInfo<MeshAsset>::ID(), mesh->name ? mesh->name : "Mesh");
		if (!meshAssetFile)
			return nullptr;

		// Get the mesh instance
		MeshAsset* meshAsset = meshAssetFile->GetInstance()->SafeCast<MeshAsset>();
		if (!meshAsset)
			return nullptr;


		size_t totalVertices = 0;
		size_t totalIndices = 0;

		for (size_t i = 0; i < mesh->primitives_count; i++)
		{
			cgltf_primitive* primitive = &mesh->primitives[i];

			for (size_t j = 0; j < primitive->attributes_count; j++)
			{
				if (primitive->attributes[j].type == cgltf_attribute_type_position)
				{
					totalVertices += primitive->attributes[j].data->count;
					break;
				}
			}

			if (primitive->indices)
			{
				totalIndices += primitive->indices->count;
			}
		}

		Array<MeshAsset::Vertex>    vertices;
		Array<u32>                  indices;
		Array<MeshAsset::Primitive> primitives;
		Array<MaterialAsset*>       materialAssets;

		vertices.Reserve(totalVertices);
		indices.Reserve(totalIndices);
		primitives.Reserve(mesh->primitives_count);
		materials.Reserve(mesh->primitives_count);

		// Process each primitive
		u32 baseIndex = 0;
		u32 baseVertex = 0;

		HashMap<cgltf_material*, u32> materialMap;

		for (size_t i = 0; i < mesh->primitives_count; i++)
		{
			cgltf_primitive* primitive = &mesh->primitives[i];

			if (primitive->material)
			{
				size_t materialIndex = primitive->material - data->materials;
				if (materialIndex < materials.Size())
				{
					materialAssets.EmplaceBack(materials[materialIndex]);
					materialMap.Emplace(primitive->material, static_cast<u32>(materialAssets.Size()) - 1);
				}
			}
		}

		bool missingNormals = false;
		bool missingTangents = false;

		for (size_t i = 0; i < mesh->primitives_count; i++)
		{
			cgltf_primitive* primitive = &mesh->primitives[i];

			cgltf_accessor* positionAccessor = nullptr;
			cgltf_accessor* normalAccessor = nullptr;
			cgltf_accessor* texcoordAccessor = nullptr;
			cgltf_accessor* colorAccessor = nullptr;
			cgltf_accessor* tangentAccessor = nullptr;
			cgltf_accessor* indicesAccessor = primitive->indices;

			for (size_t j = 0; j < primitive->attributes_count; j++)
			{
				cgltf_attribute* attr = &primitive->attributes[j];

				switch (attr->type)
				{
					case cgltf_attribute_type_position:
						positionAccessor = attr->data;
						break;
					case cgltf_attribute_type_normal:
						normalAccessor = attr->data;
						break;
					case cgltf_attribute_type_texcoord:
						texcoordAccessor = attr->data;
						break;
					case cgltf_attribute_type_color:
						colorAccessor = attr->data;
						break;
					case cgltf_attribute_type_tangent:
						tangentAccessor = attr->data;
						break;
				}
			}

			if (!positionAccessor)
			{
				continue;
			}

			if (!normalAccessor)
			{
				missingNormals = true;
			}

			if (!tangentAccessor)
			{
				missingTangents = true;
			}

			size_t vertexCount = positionAccessor->count;
			u32    currentVertex = baseVertex;

			for (size_t v = 0; v < vertexCount; v++)
			{
				MeshAsset::Vertex vertex;

				// Default values
				vertex.position = Vec3{0, 0, 0};
				vertex.normal = Vec3{0, 1, 0};
				vertex.texCoord = Vec2{0, 0};
				vertex.color = Vec3{1, 1, 1};
				vertex.tangent = Vec4{1, 0, 0, 1};

				cgltf_accessor_read_float(positionAccessor, v, &vertex.position.x, 3);

				if (normalAccessor)
				{
					cgltf_accessor_read_float(normalAccessor, v, &vertex.normal.x, 3);
				}

				if (texcoordAccessor)
				{
					cgltf_accessor_read_float(texcoordAccessor, v, &vertex.texCoord.x, 2);
				}

				if (colorAccessor)
				{
					cgltf_accessor_read_float(colorAccessor, v, &vertex.color.x, 3);
				}

				if (tangentAccessor)
				{
					cgltf_accessor_read_float(tangentAccessor, v, &vertex.tangent.x, 4);
				}

				vertices.EmplaceBack(vertex);
				currentVertex++;
			}

			if (indicesAccessor)
			{
				for (size_t idx = 0; idx < indicesAccessor->count; idx++)
				{
					u32 index = 0;
					cgltf_accessor_read_uint(indicesAccessor, idx, &index, 1);
					indices.EmplaceBack(baseVertex + index);
				}
			}

			MeshAsset::Primitive meshPrimitive;
			meshPrimitive.firstIndex = baseIndex;
			meshPrimitive.indexCount = primitive->indices->count;
			if (primitive->material)
			{
				if (auto it = materialMap.Find(primitive->material))
				{
					meshPrimitive.materialIndex = it->second;
				}
			}
			else
			{
				meshPrimitive.materialIndex = 0;
			}

			baseIndex += primitive->indices->count;
			primitives.EmplaceBack(meshPrimitive);

			baseVertex = currentVertex;
		}

		meshAsset->SetVertices(vertices);
		meshAsset->SetIndices(indices);
		meshAsset->SetPrimitives(primitives);
		meshAsset->SetMaterials(materialAssets);

		if (missingNormals)
		{
			meshAsset->CalcNormals();
		}

		//TODO: add on a asset importer settings
		const bool recalculateTangents = true;
		if (recalculateTangents || missingTangents)
		{
			meshAsset->CalcTangents(true);
		}

		// Mark the asset file as dirty to save changes
		meshAssetFile->MarkDirty();

		return meshAsset;
	}

	void ProcessNode(Scene* scene, Entity* parent, cgltf_node* node, Array<MeshAsset*>& meshes, cgltf_data* data)
	{
		if (!node)
		{
			return;
		}

		Entity* entity = Alloc<Entity>();
		entity->SetParent(parent);
		entity->SetUUID(UUID::RandomUUID());
		entity->SetName(node->name ? node->name : "Node");

		Transform transform;

		if (node->has_matrix)
		{
			Mat4 matrix(
				Vec4{node->matrix[0], node->matrix[1], node->matrix[2], node->matrix[3]},
				Vec4{node->matrix[4], node->matrix[5], node->matrix[6], node->matrix[7]},
				Vec4{node->matrix[8], node->matrix[9], node->matrix[10], node->matrix[11]},
				Vec4{node->matrix[12], node->matrix[13], node->matrix[14], node->matrix[15]}
			);

			transform.position = Math::GetTranslation(matrix);
			transform.rotation = Math::GetQuaternion(matrix);
			transform.scale = Math::GetScale(matrix);
		}
		else
		{
			if (node->has_translation)
			{
				transform.position = ToVec3(node->translation);
			}

			if (node->has_rotation)
			{
				transform.rotation = Quat{node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
			}

			if (node->has_scale)
			{
				transform.scale = ToVec3(node->scale);
			}
		}

		entity->SetTransform(transform);

		if (node->mesh)
		{
			size_t meshIndex = node->mesh - data->meshes;
			if (meshIndex < meshes.Size())
			{
				MeshRenderComponent* renderComponent = entity->AddComponent<MeshRenderComponent>();
				renderComponent->SetMesh(meshes[meshIndex]);
			}
		}

		for (size_t i = 0; i < node->children_count; i++)
		{
			ProcessNode(scene, entity, node->children[i], meshes, data);
		}
	}

	struct GLTFImporter : AssetImporter
	{
		SK_CLASS(GLTFImporter, AssetImporter);

		Array<String> ImportExtensions() override
		{
			return {".gltf", ".glb"};
		}

		Array<String> AssociatedExtensions() override
		{
			return {".bin"};
		}

		bool ImportAsset(AssetFile* assetFile, StringView path) override
		{
			cgltf_options options = {};
			cgltf_data*   data = nullptr;
			cgltf_result  result = cgltf_parse_file(&options, path.Data(), &data);

			if (result != cgltf_result_success)
			{
				logger.Error("Error on import file {} ", path);
				assetFile->SetStatus(AssetStatus::Error);
				return false;
			}

			for (cgltf_size i = 0; i < data->buffers_count; ++i)
			{
				if (data->buffers[i].data)
				{
					continue;
				}

				const char* uri = data->buffers[i].uri;

				if (uri == nullptr)
				{
					continue;
				}

				if (strncmp(uri, "data:", 5) == 0)
				{
					continue;
				}

				String bufferPath = Path::Join(Path::Parent(path), uri);

				if (!FileSystem::GetFileStatus(bufferPath).exists)
				{
					logger.Error("buffer file not found {} ", uri);
					assetFile->AddMissingFile(uri);
					assetFile->SetStatus(AssetStatus::Error);
					return false;
				}
				assetFile->AddAssociatedFile(uri);
			}

			result = cgltf_load_buffers(&options, data, path.Data());
			if (result != cgltf_result_success)
			{
				cgltf_free(data);
				return false;
			}

			// Get the scene instance from the asset file
			Scene* scene = assetFile->GetInstance()->SafeCast<Scene>();
			if (!scene)
			{
				cgltf_free(data);
				return false;
			}

			String basePath = Path::Parent(path);
			String name = Path::Name(path);

			Array<TextureAsset*> textures;
			textures.Reserve(data->images_count);

			for (size_t i = 0; i < data->images_count; i++)
			{
				if (TextureAsset* texture = ProcessTexture(assetFile, &options, &data->images[i], basePath))
				{
					textures.PushBack(texture);
				}
			}

			Array<MaterialAsset*> materials;
			materials.Reserve(data->materials_count);

			for (size_t i = 0; i < data->materials_count; i++)
			{
				MaterialAsset* material = ProcessMaterial(assetFile, &data->materials[i], textures, data);
				materials.PushBack(material);
			}

			Array<MeshAsset*> meshes;
			meshes.Reserve(data->meshes_count);

			for (size_t i = 0; i < data->meshes_count; i++)
			{
				MeshAsset* mesh = ProcessMesh(assetFile, &data->meshes[i], materials, data);
				meshes.PushBack(mesh);
			}

			Entity* root = Alloc<Entity>();
			scene->SetRootEntity(root);
			root->SetName(name);
			root->SetUUID(UUID::RandomUUID());

			if (data->scenes_count > 0)
			{
				cgltf_scene* defaultScene = data->scene ? data->scene : &data->scenes[0];
				for (size_t i = 0; i < defaultScene->nodes_count; i++)
				{
					ProcessNode(scene, root, defaultScene->nodes[i], meshes, data);
				}
			}
			else
			{
				for (size_t i = 0; i < data->nodes_count; i++)
				{
					if (data->nodes[i].parent == nullptr)
					{
						ProcessNode(scene, root, &data->nodes[i], meshes, data);
					}
				}
			}

			cgltf_free(data);

			// Mark the asset file as dirty to save changes
			assetFile->MarkDirty();

			return true;
		}

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<Scene>::ID();
		}
	};

	void RegisterGLTFImporter()
	{
		Reflection::Type<GLTFImporter>();
	}
}
