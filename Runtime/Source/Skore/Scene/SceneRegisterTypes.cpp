#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/LayerSystem.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneManager.hpp"
#include "Skore/Scene/SceneCommon.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Components/AudioListener.hpp"
#include "Skore/Scene/Components/AudioSource.hpp"
#include "Skore/Scene/Components/BloomComponent.hpp"
#include "Skore/Scene/Components/Camera.hpp"
#include "Skore/Scene/Components/CharacterController.hpp"
#include "Skore/Scene/Components/EnvironmentComponent.hpp"
#include "Skore/Scene/Components/IrradianceVolumeComponent.hpp"
#include "Skore/Scene/Components/SSAOComponent.hpp"
#include "Skore/Scene/Components/LightComponent.hpp"
#include "Skore/Scene/Components/ParticleEmitter.hpp"
#include "Skore/Scene/Components/PhysicShapes.hpp"
#include "Skore/Scene/Components/RenderComponents.hpp"
#include "Skore/Animation/Components/AnimationGraphComponent.hpp"
#include "Skore/Scene/Components/RigidBody.hpp"
#include "Skore/Scene/Components/Transform.hpp"
#include "Skore/Scene/Components/XRComponents.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	void RegisterSceneTypes()
	{
		{
			auto entityFlags = Reflection::Type<EntityFlags>();
			entityFlags.Value<EntityFlags::None>("None");
			entityFlags.Value<EntityFlags::HasTransform2D>("HasTransform2D");
			entityFlags.Value<EntityFlags::HasTransform3D>("HasTransform3D");
			entityFlags.Value<EntityFlags::HasTransformRect>("HasTransformRect");
			entityFlags.Value<EntityFlags::HasPhysics>("HasPhysics");
			entityFlags.Value<EntityFlags::HasGraphics>("HasGraphics");
			entityFlags.Value<EntityFlags::HasCharacterController>("HasCharacterController");
			entityFlags.Value<EntityFlags::HasSkeleton>("HasSkeleton");
			entityFlags.Value<EntityFlags::HasCollisionCallbacks>("HasCollisionCallbacks");
		}

		{
			auto type = Reflection::Type<EntityMobility>();
			type.Value<EntityMobility::Static>("Static");
			type.Value<EntityMobility::Mixed>("Mixed");
			type.Value<EntityMobility::Dynamic>("Dynamic");
		}

		{
			auto componentDesc = Reflection::Type<ComponentDesc>();
			componentDesc.Field<&ComponentDesc::allowMultiple>("allowMultiple");
			componentDesc.Field<&ComponentDesc::dependencies>("dependencies");
			componentDesc.Field<&ComponentDesc::category>("category");
		}

		{
			auto entityEventType = Reflection::Type<EntityEventType>();
			entityEventType.Value<EntityEventType::EntityActivated>("EntityActivated");
			entityEventType.Value<EntityEventType::EntityDeactivated>("EntityDeactivated");
			entityEventType.Value<EntityEventType::EntityParentChanged>("EntityParentChanged");
			entityEventType.Value<EntityEventType::TransformUpdated>("TransformUpdated");
			entityEventType.Value<EntityEventType::ParentTransformUpdated>("ParentTransformUpdated");
			entityEventType.Value<EntityEventType::CollectPhysicsShapes>("CollectPhysicsShapes");
			entityEventType.Value<EntityEventType::CalculateEntityAABB>("CalculateEntityAABB");
			entityEventType.Value<EntityEventType::SkeletonUpdated>("SkeletonUpdated");
			entityEventType.Value<EntityEventType::EntityLayerChanged>("EntityLayerChanged");
		}

		{
			auto collision = Reflection::Type<Collision>();
			collision.Field<&Collision::entity>("entity");
			collision.Field<&Collision::selfEntity>("selfEntity");
			collision.Field<&Collision::contactPoint>("contactPoint");
			collision.Field<&Collision::contactNormal>("contactNormal");
			collision.Field<&Collision::penetrationDepth>("penetrationDepth");
		}

		{
			auto entityEventDesc = Reflection::Type<EntityEventDesc>();
			entityEventDesc.Field<&EntityEventDesc::type>("type");
			entityEventDesc.Field<&EntityEventDesc::flags>("flags");
			entityEventDesc.Field<&EntityEventDesc::eventData>("eventData");
		}

		auto collisionDetectionType = Reflection::Type<CollisionDetectionType>();
		collisionDetectionType.Value<CollisionDetectionType::Discrete>();
		collisionDetectionType.Value<CollisionDetectionType::LinearCast>();

		auto bodyShapeType = Reflection::Type<BodyShapeType>();
		bodyShapeType.Value<BodyShapeType::None>();
		bodyShapeType.Value<BodyShapeType::Plane>();
		bodyShapeType.Value<BodyShapeType::Box>();
		bodyShapeType.Value<BodyShapeType::Sphere>();
		bodyShapeType.Value<BodyShapeType::Capsule>();
		bodyShapeType.Value<BodyShapeType::Cylinder>();
		bodyShapeType.Value<BodyShapeType::Mesh>();
		bodyShapeType.Value<BodyShapeType::Convex>();
		bodyShapeType.Value<BodyShapeType::Terrain>();

		auto collisionEventType = Reflection::Type<CollisionEventType>();
		collisionEventType.Value<CollisionEventType::Enter>();
		collisionEventType.Value<CollisionEventType::Stay>();
		collisionEventType.Value<CollisionEventType::Exit>();
		collisionEventType.Value<CollisionEventType::TriggerEnter>();
		collisionEventType.Value<CollisionEventType::TriggerExit>();

		{
			auto raycastHit = Reflection::Type<RaycastHit>();
			raycastHit.Field<&RaycastHit::entity>("entity");
			raycastHit.Field<&RaycastHit::point>("point");
			raycastHit.Field<&RaycastHit::normal>("normal");
			raycastHit.Field<&RaycastHit::distance>("distance");
		}

		Reflection::Type<Entity>();
		Reflection::Type<Scene>();
		Reflection::Type<SceneManager>();
		Reflection::Type<Physics>();

		Reflection::Type<Component>();
		Reflection::Type<Transform>();
		Reflection::Type<Transform2D>();
		Reflection::Type<Camera>();
		Reflection::Type<RendererComponent>();
		Reflection::Type<StaticMeshRenderer>();
		Reflection::Type<SkinnedMeshRenderer>();
		Reflection::Type<BoneNode>();
		Reflection::Type<Skeleton>();
		Reflection::Type<LightComponent>();
		Reflection::Type<EnvironmentComponent>();
		Reflection::Type<IrradianceVolumeComponent>();
		Reflection::Type<SSAOComponent>();
		Reflection::Type<BloomComponent>();
		Reflection::Type<AnimationPlayer>();
		Reflection::Type<AnimationGraphComponent>(); // new Esoterica-style seam; coexists with AnimationPlayer (Migration Phase A)
		Reflection::Type<RigidBody>();
		Reflection::Type<BoxCollider>();
		Reflection::Type<SphereCollider>();
		Reflection::Type<CapsuleCollider>();
		Reflection::Type<CylinderCollider>();
		Reflection::Type<CharacterController>();
		Reflection::Type<AudioSource>();
		Reflection::Type<AudioListener>();
		Reflection::Type<ParticleEmitter>();

		auto componentProxyApi = Reflection::Type<ComponentProxyApi>();
		componentProxyApi.Field<&ComponentProxyApi::onCreate>("onCreate");
		componentProxyApi.Field<&ComponentProxyApi::onDestroy>("onDestroy");
		componentProxyApi.Field<&ComponentProxyApi::onStart>("onStart");
		componentProxyApi.Field<&ComponentProxyApi::onProcessEvent>("onProcessEvent");

#ifdef SK_ENABLE_ALPHA_FEATURES
		//XR
		Reflection::Type<XROrigin>();
		Reflection::Type<XRNode>();
#endif

		Resources::Type<EntityResource>()
			.Field<EntityResource::Name>(ResourceFieldType::String)
			.Field<EntityResource::Deactivated>(ResourceFieldType::Bool)
			.Field<EntityResource::Locked>(ResourceFieldType::Bool)
			.Field<EntityResource::Layer>(ResourceFieldType::UInt)
			.Field<EntityResource::Components>(ResourceFieldType::SubObjectList)
			.Field<EntityResource::Children>(ResourceFieldType::SubObjectList)
			.Build();

		Resources::Type<SceneResource>()
			.Field<SceneResource::Entities>(ResourceFieldType::SubObjectList, sktypeid(EntityResource))
			.Build();

		Resources::Type<SceneSettings>()
			.Field<SceneSettings::DefaultScene>(ResourceFieldType::Reference, TypeInfo<SceneResource>::ID())
			.Field<SceneSettings::DefaultEditorScene>(ResourceFieldType::Reference, TypeInfo<SceneResource>::ID())
			.Attribute<EditableSettings>(EditableSettings{
				.path = "Engine/Scene Settings",
				.type = TypeInfo<ProjectSettings>::ID(),
				.order = 10
			})
			.Build();

		Resources::Type<LayerSettings>()
			.Field<LayerSettings::Name>(ResourceFieldType::String)
			.Build();

		{
			ResourceType* layerType = Resources::Type<LayerSystemSettings>()
				.Field<LayerSystemSettings::Layers>(ResourceFieldType::SubObjectList, TypeInfo<LayerSettings>::ID())
				.Attribute<EditableSettings>(EditableSettings{
					.path = "Engine/Layer Settings",
					.type = TypeInfo<ProjectSettings>::ID(),
					.order = 15
				})
				.Build()
				.GetResourceType();

			RID rid = Resources::Create<LayerSystemSettings>(UUID::RandomUUID());
			ResourceObject object = Resources::Write(rid);

			RID defaultLayer = Resources::Create<LayerSettings>(UUID::RandomUUID());
			ResourceObject defaultLayerObject = Resources::Write(defaultLayer);
			defaultLayerObject.SetString(LayerSettings::Name, "Default");
			defaultLayerObject.Commit();
			object.AddToSubObjectList(LayerSystemSettings::Layers, defaultLayer);

			object.Commit();
			layerType->SetDefaultValue(rid);
		}

		Resources::Type<CollisionMatrixItem>()
			.Field<CollisionMatrixItem::Index>(ResourceFieldType::UInt)
			.Field<CollisionMatrixItem::Value>(ResourceFieldType::UInt)
			.Build();

		{
			ResourceType* physicsType = Resources::Type<PhysicsSettings>()
				.Field<PhysicsSettings::MaxBodies>(ResourceFieldType::UInt)
				.Field<PhysicsSettings::MaxBodyPairs>(ResourceFieldType::UInt)
				.Field<PhysicsSettings::MaxContactConstraints>(ResourceFieldType::UInt)
				.Field<PhysicsSettings::PhysicsTicksPerSeconds>(ResourceFieldType::UInt)
				.Field<PhysicsSettings::CollisionMatrix>(ResourceFieldType::SubObjectList)
				.Attribute<EditableSettings>(EditableSettings{
					.path = "Engine/Physics Settings",
					.type = TypeInfo<ProjectSettings>::ID(),
					.order = 20
				})
				.Build()
				.GetResourceType();

			RID rid = Resources::Create<PhysicsSettings>(UUID::RandomUUID());
			ResourceObject object = Resources::Write(rid);
			object.SetUInt(PhysicsSettings::MaxBodies, 65536);
			object.SetUInt(PhysicsSettings::MaxBodyPairs, 65536);
			object.SetUInt(PhysicsSettings::MaxContactConstraints, 10240);
			object.SetUInt(PhysicsSettings::PhysicsTicksPerSeconds, 75);

			for (u32 i = 0; i < MaxLayers; ++i)
			{
				RID item = Resources::Create<CollisionMatrixItem>(UUID::RandomUUID());
				ResourceObject itemObject = Resources::Write(item);
				itemObject.SetUInt(CollisionMatrixItem::Index, i);
				itemObject.SetUInt(CollisionMatrixItem::Value, AllLayersMask);
				itemObject.Commit();
				object.AddToSubObjectList(PhysicsSettings::CollisionMatrix, item);
			}

			object.Commit();
			physicsType->SetDefaultValue(rid);
		}
	}
}