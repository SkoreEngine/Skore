
#include "Skore/Resource/Importers/MeshImporter.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

#include "Skore/Resource/Importers/TextureImporter.hpp"
#include "tiny_obj_loader.h"
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"

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

			Allocator* heapAllocator = MemoryGlobals::GetHeapAllocator();

			Array<u32> tempIndices{heapAllocator};
			Array<MeshStaticVertex> tempVertices{heapAllocator};


			auto &attrib = reader.GetAttrib();
			auto &shapes = reader.GetShapes();
			auto &materials = reader.GetMaterials();


			Array<RID> ridMaterials;
			ridMaterials.Resize(materials.size());

			Array<RID> allTextures;
			Array<RID> allMeshes;

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
				settings.async = ImportChildAssetsAsync;
				settings.isSubAsset = true;
				if (RID texture = ImportTexture(directory, settings, textureAbsolutePath, scope))
				{
					textureCache.Insert(texName, texture);
					allTextures.EmplaceBack(texture);
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

				HashMap<u32, Array<MeshStaticVertex>>  verticesByMaterial;

				size_t indexOffset = 0;

				for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
				{
					u32 mat = shape.mesh.material_ids[f];
					auto it = verticesByMaterial.Find(mat);
					if (it == verticesByMaterial.end())
					{
						it = verticesByMaterial.Insert(mat, Array<MeshStaticVertex>{heapAllocator}).first;
					}

					Array<MeshStaticVertex>& vertices = it->second;

					size_t fv = size_t(shape.mesh.num_face_vertices[f]);

					for (size_t v = 0; v < fv; v++)
					{
						// access to vertex
						tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
						MeshStaticVertex& vertex = vertices.EmplaceBack();

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
							vertex.uv.x = attrib.texcoords[2 * std::size_t(idx.texcoord_index) + 0];
							vertex.uv.y = 1.0f - attrib.texcoords[2 * std::size_t(idx.texcoord_index) + 1];
						}
					}
					indexOffset += fv;
				}

				logger.Debug("processing mesh {} ", name);

				Array<RID> meshMaterials;
				Array<MeshStaticVertex> allVertices = {heapAllocator};
				Array<u32> allIndices = {heapAllocator};

				Array<MeshPrimitive> primitives = {heapAllocator};

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

					primitives.EmplaceBack(MeshPrimitive{
						.firstIndex = 0,
						.indexCount = static_cast<u32>(tempIndices.Size()),
						.materialIndex = materialIndex
					});

					allVertices.Assign(tempVertices.begin(), tempVertices.end());
					allIndices.Assign(tempIndices.begin(), tempIndices.end());
				}


				// Convert to SoA for ImportMesh
				u64 vCount = allVertices.Size();
				Array<Vec3> soaPositions(vCount);
				Array<Vec3> soaNormals(vCount);
				Array<Vec2> soaUVs(vCount);
				Array<Vec3> soaColors(vCount);
				for (u64 vi = 0; vi < vCount; vi++)
				{
					soaPositions[vi] = allVertices[vi].position;
					soaNormals[vi] = allVertices[vi].normal;
					soaUVs[vi] = allVertices[vi].uv;
					soaColors[vi] = allVertices[vi].color;
				}

				MeshVertexImportData importData;
				importData.positions = soaPositions;
				importData.normals = soaNormals;
				importData.uv = soaUVs;
				importData.colors = soaColors;

				MeshImportSettings settings = {};
				settings.regenerateNormals = missingNormals;
				settings.recalculateTangents = true;
				RID meshResource = ImportMesh(directory, settings, name, meshMaterials, primitives, importData, allIndices, {}, Vec3(1.0),  scope);
				allMeshes.EmplaceBack(meshResource);

				RID entity = Resources::Create<EntityResource>(UUID::RandomUUID());

				ResourceObject entityObject = Resources::Write(entity);
				entityObject.SetString(EntityResource::Name, name);

				entityObject.AddToSubObjectList(EntityResource::Components, Resources::Create<Transform>(UUID::RandomUUID()));

				RID meshRenderer = Resources::Create<StaticMeshRenderer>(UUID::RandomUUID());

				ResourceObject staticMeshRenderObject = Resources::Write(meshRenderer);
				staticMeshRenderObject.SetReference(staticMeshRenderObject.GetIndex("mesh"), meshResource);
				staticMeshRenderObject.Commit(scope);

				entityObject.AddToSubObjectList(EntityResource::Components, meshRenderer);
				entityObject.Commit(scope);

				entities.EmplaceBack(entity);
			}

			RID rootEntity = Resources::Create<EntityResource>(UUID::RandomUUID(), scope);

			ResourceObject entityObject = Resources::Write(rootEntity);
			entityObject.SetString(EntityResource::Name, fileName);

			entityObject.AddToSubObjectList(EntityResource::Components, Resources::Create<Transform>(UUID::RandomUUID()));
			entityObject.AddToSubObjectList(EntityResource::Children, entities);
			entityObject.Commit(scope);

			RID dccAsset = ResourceAssets::CreateImportedAsset(directory, TypeInfo<DCCAsset>::ID(), fileName, scope, path);
			ResourceObject dccObject = Resources::Write(dccAsset);
			dccObject.SetString(DCCAsset::Name, fileName);
			dccObject.SetSubObject(DCCAsset::RootEntity, rootEntity);
			dccObject.AddToSubObjectList(DCCAsset::Meshes, allMeshes);
			dccObject.AddToSubObjectList(DCCAsset::Textures, allTextures);
			dccObject.AddToSubObjectList(DCCAsset::Materials, ridMaterials);
			dccObject.Commit(scope);

			logger.Debug("obj {} imported ", path);

			return true;
		}
	};

	void RegisterObjImporter()
	{
		Reflection::Type<ObjImporter>();
	}
}
