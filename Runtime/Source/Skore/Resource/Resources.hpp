#pragma once

#include <memory>
#include <mutex>

#include "ResourceCommon.hpp"
#include "ResourceObject.hpp"
#include "ResourceType.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	struct UndoRedoChange;

	struct SK_API UndoRedoScope
	{
		String                                 name;
		std::mutex                             mutex;
		Array<std::unique_ptr<UndoRedoChange>> changes;

		explicit UndoRedoScope(StringView name);
		~UndoRedoScope();

		void       PushChange(ResourceStorage* storage, ResourceInstance before, ResourceInstance after);
		void       Undo();
		void       Redo();
		StringView GetName() const;
		void       Destroy();

		static UndoRedoScope* Create(StringView name = "");
	};

	class SK_API Resources
	{
	public:
		//type api
		static ResourceTypeBuilder Type(TypeID typeId, StringView name);
		static ResourceType*       FindTypeByID(TypeID typeId);
		static ResourceType*       FindOrCreateTypeByID(TypeID typeId);
		static ResourceType*       FindTypeByName(StringView name);
		static Array<ResourceType*> GetTypes();
		static Array<TypeID>       FindTypesByAttribute(TypeID attributeId);

		template <typename T>
		static ResourceTypeBuilder Type(StringView name)
		{
			return Type(TypeInfo<T>::ID(), name);
		}

		template <typename T>
		static ResourceTypeBuilder Type()
		{
			return Type(TypeInfo<T>::ID(), TypeInfo<T>::Name());
		}

		template <typename T>
		static ResourceType* FindType()
		{
			return FindTypeByID(TypeInfo<T>::ID());
		}

		template <typename T>
		static Array<TypeID> FindTypesByAttribute()
		{
			return FindTypesByAttribute(TypeInfo<T>::ID());
		}

		//resource API
		static RID              Create(TypeID typeId, UUID uuid = {}, UndoRedoScope* scope = nullptr);
		static RID              CreateFromPrototype(RID rid, UUID uuid = {}, UndoRedoScope* scope = nullptr);
		static ResourceStorage* GetStorage(RID rid);
		static ResourceObject   Write(RID rid);
		static ResourceObject   Read(RID rid);
		static bool             HasValue(RID rid);
		static RID              GetParent(RID rid);
		static RID							GetTopParent(RID rid);
		static RID              GetPrototype(RID rid);
		static RID              Clone(RID rid, UUID uuid = {}, UndoRedoScope* scope = nullptr);
		static void             Reset(RID rid, UndoRedoScope* scope = nullptr);
		static void             Destroy(RID rid, UndoRedoScope* scope = nullptr);
		static u64              GetVersion(RID rid);
		static UUID             GetUUID(RID rid);
		static ResourceType*    GetType(RID rid);
		static RID              FindByUUID(const UUID& uuid);
		static RID              FindOrReserveByUUID(const UUID& uuid);
		static bool             IsParentOf(RID parent, RID child);
		static Array<RID>       GetResourcesByType(ResourceType* type);

		//path
		static void       SetPath(RID rid, StringView path);
		static StringView GetPath(RID rid);
		static RID        FindByPath(StringView path);

		static void Serialize(RID rid, ArchiveWriter& writer);
		static RID  Deserialize(ArchiveReader& reader, UndoRedoScope* scope = nullptr);
		static void SerializeTypes(ArchiveWriter& writer);
		static u32  DeserializeTypes(ArchiveReader& reader);

		static bool ToResource(RID rid, ConstPtr instance, UndoRedoScope* scope = nullptr, VoidPtr userData = nullptr);
		static bool FromResource(RID rid, VoidPtr instance, VoidPtr userData = nullptr);
		static bool FromResource(const ResourceObject& object, VoidPtr instance, VoidPtr userData = nullptr);
		static bool FromResource(RID rid, Object* object, VoidPtr userData = nullptr);

		static Array<CompareSubObjectListResult> CompareSubObjectList(const ResourceObject& oldObject, const ResourceObject& newObject, u32 index);
		static void CompareSubObjectList(const ResourceObject& oldObject, const ResourceObject& newObject, u32 index, VoidPtr userData, FnCompareSubObjectListCallback callback);

		template <typename T>
		static RID Create(UUID uuid = {}, UndoRedoScope* scope = nullptr)
		{
			return Create(TypeInfo<T>::ID(), uuid, scope);
		}

		static RID  LoadResources(StringView filePath);
		static void GarbageCollect();
		static void DispatchEvents();
		static void EndFrame();
		static void MigrateResourcesForReflection();
		static void MigrateResourceForType(TypeID typeId);
		static void MigrateResources(ResourceType* oldType, ResourceType* newType);

	private:
		static ResourceType* CreateFromReflectType(ReflectType* reflectType);
	};
}
