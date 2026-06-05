#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

#include <cgltf.h>
#include <base64.hpp>
#include "yyjson.h"

#include "Skore/Resource/Importers/MeshImporter.hpp"
#include "Skore/Resource/Importers/TextureImporter.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/PhysicShapes.hpp"
#include "Skore/Scene/Components/RigidBody.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Scene/Components/Transform.hpp"

namespace Skore
{
	namespace
	{
		enum class OMIShapeType
		{
			Box,
			Sphere,
			Capsule,
			Cylinder,
			Convex,
			Trimesh
		};

		struct OMIPhysicsShape
		{
			OMIShapeType type = OMIShapeType::Box;
			Vec3         size = {1.0f, 1.0f, 1.0f};        // box
			f32          radius = 0.5f;                      // sphere, capsule, cylinder
			f32          height = 1.0f;                      // capsule, cylinder
			f32          radiusTop = 0.5f;                   // capsule, cylinder (tapered)
			f32          radiusBottom = 0.5f;                // capsule, cylinder (tapered)
			i32          meshIndex = -1;                     // convex, trimesh
		};

		struct OMIPhysicsMaterial
		{
			f32 staticFriction = 0.6f;
			f32 dynamicFriction = 0.6f;
			f32 restitution = 0.0f;
		};

		struct GLTFImportData
		{
			RID                           directory;
			String                        basePath;
			UndoRedoScope*                scope;
			cgltf_data*                   gltfData = nullptr;
			HashMap<cgltf_image*, RID>    images;
			HashMap<cgltf_material*, RID> materials;
			HashMap<cgltf_mesh*, RID>     meshes;
			HashMap<cgltf_skin*, RID>     skins;          // cgltf_skin* → SkinResource RID
			HashMap<cgltf_skin*, RID>     skinSkeletons;  // cgltf_skin* → skeleton entity RID
			HashMap<cgltf_node*, u32>     jointIndexMap;   // joint node → index in skin's joints array
			HashSet<cgltf_skin*>          placedSkeletons; // tracks which skin skeletons have been added to the hierarchy
			Array<RID>                    animations;
			Array<OMIPhysicsShape>        omiShapes;
			Array<OMIPhysicsMaterial>     omiMaterials;
		};

		Logger& logger = Logger::GetLogger("Skore::GLTFImporter");

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

		FilterMode ToFilterMode(cgltf_filter_type filter)
		{
			switch (filter)
			{
				case cgltf_filter_type_nearest:
				case cgltf_filter_type_nearest_mipmap_nearest:
				case cgltf_filter_type_nearest_mipmap_linear:
					return FilterMode::Nearest;
				case cgltf_filter_type_linear:
				case cgltf_filter_type_linear_mipmap_nearest:
				case cgltf_filter_type_linear_mipmap_linear:
					return FilterMode::Linear;
				default:
					return FilterMode::Linear;
			}
		}

		AddressMode ToAddressMode(cgltf_wrap_mode wrap)
		{
			switch (wrap)
			{
				case cgltf_wrap_mode_clamp_to_edge:
					return AddressMode::ClampToEdge;
				case cgltf_wrap_mode_mirrored_repeat:
					return AddressMode::MirroredRepeat;
				case cgltf_wrap_mode_repeat:
					return AddressMode::Repeat;
				default:
					return AddressMode::Repeat;
			}
		}

		LightType ToLightType(cgltf_light_type type)
		{
			switch (type)
			{
				case cgltf_light_type_directional:
					return LightType::Directional;
				case cgltf_light_type_point:
					return LightType::Point;
				case cgltf_light_type_spot:
					return LightType::Spot;
				default:
					return LightType::Directional;
			}
		}

		Vec3 GetNodeLocalPosition(cgltf_node* node)
		{
			if (node->has_translation)
			{
				return ToVec3(node->translation);
			}
			if (node->has_matrix)
			{
				return Vec3{node->matrix[12], node->matrix[13], node->matrix[14]};
			}
			return Vec3{0.0f};
		}

