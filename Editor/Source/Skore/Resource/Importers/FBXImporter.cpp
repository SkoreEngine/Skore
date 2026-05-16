#include "Skore/Resource/Importers/FBXimporter.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

#include <ufbx.h>

#include "Skore/Resource/Importers/MeshImporter.hpp"
#include "Skore/Resource/Importers/TextureImporter.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/RenderTools.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"

namespace Skore
{
	struct FBXImportData
	{
		String                       basePath;
		RID                          directory;
		ufbx_scene*                  scene = nullptr;
		UndoRedoScope*               scope = nullptr;
		FBXImportSettings            settings;
		HashMap<ufbx_texture*, RID>  textures;
		HashMap<ufbx_material*, RID> materials;
		HashMap<ufbx_mesh*, RID>     meshes;
		HashMap<ufbx_node*, RID>     entities;

		HashMap<ufbx_mesh*, RID> meshSkeleton;  //RID = entity
		HashMap<ufbx_node*, RID> nodeSkeletons; //RID = entity
		HashMap<ufbx_mesh*, RID> skins;         //RID = SkinResource

		HashMap<ufbx_skin_deformer*, RID> deformers;
		Array<RID> animations;
	};

	RID ProcessNode(FBXImportData& fbxData, ufbx_node* node, StringView name, RID entity = {});
	void ProcessSkeleton(FBXImportData& fbxData, ufbx_mesh* mesh);

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

	Quat ToQuat(const ufbx_quat& quat)
	{
		return Quat{quat.x, quat.y, quat.z, quat.w};
	}

	Mat4 ToMat4(const ufbx_matrix& m)
	{
		return {
			Vec4(m.cols[0].x, m.cols[0].y, m.cols[0].z, 0),
			Vec4(m.cols[1].x, m.cols[1].y, m.cols[1].z, 0),
			Vec4(m.cols[2].x, m.cols[2].y, m.cols[2].z, 0),
			Vec4(m.cols[3].x, m.cols[3].y, m.cols[3].z, 1)
		};
	}

