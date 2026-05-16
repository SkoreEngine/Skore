
#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Resource/ResourceObject.hpp"

namespace Skore
{
	struct UndoRedoScope;
	struct GPUCommandBuffer;
	struct GPUPipeline;

	constexpr u32 MaxLayers = 64;
	constexpr u64 AllLayersMask = ~0ULL;

	SK_FINLINE u64 LayerToMask(u8 layer)
	{
		return 1ULL << layer;
	}

	SK_FINLINE bool LayerMatchesMask(u8 layer, u64 mask)
	{
		return (LayerToMask(layer) & mask) != 0;
	}

	struct EntityResource
	{
		enum
		{
			Name,
			Deactivated,
			Locked,
			Layer,
			Components,
			Children,
		};

		SK_API static RID GetOrCreateComponent(RID entity, TypeID type, UndoRedoScope* scope);
		SK_API static RID GetOrCreateComponent(ResourceObject& entityObject, TypeID type, UndoRedoScope* scope);
	};

	struct SceneResource
	{
		enum
		{
			Entities
		};
	};

	struct SceneSettings
	{
		enum
		{
			DefaultScene,
			DefaultEditorScene
		};
	};

	struct EntityEventType
	{
		constexpr static i32 EntityActivated = 100;
		constexpr static i32 EntityDeactivated = 110;
		constexpr static i32 EntityParentChanged = 120;
		constexpr static i32 EntityIsSelectedOnEditor = 130;

		constexpr static i32 TransformUpdated = 1000;
		constexpr static i32 ParentTransformUpdated = 1010;

		constexpr static i32 CollectPhysicsShapes = 1100;
		constexpr static i32 ProcessUILayout = 1110;
		constexpr static i32 ProcessUIWidget = 1120;
		constexpr static i32 ProcessUICreation = 1130;
		constexpr static i32 CalculateEntityAABB = 1140;
		constexpr static i32 SkeletonUpdated = 1150;
		constexpr static i32 EntityLayerChanged = 1160;
		constexpr static i32 DrawPhysicsShape = 1170;

	};

	struct DrawPhysicsShapeEvent
	{
		GPUCommandBuffer* cmd = nullptr;
		GPUPipeline*      pipeline = nullptr;
	};

	enum class EntityFlags : u64
	{
		None                   = 0,
		HasTransform2D         = 1 << 0,
		HasTransform3D         = 1 << 1,
		HasTransformRect       = 1 << 2,
		HasPhysics             = 1 << 3,
		HasGraphics            = 1 << 4,
		HasCharacterController = 1 << 5,
		HasSkeleton            = 1 << 6,
		HasCollisionCallbacks  = 1 << 7,
	};

	struct ComponentDesc
	{
		bool          allowMultiple = true;
		Array<TypeID> dependencies{};
		String		  category = "";
	};

	struct EntityEventDesc
	{
		i64     type = 0;
		u64     flags = 0;
		VoidPtr eventData = nullptr;
	};
}
