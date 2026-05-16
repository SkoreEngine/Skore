#pragma once

#include "ResourceCommon.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
	class ReflectType;
	typedef void (*FnObjectFieldEvent)(ConstPtr value, ResourceObject& object);


	enum class ResourceFieldProperties : u32
	{
		None = 0,
		NoUI = 1 << 0,
	};

	ENUM_FLAGS(ResourceFieldProperties, u32)

	class SK_API ResourceField
	{
	public:
		StringView              GetName() const;
		u32                     GetIndex() const;
		u32                     GetSize() const;
		u32                     GetOffset() const;
		ResourceFieldType       GetType() const;
		TypeID                  GetSubType() const;
		FieldProps              GetTypeStaticProps() const;
		ReflectField*           GetReflectField() const;
		ResourceFieldProperties GetProps() const;

		friend class ResourceTypeBuilder;
		friend class ResourceType;
		friend class ResourceObject;

	private:
		String                    name;
		u32                       index = U32_MAX;
		u32                       size = 0;
		u32                       offset = 0;
		ResourceFieldType         type = ResourceFieldType::None;
		ReflectField*             reflectField = nullptr;
		TypeID                    subType = TypeID{};
		ResourceFieldProperties   props = ResourceFieldProperties::None;
		Array<FnObjectFieldEvent> events[static_cast<u32>(ResourceFieldEventType::MAX)];
	};


	class SK_API ResourceType
	{
	public:
		ResourceType(TypeID type, StringView name);
		~ResourceType();
		SK_NO_COPY_CONSTRUCTOR(ResourceType);

		ResourceInstance Allocate() const;

		TypeID               GetID() const;
		StringView           GetName() const;
		StringView					 GetSimpleName() const;
		RID                  GetDefaultValue() const;
		void                 SetDefaultValue(RID defaultValue);
		u32                  GetAllocSize() const;
		u32                  GetVersion() const;
		ReflectType*         GetReflectType() const;
		Span<ResourceField*> GetFields() const;
		bool                 HasFieldWithType(ResourceFieldType fieldType);
		ResourceField*       FindFieldByName(StringView name) const;
		void                 RegisterEvent(ResourceEventType eventType, FnObjectEvent event, VoidPtr userData);
		void                 UnregisterEvent(ResourceEventType eventType, FnObjectEvent event, VoidPtr userData);
		Span<ResourceEvent>  GetEvents(ResourceEventType eventType) const;
		ConstPtr             GetAttribute(TypeID attributeId) const;


		template<typename AttType>
		const AttType* GetAttribute() const
		{
			return static_cast<const AttType*>(GetAttribute(TypeInfo<AttType>::ID()));
		}

		friend class ResourceTypeBuilder;
		friend class ResourceObject;
		friend class Resources;

	private:
		TypeID       type = {};
		u32          version = 0;
		String       name;
		String       simpleName;
		RID          defaultValue;
		u32          allocSize = 0;
		ReflectType* reflectType = nullptr;

		Array<ResourceField*>    fields;
		Array<ResourceEvent>     events[static_cast<u32>(ResourceEventType::MAX)];
		HashMap<TypeID, VoidPtr> attributes;
	};

	struct ResourceInstanceInfo
	{
		ResourceInstance dataOnWrite;
		bool             readOnly;
	};


	class SK_API ResourceTypeBuilder
	{
	public:
		SK_NO_COPY_CONSTRUCTOR(ResourceTypeBuilder);
		ResourceTypeBuilder(ResourceType* resourceType) : resourceType(resourceType) {}

		ResourceTypeBuilder& Field(u32 index, StringView name, ResourceFieldType type);
		ResourceTypeBuilder& Field(u32 index, StringView name, ResourceFieldType type, TypeID subType);
		ResourceTypeBuilder& Field(u32 index, StringView name, ResourceFieldType type, TypeID subType, ResourceFieldProperties props);
		ResourceTypeBuilder& Field(ReflectField* field);
		ResourceTypeBuilder& Attribute(TypeID type, ConstPtr value);

		template <typename Type, typename... Args>
		ResourceTypeBuilder& Attribute(Args&&... args)
		{
			Type value = Type{Traits::Forward<Args>(args)...};
			return Attribute(TypeInfo<Type>::ID(), &value);
		}

		template <auto T>
		ResourceTypeBuilder& Field(ResourceFieldType type, TypeID subType = {})
		{
			return Field(static_cast<u32>(T), GetEnumValueName<T>(), type, subType);
		}

		template <auto T>
		ResourceTypeBuilder& Field(ResourceFieldType type, TypeID subType, ResourceFieldProperties props)
		{
			return Field(static_cast<u32>(T), GetEnumValueName<T>(), type, subType, props);
		}

		ResourceTypeBuilder& Build();

		ResourceType* GetResourceType() const;

	private:
		ResourceType* resourceType = nullptr;
	};
}