	StringView GetStr(const ufbx_string& str, StringView defaultValue)
	{
		if (!IsStrNullOrEmpty(str.data))
		{
			return StringView{str.data, str.length};
		}
		return defaultValue;
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

	bool ProcessTextures(RID directory, FBXImportData& fbxData, StringView basePath, ufbx_texture* texture)
	{
		if (!texture)
		{
			return false;
		}

		RID textureRID = {};

		TextureImportSettings textureImportSettings;
		textureImportSettings.filterMode = FilterMode::Linear;
		textureImportSettings.wrapMode = texture->wrap_u == UFBX_WRAP_CLAMP ? AddressMode::ClampToBorder : AddressMode::Repeat;
		textureImportSettings.async = ImportChildAssetsAsync;
		textureImportSettings.isSubAsset = true;

		if (texture->content.data != nullptr && texture->content.size > 0)
		{
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
		}

		return true;
	}


	void ProcessMaterials(FBXImportData& fbxData, ufbx_material* material)
	{
		String materialName = !IsStrNullOrEmpty(material->name.data) ? material->name.data : "Material";
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

		fbxData.materials.Insert(material, materialResource);
	}

	void ProcessMesh(FBXImportData& fbxData, ufbx_mesh* mesh, StringView name)
	{
		if (!mesh) return;

		bool skinned = mesh->skin_deformers.count > 0;

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

		ProcessSkeleton(fbxData, mesh);

		Array<RID> meshMaterials;

		//temp buffers
		size_t     numTriIndices = mesh->max_face_triangles * 3;
		Array<u32> triIndices = {heapAllocator, numTriIndices};

		Array<Vec3> tempPositions = {heapAllocator, maxTriangles * 3};
		Array<Vec3> tempNormals = {heapAllocator, maxTriangles * 3};
		Array<Vec2> tempUVs = {heapAllocator, maxTriangles * 3};
		Array<Vec3> tempColors = {heapAllocator, maxTriangles * 3};
		Array<BoneInfluence> tempBones;
		if (skinned)
		{
			tempBones = {heapAllocator, maxTriangles * 3};
		}
		Array<u32> tempIndices = {heapAllocator, maxTriangles * 3};

		Array<Vec3> allPositions = {heapAllocator, totalTriangles * 3};
		Array<Vec3> allNormals = {heapAllocator, totalTriangles * 3};
		Array<Vec2> allUVs = {heapAllocator, totalTriangles * 3};
		Array<Vec3> allColors = {heapAllocator, totalTriangles * 3};
		Array<BoneInfluence> allBones;
		if (skinned)
		{
			allBones = {heapAllocator, totalTriangles * 3};
		}
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

				for (size_t vi = 0; vi < numTris * 3; vi++)
				{
					u32 ix = triIndices[vi];

					ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
					ufbx_vec3 normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
					ufbx_vec4 color = mesh->vertex_color.exists ? ufbx_get_vertex_vec4(&mesh->vertex_color, ix) : defaultColor;
					ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : defaultUV;

					uv.y = 1.0f - uv.y; //Flip Y for UV

					tempPositions[numIndices] = ToVec3(pos);
					tempNormals[numIndices] = Vec3::Normalize(ToVec3(normal));
					tempUVs[numIndices] = ToVec2(uv);
					tempColors[numIndices] = Vec3(ToVec4(color));

					if (skinned && mesh->skin_deformers.count > 0)
					{
						BoneInfluence& bone = tempBones[numIndices];
						bone = {};

						ufbx_skin_deformer* skin = mesh->skin_deformers[0];
						ufbx_skin_vertex vertex_weights = skin->vertices.data[mesh->vertex_indices.data[ix]];

						size_t numWeights = 0;

						for (size_t wi = 0; wi < vertex_weights.num_weights; wi++)
						{
							if (numWeights >= 4) break;

							ufbx_skin_weight weight = skin->weights.data[vertex_weights.weight_begin + wi];

							bone.indices[wi] = weight.cluster_index;
							bone.weights[wi] = weight.weight;

							numWeights++;
						}
					}
					numIndices++;
				}
			}

			// Use multiple streams for ufbx_generate_indices so it deduplicates across all attributes
			ufbx_vertex_stream streams[5];
			size_t numStreams = 0;

			streams[numStreams].data = tempPositions.Data();
			streams[numStreams].vertex_count = numIndices;
			streams[numStreams].vertex_size = sizeof(Vec3);
			numStreams++;

			streams[numStreams].data = tempNormals.Data();
			streams[numStreams].vertex_count = numIndices;
			streams[numStreams].vertex_size = sizeof(Vec3);
			numStreams++;

			streams[numStreams].data = tempUVs.Data();
			streams[numStreams].vertex_count = numIndices;
			streams[numStreams].vertex_size = sizeof(Vec2);
			numStreams++;

			streams[numStreams].data = tempColors.Data();
			streams[numStreams].vertex_count = numIndices;
			streams[numStreams].vertex_size = sizeof(Vec3);
			numStreams++;

			if (skinned)
			{
				streams[numStreams].data = tempBones.Data();
				streams[numStreams].vertex_count = numIndices;
				streams[numStreams].vertex_size = sizeof(BoneInfluence);
				numStreams++;
			}

			ufbx_error error;

			usize numVertices = ufbx_generate_indices(streams, numStreams, tempIndices.Data(), numIndices, NULL, &error);
			if (error.type != UFBX_ERROR_NONE)
			{
				logger.Error("Failed to generate index buffer");
				return;
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

			memcpy(allPositions.Data() + totalVerticesCount, tempPositions.Data(), sizeof(Vec3) * numVertices);
			memcpy(allNormals.Data() + totalVerticesCount, tempNormals.Data(), sizeof(Vec3) * numVertices);
			memcpy(allUVs.Data() + totalVerticesCount, tempUVs.Data(), sizeof(Vec2) * numVertices);
			memcpy(allColors.Data() + totalVerticesCount, tempColors.Data(), sizeof(Vec3) * numVertices);
			if (skinned)
			{
				memcpy(allBones.Data() + totalVerticesCount, tempBones.Data(), sizeof(BoneInfluence) * numVertices);
			}
			memcpy(allIndices.Data() + totalIndicesCount, tempIndices.Data(), sizeof(u32) * numIndices);

			totalVerticesCount += numVertices;
			totalIndicesCount += numIndices;
		}

		allPositions.Resize(totalVerticesCount);
		allNormals.Resize(totalVerticesCount);
		allUVs.Resize(totalVerticesCount);
		allColors.Resize(totalVerticesCount);
		if (skinned)
		{
			allBones.Resize(totalVerticesCount);
		}
		allIndices.Resize(totalIndicesCount);

		MeshImportSettings meshImportSettings = {};
		meshImportSettings.generateUV1s = true;

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

		MeshVertexImportData importData;
		importData.positions = allPositions;
		importData.normals = allNormals;
		importData.uv = allUVs;
		importData.colors = allColors;
		if (skinned)
		{
			importData.bones = allBones;
		}

		RID skin = {};
		if (auto itSkin = fbxData.skins.Find(mesh))
		{
			skin = itSkin->second;
		}

		RID meshRID = ImportMesh(fbxData.directory, meshImportSettings, meshName, meshMaterials, primitives, importData, allIndices, skin, Vec3(1.0), fbxData.scope);
		fbxData.meshes.Insert(mesh, meshRID);
	}

