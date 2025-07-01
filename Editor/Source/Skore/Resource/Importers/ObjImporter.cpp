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

#include "fast_obj.h"
#include "TextureImporter.hpp"
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
			fastObjMesh* mesh = fast_obj_read(path.CStr());

			String fileName = Path::Name(path);

			RID    dccAsset = ResourceAssets::CreateImportedAsset(directory, TypeInfo<DCCAssetResource>::ID(), fileName, scope, path);

			ResourceObject dccAssetObject = Resources::Write(dccAsset);
			dccAssetObject.SetString(DCCAssetResource::Name, Path::Name(path));

			Allocator* heapAllocator = MemoryGlobals::GetHeapAllocator();

			Array<u32> tempIndices{heapAllocator};
			Array<MeshResource::Vertex> tempVertices{heapAllocator};

			Array<RID> materials;
			materials.Resize(mesh->material_count);

			Array<RID> textures;
			textures.Resize(mesh->texture_count);

			auto processTexture = [&](u32 index) -> RID
			{
				if (index == 0)
				{
					return {};
				}

				if (index >= mesh->texture_count)
				{
					return {};
				}

				if (!textures[index])
				{
					TextureImportSettings settings = {};
					textures[index] = ImportTexture(directory, settings, StringView{mesh->textures[index].path}, scope);
				}

				return textures[index];
			};


			for (u32 m = 0; m < mesh->material_count; m++)
			{
				const fastObjMaterial& mat = mesh->materials[m];

				RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), scope);

				ResourceObject materialObject = Resources::Write(materialResource);
				materialObject.SetString(MaterialResource::Name, mat.name);

				materialObject.SetColor(MaterialResource::BaseColor, Color::FromVec3(Vec3(mat.Kd[0], mat.Kd[1], mat.Kd[2])));
				materialObject.SetColor(MaterialResource::EmissiveColor, Color::FromVec3(Vec3(mat.Ke[0], mat.Ke[1], mat.Ke[2])));

				if (mat.map_Kd)
				{
					if (RID texture = processTexture(mat.map_Kd))
					{
						materialObject.SetReference(MaterialResource::BaseColorTexture, texture);
					}
				}

				if (mat.map_Ke)
				{
					if (RID texture = processTexture(mat.map_Ke))
					{
						materialObject.SetReference(MaterialResource::EmissiveTexture, texture);
					}
				}

				materialObject.Commit(scope);

				materials[m] = materialResource;
			}


			Array<RID> entities;

			for (u32 ii = 0; ii < mesh->group_count; ii++)
			{
				bool missingNormals = false;

				const fastObjGroup& grp = mesh->groups[ii];


				String name = "";

				if (mesh->group_count > 1 && grp.name)
				{
					name = String(grp.name);
				}
				else if (mesh->group_count == 1)
				{
					name = fileName;
				}

				if (name.Empty())
				{
					name = fileName + "_" + ToString(ii);
				}

				if (name != "Stone_Button")
				{
					continue;
				}

				HashMap<u32, Array<MeshResource::Vertex>>  verticesByMaterial;
				int idx = 0;

				for (u32 jj = 0; jj < grp.face_count; jj++)
				{
					u32 fv = mesh->face_vertices[grp.face_offset + jj];
					u32 mat = mesh->face_materials[grp.face_offset + jj];

					auto it = verticesByMaterial.Find(mat);
					if (it == verticesByMaterial.end())
					{
						it = verticesByMaterial.Insert(mat, Array<MeshResource::Vertex>{heapAllocator}).first;
					}

					Array<MeshResource::Vertex>& vertices = it->second;

					for (u32 kk = 0; kk < fv; kk++)
					{
						fastObjIndex mi = mesh->indices[grp.index_offset + idx];

						MeshResource::Vertex& vertex = vertices.EmplaceBack();
						vertex.color = Vec3(1, 1, 1);

						if (mi.p)
						{
							vertex.position.x = mesh->positions[3 * mi.p + 0];
							vertex.position.y = mesh->positions[3 * mi.p + 1];
							vertex.position.z = mesh->positions[3 * mi.p + 2];
						}

						if (mi.t)
						{
							vertex.texCoord.x = mesh->texcoords[2 * mi.t + 0];
							vertex.texCoord.y = mesh->texcoords[2 * mi.t + 1];
						}

						if (mi.n)
						{
							vertex.normal.x = mesh->normals[3 * mi.n + 0];
							vertex.normal.y = mesh->normals[3 * mi.n + 1];
							vertex.normal.z = mesh->normals[3 * mi.n + 2];
						}
						else
						{
							missingNormals = true;
						}
						idx++;

						logger.Debug("vertex {}:  {}, {}, {} ", vertices.Size(), vertex.position.x, vertex.position.y, vertex.position.z);
					}
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

					if (it.first < materials.Size())
					{
						materialIndex = meshMaterials.Size();
						meshMaterials.EmplaceBack(materials[it.first]);
					}
					else
					{
						materialIndex = 0;
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

			fast_obj_destroy(mesh);

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

			if (!materials.Empty())
			{
				dccAssetObject.AddToSubObjectSet(DCCAssetResource::Materials, materials);
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
