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


#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

#include "TextureImporter.hpp"
#include "tiny_obj_loader.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Components/MeshRenderer.hpp"

namespace Skore
{

	static Logger& logger = Logger::GetLogger("Skore::ObjImporter");

	struct ObjImporter : ResourceAssetImporter
	{
		SK_CLASS(ObjImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".obj"};
		}

		bool ImportAsset(RID directory, ConstPtr settings, StringView path, UndoRedoScope* scope) override
		{
			tinyobj::ObjReaderConfig readerConfig{};
			String parent = Path::Parent(path);
			readerConfig.mtl_search_path = parent.CStr();

			tinyobj::ObjReader reader{};
			if (!reader.ParseFromFile(std::string{path.CStr(), path.Size()}, readerConfig))
			{
				if (!reader.Error().empty())
				{
					logger.Error("tinyobj {}", reader.Error().c_str());
				}
				return false;
			}

			if (!reader.Warning().empty())
			{
				logger.Warn("tinyobj {}", reader.Warning().c_str());
			}

			String fileName = Path::Name(path);

			RID    dccAsset = ResourceAssets::CreateImportedAsset(directory, TypeInfo<DCCAssetResource>::ID(), fileName, scope, path);

			ResourceObject dccAssetObject = Resources::Write(dccAsset);
			dccAssetObject.SetString(DCCAssetResource::Name, Path::Name(path));

			Allocator* heapAllocator = MemoryGlobals::GetHeapAllocator();

			Array<u32> tempIndices{heapAllocator};
			Array<MeshResource::Vertex> tempVertices{heapAllocator};


			auto &attrib = reader.GetAttrib();
			auto &shapes = reader.GetShapes();
			auto &materials = reader.GetMaterials();


			Array<RID> ridMaterials;
			ridMaterials.Resize(materials.size());

			Array<RID> textures;

			HashMap<String, RID> textureCache;

			auto processTexture = [&](const std::string& diffuseTexName) -> RID
			{
				if (diffuseTexName.empty())
				{
					return {};
				}

				String texName = diffuseTexName.c_str();

				auto it = textureCache.Find(texName);
				if (it != textureCache.end())
				{
					return it->second;
				}

				String textureAbsolutePath = Path::Join(parent, diffuseTexName.c_str());

				TextureImportSettings settings = {};
				if (RID texture = ImportTexture(directory, settings, textureAbsolutePath, scope))
				{
					textureCache.Insert(texName, texture);
					return texture;
				}
				return {};
			};

			for (int m = 0; m < materials.size(); ++m)
			{
				const tinyobj::material_t& material = materials[m];
				String materialName = !material.name.empty() ? material.name.c_str() : String{"Material_"}.Append(m);

				RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), scope);

				ResourceObject materialObject = Resources::Write(materialResource);
				materialObject.SetString(MaterialResource::Name, materialName);

				materialObject.SetColor(MaterialResource::BaseColor, Color::FromVec3(material.diffuse));
				materialObject.SetColor(MaterialResource::EmissiveColor, Color::FromVec3(material.emission));

				materialObject.SetReference(MaterialResource::EmissiveTexture, processTexture(material.emissive_texname));
				materialObject.SetReference(MaterialResource::NormalTexture, processTexture(material.normal_texname));
				materialObject.SetReference(MaterialResource::BaseColorTexture, processTexture(material.diffuse_texname));


				materialObject.Commit(scope);