	RID ReadAnimationNode(FBXImportData& fbxData, u32 numFrames, f32 framerate, ufbx_anim_stack* stack, ufbx_node* node, FileHandler handler, u64& offset)
	{
		StringView nodeName = "Bone";
		if (!IsStrNullOrEmpty(node->name.data))
		{
			nodeName = node->name.data;
		}

		RID animationChannel = Resources::Create<AnimationChannelResource>(UUID::RandomUUID());
		ResourceObject animationChannelObject = Resources::Write(animationChannel);
		animationChannelObject.SetString(AnimationChannelResource::Name, nodeName);
		animationChannelObject.SetUInt(AnimationChannelResource::BufferOffset, offset);

		offset += sizeof(AnimationKeyFrame) * numFrames;

		for (size_t i = 0; i < numFrames; ++i)
		{
			f64 time = stack->time_begin + static_cast<f64>(i) / framerate;

			ufbx_transform transform = ufbx_evaluate_transform(stack->anim, node, time);

			Vec3 position = ToVec3(transform.translation);
			Quat rotation = ToQuat(transform.rotation);
			Vec3 scale = ToVec3(transform.scale);

			AnimationKeyFrame keyFrame;
			keyFrame.time = time;
			keyFrame.position = position;
			keyFrame.rotation = rotation;
			keyFrame.scale = scale;

			FileSystem::WriteFile(handler, &keyFrame, sizeof(AnimationKeyFrame));
		}

		animationChannelObject.Commit(fbxData.scope);

		return animationChannel;
	}