		Quat GetNodeLocalRotation(cgltf_node* node)
		{
			if (node->has_rotation)
			{
				return Quat{node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
			}
			if (node->has_matrix)
			{
				Mat4 m(
					Vec4{node->matrix[0], node->matrix[1], node->matrix[2], node->matrix[3]},
					Vec4{node->matrix[4], node->matrix[5], node->matrix[6], node->matrix[7]},
					Vec4{node->matrix[8], node->matrix[9], node->matrix[10], node->matrix[11]},
					Vec4{node->matrix[12], node->matrix[13], node->matrix[14], node->matrix[15]}
				);
				return Mat4::GetQuaternion(m);
			}
			return Quat{0.0f, 0.0f, 0.0f, 1.0f};
		}

		Vec3 GetNodeLocalScale(cgltf_node* node)
		{
			if (node->has_scale)
			{
				return ToVec3(node->scale);
			}
			if (node->has_matrix)
			{
				Mat4 m(
					Vec4{node->matrix[0], node->matrix[1], node->matrix[2], node->matrix[3]},
					Vec4{node->matrix[4], node->matrix[5], node->matrix[6], node->matrix[7]},
					Vec4{node->matrix[8], node->matrix[9], node->matrix[10], node->matrix[11]},
					Vec4{node->matrix[12], node->matrix[13], node->matrix[14], node->matrix[15]}
				);
				return Mat4::GetScale(m);
			}
			return Vec3{1.0f};
		}

		const char* FindNodeExtensionData(cgltf_node* node, const char* extensionName)
		{
			for (cgltf_size i = 0; i < node->extensions_count; i++)
			{
				if (node->extensions[i].name && strcmp(node->extensions[i].name, extensionName) == 0)
				{
					return node->extensions[i].data;
				}
			}
			return nullptr;
		}

		const char* FindDocExtensionData(cgltf_data* gltfData, const char* extensionName)
		{
			for (cgltf_size i = 0; i < gltfData->data_extensions_count; i++)
			{
				if (gltfData->data_extensions[i].name && strcmp(gltfData->data_extensions[i].name, extensionName) == 0)
				{
					return gltfData->data_extensions[i].data;
				}
			}
			return nullptr;
		}

		void ParseOMIPhysicsShapes(GLTFImportData& data)
		{
			const char* shapeJson = FindDocExtensionData(data.gltfData, "OMI_physics_shape");
			if (!shapeJson)
			{
				return;
			}

			yyjson_doc* doc = yyjson_read(shapeJson, strlen(shapeJson), 0);
			if (!doc)
			{
				logger.Warn("Failed to parse OMI_physics_shape JSON");
				return;
			}

			yyjson_val* root = yyjson_doc_get_root(doc);
			yyjson_val* shapesArr = yyjson_obj_get(root, "shapes");
			if (!shapesArr || !yyjson_is_arr(shapesArr))
			{
				yyjson_doc_free(doc);
				return;
			}

			yyjson_val* shapeVal;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(shapesArr, &iter);

			while ((shapeVal = yyjson_arr_iter_next(&iter)) != nullptr)
			{
				OMIPhysicsShape shape{};
				yyjson_val* typeVal = yyjson_obj_get(shapeVal, "type");
				if (!typeVal)
				{
					data.omiShapes.EmplaceBack(shape);
					continue;
				}

				const char* typeStr = yyjson_get_str(typeVal);
				if (strcmp(typeStr, "box") == 0)
				{
					shape.type = OMIShapeType::Box;
					yyjson_val* boxObj = yyjson_obj_get(shapeVal, "box");
					if (boxObj)
					{
						yyjson_val* sizeArr = yyjson_obj_get(boxObj, "size");
						if (sizeArr && yyjson_arr_size(sizeArr) == 3)
						{
							shape.size.x = static_cast<f32>(yyjson_get_num(yyjson_arr_get(sizeArr, 0)));
							shape.size.y = static_cast<f32>(yyjson_get_num(yyjson_arr_get(sizeArr, 1)));
							shape.size.z = static_cast<f32>(yyjson_get_num(yyjson_arr_get(sizeArr, 2)));
						}
					}
				}
				else if (strcmp(typeStr, "sphere") == 0)
				{
					shape.type = OMIShapeType::Sphere;
					yyjson_val* sphereObj = yyjson_obj_get(shapeVal, "sphere");
					if (sphereObj)
					{
						yyjson_val* radiusVal = yyjson_obj_get(sphereObj, "radius");
						if (radiusVal) shape.radius = static_cast<f32>(yyjson_get_num(radiusVal));
					}
				}
				else if (strcmp(typeStr, "capsule") == 0)
				{
					shape.type = OMIShapeType::Capsule;
					yyjson_val* capsuleObj = yyjson_obj_get(shapeVal, "capsule");
					if (capsuleObj)
					{
						yyjson_val* heightVal = yyjson_obj_get(capsuleObj, "height");
						if (heightVal) shape.height = static_cast<f32>(yyjson_get_num(heightVal));
						yyjson_val* radiusVal = yyjson_obj_get(capsuleObj, "radius");
						if (radiusVal)
						{
							shape.radius = static_cast<f32>(yyjson_get_num(radiusVal));
							shape.radiusTop = shape.radius;
							shape.radiusBottom = shape.radius;
						}
						yyjson_val* radiusTopVal = yyjson_obj_get(capsuleObj, "radiusTop");
						if (radiusTopVal) shape.radiusTop = static_cast<f32>(yyjson_get_num(radiusTopVal));
						yyjson_val* radiusBottomVal = yyjson_obj_get(capsuleObj, "radiusBottom");
						if (radiusBottomVal) shape.radiusBottom = static_cast<f32>(yyjson_get_num(radiusBottomVal));
					}
				}
				else if (strcmp(typeStr, "cylinder") == 0)
				{
					shape.type = OMIShapeType::Cylinder;
					yyjson_val* cylinderObj = yyjson_obj_get(shapeVal, "cylinder");
					if (cylinderObj)
					{
						yyjson_val* heightVal = yyjson_obj_get(cylinderObj, "height");
						if (heightVal) shape.height = static_cast<f32>(yyjson_get_num(heightVal));
						yyjson_val* radiusVal = yyjson_obj_get(cylinderObj, "radius");
						if (radiusVal)
						{
							shape.radius = static_cast<f32>(yyjson_get_num(radiusVal));
							shape.radiusTop = shape.radius;
							shape.radiusBottom = shape.radius;
						}
						yyjson_val* radiusTopVal = yyjson_obj_get(cylinderObj, "radiusTop");
						if (radiusTopVal) shape.radiusTop = static_cast<f32>(yyjson_get_num(radiusTopVal));
						yyjson_val* radiusBottomVal = yyjson_obj_get(cylinderObj, "radiusBottom");
						if (radiusBottomVal) shape.radiusBottom = static_cast<f32>(yyjson_get_num(radiusBottomVal));
					}
				}
				else if (strcmp(typeStr, "convex") == 0)
				{
					shape.type = OMIShapeType::Convex;
					yyjson_val* convexObj = yyjson_obj_get(shapeVal, "convex");
					if (convexObj)
					{
						yyjson_val* meshVal = yyjson_obj_get(convexObj, "mesh");
						if (meshVal) shape.meshIndex = yyjson_get_int(meshVal);
					}
				}
				else if (strcmp(typeStr, "trimesh") == 0)
				{
					shape.type = OMIShapeType::Trimesh;
					yyjson_val* trimeshObj = yyjson_obj_get(shapeVal, "trimesh");
					if (trimeshObj)
					{
						yyjson_val* meshVal = yyjson_obj_get(trimeshObj, "mesh");
						if (meshVal) shape.meshIndex = yyjson_get_int(meshVal);
					}
				}
				else
				{
					logger.Warn("Unknown OMI_physics_shape type: {}", typeStr);
				}

				data.omiShapes.EmplaceBack(shape);
			}

			yyjson_doc_free(doc);
			logger.Debug("Parsed {} OMI physics shapes", data.omiShapes.Size());
		}

		void ParseOMIPhysicsMaterials(GLTFImportData& data)
		{
			const char* bodyJson = FindDocExtensionData(data.gltfData, "OMI_physics_body");
			if (!bodyJson)
			{
				return;
			}

			yyjson_doc* doc = yyjson_read(bodyJson, strlen(bodyJson), 0);
			if (!doc)
			{
				return;
			}

			yyjson_val* root = yyjson_doc_get_root(doc);
			yyjson_val* materialsArr = yyjson_obj_get(root, "physicsMaterials");
			if (materialsArr && yyjson_is_arr(materialsArr))
			{
				yyjson_val* matVal;
				yyjson_arr_iter iter;
				yyjson_arr_iter_init(materialsArr, &iter);

				while ((matVal = yyjson_arr_iter_next(&iter)) != nullptr)
				{
					OMIPhysicsMaterial mat{};
					yyjson_val* v;
					if ((v = yyjson_obj_get(matVal, "staticFriction"))) mat.staticFriction = static_cast<f32>(yyjson_get_num(v));
					if ((v = yyjson_obj_get(matVal, "dynamicFriction"))) mat.dynamicFriction = static_cast<f32>(yyjson_get_num(v));
					if ((v = yyjson_obj_get(matVal, "restitution"))) mat.restitution = static_cast<f32>(yyjson_get_num(v));
					data.omiMaterials.EmplaceBack(mat);
				}
			}

			yyjson_doc_free(doc);
		}

		void ProcessOMIPhysicsBody(GLTFImportData& data, cgltf_node* node, ResourceObject& entityObject)
		{
			const char* bodyJson = FindNodeExtensionData(node, "OMI_physics_body");
			if (!bodyJson)
			{
				return;
			}

			yyjson_doc* doc = yyjson_read(bodyJson, strlen(bodyJson), 0);
			if (!doc)
			{
				logger.Warn("Failed to parse OMI_physics_body JSON on node '{}'", node->name ? node->name : "unnamed");
				return;
			}

			yyjson_val* root = yyjson_doc_get_root(doc);

			// Resolve physics material from collider (applied to RigidBody)
			f32 friction = 0.6f;
			f32 restitution = 0.0f;
			yyjson_val* colliderObj = yyjson_obj_get(root, "collider");
			if (colliderObj)
			{
				yyjson_val* matIndexVal = yyjson_obj_get(colliderObj, "physicsMaterial");
				if (matIndexVal)
				{
					i32 matIndex = yyjson_get_int(matIndexVal);
					if (matIndex >= 0 && matIndex < static_cast<i32>(data.omiMaterials.Size()))
					{
						friction = data.omiMaterials[matIndex].dynamicFriction;
						restitution = data.omiMaterials[matIndex].restitution;
					}
				}
			}

			// Parse motion → RigidBody
			yyjson_val* motionObj = yyjson_obj_get(root, "motion");
			if (motionObj)
			{
				yyjson_val* typeVal = yyjson_obj_get(motionObj, "type");
				const char* motionType = typeVal ? yyjson_get_str(typeVal) : "static";

				if (strcmp(motionType, "dynamic") == 0 || strcmp(motionType, "kinematic") == 0)
				{
					RID rigidBodyRID = Resources::Create<RigidBody>(UUID::RandomUUID());
					ResourceObject rbObject = Resources::Write(rigidBodyRID);

					rbObject.SetBool(rbObject.GetIndex("isKinematic"), strcmp(motionType, "kinematic") == 0);
					rbObject.SetFloat(rbObject.GetIndex("friction"), friction);
					rbObject.SetFloat(rbObject.GetIndex("restitution"), restitution);

					yyjson_val* v;
					if ((v = yyjson_obj_get(motionObj, "mass")))
						rbObject.SetFloat(rbObject.GetIndex("mass"), static_cast<f32>(yyjson_get_num(v)));
					if ((v = yyjson_obj_get(motionObj, "gravityFactor")))
						rbObject.SetFloat(rbObject.GetIndex("gravityFactor"), static_cast<f32>(yyjson_get_num(v)));

					rbObject.Commit(data.scope);
					entityObject.AddToSubObjectList(EntityResource::Components, rigidBodyRID);
				}
				// static motion type → no RigidBody needed, collider alone is static
			}

			// Parse collider → appropriate collider component
			if (colliderObj)
			{
				yyjson_val* shapeIndexVal = yyjson_obj_get(colliderObj, "shape");
				i32 shapeIndex = shapeIndexVal ? yyjson_get_int(shapeIndexVal) : -1;

				if (shapeIndex >= 0 && shapeIndex < static_cast<i32>(data.omiShapes.Size()))
				{
					const OMIPhysicsShape& shape = data.omiShapes[shapeIndex];

					switch (shape.type)
					{
						case OMIShapeType::Box:
						{
							RID colliderRID = Resources::Create<BoxCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							// BoxCollider uses half-size, OMI uses full size
							colliderObject.SetVec3(colliderObject.GetIndex("halfSize"), shape.size * 0.5f);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), false);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Sphere:
						{
							RID colliderRID = Resources::Create<SphereCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							colliderObject.SetFloat(colliderObject.GetIndex("radius"), shape.radius);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), false);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Capsule:
						{
							RID colliderRID = Resources::Create<CapsuleCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							// Use average of top/bottom radius for engines that don't support tapered capsules
							colliderObject.SetFloat(colliderObject.GetIndex("radius"), (shape.radiusTop + shape.radiusBottom) * 0.5f);
							colliderObject.SetFloat(colliderObject.GetIndex("height"), shape.height);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), false);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Cylinder:
						{
							RID colliderRID = Resources::Create<CylinderCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							colliderObject.SetFloat(colliderObject.GetIndex("radius"), (shape.radiusTop + shape.radiusBottom) * 0.5f);
							colliderObject.SetFloat(colliderObject.GetIndex("height"), shape.height);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), false);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Convex:
						case OMIShapeType::Trimesh:
						{
							logger.Warn("OMI_physics_shape convex/trimesh not yet supported for node '{}'", node->name ? node->name : "unnamed");
							break;
						}
					}
				}
			}

			// Parse trigger → collider with isSensor=true
			yyjson_val* triggerObj = yyjson_obj_get(root, "trigger");
			if (triggerObj)
			{
				yyjson_val* shapeIndexVal = yyjson_obj_get(triggerObj, "shape");
				i32 shapeIndex = shapeIndexVal ? yyjson_get_int(shapeIndexVal) : -1;

				if (shapeIndex >= 0 && shapeIndex < static_cast<i32>(data.omiShapes.Size()))
				{
					const OMIPhysicsShape& shape = data.omiShapes[shapeIndex];

					switch (shape.type)
					{
						case OMIShapeType::Box:
						{
							RID colliderRID = Resources::Create<BoxCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							colliderObject.SetVec3(colliderObject.GetIndex("halfSize"), shape.size * 0.5f);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), true);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Sphere:
						{
							RID colliderRID = Resources::Create<SphereCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							colliderObject.SetFloat(colliderObject.GetIndex("radius"), shape.radius);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), true);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Capsule:
						{
							RID colliderRID = Resources::Create<CapsuleCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							colliderObject.SetFloat(colliderObject.GetIndex("radius"), (shape.radiusTop + shape.radiusBottom) * 0.5f);
							colliderObject.SetFloat(colliderObject.GetIndex("height"), shape.height);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), true);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Cylinder:
						{
							RID colliderRID = Resources::Create<CylinderCollider>(UUID::RandomUUID());
							ResourceObject colliderObject = Resources::Write(colliderRID);
							colliderObject.SetFloat(colliderObject.GetIndex("radius"), (shape.radiusTop + shape.radiusBottom) * 0.5f);
							colliderObject.SetFloat(colliderObject.GetIndex("height"), shape.height);
							colliderObject.SetBool(colliderObject.GetIndex("isSensor"), true);
							colliderObject.Commit(data.scope);
							entityObject.AddToSubObjectList(EntityResource::Components, colliderRID);
							break;
						}
						case OMIShapeType::Convex:
						case OMIShapeType::Trimesh:
						{
							logger.Warn("OMI_physics_shape convex/trimesh not yet supported for trigger on node '{}'", node->name ? node->name : "unnamed");
							break;
						}
					}
				}
			}

			yyjson_doc_free(doc);
		}

		void ProcessSkin(GLTFImportData& data, cgltf_skin* skin)
		{
			if (!skin || data.skins.Has(skin))
			{
				return;
			}

			StringView skinName = skin->name ? skin->name : "Skin";
			logger.Debug("Processing skin: {} ({} joints)", skinName, skin->joints_count);

			// Create SkinResource with inverse bind matrices
			RID skinRID = Resources::Create<SkinResource>(UUID::RandomUUID(), data.scope);
			ResourceObject skinObject = Resources::Write(skinRID);
			skinObject.SetString(SkinResource::Name, skinName);

			for (cgltf_size i = 0; i < skin->joints_count; i++)
			{
				RID skinBind = Resources::Create<SkinBindResource>(UUID::RandomUUID());
				ResourceObject skinBindObject = Resources::Write(skinBind);

				Mat4 inverseBindMatrix{1.0f};
				if (skin->inverse_bind_matrices)
				{
					cgltf_float m[16];
					cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16);
					inverseBindMatrix = Mat4(
						Vec4{m[0], m[1], m[2], m[3]},
						Vec4{m[4], m[5], m[6], m[7]},
						Vec4{m[8], m[9], m[10], m[11]},
						Vec4{m[12], m[13], m[14], m[15]}
					);
				}

				skinBindObject.SetMat4(SkinBindResource::Pose, inverseBindMatrix);
				skinBindObject.Commit(data.scope);
				skinObject.AddToSubObjectList(SkinResource::Binds, skinBind);
			}

			skinObject.Commit(data.scope);
			data.skins.Insert(skin, skinRID);

			// Build joint index map for this skin
			for (cgltf_size i = 0; i < skin->joints_count; i++)
			{
				data.jointIndexMap.Insert(skin->joints[i], static_cast<u32>(i));
			}

			// Create Skeleton component with BoneNodes
			RID skeletonComponent = Resources::Create<Skeleton>(UUID::RandomUUID());
			ResourceObject skeletonComponentObject = Resources::Write(skeletonComponent);

			Array<RID> boneRIDs;

			for (cgltf_size i = 0; i < skin->joints_count; i++)
			{
				cgltf_node* joint = skin->joints[i];

				u32 parentIndex = U32_MAX;
				if (joint->parent)
				{
					if (auto it = data.jointIndexMap.Find(joint->parent))
					{
						parentIndex = it->second;
					}
				}

				RID boneNodeRID = Resources::Create<BoneNode>(UUID::RandomUUID());
				ResourceObject boneObject = Resources::Write(boneNodeRID);

				StringView boneName = joint->name ? joint->name : "Bone";
				boneObject.SetString(boneObject.GetIndex("name"), boneName);
				boneObject.SetUInt(boneObject.GetIndex("parentIndex"), parentIndex);
				boneObject.SetVec3(boneObject.GetIndex("position"), GetNodeLocalPosition(joint));
				boneObject.SetQuat(boneObject.GetIndex("rotation"), GetNodeLocalRotation(joint));
				boneObject.SetVec3(boneObject.GetIndex("scale"), GetNodeLocalScale(joint));
				boneObject.Commit(data.scope);

				boneRIDs.EmplaceBack(boneNodeRID);
			}

			skeletonComponentObject.AddToSubObjectList(skeletonComponentObject.GetIndex("bones"), boneRIDs);
			skeletonComponentObject.Commit(data.scope);

			// Create skeleton entity
			RID skeletonEntity = Resources::Create<EntityResource>(UUID::RandomUUID());
			ResourceObject skeletonObject = Resources::Write(skeletonEntity);
			skeletonObject.SetString(EntityResource::Name, "Skeleton");
			skeletonObject.AddToSubObjectList(EntityResource::Components, skeletonComponent);
			skeletonObject.Commit(data.scope);

			data.skinSkeletons.Insert(skin, skeletonEntity);
		}

		void ProcessAnimation(GLTFImportData& data, cgltf_animation* animation)
		{
			String animationName = animation->name ? animation->name : "Animation";
			logger.Debug("Processing animation: {} ({} channels, {} samplers)", animationName, animation->channels_count, animation->samplers_count);

			RID animRID = Resources::Create<AnimationClipResource>(UUID::RandomUUID(), data.scope);
			ResourceObject animObject = Resources::Write(animRID);
			animObject.SetString(AnimationClipResource::Name, animationName);

			// Find the time range across all samplers
			f32 timeBegin = FLT_MAX;
			f32 timeEnd = -FLT_MAX;

			for (cgltf_size i = 0; i < animation->samplers_count; i++)
			{
				cgltf_animation_sampler* sampler = &animation->samplers[i];
				if (sampler->input && sampler->input->count > 0)
				{
					cgltf_float t;
					cgltf_accessor_read_float(sampler->input, 0, &t, 1);
					if (t < timeBegin) timeBegin = t;

					cgltf_accessor_read_float(sampler->input, sampler->input->count - 1, &t, 1);
					if (t > timeEnd) timeEnd = t;
				}
			}

			if (timeBegin >= timeEnd)
			{
				logger.Warn("Animation '{}' has invalid time range", animationName);
				animObject.Commit(data.scope);
				return;
			}

			const f32 targetFramerate = 30.0f;
			const u32 maxFrames = 4096;

			f32 duration = timeEnd - timeBegin;
			u32 numFrames = Math::Clamp(static_cast<u32>(duration * targetFramerate), 2u, maxFrames);
			f32 framerate = static_cast<f32>(numFrames - 1) / duration;

			animObject.SetUInt(AnimationClipResource::NumFrames, numFrames);
			animObject.SetFloat(AnimationClipResource::Duration, duration);
			animObject.SetFloat(AnimationClipResource::FrameRate, framerate);
			animObject.SetFloat(AnimationClipResource::TimeBegin, timeBegin);
			animObject.SetFloat(AnimationClipResource::TimeEnd, timeEnd);

			// Collect all unique target nodes (bones) from channels
			// Group channels by target node so we can merge T/R/S per bone
			struct ChannelGroup
			{
				cgltf_node* node = nullptr;
				cgltf_animation_channel* translationChannel = nullptr;
				cgltf_animation_channel* rotationChannel = nullptr;
				cgltf_animation_channel* scaleChannel = nullptr;
			};

			HashMap<cgltf_node*, u32> nodeToGroupIndex;
			Array<ChannelGroup> channelGroups;

			for (cgltf_size i = 0; i < animation->channels_count; i++)
			{
				cgltf_animation_channel* channel = &animation->channels[i];
				if (!channel->target_node) continue;

				// Skip weight animations (morph targets)
				if (channel->target_path == cgltf_animation_path_type_weights) continue;

				auto it = nodeToGroupIndex.Find(channel->target_node);
				u32 groupIdx;
				if (it == nodeToGroupIndex.end())
				{
					groupIdx = static_cast<u32>(channelGroups.Size());
					nodeToGroupIndex.Insert(channel->target_node, groupIdx);
					ChannelGroup group;
					group.node = channel->target_node;
					channelGroups.EmplaceBack(group);
				}
				else
				{
					groupIdx = it->second;
				}

				switch (channel->target_path)
				{
					case cgltf_animation_path_type_translation:
						channelGroups[groupIdx].translationChannel = channel;
						break;
					case cgltf_animation_path_type_rotation:
						channelGroups[groupIdx].rotationChannel = channel;
						break;
					case cgltf_animation_path_type_scale:
						channelGroups[groupIdx].scaleChannel = channel;
						break;
					default:
						break;
				}
			}

			// Sample all channels at fixed framerate and write to buffer
			ResourceBuffer buffer = ResourceAssets::CreateTempBuffer();
			FileHandler handler = buffer.OpenFile(AccessMode::ReadAndWrite);
			u64 offset = 0;

			for (u32 g = 0; g < channelGroups.Size(); g++)
			{
				ChannelGroup& group = channelGroups[g];
				StringView boneName = group.node->name ? group.node->name : "Bone";

				RID animationChannel = Resources::Create<AnimationChannelResource>(UUID::RandomUUID());
				ResourceObject channelObject = Resources::Write(animationChannel);
				channelObject.SetString(AnimationChannelResource::Name, boneName);
				channelObject.SetUInt(AnimationChannelResource::BufferOffset, offset);

				// Default pose from node's rest transform
				Vec3 defaultPosition = GetNodeLocalPosition(group.node);
				Quat defaultRotation = GetNodeLocalRotation(group.node);
				Vec3 defaultScale = GetNodeLocalScale(group.node);

				for (u32 f = 0; f < numFrames; f++)
				{
					f64 time = timeBegin + static_cast<f64>(f) / framerate;

					Vec3 position = defaultPosition;
					Quat rotation = defaultRotation;
					Vec3 scale = defaultScale;

					// Sample translation
					if (group.translationChannel)
					{
						cgltf_animation_sampler* sampler = group.translationChannel->sampler;
						cgltf_size inputCount = sampler->input->count;

						// Find surrounding keyframes
						cgltf_float t0 = 0, t1 = 0;
						cgltf_accessor_read_float(sampler->input, 0, &t0, 1);

						if (time <= t0 || inputCount == 1)
						{
							cgltf_float v[3];
							if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
								cgltf_accessor_read_float(sampler->output, 1, v, 3); // middle tangent value
							else
								cgltf_accessor_read_float(sampler->output, 0, v, 3);
							position = ToVec3(v);
						}
						else
						{
							cgltf_accessor_read_float(sampler->input, inputCount - 1, &t1, 1);
							if (time >= t1)
							{
								cgltf_float v[3];
								if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
									cgltf_accessor_read_float(sampler->output, (inputCount - 1) * 3 + 1, v, 3);
								else
									cgltf_accessor_read_float(sampler->output, inputCount - 1, v, 3);
								position = ToVec3(v);
							}
							else
							{
								for (cgltf_size k = 0; k < inputCount - 1; k++)
								{
									cgltf_accessor_read_float(sampler->input, k, &t0, 1);
									cgltf_accessor_read_float(sampler->input, k + 1, &t1, 1);
									if (time >= t0 && time <= t1)
									{
										f32 alpha = (t1 > t0) ? static_cast<f32>((time - t0) / (t1 - t0)) : 0.0f;

										cgltf_float v0[3], v1[3];
										if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
										{
											cgltf_accessor_read_float(sampler->output, k * 3 + 1, v0, 3);
											cgltf_accessor_read_float(sampler->output, (k + 1) * 3 + 1, v1, 3);
										}
										else
										{
											cgltf_accessor_read_float(sampler->output, k, v0, 3);
											cgltf_accessor_read_float(sampler->output, k + 1, v1, 3);
										}

										if (sampler->interpolation == cgltf_interpolation_type_step)
										{
											position = ToVec3(v0);
										}
										else
										{
											position = Vec3::Lerp(ToVec3(v0), ToVec3(v1), alpha);
										}
										break;
									}
								}
							}
						}
					}

					// Sample rotation
					if (group.rotationChannel)
					{
						cgltf_animation_sampler* sampler = group.rotationChannel->sampler;
						cgltf_size inputCount = sampler->input->count;

						cgltf_float t0 = 0, t1 = 0;
						cgltf_accessor_read_float(sampler->input, 0, &t0, 1);

						if (time <= t0 || inputCount == 1)
						{
							cgltf_float v[4];
							if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
								cgltf_accessor_read_float(sampler->output, 1, v, 4);
							else
								cgltf_accessor_read_float(sampler->output, 0, v, 4);
							rotation = Quat{v[0], v[1], v[2], v[3]};
						}
						else
						{
							cgltf_accessor_read_float(sampler->input, inputCount - 1, &t1, 1);
							if (time >= t1)
							{
								cgltf_float v[4];
								if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
									cgltf_accessor_read_float(sampler->output, (inputCount - 1) * 3 + 1, v, 4);
								else
									cgltf_accessor_read_float(sampler->output, inputCount - 1, v, 4);
								rotation = Quat{v[0], v[1], v[2], v[3]};
							}
							else
							{
								for (cgltf_size k = 0; k < inputCount - 1; k++)
								{
									cgltf_accessor_read_float(sampler->input, k, &t0, 1);
									cgltf_accessor_read_float(sampler->input, k + 1, &t1, 1);
									if (time >= t0 && time <= t1)
									{
										f32 alpha = (t1 > t0) ? static_cast<f32>((time - t0) / (t1 - t0)) : 0.0f;

										cgltf_float v0[4], v1[4];
										if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
										{
											cgltf_accessor_read_float(sampler->output, k * 3 + 1, v0, 4);
											cgltf_accessor_read_float(sampler->output, (k + 1) * 3 + 1, v1, 4);
										}
										else
										{
											cgltf_accessor_read_float(sampler->output, k, v0, 4);
											cgltf_accessor_read_float(sampler->output, k + 1, v1, 4);
										}

										Quat q0{v0[0], v0[1], v0[2], v0[3]};
										Quat q1{v1[0], v1[1], v1[2], v1[3]};

										if (sampler->interpolation == cgltf_interpolation_type_step)
										{
											rotation = q0;
										}
										else
										{
											rotation = Quat::Slerp(q0, q1, alpha);
										}
										break;
									}
								}
							}
						}
					}

					// Sample scale
					if (group.scaleChannel)
					{
						cgltf_animation_sampler* sampler = group.scaleChannel->sampler;
						cgltf_size inputCount = sampler->input->count;

						cgltf_float t0 = 0, t1 = 0;
						cgltf_accessor_read_float(sampler->input, 0, &t0, 1);

						if (time <= t0 || inputCount == 1)
						{
							cgltf_float v[3];
							if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
								cgltf_accessor_read_float(sampler->output, 1, v, 3);
							else
								cgltf_accessor_read_float(sampler->output, 0, v, 3);
							scale = ToVec3(v);
						}
						else
						{
							cgltf_accessor_read_float(sampler->input, inputCount - 1, &t1, 1);
							if (time >= t1)
							{
								cgltf_float v[3];
								if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
									cgltf_accessor_read_float(sampler->output, (inputCount - 1) * 3 + 1, v, 3);
								else
									cgltf_accessor_read_float(sampler->output, inputCount - 1, v, 3);
								scale = ToVec3(v);
							}
							else
							{
								for (cgltf_size k = 0; k < inputCount - 1; k++)
								{
									cgltf_accessor_read_float(sampler->input, k, &t0, 1);
									cgltf_accessor_read_float(sampler->input, k + 1, &t1, 1);
									if (time >= t0 && time <= t1)
									{
										f32 alpha = (t1 > t0) ? static_cast<f32>((time - t0) / (t1 - t0)) : 0.0f;

										cgltf_float v0[3], v1[3];
										if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
										{
											cgltf_accessor_read_float(sampler->output, k * 3 + 1, v0, 3);
											cgltf_accessor_read_float(sampler->output, (k + 1) * 3 + 1, v1, 3);
										}
										else
										{
											cgltf_accessor_read_float(sampler->output, k, v0, 3);
											cgltf_accessor_read_float(sampler->output, k + 1, v1, 3);
										}

										if (sampler->interpolation == cgltf_interpolation_type_step)
										{
											scale = ToVec3(v0);
										}
										else
										{
											scale = Vec3::Lerp(ToVec3(v0), ToVec3(v1), alpha);
										}
										break;
									}
								}
							}
						}
					}

					AnimationKeyFrame keyFrame;
					keyFrame.time = time;
					keyFrame.position = position;
					keyFrame.rotation = rotation;
					keyFrame.scale = scale;

					FileSystem::WriteFile(handler, &keyFrame, sizeof(AnimationKeyFrame));
				}

				offset += sizeof(AnimationKeyFrame) * numFrames;
				channelObject.Commit(data.scope);
				animObject.AddToSubObjectList(AnimationClipResource::Channels, animationChannel);
			}

			FileSystem::CloseFile(handler);
			animObject.SetBuffer(AnimationClipResource::KeyFramesBuffer, buffer);
			animObject.Commit(data.scope);

			data.animations.EmplaceBack(animRID);

			logger.Debug("Animation '{}': {} frames, {:.1f}s duration, {:.1f} fps, {} bone channels", animationName, numFrames, duration, framerate, channelGroups.Size());
		}
	}


	struct GLTFImporter : ResourceAssetImporter
	{
		SK_CLASS(GLTFImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".gltf", ".glb"};
		}

		void ProcessTexture(GLTFImportData& gltfData, u32 index, cgltf_texture* texture)
		{
			RID textureRID = {};

			cgltf_image* image = texture->image;
			if (image == nullptr)
			{
				logger.Debug("Skipping texture with null image");
				return;
			}

			String texName;
			if (image->name)
			{
				texName = image->name;
			}
			else
			{
				auto s = fmt::format("texture {}", index);
				texName = s.c_str();
			}

			logger.Debug("Processing texture: {}, {} ", texName, index);

			TextureImportSettings textureImportSettings;
			if (texture->sampler)
			{
				cgltf_filter_type filter = texture->sampler->mag_filter != cgltf_filter_type_undefined ? texture->sampler->mag_filter : texture->sampler->min_filter;
				textureImportSettings.filterMode = ToFilterMode(filter);
				textureImportSettings.wrapMode = ToAddressMode(texture->sampler->wrap_s);
			}
			textureImportSettings.async = false;
			textureImportSettings.isSubAsset = true;


			if (bool embedded = (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) || (image->uri && strncmp(image->uri, "data:", 5) == 0))
			{
				logger.Debug("Texture '{}' is embedded", texName);
				if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data)
				{
					const u8* bufferData = static_cast<const u8*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
					usize     bufferSize = image->buffer_view->size;

					Span<u8> data = Span(const_cast<u8*>(bufferData), bufferSize);
					textureRID = ImportTextureFromMemory(gltfData.directory, textureImportSettings, texName, data, gltfData.scope);
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
							textureRID = ImportTextureFromMemory(gltfData.directory, textureImportSettings, texName, Span(output.Data(), output.Size()), gltfData.scope);
						}
					}
				}
				else
				{
					logger.Error("Invalid image data");
					return;
				}
			}
			else if (image->uri)
			{
				logger.Debug("Texture '{}' loaded from external file: {}", texName, image->uri);
				String uri = image->uri;
				u32 size = cgltf_decode_uri(uri.begin());
				uri.Resize(size);
				String texturePath = Path::Join(gltfData.basePath, uri);
				textureRID = ImportTexture(gltfData.directory, textureImportSettings, texturePath, gltfData.scope);
			}

			gltfData.images.Insert(image, textureRID);
		}

		void ProcessMaterial(GLTFImportData& data, cgltf_material* material)
		{
			if (!material)
			{
				logger.Debug("Skipping null material");
				return;
			}

			StringView name = material->name ? material->name : "Material";
			logger.Debug("Processing material: {}", name);
			RID materialResource = Resources::Create<MaterialResource>(UUID::RandomUUID(), data.scope);

			ResourceObject materialObject = Resources::Write(materialResource);
			materialObject.SetString(MaterialResource::Name, name);

			if (material->has_pbr_metallic_roughness)
			{
				logger.Debug("  Material '{}' has PBR metallic roughness (metallic={}, roughness={})", name, material->pbr_metallic_roughness.metallic_factor, material->pbr_metallic_roughness.roughness_factor);
			}

			// Process PBR metallic roughness
			if (material->has_pbr_metallic_roughness)
			{
				auto& pbr = material->pbr_metallic_roughness;

				materialObject.SetColor(MaterialResource::BaseColor, Color::FromVec4Gamma(pbr.base_color_factor));

				materialObject.SetFloat(MaterialResource::Metallic, pbr.metallic_factor);
				materialObject.SetFloat(MaterialResource::Roughness, pbr.roughness_factor);

				if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
				{
					if (auto it = data.images.Find(pbr.base_color_texture.texture->image))
					{
						materialObject.SetReference(MaterialResource::BaseColorTexture, it->second);
					}
				}

				if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image)
				{
					if (auto it = data.images.Find(pbr.metallic_roughness_texture.texture->image))
					{
						materialObject.SetReference(MaterialResource::MetallicTexture, it->second);
						materialObject.SetReference(MaterialResource::RoughnessTexture, it->second);
						materialObject.SetEnum(MaterialResource::MetallicTextureChannel, TextureChannel::Blue);
						materialObject.SetEnum(MaterialResource::RoughnessTextureChannel, TextureChannel::Green);
					}
				}
			}

			if (material->normal_texture.texture && material->normal_texture.texture->image)
			{
				if (auto it = data.images.Find(material->normal_texture.texture->image))
				{
					materialObject.SetReference(MaterialResource::NormalTexture, it->second);
					materialObject.SetFloat(MaterialResource::NormalMultiplier, material->normal_texture.scale);
				}
			}

			if (material->occlusion_texture.texture && material->occlusion_texture.texture->image)
			{
				if (auto it = data.images.Find(material->occlusion_texture.texture->image))
				{
					materialObject.SetReference(MaterialResource::OcclusionTexture, it->second);
					materialObject.SetFloat(MaterialResource::OcclusionStrength, material->occlusion_texture.scale);
					materialObject.SetEnum(MaterialResource::OcclusionTextureChannel, TextureChannel::Red);
				}
			}

			if (material->emissive_texture.texture && material->emissive_texture.texture->image)
			{
				if (auto it = data.images.Find(material->emissive_texture.texture->image))
				{
					materialObject.SetReference(MaterialResource::EmissiveTexture, it->second);
				}
			}

			materialObject.SetFloat(MaterialResource::AlphaCutoff, material->alpha_cutoff);

			switch (material->alpha_mode)
			{
				case cgltf_alpha_mode_opaque:
					materialObject.SetEnum(MaterialResource::AlphaMode, MaterialResource::MaterialAlphaMode::Opaque);
					break;
				case cgltf_alpha_mode_mask:
					materialObject.SetEnum(MaterialResource::AlphaMode, MaterialResource::MaterialAlphaMode::Mask);
					break;
				case cgltf_alpha_mode_blend:
					materialObject.SetEnum(MaterialResource::AlphaMode, MaterialResource::MaterialAlphaMode::Blend);
					break;
				default:
					materialObject.SetEnum(MaterialResource::AlphaMode, MaterialResource::MaterialAlphaMode::None);
			}

			{
				f32 emissive[4] = {material->emissive_factor[0], material->emissive_factor[1], material->emissive_factor[2], 1.0f};
				materialObject.SetColor(MaterialResource::EmissiveColor, Color::FromVec4Gamma(emissive));
			}

			if (material->has_emissive_strength)
			{
				materialObject.SetFloat(MaterialResource::EmissiveFactor, material->emissive_strength.emissive_strength);
			}

			materialObject.Commit();

			data.materials.Insert(material, materialResource);
		}

		void ProcessMesh(GLTFImportData& data, cgltf_mesh* mesh, cgltf_skin* skin, const Mat4& worldTransform = Mat4{1.0})
		{
			if (!mesh)
			{
				logger.Debug("Skipping null mesh");
				return;
			}

			bool skinned = skin != nullptr;

			if (skinned)
			{
				ProcessSkin(data, skin);
			}

			String meshName = mesh->name ? mesh->name : "Mesh";
			logger.Debug("Processing mesh: {} ({} primitives, skinned={})", meshName, mesh->primitives_count, skinned);

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

			Array<Vec3>          positions;
			Array<Vec3>          normals;
			Array<Vec2>          uvs;
			Array<Vec3>          colors;
			Array<Vec4>          tangents;
			Array<BoneInfluence> boneInfluences;
			Array<u32>           indices;
			Array<MeshPrimitive> primitives;
			Array<RID>           materials;

			positions.Reserve(totalVertices);
			normals.Reserve(totalVertices);
			uvs.Reserve(totalVertices);
			colors.Reserve(totalVertices);
			tangents.Reserve(totalVertices);
			if (skinned)
			{
				boneInfluences.Reserve(totalVertices);
			}
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
					auto it = data.materials.Find(primitive->material);
					{
						materials.EmplaceBack(it->second);
						materialMap.Emplace(primitive->material, static_cast<u32>(materials.Size()) - 1);
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
				cgltf_accessor* jointsAccessor = nullptr;
				cgltf_accessor* weightsAccessor = nullptr;
				cgltf_accessor* indicesAccessor = primitive->indices;
				if (!indicesAccessor)
				{
					logger.Error("Mesh primitive has no indices");
					continue;
				}

				for (size_t j = 0; j < primitive->attributes_count; j++)
				{
					cgltf_attribute* attr = &primitive->attributes[j];
					StringView name = StringView(attr->name);

					if (name == "POSITION")
					{
						positionAccessor = attr->data;
					}
					else if (name == "NORMAL")
					{
						normalAccessor = attr->data;
					}
					else if (name == "TEXCOORD_0")
					{
						texcoordAccessor = attr->data;
					}
					else if (name == "COLOR_0")
					{
						colorAccessor = attr->data;
					}
					else if (name == "TANGENT")
					{
						tangentAccessor = attr->data;
					}
					else if (name == "JOINTS_0")
					{
						jointsAccessor = attr->data;
					}
					else if (name == "WEIGHTS_0")
					{
						weightsAccessor = attr->data;
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
					Vec3 position{0, 0, 0};
					Vec3 normal{0, 1, 0};
					Vec2 uv{0, 0};
					Vec3 color{1, 1, 1};
					Vec4 tangent{1, 0, 0, 1};

					cgltf_accessor_read_float(positionAccessor, v, &position.x, 3);

					if (normalAccessor)
					{
						cgltf_accessor_read_float(normalAccessor, v, &normal.x, 3);
					}

					if (texcoordAccessor)
					{
						cgltf_accessor_read_float(texcoordAccessor, v, &uv.x, 2);
					}

					if (colorAccessor)
					{
						cgltf_accessor_read_float(colorAccessor, v, &color.x, 3);
					}

					if (tangentAccessor)
					{
						cgltf_accessor_read_float(tangentAccessor, v, &tangent.x, 4);
					}

					positions.EmplaceBack(position);
					normals.EmplaceBack(normal);
					uvs.EmplaceBack(uv);
					colors.EmplaceBack(color);
					tangents.EmplaceBack(tangent);

					if (skinned)
					{
						BoneInfluence bone = {};

						if (jointsAccessor && weightsAccessor)
						{
							cgltf_uint jointIndices[4] = {};
							cgltf_float jointWeights[4] = {};
							cgltf_accessor_read_uint(jointsAccessor, v, jointIndices, 4);
							cgltf_accessor_read_float(weightsAccessor, v, jointWeights, 4);

							for (u32 w = 0; w < 4; w++)
							{
								bone.indices[w] = jointIndices[w];
								bone.weights[w] = jointWeights[w];
							}
						}

						boneInfluences.EmplaceBack(bone);
					}

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

				MeshPrimitive meshPrimitive;
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

			logger.Debug("Mesh '{}': {} vertices, {} indices, {} primitives", meshName, positions.Size(), indices.Size(), primitives.Size());

			MeshImportSettings meshImportSettings = {};

			MeshVertexImportData importData;
			importData.positions = positions;
			importData.normals = normals;
			importData.uv = uvs;
			importData.colors = colors;
			importData.tangents = tangents;
			if (skinned)
			{
				importData.bones = boneInfluences;
			}

			RID skinRID = {};
			if (skinned)
			{
				if (auto it = data.skins.Find(skin))
				{
					skinRID = it->second;
				}
			}

			RID meshRID = ImportMesh(data.directory, meshImportSettings, meshName, materials, primitives, importData, indices, skinRID, Mat4::GetScale(worldTransform), data.scope);

			data.meshes.Insert(mesh, meshRID);
		}

		bool IsJointNode(GLTFImportData& data, cgltf_node* node)
		{
			return data.jointIndexMap.Has(node);
		}

		RID ProcessNode(GLTFImportData& data, cgltf_node* node, const Mat4& parentWorldTransform = Mat4{1.0})
		{
			if (!node)
			{
				return {};
			}

			RID entity = Resources::Create<EntityResource>(UUID::RandomUUID());
			StringView nodeName = node->name ? node->name : "Node";
			logger.Debug("Processing node: '{}' (mesh={}, skin={}, light={}, children={})", nodeName, node->mesh != nullptr, node->skin != nullptr, node->light != nullptr, node->children_count);

			ResourceObject entityObject = Resources::Write(entity);
			entityObject.SetString(EntityResource::Name, nodeName);

			// Extract local transform
			Mat4 localTransform{1.0};

			RID transformRID = Resources::Create<Transform>(UUID::RandomUUID(), data.scope);
			ResourceObject transformObject = Resources::Write(transformRID);

			entityObject.AddToSubObjectList(EntityResource::Components, transformRID);

			if (node->has_matrix)
			{
				localTransform = Mat4(
					Vec4{node->matrix[0], node->matrix[1], node->matrix[2], node->matrix[3]},
					Vec4{node->matrix[4], node->matrix[5], node->matrix[6], node->matrix[7]},
					Vec4{node->matrix[8], node->matrix[9], node->matrix[10], node->matrix[11]},
					Vec4{node->matrix[12], node->matrix[13], node->matrix[14], node->matrix[15]}
				);

				transformObject.SetVec3(Transform::Position, Mat4::GetTranslation(localTransform));
				transformObject.SetQuat(Transform::Rotation, Mat4::GetQuaternion(localTransform));
				transformObject.SetVec3(Transform::Scale, Mat4::GetScale(localTransform));
			}
			else
			{
				Vec3 position{0.0f};
				Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
				Vec3 scale{1.0f};

				if (node->has_translation)
				{
					position = ToVec3(node->translation);
					transformObject.SetVec3(Transform::Position, position);
				}

				if (node->has_rotation)
				{
					rotation = Quat{node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
					transformObject.SetQuat(Transform::Rotation, rotation);
				}

				if (node->has_scale)
				{
					scale = ToVec3(node->scale);
					transformObject.SetVec3(Transform::Scale, scale);
				}

				localTransform = Mat4::Translate(Mat4{1.0}, position) * Quat::ToMatrix4(rotation) * Mat4::Scale(Mat4{1.0}, scale);
			}

			Mat4 worldTransform = parentWorldTransform * localTransform;

			transformObject.Commit(data.scope);

			if (node->mesh)
			{
				ProcessMesh(data, node->mesh, node->skin, worldTransform);

				if (auto it = data.meshes.Find(node->mesh))
				{
					ResourceObject meshObject = Resources::Write(it->second);
					if (RID skin = meshObject.GetSubObject(MeshResource::Skin))
					{
						// Skinned mesh — use SkinnedMeshRenderer and link skeleton
						RID meshRenderer = Resources::Create<SkinnedMeshRenderer>(UUID::RandomUUID());
						ResourceObject skinnedMeshRenderObject = Resources::Write(meshRenderer);
						skinnedMeshRenderObject.SetReference(skinnedMeshRenderObject.GetIndex("mesh"), it->second);

						if (node->skin)
						{
							if (auto skelIt = data.skinSkeletons.Find(node->skin))
							{
								skinnedMeshRenderObject.SetReference(skinnedMeshRenderObject.GetIndex("skeleton"), skelIt->second);
							}
						}

						skinnedMeshRenderObject.Commit(data.scope);
						entityObject.AddToSubObjectList(EntityResource::Components, meshRenderer);
					}
					else
					{
						// Static mesh
						RID meshRenderer = Resources::Create<StaticMeshRenderer>(UUID::RandomUUID());
						ResourceObject staticMeshRenderObject = Resources::Write(meshRenderer);
						staticMeshRenderObject.SetReference(staticMeshRenderObject.GetIndex("mesh"), it->second);
						staticMeshRenderObject.Commit(data.scope);
						entityObject.AddToSubObjectList(EntityResource::Components, meshRenderer);
					}
				}
			}

			if (node->light)
			{
				// glTF KHR_lights_punctual: range == 0 (or omitted) means infinite range.
				// The engine's attenuation is `1 - saturate(d/range)`, so 0 would kill the light — remap to a large value.
				f32 range = node->light->range > 0.0f ? node->light->range : 1000.0f;

				// glTF intensity is candela for point/spot and lux for directional. Convert via luminous efficacy
				// (683 lm/W) to a watts-like scalar so the engine's unitless multiplier sits in a sensible range.
				f32 intensity = node->light->intensity / 683.0f;

				RID lightRID = Resources::Create<LightComponent>(UUID::RandomUUID());
				ResourceObject lightObject = Resources::Write(lightRID);
				lightObject.SetEnum(lightObject.GetIndex("lightType"), ToLightType(node->light->type));
				lightObject.SetColor(lightObject.GetIndex("color"), Color::FromVec3(ToVec3(node->light->color)));
				lightObject.SetFloat(lightObject.GetIndex("intensity"), intensity);
				lightObject.SetFloat(lightObject.GetIndex("range"), range);
				lightObject.SetFloat(lightObject.GetIndex("innerConeAngle"),	Math::Degrees(node->light->spot_inner_cone_angle));
				lightObject.SetFloat(lightObject.GetIndex("outerConeAngle"), Math::Degrees(node->light->spot_outer_cone_angle));
				lightObject.SetBool(lightObject.GetIndex("enableShadows"), true);
				lightObject.Commit(data.scope);

				entityObject.AddToSubObjectList(EntityResource::Components, lightRID);
			}

			// Process OMI_physics_body extension
			ProcessOMIPhysicsBody(data, node, entityObject);

			for (size_t i = 0; i < node->children_count; i++)
			{
				// Skip joint nodes — their data is already in the Skeleton component
				if (IsJointNode(data, node->children[i]))
				{
					continue;
				}
				if (RID childEntity = ProcessNode(data, node->children[i], worldTransform))
				{
					entityObject.AddToSubObjectList(EntityResource::Children, childEntity);
				}

				// Place skeleton entity as sibling of the skinned mesh node (once per skin)
				cgltf_skin* childSkin = node->children[i]->skin;
				if (childSkin && !data.placedSkeletons.Has(childSkin))
				{
					if (auto skelIt = data.skinSkeletons.Find(childSkin))
					{
						entityObject.AddToSubObjectList(EntityResource::Children, skelIt->second);
						data.placedSkeletons.Insert(childSkin);
					}
				}
			}

			entityObject.Commit(data.scope);
			return entity;
		}

		StringView OutputExtension() override
		{
			return ".dcc_asset";
		}

		u32 CookerVersion() override
		{
			return 1;
		}

		void Ingest(IngestContext& ctx) override
		{
			ctx.DeclareSubResource("root", TypeInfo<DCCAsset>::ID());

			String parent = Path::Parent(ctx.sourcePath);
			String sourcePath = ctx.sourcePath;

			cgltf_options options = {};
			cgltf_data*   data = nullptr;
			if (cgltf_parse_file(&options, sourcePath.CStr(), &data) != cgltf_result_success)
			{
				return;
			}

			auto addDep = [&](const char* uri)
			{
				if (!uri || strncmp(uri, "data:", 5) == 0) return;
				String decoded = uri;
				u32    size = cgltf_decode_uri(decoded.begin());
				decoded.Resize(size);
				if (decoded.Empty() || ctx.HasDependency(decoded)) return;
				String abs = Path::Join(parent, decoded);
				if (!FileSystem::GetFileStatus(abs).exists)
				{
					logger.Warn("gltf dependency {} not found", abs);
					return;
				}
				ByteBuffer bytes;
				FileSystem::ReadFileAsByteBuffer(abs, bytes);
				ctx.AddDependency(decoded, Span<u8>(bytes.begin(), bytes.Size()));
			};

			for (cgltf_size i = 0; i < data->buffers_count; i++)
			{
				addDep(data->buffers[i].uri);
			}
			for (cgltf_size i = 0; i < data->images_count; i++)
			{
				addDep(data->images[i].uri);
			}

			cgltf_free(data);
		}

		void Cook(CookContext& ctx) override
		{
			UndoRedoScope* scope = ctx.scope;

			String originalName = "model.gltf";
			if (ResourceObject wrapper = Resources::Read(ctx.importedAsset))
			{
				originalName = wrapper.GetString(ResourceImportedAsset::OriginalFileName);
			}

			String tempDir = Path::Join(Editor::GetProjectTempPath(), String("GltfCook_") + UUID::RandomUUID().ToString());
			FileSystem::CreateDirectory(tempDir);

			String path = Path::Join(tempDir, originalName);
			FileSystem::SaveFileAsByteArray(path, ctx.sourceBytes);

			if (ResourceObject wrapper = Resources::Read(ctx.importedAsset))
			{
				for (RID depRid : wrapper.GetSubObjectList(ResourceImportedAsset::Dependencies))
				{
					ResourceObject dep = Resources::Read(depRid);
					if (!dep) continue;
					String    relPath = dep.GetString(ResourceDependencyEntry::RelPath);
					ByteBuffer bytes = ctx.Dependency(relPath);
					String    depPath = Path::Join(tempDir, relPath);
					FileSystem::CreateDirectory(Path::Parent(depPath));
					FileSystem::SaveFileAsByteArray(depPath, Span<u8>(bytes.begin(), bytes.Size()));
				}
			}

			logger.Info("Starting GLTF import: {}", path);

			cgltf_options options = {};
			cgltf_data*   data = nullptr;
			cgltf_result  result = cgltf_parse_file(&options, path.CStr(), &data);

			if (result != cgltf_result_success)
			{
				logger.Error("Failed to parse GLTF file: {}", path);
				FileSystem::Remove(tempDir);
				return;
			}

			logger.Debug("GLTF file parsed successfully");

			result = cgltf_load_buffers(&options, data, path.CStr());
			if (result != cgltf_result_success)
			{
				logger.Error("Failed to load GLTF buffers: {}", path);
				cgltf_free(data);
				FileSystem::Remove(tempDir);
				return;
			}

			logger.Debug("GLTF buffers loaded ({} buffers)", data->buffers_count);

			String fileName = Path::Name(path);

			GLTFImportData importData = {};
			importData.directory = {};
			importData.basePath = Path::Parent(path);
			importData.scope = scope;
			importData.gltfData = data;

			// Parse OMI physics extensions from document-level data
			ParseOMIPhysicsShapes(importData);
			ParseOMIPhysicsMaterials(importData);

			logger.Debug("Processing {} textures", data->textures_count);
			for (size_t i = 0; i < data->textures_count; i++)
			{
				ProcessTexture(importData, i, &data->textures[i]);
			}

			logger.Debug("Processing {} materials", data->materials_count);
			for (size_t i = 0; i < data->materials_count; i++)
			{
				ProcessMaterial(importData, &data->materials[i]);
			}

			logger.Debug("Building scene hierarchy ({} nodes, {} root nodes)", data->nodes_count, data->scenes_count > 0 ? data->scene->nodes_count : 0);

			// Pre-populate joint index map for all skins so joint nodes can be
			// identified before the scene traversal begins.
			for (cgltf_size s = 0; s < data->skins_count; s++)
			{
				cgltf_skin* skin = &data->skins[s];
				for (cgltf_size j = 0; j < skin->joints_count; j++)
				{
					importData.jointIndexMap.Insert(skin->joints[j], static_cast<u32>(j));
				}
			}

			RID dccAsset = ctx.SubResource("root", TypeInfo<DCCAsset>::ID());
			ResourceObject dccObject = Resources::Write(dccAsset);
			dccObject.SetString(DCCAsset::Name, fileName);

			RID transformRID = Resources::Create<Transform>(UUID::RandomUUID(), scope);
			RID root = Resources::Create<EntityResource>(UUID::RandomUUID(), scope);

			ResourceObject entityObject = Resources::Write(root);
			entityObject.SetString(EntityResource::Name, fileName);
			entityObject.AddToSubObjectList(EntityResource::Components, transformRID);

			for (size_t i = 0; i < data->nodes_count; i++)
			{
				if (data->nodes[i].parent == nullptr && !IsJointNode(importData, &data->nodes[i]))
				{
					if (RID node = ProcessNode(importData, &data->nodes[i]))
					{
						entityObject.AddToSubObjectList(EntityResource::Children, node);
					}

					cgltf_skin* nodeSkin = data->nodes[i].skin;
					if (nodeSkin && !importData.placedSkeletons.Has(nodeSkin))
					{
						if (auto skelIt = importData.skinSkeletons.Find(nodeSkin))
						{
							entityObject.AddToSubObjectList(EntityResource::Children, skelIt->second);
							importData.placedSkeletons.Insert(nodeSkin);
						}
					}
				}
			}

			entityObject.Commit(scope);

			// Process animations
			if (data->animations_count > 0)
			{
				logger.Debug("Processing {} animations", data->animations_count);
				for (size_t i = 0; i < data->animations_count; i++)
				{
					ProcessAnimation(importData, &data->animations[i]);
				}
			}

			// Populate DCCAsset
			dccObject.SetSubObject(DCCAsset::RootEntity, root);
			for (auto& it : importData.meshes)
			{
				dccObject.AddToSubObjectList(DCCAsset::Meshes, it.second);
			}
			for (auto& it : importData.images)
			{
				dccObject.AddToSubObjectList(DCCAsset::Textures, it.second);
			}
			for (auto& it : importData.materials)
			{
				dccObject.AddToSubObjectList(DCCAsset::Materials, it.second);
			}
			for (RID anim : importData.animations)
			{
				dccObject.AddToSubObjectList(DCCAsset::AnimationClips, anim);
			}
			dccObject.Commit(scope);

			logger.Info("GLTF import completed: {} ({} textures, {} materials, {} meshes, {} skins, {} animations, {} nodes)",
				fileName, importData.images.Size(), importData.materials.Size(), importData.meshes.Size(),
				importData.skins.Size(), data->animations_count, data->nodes_count);

			cgltf_free(data);
			FileSystem::Remove(tempDir);
		}
	};

	void RegisterGLTFImporter()
	{
		Reflection::Type<GLTFImporter>();
	}
}