				ridMaterials[m] = materialResource;
			}


			// for (u32 m = 0; m < mesh->material_count; m++)
			// {
			// 	const fastObjMaterial& mat = mesh->materials[m];
			//
			// 	RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), scope);
			//
			// 	ResourceObject materialObject = Resources::Write(materialResource);
			// 	materialObject.SetString(MaterialResource::Name, mat.name);
			//
			// 	materialObject.SetColor(MaterialResource::BaseColor, Color::FromVec3(Vec3(mat.Kd[0], mat.Kd[1], mat.Kd[2])));
			// 	materialObject.SetColor(MaterialResource::EmissiveColor, Color::FromVec3(Vec3(mat.Ke[0], mat.Ke[1], mat.Ke[2])));
			//
			// 	if (mat.map_Kd)
			// 	{
			// 		if (RID texture = processTexture(mat.map_Kd))
			// 		{
			// 			materialObject.SetReference(MaterialResource::BaseColorTexture, texture);
			// 		}
			// 	}
			//
			// 	if (mat.map_Ke)
			// 	{
			// 		if (RID texture = processTexture(mat.map_Ke))
			// 		{
			// 			materialObject.SetReference(MaterialResource::EmissiveTexture, texture);
			// 		}
			// 	}
			//
			// 	materialObject.Commit(scope);
			//
			// 	ridMaterials[m] = materialResource;
			// }


			Array<RID> entities;

			for (auto i = 0; i < shapes.size(); ++i)
			{
				bool missingNormals = false;
				auto& shape = shapes[i];

				String name = "";

				if (shapes.size() > 1 && !shape.name.empty())
				{
					name = String(shape.name.c_str());
				}
				else if (shapes.size() == 1)
				{
					name = fileName;
				}

				if (name.Empty())
				{
					name = fileName + "_" + ToString(i);
				}

				HashMap<u32, Array<MeshResource::Vertex>>  verticesByMaterial;

				size_t indexOffset = 0;

				for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
				{
					u32 mat = shape.mesh.material_ids[f];
					auto it = verticesByMaterial.Find(mat);
					if (it == verticesByMaterial.end())
					{
						it = verticesByMaterial.Insert(mat, Array<MeshResource::Vertex>{heapAllocator}).first;
					}

					Array<MeshResource::Vertex>& vertices = it->second;

					size_t fv = size_t(shape.mesh.num_face_vertices[f]);

					for (size_t v = 0; v < fv; v++)
					{
						// access to vertex
						tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
						MeshResource::Vertex& vertex = vertices.EmplaceBack();

						vertex.position.x = attrib.vertices[3 * std::size_t(idx.vertex_index) + 0];
						vertex.position.y = attrib.vertices[3 * std::size_t(idx.vertex_index) + 1];
						vertex.position.z = attrib.vertices[3 * std::size_t(idx.vertex_index) + 2];

						vertex.color.x = attrib.colors[3 * size_t(idx.vertex_index) + 0];
						vertex.color.y = attrib.colors[3 * size_t(idx.vertex_index) + 1];
						vertex.color.z = attrib.colors[3 * size_t(idx.vertex_index) + 2];

						if (idx.normal_index >= 0)
						{
							vertex.normal.x = attrib.normals[3 * std::size_t(idx.normal_index) + 0];
							vertex.normal.y = attrib.normals[3 * std::size_t(idx.normal_index) + 1];
							vertex.normal.z = attrib.normals[3 * std::size_t(idx.normal_index) + 2];
						}
						else
						{
							missingNormals = true;
							vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
						}

						if (idx.texcoord_index >= 0)
						{
							vertex.texCoord.x = attrib.texcoords[2 * std::size_t(idx.texcoord_index) + 0];
							vertex.texCoord.y = 1.0f - attrib.texcoords[2 * std::size_t(idx.texcoord_index) + 1];
						}
					}
					indexOffset += fv;
				}

				logger.Debug("processing mesh {} ", name);

				Array<RID> meshMaterials;
				Array<MeshResource::Vertex> allVertices = {heapAllocator};
				Array<u32> allIndices = {heapAllocator};

				Array<MeshResource::Primitive> primitives = {heapAllocator};

				for (auto& it : verticesByMaterial)
				{
					tempIndices.Clear();
					tempVertices.Clear();

					u64 reduced = MeshTools::GenerateIndices(it.second, tempIndices, tempVertices);
					logger.Debug("reduced {} from mesh {} ", reduced, name);

					u32 materialIndex = 0;

					if (it.first < ridMaterials.Size())
					{
						materialIndex = meshMaterials.Size();
						meshMaterials.EmplaceBack(ridMaterials[it.first]);
					}

					primitives.EmplaceBack(MeshResource::Primitive{
						.firstIndex = 0,
						.indexCount = static_cast<u32>(tempIndices.Size()),
						.materialIndex = materialIndex
					});

					allVertices.Assign(tempVertices.begin(), tempVertices.end());
					allIndices.Assign(tempIndices.begin(), tempIndices.end());
				}

				if (missingNormals)
				{
					MeshTools::CalcNormals(allVertices, allIndices);
				}

				MeshTools::CalcTangents(allVertices, allIndices, true);

				RID meshResource = Resources::Create<MeshResource>(UUID::RandomUUID(), scope);

				ResourceObject meshObject = Resources::Write(meshResource);
				meshObject.SetString(MeshResource::Name, name);
				meshObject.SetReferenceArray(MeshResource::Materials, meshMaterials);
				meshObject.SetBlob(MeshResource::Vertices, Span(reinterpret_cast<u8*>(allVertices.Data()), allVertices.Size() * sizeof(MeshResource::Vertex)));
				meshObject.SetBlob(MeshResource::Indices, Span(reinterpret_cast<u8*>(allIndices.Data()), allIndices.Size() * sizeof(u32)));
				meshObject.SetBlob(MeshResource::Primitives, Span(reinterpret_cast<u8*>(primitives.Data()), primitives.Size() * sizeof(MeshResource::Primitive)));
				meshObject.Commit(scope);

				dccAssetObject.AddToSubObjectSet(DCCAssetResource::Meshes, meshResource);

				RID entity = Resources::Create<EntityResource>(UUID::RandomUUID());

				ResourceObject entityObject = Resources::Write(entity);
				entityObject.SetString(EntityResource::Name, name);

				Transform transform = {};
				RID transformRID = Resources::Create<Transform>(UUID::RandomUUID());
				Resources::ToResource(transformRID, &transform, scope);
				entityObject.SetSubObject(EntityResource::Transform, transformRID);

				RID meshRenderer = Resources::Create<MeshRenderer>(UUID::RandomUUID());

				ResourceObject staticMeshRenderObject = Resources::Write(meshRenderer);
				staticMeshRenderObject.SetReference(staticMeshRenderObject.GetIndex("mesh"), meshResource);
				staticMeshRenderObject.Commit(scope);

				entityObject.AddToSubObjectSet(EntityResource::Components, meshRenderer);
				entityObject.Commit(scope);

				entities.EmplaceBack(entity);
			}

			if (entities.Size() == 1)
			{
				dccAssetObject.SetSubObject(DCCAssetResource::Entity, entities[0]);
			}
			else
			{
				RID entity = Resources::Create<EntityResource>(UUID::RandomUUID());

				ResourceObject entityObject = Resources::Write(entity);
				entityObject.SetString(EntityResource::Name, fileName);

				Transform transform = {};
				RID transformRID = Resources::Create<Transform>(UUID::RandomUUID());
				Resources::ToResource(transformRID, &transform, scope);
				entityObject.SetSubObject(EntityResource::Transform, transformRID);
				entityObject.AddToSubObjectSet(EntityResource::Children, entities);
				entityObject.Commit(scope);

				dccAssetObject.SetSubObject(DCCAssetResource::Entity, entity);
			}

			if (!ridMaterials.Empty())
			{
				dccAssetObject.AddToSubObjectSet(DCCAssetResource::Materials, ridMaterials);
			}

			dccAssetObject.Commit(scope);

			logger.Debug("obj {} imported ", path);

			return true;
		}
	};

	void RegisterObjImporter()
	{
		Reflection::Type<ObjImporter>();
	}
}