	void ProcessAnimation(FBXImportData& fbxData, ufbx_anim_stack* stack)
	{
		String animationName;

		// Mixamo FBX files always name their anim stack "mixamo.com", which is not useful.
		// When there's only one animation, use the filename instead (like we do for the root entity).
		if (fbxData.scene->anim_stacks.count == 1)
		{
			animationName = Path::Name(fbxData.basePath);
		}
		else if (!IsStrNullOrEmpty(stack->name.data))
		{
			animationName = stack->name.data;
		}
		else
		{
			animationName = "Animation";
		}
		RID animation = Resources::Create<AnimationClipResource>(UUID::RandomUUID(), fbxData.scope);

		ResourceObject animationObject = Resources::Write(animation);
		animationObject.SetString(AnimationClipResource::Name, animationName);

		//fbxData.scene->settings.frames_per_second ?

		const f32 targetFramerate = 30.0f;
		const u32 maxFrames = 4096;

		// Sample the animation evenly at `target_framerate` if possible while limiting the maximum
		// number of frames to `max_frames` by potentially dropping FPS.
		f32 duration = static_cast<f32>(stack->time_end) - static_cast<f32>(stack->time_begin);
		u32 numFrames = Math::Clamp(static_cast<u32>(duration * targetFramerate), 2u, maxFrames);
		f32 framerate = static_cast<f32>(numFrames - 1) / duration;

		animationObject.SetUInt(AnimationClipResource::NumFrames, numFrames);
		animationObject.SetFloat(AnimationClipResource::Duration, duration);
		animationObject.SetFloat(AnimationClipResource::FrameRate, framerate);
		animationObject.SetFloat(AnimationClipResource::TimeBegin, stack->time_begin);
		animationObject.SetFloat(AnimationClipResource::TimeEnd, stack->time_end);

		ResourceBuffer buffer = ResourceAssets::CreateTempBuffer();
		FileHandler handler = buffer.OpenFile(AccessMode::ReadAndWrite);
		u64 offset = 0;

		for (u32 i = 0; i < fbxData.scene->nodes.count; i++)
		{
			ufbx_node* node = fbxData.scene->nodes.data[i];
			if (!node->bone) continue;

			if (RID channel = ReadAnimationNode(fbxData, numFrames, framerate, stack, node, handler, offset))
			{
				animationObject.AddToSubObjectList(AnimationClipResource::Channels, channel);
			}
		}

		FileSystem::CloseFile(handler);
		animationObject.SetBuffer(AnimationClipResource::KeyFramesBuffer, buffer);

		animationObject.Commit(fbxData.scope);

		fbxData.animations.EmplaceBack(animation);
	}

	void ProcessSkeleton(FBXImportData& fbxData, ufbx_mesh* mesh)
	{
		if (mesh->skin_deformers.count == 0)
		{
			return;
		}

		ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];

		if (!fbxData.deformers.Has(skin))
		{
			String skinName = GetStr(skin->name, "Skin");

			RID skinRID = Resources::Create<SkinResource>(UUID::RandomUUID(), fbxData.scope);

			ResourceObject skinObject = Resources::Write(skinRID);
			skinObject.SetString(SkinResource::Name, skinName);

			for (u64 i = 0; i < skin->clusters.count; ++i)
			{
				ufbx_skin_cluster* cluster = skin->clusters[i];

				RID skinBind = Resources::Create<SkinBindResource>(UUID::RandomUUID());
				ResourceObject skinBindObject = Resources::Write(skinBind);
				skinBindObject.SetMat4(SkinBindResource::Pose, ToMat4(cluster->geometry_to_bone));
				skinBindObject.Commit(fbxData.scope);

				skinObject.AddToSubObjectList(SkinResource::Binds, skinBind);
			}

			skinObject.Commit(fbxData.scope);

			fbxData.skins.Insert(mesh, skinRID);
			fbxData.deformers.Insert(skin, skinRID);
		}

