#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/ResourceType.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterResourceTypes()
	{
		Resources::Type<ResourceFile>()
			.Field<ResourceFile::Name>(ResourceFieldType::String)
			.Build();

		// RID
		{
			auto rid = Reflection::Type<RID>();
			rid.Field<&RID::id>("id");
		}

		// UUID
		{
			auto uuid = Reflection::Type<UUID>();
			uuid.Field<&UUID::firstValue>("firstValue");
			uuid.Field<&UUID::secondValue>("secondValue");
			uuid.Function<&UUID::RandomUUID>("RandomUUID");
			uuid.Function<&UUID::FromString>("FromString", "str");
			uuid.Function<&UUID::ToString>("ToString");
		}

		// ResourceFieldType
		{
			auto resourceFieldType = Reflection::Type<ResourceFieldType>();
			resourceFieldType.Value<ResourceFieldType::None>("None");
			resourceFieldType.Value<ResourceFieldType::Bool>("Bool");
			resourceFieldType.Value<ResourceFieldType::Int>("Int");
			resourceFieldType.Value<ResourceFieldType::UInt>("UInt");
			resourceFieldType.Value<ResourceFieldType::Float>("Float");
			resourceFieldType.Value<ResourceFieldType::String>("String");
			resourceFieldType.Value<ResourceFieldType::Vec2>("Vec2");
			resourceFieldType.Value<ResourceFieldType::Vec3>("Vec3");
			resourceFieldType.Value<ResourceFieldType::Vec4>("Vec4");
			resourceFieldType.Value<ResourceFieldType::Quat>("Quat");
			resourceFieldType.Value<ResourceFieldType::Mat4>("Mat4");
			resourceFieldType.Value<ResourceFieldType::Color>("Color");
			resourceFieldType.Value<ResourceFieldType::Enum>("Enum");
			resourceFieldType.Value<ResourceFieldType::Blob>("Blob");
			resourceFieldType.Value<ResourceFieldType::Reference>("Reference");
			resourceFieldType.Value<ResourceFieldType::ReferenceArray>("ReferenceArray");
			resourceFieldType.Value<ResourceFieldType::SubObject>("SubObject");
			resourceFieldType.Value<ResourceFieldType::SubObjectList>("SubObjectList");
			resourceFieldType.Value<ResourceFieldType::Buffer>("Buffer");
			resourceFieldType.Value<ResourceFieldType::TypeID>("TypeID");
		}

		// ResourceFieldEventType
		{
			auto resourceFieldEventType = Reflection::Type<ResourceFieldEventType>();
			resourceFieldEventType.Value<ResourceFieldEventType::OnSubObjectSetAdded>("OnSubObjectSetAdded");
			resourceFieldEventType.Value<ResourceFieldEventType::OnSubObjectSetRemoved>("OnSubObjectSetRemoved");
		}

		// ResourceEventType
		{
			auto resourceEventType = Reflection::Type<ResourceEventType>();
			resourceEventType.Value<ResourceEventType::Changed>("Changed");
			resourceEventType.Value<ResourceEventType::VersionUpdated>("VersionUpdated");
		}

		// CompareSubObjectSetType
		{
			auto compareSubObjectSetType = Reflection::Type<CompareSubObjectSetType>();
			compareSubObjectSetType.Value<CompareSubObjectSetType::Added>("Added");
			compareSubObjectSetType.Value<CompareSubObjectSetType::Removed>("Removed");
		}

		// ResourceField
		{
			auto resourceField = Reflection::Type<ResourceField>();
			resourceField.Function<&ResourceField::GetName>("GetName");
			resourceField.Function<&ResourceField::GetIndex>("GetIndex");
			resourceField.Function<&ResourceField::GetSize>("GetSize");
			resourceField.Function<&ResourceField::GetOffset>("GetOffset");
			resourceField.Function<&ResourceField::GetType>("GetType");
			resourceField.Function<&ResourceField::GetSubType>("GetSubType");
			resourceField.Function<&ResourceField::GetReflectField>("GetReflectField");
		}

		// ResourceType
		{
			auto resourceType = Reflection::Type<ResourceType>();
			resourceType.Function<&ResourceType::GetID>("GetID");
			resourceType.Function<&ResourceType::GetName>("GetName");
			resourceType.Function<&ResourceType::GetSimpleName>("GetSimpleName");
			resourceType.Function<&ResourceType::GetDefaultValue>("GetDefaultValue");
			resourceType.Function<&ResourceType::SetDefaultValue>("SetDefaultValue", "defaultValue");
			resourceType.Function<&ResourceType::GetAllocSize>("GetAllocSize");
			resourceType.Function<&ResourceType::GetVersion>("GetVersion");
			resourceType.Function<&ResourceType::GetReflectType>("GetReflectType");
			resourceType.Function<&ResourceType::GetFields>("GetFields");
			resourceType.Function<&ResourceType::HasFieldWithType>("HasFieldWithType", "fieldType");
			resourceType.Function<&ResourceType::FindFieldByName>("FindFieldByName", "name");
		}

		// ResourceObject
		{
			auto resourceObject = Reflection::Type<ResourceObject>();

			// Setters
			resourceObject.Function<&ResourceObject::SetBool>("SetBool", "index", "value");
			resourceObject.Function<&ResourceObject::SetInt>("SetInt", "index", "value");
			resourceObject.Function<&ResourceObject::SetUInt>("SetUInt", "index", "value");
			resourceObject.Function<&ResourceObject::SetFloat>("SetFloat", "index", "value");
			resourceObject.Function<&ResourceObject::SetString>("SetString", "index", "value");
			resourceObject.Function<&ResourceObject::SetVec2>("SetVec2", "index", "value");
			resourceObject.Function<&ResourceObject::SetVec3>("SetVec3", "index", "value");
			resourceObject.Function<&ResourceObject::SetVec4>("SetVec4", "index", "value");
			resourceObject.Function<&ResourceObject::SetQuat>("SetQuat", "index", "value");
			resourceObject.Function<&ResourceObject::SetMat4>("SetMat4", "index", "value");
			resourceObject.Function<&ResourceObject::SetColor>("SetColor", "index", "value");
			resourceObject.Function<static_cast<void(ResourceObject::*)(u32, i64)>(&ResourceObject::SetEnum)>("SetEnum", "index", "enumValue");
			resourceObject.Function<&ResourceObject::SetTypeID>("SetTypeID", "index", "value");
			resourceObject.Function<&ResourceObject::SetReference>("SetReference", "index", "rid");
			resourceObject.Function<&ResourceObject::SetSubObject>("SetSubObject", "index", "subObject");
			resourceObject.Function<&ResourceObject::SetBlob>("SetBlob", "index", "bytes");
			resourceObject.Function<static_cast<void(ResourceObject::*)(u32, Span<RID>)>(&ResourceObject::SetReferenceArray)>("SetReferenceArray", "index", "refs");
			resourceObject.Function<static_cast<void(ResourceObject::*)(u32, usize, RID)>(&ResourceObject::SetReferenceArray)>("SetReferenceArrayAt", "index", "arrIndex", "ref");
			resourceObject.Function<&ResourceObject::AddToReferenceArray>("AddToReferenceArray", "index", "ref");
			resourceObject.Function<&ResourceObject::ClearReferenceArray>("ClearReferenceArray", "index");
			resourceObject.Function<static_cast<void(ResourceObject::*)(u32, RID)>(&ResourceObject::RemoveFromReferenceArray)>("RemoveFromReferenceArray", "index", "ref");
			resourceObject.Function<static_cast<void(ResourceObject::*)(u32, usize)>(&ResourceObject::RemoveFromReferenceArray)>("RemoveFromReferenceArrayAt", "index", "arrIndex");
			resourceObject.Function<static_cast<void(ResourceObject::*)(u32, RID)>(&ResourceObject::AddToSubObjectList)>("AddToSubObjectList", "index", "subObject");
			resourceObject.Function<&ResourceObject::RemoveFromSubObjectList>("RemoveFromSubObjectList", "index", "subObject");
			resourceObject.Function<&ResourceObject::RemoveSubObject>("RemoveSubObject", "index", "rid");
			resourceObject.Function<&ResourceObject::ResetValue>("ResetValue", "index");

			// Getters
			resourceObject.Function<&ResourceObject::HasValue>("HasValue", "index");
			resourceObject.Function<&ResourceObject::HasValueOnThisObject>("HasValueOnThisObject", "index");
			resourceObject.Function<&ResourceObject::IsValueOverridden>("IsValueOverridden", "index");
			resourceObject.Function<&ResourceObject::GetBool>("GetBool", "index");
			resourceObject.Function<&ResourceObject::GetInt>("GetInt", "index");
			resourceObject.Function<&ResourceObject::GetUInt>("GetUInt", "index");
			resourceObject.Function<&ResourceObject::GetFloat>("GetFloat", "index");
			resourceObject.Function<&ResourceObject::GetString>("GetString", "index");
			resourceObject.Function<&ResourceObject::GetVec2>("GetVec2", "index");
			resourceObject.Function<&ResourceObject::GetVec3>("GetVec3", "index");
			resourceObject.Function<&ResourceObject::GetVec4>("GetVec4", "index");
			resourceObject.Function<&ResourceObject::GetQuat>("GetQuat", "index");
			resourceObject.Function<&ResourceObject::GetMat4>("GetMat4", "index");
			resourceObject.Function<&ResourceObject::GetColor>("GetColor", "index");
			resourceObject.Function<static_cast<i64(ResourceObject::*)(u32) const>(&ResourceObject::GetEnum)>("GetEnum", "index");
			resourceObject.Function<&ResourceObject::GetTypeID>("GetTypeID", "index");
			resourceObject.Function<&ResourceObject::GetSubObject>("GetSubObject", "index");
			resourceObject.Function<&ResourceObject::GetReference>("GetReference", "index");
			resourceObject.Function<&ResourceObject::GetBlob>("GetBlob", "index");
			resourceObject.Function<&ResourceObject::GetReferenceArray>("GetReferenceArray", "index");
			resourceObject.Function<&ResourceObject::HasOnReferenceArray>("HasOnReferenceArray", "index", "rid");
			resourceObject.Function<&ResourceObject::GetSubObjectListCount>("GetSubObjectListCount", "index");
			resourceObject.Function<&ResourceObject::GetSubObjectList>("GetSubObjectList", "index");
			resourceObject.Function<&ResourceObject::HasOnSubObjectList>("HasOnSubObjectList", "index", "rid");
			resourceObject.Function<&ResourceObject::GetPrototypeRemovedCount>("GetPrototypeRemovedCount", "index");
			resourceObject.Function<&ResourceObject::IsRemoveFromPrototypeSubObjectList>("IsRemoveFromPrototypeSubObjectList", "index", "rid");

			// Info / lifecycle
			resourceObject.Function<&ResourceObject::GetIndex>("GetIndex", "fieldName");
			resourceObject.Function<&ResourceObject::GetRID>("GetRID");
			resourceObject.Function<&ResourceObject::GetPrototype>("GetPrototype");
			resourceObject.Function<&ResourceObject::GetUUID>("GetUUID");
			resourceObject.Function<&ResourceObject::GetVersion>("GetVersion");
			resourceObject.Function<&ResourceObject::Commit>("Commit", "scope");
		}

		// Resources
		{
			auto resources = Reflection::Type<Resources>();

			// Type API
			resources.Function<&Resources::FindTypeByID>("FindTypeByID", "typeId");
			resources.Function<&Resources::FindTypeByName>("FindTypeByName", "name");
			resources.Function<&Resources::GetTypes>("GetTypes");
			resources.Function<static_cast<Array<TypeID>(*)(TypeID)>(&Resources::FindTypesByAttribute)>("FindTypesByAttribute", "attributeId");

			// Resource API
			resources.Function<static_cast<RID(*)(TypeID, UUID, UndoRedoScope*)>(&Resources::Create)>("Create", "typeId", "uuid", "scope");
			resources.Function<&Resources::CreateFromPrototype>("CreateFromPrototype", "rid", "uuid", "scope");
			resources.Function<&Resources::Write>("Write", "rid");
			resources.Function<&Resources::Read>("Read", "rid");
			resources.Function<&Resources::HasValue>("HasValue", "rid");
			resources.Function<&Resources::GetParent>("GetParent", "rid");
			resources.Function<&Resources::GetPrototype>("GetPrototype", "rid");
			resources.Function<&Resources::Clone>("Clone", "rid", "uuid", "scope");
			resources.Function<&Resources::Reset>("Reset", "rid", "scope");
			resources.Function<&Resources::Destroy>("Destroy", "rid", "scope");
			resources.Function<&Resources::GetVersion>("GetVersion", "rid");
			resources.Function<&Resources::GetUUID>("GetUUID", "rid");
			resources.Function<&Resources::GetType>("GetType", "rid");
			resources.Function<&Resources::FindByUUID>("FindByUUID", "uuid");
			resources.Function<&Resources::FindOrReserveByUUID>("FindOrReserveByUUID", "uuid");
			resources.Function<&Resources::IsParentOf>("IsParentOf", "parent", "child");
			resources.Function<&Resources::GetResourcesByType>("GetResourcesByType", "type");

			// Path API
			resources.Function<&Resources::SetPath>("SetPath", "rid", "path");
			resources.Function<&Resources::GetPath>("GetPath", "rid");
			resources.Function<&Resources::FindByPath>("FindByPath", "path");
		}

		// UndoRedoScope
		{
			auto undoRedoScope = Reflection::Type<UndoRedoScope>();
			undoRedoScope.Function<&UndoRedoScope::Create>("Create", "name");
			undoRedoScope.Function<&UndoRedoScope::Destroy>("Destroy");
			undoRedoScope.Function<&UndoRedoScope::Undo>("Undo");
			undoRedoScope.Function<&UndoRedoScope::Redo>("Redo");
			undoRedoScope.Function<&UndoRedoScope::GetName>("GetName");
		}
	}
}