		auto it = fbxData.nodeSkeletons.Find(skin->clusters[0]->bone_node);
		if (it == fbxData.nodeSkeletons.end())
		{
			RID            skeletonComponent = Resources::Create<Skeleton>(UUID::RandomUUID());
			ResourceObject skeletonComponentObject = Resources::Write(skeletonComponent);

			Array<RID>               bones;
			HashMap<ufbx_node*, u32> indicesRemap;


			for (u64 i = 0; i < skin->clusters.count; ++i)
			{
				ufbx_skin_cluster* cluster = skin->clusters[i];
				ufbx_node*         bone_node = cluster->bone_node;
				indicesRemap[bone_node] = i;

				u32 parentIndex = U32_MAX;

				if (bone_node->parent)
				{
					if (auto itIndex = indicesRemap.Find(bone_node->parent))
					{
						parentIndex = itIndex->second;
					}
				}

				RID boneNodeRID = Resources::Create<BoneNode>(UUID::RandomUUID());

				ResourceObject boneObject = Resources::Write(boneNodeRID);
				boneObject.SetString(boneObject.GetIndex("name"), !IsStrNullOrEmpty(bone_node->name.data) ? bone_node->name.data : "Bone");
				boneObject.SetUInt(boneObject.GetIndex("parentIndex"), parentIndex);
				boneObject.SetVec3(boneObject.GetIndex("position"), ToVec3(bone_node->local_transform.translation));
				boneObject.SetQuat(boneObject.GetIndex("rotation"), ToQuat(bone_node->local_transform.rotation));
				boneObject.SetVec3(boneObject.GetIndex("scale"), ToVec3(bone_node->local_transform.scale));
				boneObject.Commit(fbxData.scope);

				bones.EmplaceBack(boneNodeRID);
			}

			skeletonComponentObject.AddToSubObjectList(skeletonComponentObject.GetIndex("bones"), bones);
			skeletonComponentObject.Commit(fbxData.scope);

			RID skeletonEntity = Resources::Create<EntityResource>(UUID::RandomUUID());

			ResourceObject skeletonObject = Resources::Write(skeletonEntity);
			skeletonObject.SetString(EntityResource::Name, "Skeleton"); //need a name here?
			skeletonObject.AddToSubObjectList(EntityResource::Components, skeletonComponent);
			skeletonObject.Commit(fbxData.scope);

			it = fbxData.nodeSkeletons.Insert(skin->clusters[0]->bone_node, skeletonEntity).first;
		}
		fbxData.meshSkeleton.Insert(mesh, it->second);
	}

	RID ProcessNode(FBXImportData& fbxData, ufbx_node* node, StringView name, RID entity)
	{
		if (!node)
		{
			return {};
		}

		if (auto it = fbxData.entities.Find(node))
		{
			return it->second;
		}

		if (node->bone != nullptr)
		{
			return {};
		}

		//ignore camera or lights
		if ((node->camera || node->light) && node->children.count == 0)
		{
			return {};
		}

		bool newEntity = !entity;

		if (newEntity)
		{
			entity = Resources::Create<EntityResource>(UUID::RandomUUID());
		}

		ResourceObject entityObject = Resources::Write(entity);

		StringView nodeName = name.Empty() ? "Node" : name;

		if (!IsStrNullOrEmpty(node->name.data))
		{
			nodeName = node->name.data;
		}

		if (newEntity)
		{
			entityObject.SetString(EntityResource::Name, nodeName);
		}

		// Extract transform
		RID transformRID = EntityResource::GetOrCreateComponent(entityObject, sktypeid(Transform), fbxData.scope);

		ResourceObject transformObject = Resources::Write(transformRID);
		transformObject.SetVec3(Transform::Position, ToVec3(node->local_transform.translation));
		transformObject.SetQuat(Transform::Rotation, ToQuat(node->local_transform.rotation));
		transformObject.SetVec3(Transform::Scale, ToVec3(node->local_transform.scale));
		transformObject.Commit(fbxData.scope);

		//typed_id
		// Handle mesh
		if (node->mesh)
		{
			auto meshIt = fbxData.meshes.Find(node->mesh);
			if (meshIt == fbxData.meshes.end())
			{
				ProcessMesh(fbxData, node->mesh, nodeName);
				meshIt = fbxData.meshes.Find(node->mesh);
			}

			if (meshIt)
			{
				ResourceObject meshObject = Resources::Write(meshIt->second);
				if (RID skin = meshObject.GetSubObject(MeshResource::Skin))
				{
					RID meshRenderer = Resources::Create<SkinnedMeshRenderer>(UUID::RandomUUID());
					ResourceObject skinnedMeshRenderObject = Resources::Write(meshRenderer);

					if (auto rootIt = fbxData.meshSkeleton.Find(meshIt->first))
					{
						skinnedMeshRenderObject.SetReference(skinnedMeshRenderObject.GetIndex("skeleton"), rootIt->second);
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

		//process bones
		for (u32 i = 0; i < node->children.count; i++)
		{
			if (node->children.data[i]->bone)
			{
				if (auto it = fbxData.nodeSkeletons.Find(node->children.data[i]))
				{
					entityObject.AddToSubObjectList(EntityResource::Children, it->second);
				}
			}
		}

		fbxData.entities.Insert(node, entity);

		entityObject.Commit(fbxData.scope);

		return entity;
	}

	bool ImportFBX(RID directory, const FBXImportSettings& settings, StringView path, UndoRedoScope* scope)
	{
		String fileName = Path::Name(path);

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
				return {};
			}

			FBXImportData fbxData;
			fbxData.basePath = path;
			fbxData.directory = directory;
			fbxData.scene = scene;
			fbxData.scope = scope;

			String basePath = Path::Parent(path);
			String name = Path::Name(path);

			for (u32 i = 0; i < scene->textures.count; i++)
			{
				ProcessTextures(directory, fbxData, basePath, scene->textures.data[i]);
			}

			// Process Materials
			for (u32 i = 0; i < scene->materials.count; i++)
			{
				ProcessMaterials(fbxData, scene->materials.data[i]);
			}

			//not sure about mesh > 0
			RID dccAsset = ResourceAssets::CreateImportedAsset(directory, TypeInfo<DCCAsset>::ID(), fileName, scope, path);
			ResourceObject dccObject = Resources::Write(dccAsset);
			dccObject.SetString(DCCAsset::Name, fileName);

			if (scene->nodes.count > 0 && scene->meshes.count > 0)
			{
				RID transformRID = Resources::Create<Transform>(UUID::RandomUUID(), scope);
				RID root = Resources::Create<EntityResource>(UUID::RandomUUID(), scope);

				ResourceObject entityObject = Resources::Write(root);
				entityObject.SetString(EntityResource::Name, fileName);
				entityObject.AddToSubObjectList(EntityResource::Components, transformRID);
				entityObject.Commit(scope);

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
							// Process children
							ProcessNode(fbxData, current, fileName, root);
						}
					}
				}

				dccObject.SetSubObject(DCCAsset::RootEntity, root);
			}

			//import missing meshes.
			for (u32 i = 0; i < scene->meshes.count; i++)
			{
				ufbx_mesh* mesh = scene->meshes.data[i];
				if (!fbxData.meshes.Has(mesh))
				{
					ProcessMesh(fbxData, mesh, "");
				}
			}

			//animations
			for (u32 i = 0; i < scene->anim_stacks.count; i++)
			{
				ProcessAnimation(fbxData, scene->anim_stacks.data[i]);
			}

			// Add sub-assets to DCCAsset
			for (auto& it : fbxData.meshes)
			{
				dccObject.AddToSubObjectList(DCCAsset::Meshes, it.second);
			}
			for (auto& it : fbxData.textures)
			{
				dccObject.AddToSubObjectList(DCCAsset::Textures, it.second);
			}
			for (auto& it : fbxData.materials)
			{
				dccObject.AddToSubObjectList(DCCAsset::Materials, it.second);
			}
			for (RID anim : fbxData.animations)
			{
				dccObject.AddToSubObjectList(DCCAsset::AnimationClips, anim);
			}

			dccObject.Commit(scope);

			ufbx_free_scene(scene);
		}
		return true;
	}

	void RegisterFBXImporter()
	{
		Reflection::Type<FBXImporter>();
	}
}
