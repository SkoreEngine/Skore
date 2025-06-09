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

#pragma once
#include "Array.hpp"
#include "Span.hpp"
#include "StringView.hpp"
#include "TypeInfo.hpp"
#include "Object.hpp"
#include "HashMap.hpp"

#include "Serialization.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Resource/ResourceReflection.hpp"

namespace Skore
{
	template <typename T>
	class NativeReflectType;

	class SK_API ReflectAttribute
	{
	public:
		ReflectAttribute(const TypeProps& typeProps) : typeProps(typeProps) {}

		typedef ConstPtr (*FnGetValue)(const ReflectAttribute* handler);

		const TypeProps& GetProps() const;
		ConstPtr         GetPointer() const;

		friend class ReflectAttributeBuilder;

	private:
		TypeProps  typeProps = {};
		FnGetValue fnGetValue = nullptr;
	};

	class SK_API ReflectAttributeHolder
	{
	public:
		virtual ~ReflectAttributeHolder();

		ConstPtr GetAttribute(TypeID attributeId) const;

		template <typename AttType>
		const AttType* GetAttribute() const
		{
			return static_cast<const AttType*>(GetAttribute(TypeInfo<AttType>::ID()));
		}

	protected:
		Array<ReflectAttribute*> attributeArray{};
	};

	class SK_API ReflectValue
	{
	public:
		explicit ReflectValue(StringView valueDesc) : valueDesc(valueDesc) {}


		typedef ConstPtr (*FnGetValue)(const ReflectValue* reflectValue);
		typedef i64 (*     FnGetCode)(const ReflectValue* reflectValue);
		typedef bool (*    FnCompare)(const ReflectValue* reflectValue, ConstPtr value);

		StringView GetDesc() const;
		i64        GetCode() const;
		ConstPtr   GetValue() const;
		bool       Compare(ConstPtr value) const;

		friend class ReflectValueBuilder;

	private:
		String     valueDesc;
		FnGetValue fnGetValue;
		FnGetCode  fnGetCode;
		FnCompare  fnCompare;
	};

	class SK_API ReflectParam
	{
	public:
		ReflectParam(StringView name, const FieldProps& props) : name(name), props(props) {}
		const FieldProps& GetProps() const;
		StringView        GetName() const;

	private:
		String     name;
		FieldProps props;
	};

	class SK_API ReflectConstructor
	{
	public:
		ReflectConstructor() = default;
		SK_NO_COPY_CONSTRUCTOR(ReflectConstructor);

		typedef void (*   PlacementNewFn)(const ReflectConstructor* constructor, VoidPtr memory, VoidPtr* params);
		typedef Object* (*NewObjectFn)(const ReflectConstructor* constructor, Allocator* allocator, VoidPtr* params);

		friend class ReflectTypeBuilder;
		friend class ReflectConstructorBuilder;

		Span<ReflectParam*> GetParams() const;
		Object*             NewObject(Allocator* allocator, VoidPtr* params) const;
		void                Construct(VoidPtr memory, VoidPtr* params) const;

		~ReflectConstructor();

	private:
		PlacementNewFn       placementNewFn = nullptr;
		NewObjectFn          newInstanceFn = nullptr;
		Array<ReflectParam*> params;
	};

	class SK_API ReflectField : public ReflectAttributeHolder
	{
	public:
		typedef void (*FnCopy)(const ReflectField* field, ConstPtr src, VoidPtr dest);
		typedef void (*FnGet)(const ReflectField* field, ConstPtr instance, VoidPtr dest, usize destSize);
		typedef void (*FnSet)(const ReflectField* field, VoidPtr instance, ConstPtr src, usize srcSize);
		typedef void (*FnSerialize)(ArchiveWriter& writer, const ReflectField* field, ConstPtr src);
		typedef void (*FnDeserialize)(ArchiveReader& reader, const ReflectField* field, VoidPtr instance);
		typedef void (*FnToResource)(const ReflectField* field, ResourceObject& resourceObject, u32 index, ConstPtr instance, UndoRedoScope* scope);
		typedef void (*FnFromResource)(const ReflectField* field, const ResourceObject& resourceObject, u32 index, VoidPtr instance);
		typedef ResourceFieldInfo (*FnGetResourceFieldInfo)(const ReflectField* field);


		typedef const Object* (*FnGetObject)(const ReflectField* field, ConstPtr instance);

		ReflectField(const FieldProps& props, StringView name, u32 index) : m_name(name), m_index(index), m_props(props) {}

		StringView        GetName() const;
		u32               GetIndex() const;
		void              CopyFromType(ConstPtr src, VoidPtr dest) const;
		void              Get(ConstPtr instance, VoidPtr dest, usize destSize) const;
		const Object*     GetObject(ConstPtr instance) const;
		void              Set(VoidPtr instance, ConstPtr src, usize srcSize) const;
		const FieldProps& GetProps() const;
		void              Serialize(ArchiveWriter& writer, ConstPtr instance) const;
		void              Deserialize(ArchiveReader& reader, VoidPtr instance) const;

		ResourceFieldInfo GetResourceFieldInfo() const;
		void              ToResource(ResourceObject& resourceObject, u32 index, ConstPtr instance, UndoRedoScope* scope) const;
		void              FromResource(const ResourceObject& resourceObject, u32 index, VoidPtr instance) const;

		friend class ReflectFieldBuilder;

		template <typename T>
		void Set(VoidPtr instance, const T& value) const
		{
			Set(instance, &value, sizeof(T));
		}

		template <typename T>
		void Get(VoidPtr instance, T& value) const
		{
			Get(instance, &value, sizeof(T));
		}

	private:
		String                 m_name;
		u32                    m_index = U32_MAX;
		FnCopy                 m_copy = nullptr;
		FnGet                  m_get = nullptr;
		FnSet                  m_set = nullptr;
		FnGetObject            m_getObject = nullptr;
		FieldProps             m_props = {};
		FnSerialize            m_serialize = nullptr;
		FnDeserialize          m_deserialize = nullptr;
		FnToResource           m_toResource = nullptr;
		FnFromResource         m_fromResource = nullptr;
		FnGetResourceFieldInfo m_getResourceFieldInfo = nullptr;
	};

	class SK_API ReflectFunction
	{
	public:
		~ReflectFunction();
		typedef void (*FnInvoke)(const ReflectFunction* function, VoidPtr instance, VoidPtr ret, VoidPtr* params);

		ReflectFunction(StringView name) : name(name) {}
		StringView          GetName() const;
		FieldProps          GetReturn() const;
		Span<ReflectParam*> GetParams() const;

		void Invoke(VoidPtr instance, VoidPtr ret, VoidPtr* params) const;

		friend class ReflectFunctionBuilder;

	private:
		String     name;
		String     simpleName{};
		FnInvoke   invoke = nullptr;
		FieldProps returnProps = {};
		VoidPtr    functionPointer = nullptr;

		Array<ReflectParam*> params;
	};

	class SK_API ReflectType : public ReflectAttributeHolder
	{
	public:
		typedef void (*FnDestroy)(const ReflectType* type, Allocator* allocator, VoidPtr instance);
		typedef void (*FnDestructor)(const ReflectType* type, VoidPtr instance);
		typedef void (*FnBatchDestructor)(const ReflectType* type, VoidPtr data, usize count);
		typedef void (*FnCopy)(const ReflectType* type, ConstPtr source, VoidPtr dest);

		ReflectType(StringView name, TypeProps props);
		~ReflectType() override;

		SK_NO_COPY_CONSTRUCTOR(ReflectType);

		StringView       GetName() const;
		StringView       GetSimpleName() const;
		StringView       GetScope() const;
		u32              GetVersion() const;
		const TypeProps& GetProps() const;
		bool             IsDerivedOf(TypeID typeId) const;

		ReflectConstructor*       FindConstructor(Span<TypeID> ids) const;
		Span<ReflectConstructor*> GetConstructors() const;
		ReflectConstructor*       GetDefaultConstructor() const;

		ReflectField*       FindField(StringView fieldName) const;
		Span<ReflectField*> GetFields() const;

		Span<ReflectFunction*> FindFunctionByName(StringView functionName) const;
		ReflectFunction*       FindFunction(StringView functionName, Span<TypeID> ids) const;
		ReflectFunction*       FindFunction(StringView functionName) const;
		Span<ReflectFunction*> GetFunctions() const;

		ReflectValue*       FindValueByName(StringView valueName) const;
		ReflectValue*       FindValueByCode(i64 code) const;
		ReflectValue*       FindValue(ConstPtr value) const;
		Span<ReflectValue*> GetValues() const;

		void Destroy(VoidPtr instance, Allocator* allocator = MemoryGlobals::GetDefaultAllocator()) const;
		void Destructor(VoidPtr instance) const;
		void BatchDestructor(VoidPtr data, usize count) const;
		void Copy(ConstPtr source, VoidPtr dest) const;
		void DeepCopy(ConstPtr source, VoidPtr dest) const;

		Object* NewObject(Allocator* allocator = MemoryGlobals::GetDefaultAllocator()) const
		{
			if (ReflectConstructor* constructor = GetDefaultConstructor())
			{
				return constructor->NewObject(allocator, nullptr);
			}
			return nullptr;
		}

		template <typename... Args>
		Object* NewObject(Allocator* allocator, Args&&... args) const
		{
			Span<TypeID> ids = {TypeInfo<Args>::ID()...,};
			if (ReflectConstructor* constructor = FindConstructor(ids))
			{
				VoidPtr params[] = {&args...};
				return constructor->NewObject(allocator, params);
			}
			return nullptr;
		}

		friend class ReflectTypeBuilder;
		friend class Reflection;

	private:
		String        name;
		TypeProps     props;
		String        simpleName;
		u32           version = 0;
		String        scope;
		Array<TypeID> baseTypes;

		Array<ReflectConstructor*>               constructors;
		ReflectConstructor*                      defaultConstructor{};
		Array<ReflectField*>                     fields;
		Array<ReflectFunction*>                  functions;
		Array<ReflectValue*>                     values;
		HashMap<String, Array<ReflectFunction*>> functionsByName;

		FnDestroy         fnDestroy{};
		FnCopy            fnCopy{};
		FnDestructor      fnDestructor{};
		FnBatchDestructor fnBatchDestructor{};
	};

	class SK_API ReflectValueBuilder
	{
	public:
		ReflectValueBuilder(ReflectValue* reflectValue) : reflectValue(reflectValue) {}

		void SetFnGetValue(ReflectValue::FnGetValue fnGetValue);
		void SerFnGetCode(ReflectValue::FnGetCode fnGetCode);
		void SetFnCompare(ReflectValue::FnCompare fnCompare);

	private:
		ReflectValue* reflectValue;
	};

	class SK_API ReflectAttributeBuilder
	{
	public:
		void SetGetValue(ReflectAttribute::FnGetValue value);
		ReflectAttributeBuilder(ReflectAttribute* attribute);

	private:
		ReflectAttribute* attribute;
	};

	class SK_API ReflectConstructorBuilder
	{
	public:
		friend class ReflectTypeBuilder;

		void SetPlacementNewFn(ReflectConstructor::PlacementNewFn placementNew);
		void SetNewObjectFn(ReflectConstructor::NewObjectFn newInstance);

	private:
		ReflectConstructorBuilder(ReflectConstructor* constructor) : constructor(constructor) {}
		ReflectConstructor* constructor;
	};

	class SK_API ReflectFunctionBuilder
	{
	public:
		void AddParams(StringView* names, FieldProps* props, u32 size);
		friend class ReflectTypeBuilder;

		void SetFnInvoke(ReflectFunction::FnInvoke fnInvoke);
		void SetFunctionPointer(VoidPtr functionPointer);
		void SetReturnProps(FieldProps returnProps);

	private:
		ReflectFunctionBuilder(ReflectFunction* function) : function(function) {}
		ReflectFunction* function;
	};

	class SK_API ReflectFieldBuilder
	{
	public:
		friend class ReflectTypeBuilder;
		void SetSerializer(ReflectField::FnSerialize serialize);
		void SetDeserialize(ReflectField::FnDeserialize deserialize);
		void SetCopy(ReflectField::FnCopy copy);
		void SetGet(ReflectField::FnGet get);
		void SetGetObject(ReflectField::FnGetObject getObject);
		void SetFnSet(ReflectField::FnSet set);
		void SetFnToResource(ReflectField::FnToResource fnToResource);
		void SetFnFromResource(ReflectField::FnFromResource fnGetFromResource);
		void SetFnGetResourceFieldInfo(ReflectField::FnGetResourceFieldInfo fnGetResourceField);

		ReflectAttributeBuilder AddAttribute(const TypeProps& props);

	private:
		ReflectFieldBuilder(ReflectField* field) : field(field) {}
		ReflectField* field;
	};

	class SK_API ReflectTypeBuilder
	{
	public:
		friend class Reflection;
		ReflectFieldBuilder       AddField(const FieldProps& props, StringView name);
		ReflectFunctionBuilder    AddFunction(StringView name);
		ReflectConstructorBuilder AddConstructor(FieldProps* props, StringView* names, u32 size);
		ReflectAttributeBuilder   AddAttribute(const TypeProps& props);
		ReflectValueBuilder       AddValue(StringView valueDesc);

		void SetFnDestroy(ReflectType::FnDestroy fnDestroy);
		void SetFnCopy(ReflectType::FnCopy fnCopy);
		void SetFnDestructor(ReflectType::FnDestructor destructor);
		void SetFnBatchDestructor(ReflectType::FnBatchDestructor batchDestructor);

		void AddBaseType(TypeID typeId);

	private:
		ReflectTypeBuilder(ReflectType* type) : type(type) {}
		ReflectType* type;
	};

	class SK_API Reflection
	{
	public:
		static ReflectTypeBuilder RegisterType(StringView name, TypeProps props);

		static ReflectType*  FindTypeByName(StringView name);
		static ReflectType*  FindTypeById(TypeID typeId);
		static Array<TypeID> GetDerivedTypes(TypeID typeId);
		static Span<TypeID>  GetTypesAnnotatedWith(TypeID typeId);

		static void PushGroup(StringView name);
		static void PopGroup();

		static void Export(StringView path);

		template <typename T>
		static decltype(auto) Type(StringView name);

		template <typename T>
		static decltype(auto) Type();


		template <typename T>
		static ReflectType* FindType()
		{
			return FindTypeById(TypeInfo<T>::ID());
		}
	};


	struct GroupScope
	{
		GroupScope(StringView name)
		{
			Reflection::PushGroup(name);
		}

		~GroupScope()
		{
			Reflection::PopGroup();
		}
	};


	template <typename Owner, typename Type>
	class NativeReflectAttribute
	{
	public:
		inline static Type s_Value{};

		NativeReflectAttribute(ReflectAttributeBuilder builder)
		{
			builder.SetGetValue(FnGetValueImpl);
		}

	private:
		static ConstPtr FnGetValueImpl(const ReflectAttribute* handler)
		{
			return &s_Value;
		}
	};


	template <auto Value, typename Type, typename Owner>
	class NativeReflectValue
	{
	public:
		NativeReflectValue(ReflectValueBuilder builder)
		{
			builder.SetFnGetValue(&FnGetValueImpl);
			builder.SerFnGetCode(&FnGetCodeImpl);
			builder.SetFnCompare(&FnCompareImpl);
		}

	private:
		static constexpr Type value = Value;

		static ConstPtr FnGetValueImpl(const ReflectValue* reflectValue)
		{
			return &value;
		}

		static i64 FnGetCodeImpl(const ReflectValue* reflectValue)
		{
			return static_cast<i64>(Value);
		}

		static bool FnCompareImpl(const ReflectValue* reflectValue, ConstPtr value)
		{
			return *static_cast<const Type*>(reflectValue->GetValue()) == *static_cast<const Type*>(value);
		}
	};

	template <typename Owner, typename... Args>
	class NativeReflectConstructor
	{
	public:
		NativeReflectConstructor(ReflectConstructorBuilder builder) : builder(builder)
		{
			builder.SetPlacementNewFn(&PlacementNewImpl);
			builder.SetNewObjectFn(NewObjectImpl);
		}

	private:
		ReflectConstructorBuilder builder;

		template <typename... Vals>
		static void Eval(Owner* owner, VoidPtr* params, Traits::IndexSequence<>, Vals&&... vals)
		{
			if constexpr (Traits::IsAggregate<Owner>)
			{
				new(PlaceHolder(), owner) Owner{Traits::Forward<Vals>(vals)...};
			}
			else
			{
				new(PlaceHolder(), owner) Owner(Traits::Forward<Vals>(vals)...);
			}
		}

		template <typename T, typename... Tp, usize I, usize... Is, typename... Vls>
		static void Eval(Owner* owner, VoidPtr* params, Traits::IndexSequence<I, Is...> seq, Vls&&... vls)
		{
			return Eval<Tp...>(owner, params, Traits::IndexSequence<Is...>(), Traits::Forward<Vls>(vls)..., *static_cast<T*>(params[I]));
		}

		static void PlacementNewImpl(const ReflectConstructor* constructor, VoidPtr memory, VoidPtr* params)
		{
			const usize size = sizeof...(Args);
			Eval<Args...>(static_cast<Owner*>(memory), params, Traits::MakeIntegerSequence<usize, size>{});
		}

		static Object* NewObjectImpl(const ReflectConstructor* constructor, Allocator* allocator, VoidPtr* params)
		{
			if constexpr (Traits::IsBaseOf<Object, Owner>)
			{
				Owner* ptr = static_cast<Owner*>(allocator->MemAlloc(allocator->allocator, sizeof(Owner)));
				PlacementNewImpl(constructor, ptr, params);
				return ptr;
			}
			return nullptr;
		}
	};

	template <typename Return, typename... Args>
	class NativeReflectFunctionBase
	{
	public:
		constexpr static u32 params = sizeof...(Args);

		static void AddParams(ReflectFunctionBuilder& builder) {}

		template <typename... TNames>
		static void AddParams(ReflectFunctionBuilder& builder, TNames&&... tnames)
		{
			FieldProps props[] = {GetFieldProps<void, Args>()...,};
			StringView names[] = {tnames...,};
			builder.AddParams(names, props, sizeof...(Args));
		}

	protected:
		NativeReflectFunctionBase(ReflectFunctionBuilder builder): builder(builder)
		{
			builder.SetReturnProps(GetFieldProps<void, Return>());
		}

		ReflectFunctionBuilder builder;
	};

	template <auto fp, typename Return, typename... Args>
	class NativeReflectFunction : public NativeReflectFunctionBase<Return, Args...>
	{
	public:
		NativeReflectFunction(const ReflectFunctionBuilder& builder) : NativeReflectFunctionBase<Return, Args...>(builder)
		{
			builder.SetFnInvoke(InvokeImpl);
			builder.SetFunctionPointer(reinterpret_cast<VoidPtr>(&FunctionImpl));
		}

	private:
		static void InvokeImpl(const ReflectFunction* func, VoidPtr instance, VoidPtr ret, VoidPtr* params)
		{
			u32 i{sizeof...(Args)};
			if constexpr (Traits::IsSame<Return, void>)
			{
				fp(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
			}
			else
			{
				*static_cast<Traits::RemoveReference<Return>*>(ret) = fp(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
			}
		}

		static Return FunctionImpl(const ReflectFunction* func, VoidPtr instance, Args... args)
		{
			return fp(args...);
		}
	};

	template <auto mfp, typename Return, typename Owner, typename... Args>
	class NativeReflectMemberFunction : public NativeReflectFunctionBase<Return, Args...>
	{
	public:
		NativeReflectMemberFunction(ReflectFunctionBuilder builder) : NativeReflectFunctionBase<Return, Args...>(builder)
		{
			builder.SetFnInvoke(InvokeImpl);
			builder.SetFunctionPointer(reinterpret_cast<VoidPtr>(&FunctionImpl));
		}

	private:
		static void InvokeImpl(const ReflectFunction* func, VoidPtr instance, VoidPtr ret, VoidPtr* params)
		{
			u32 i{sizeof...(Args)};
			if constexpr (Traits::IsSame<Return, void>)
			{
				(static_cast<Owner*>(instance)->*mfp)(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
			}
			else if constexpr (!std::is_reference_v<Return>)
			{
				*static_cast<Return*>(ret) = (static_cast<Owner*>(instance)->*mfp)(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
			}
			else if constexpr (std::is_reference_v<Return>)
			{
				*static_cast<Traits::RemoveAll<Return>*>(ret) = (static_cast<Owner*>(instance)->*mfp)(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
			}
		}

		static Return FunctionImpl(const ReflectFunction* func, VoidPtr instance, Args... args)
		{
			return (static_cast<Owner*>(instance)->*mfp)(args...);
		}
	};

	template <auto mfp, auto getFp, auto setFp, typename Owner, typename FieldType>
	class NativeReflectField
	{
	public:
		NativeReflectField(ReflectFieldBuilder builder) : builder(builder)
		{
			builder.SetSerializer(FnSerializeImpl);
			builder.SetDeserialize(FnDeserializeImpl);
			builder.SetFnToResource(FnToResourceImpl);
			builder.SetFnFromResource(FnFromResourceImpl);
			builder.SetFnGetResourceFieldInfo(FnGetResourceFieldInfo);
			builder.SetCopy(FnCopyImpl);
			builder.SetGet(GetImpl);
			builder.SetGetObject(FnGetObjectImpl);
			builder.SetFnSet(SetImpl);
		}

		template <typename AttrType, typename... Args>
		decltype(auto) Attribute(Args&&... args)
		{
			NativeReflectAttribute<NativeReflectField, AttrType>::s_Value = AttrType{Traits::Forward<Args>(args)...,};
			NativeReflectAttribute<NativeReflectField, AttrType>(builder.AddAttribute(TypeInfo<AttrType>::GetProps()));

			return *this;
		}

	private:
		ReflectFieldBuilder builder;

		static void FnSerializeImpl(ArchiveWriter& writer, const ReflectField* field, ConstPtr instance)
		{
			SerializeField<FieldType>::Write(writer, field->GetName(), &(static_cast<const Owner*>(instance)->*mfp));
		}

		static void FnDeserializeImpl(ArchiveReader& reader, const ReflectField* field, VoidPtr instance)
		{
			FieldType value = {};
			SerializeField<FieldType>::Get(reader, &value);
			(*static_cast<Owner*>(instance).*setFp)(Traits::Forward<FieldType>(value));
		}

		static void FnToResourceImpl(const ReflectField* field, ResourceObject& resourceObject, u32 index, ConstPtr instance, UndoRedoScope* scope)
		{
			if constexpr (ResourceCast<FieldType>::hasSpecialization)
			{
				ResourceCast<FieldType>::ToResource(resourceObject, index, scope, static_cast<const Owner*>(instance)->*mfp);
			}
		}

		static void FnFromResourceImpl(const ReflectField* field, const ResourceObject& resourceObject, u32 index, VoidPtr instance)
		{
			if constexpr (ResourceCast<FieldType>::hasSpecialization)
			{
				FieldType value = {};
				ResourceCast<FieldType>::FromResource(resourceObject, index, value);
				(*static_cast<Owner*>(instance).*setFp)(Traits::Forward<FieldType>(value));
			}
		}

		static ResourceFieldInfo FnGetResourceFieldInfo(const ReflectField* field)
		{
			if constexpr (ResourceCast<FieldType>::hasSpecialization)
			{
				return ResourceCast<FieldType>::GetResourceFieldInfo();
			}
			return {ResourceFieldType::None};
		}

		static void FnCopyImpl(const ReflectField* field, ConstPtr src, VoidPtr dest)
		{
			if constexpr (Traits::IsCopyConstructible<FieldType>)
			{
				(*static_cast<Owner*>(dest).*setFp)((*static_cast<const Owner*>(src).*getFp)());
			}
		}

		static void SetImpl(const ReflectField* field, VoidPtr instance, ConstPtr src, usize srcSize)
		{
			if (sizeof(FieldType) <= srcSize)
			{
				(*static_cast<Owner*>(instance).*setFp)(*static_cast<const FieldType*>(src));
			}
		}

		static void GetImpl(const ReflectField* field, ConstPtr instance, VoidPtr dest, usize destSize)
		{
			if (sizeof(FieldType) <= destSize)
			{
				const FieldType& vl = (*static_cast<const Owner*>(instance).*getFp)();
				memcpy(dest, &vl, sizeof(vl));
			}
		}

		static const Object* FnGetObjectImpl(const ReflectField* field, ConstPtr instance)
		{
			if constexpr (std::is_base_of_v<Object, FieldType> && std::is_reference_v<Traits::FunctionReturnType<getFp>>)
			{
				return &(*static_cast<const Owner*>(instance).*getFp)();
			}
			return nullptr;
		}
	};

	template <auto mfp, typename Owner, typename FieldType>
	class NativeReflectField<mfp, nullptr, nullptr, Owner, FieldType>
	{
	public:
		static_assert(Traits::IsComplete<Traits::RemoveAll<FieldType>> || std::is_void_v<Traits::RemoveAll<FieldType>>, "fields cannot be incomplete type");
		SK_NO_COPY_CONSTRUCTOR(NativeReflectField);

		NativeReflectField(ReflectFieldBuilder builder) : builder(builder)
		{
			builder.SetSerializer(FnSerializeImpl);
			builder.SetDeserialize(FnDeserializeImpl);
			builder.SetFnToResource(FnToResourceImpl);
			builder.SetFnFromResource(FnFromResourceImpl);
			builder.SetFnGetResourceFieldInfo(FnGetResourceFieldInfo);
			builder.SetCopy(FnCopyImpl);
			builder.SetGet(GetImpl);
			builder.SetGetObject(FnGetObjectImpl);
			builder.SetFnSet(SetImpl);
		}

		template <typename AttrType, typename... Args>
		decltype(auto) Attribute(Args&&... args)
		{
			NativeReflectAttribute<NativeReflectField, AttrType>::s_Value = AttrType{Traits::Forward<Args>(args)...,};
			NativeReflectAttribute<NativeReflectField, AttrType>(builder.AddAttribute(TypeInfo<AttrType>::GetProps()));

			return *this;
		}

	private:
		ReflectFieldBuilder builder;

		static void FnSerializeImpl(ArchiveWriter& writer, const ReflectField* field, ConstPtr instance)
		{
			SerializeField<FieldType>::Write(writer, field->GetName(), &(static_cast<const Owner*>(instance)->*mfp));
		}

		static void FnDeserializeImpl(ArchiveReader& reader, const ReflectField* field, VoidPtr instance)
		{
			SerializeField<FieldType>::Get(reader, &(static_cast<Owner*>(instance)->*mfp));
		}

		static void FnToResourceImpl(const ReflectField* field, ResourceObject& resourceObject, u32 index, ConstPtr instance, UndoRedoScope* scope)
		{
			if constexpr (ResourceCast<FieldType>::hasSpecialization)
			{
				ResourceCast<FieldType>::ToResource(resourceObject, index, scope, static_cast<const Owner*>(instance)->*mfp);
			}
		}

		static void FnFromResourceImpl(const ReflectField* field, const ResourceObject& resourceObject, u32 index, VoidPtr instance)
		{
			if constexpr (ResourceCast<FieldType>::hasSpecialization)
			{
				ResourceCast<FieldType>::FromResource(resourceObject, index, static_cast<Owner*>(instance)->*mfp);
			}
		}

		static ResourceFieldInfo FnGetResourceFieldInfo(const ReflectField* field)
		{
			if constexpr (ResourceCast<FieldType>::hasSpecialization)
			{
				return ResourceCast<FieldType>::GetResourceFieldInfo();
			}
			return {ResourceFieldType::None};
		}

		static void FnCopyImpl(const ReflectField* field, ConstPtr src, VoidPtr dest)
		{
			if constexpr (Traits::IsCopyConstructible<FieldType>)
			{
				static_cast<Owner*>(dest)->*mfp = static_cast<const Owner*>(src)->*mfp;
			}
		}

		static void SetImpl(const ReflectField* field, VoidPtr instance, ConstPtr src, usize srcSize)
		{
			if (sizeof(FieldType) <= srcSize)
			{
				static_cast<Owner*>(instance)->*mfp = *static_cast<const FieldType*>(src);
			}
		}

		static void GetImpl(const ReflectField* field, ConstPtr instance, VoidPtr dest, usize destSize)
		{
			if (sizeof(FieldType) <= destSize)
			{
				memcpy(dest, &(static_cast<const Owner*>(instance)->*mfp), sizeof(FieldType));
			}
		}

		static const Object* FnGetObjectImpl(const ReflectField* field, ConstPtr instance)
		{
			return nullptr;
		}
	};

	template <auto mfp, auto getFp, auto setFp, typename Field>
	struct FieldTemplateDecomposer {};

	template <auto mfp, auto getFp, auto setFp, typename Owner, typename FieldType>
	struct FieldTemplateDecomposer<mfp, getFp, setFp, FieldType Owner::*>
	{
		using Type = FieldType;
		using DescType = NativeReflectField<mfp, getFp, setFp, Owner, FieldType>;
	};

	template <typename, typename = void>
	struct HasRegisterTypeImpl : Traits::FalseType {};

	template <typename T>
	struct HasRegisterTypeImpl<T, Traits::VoidType<decltype(static_cast<void(*)(NativeReflectType<T>&)>(&T::RegisterType))>> : Traits::TrueType {};

	template <typename T>
	constexpr bool HasRegisterType = HasRegisterTypeImpl<T>::value;

	template <auto func, typename Function>
	struct FunctionTemplateDecomposer {};

	template <auto mfp, typename Return, typename Owner, typename... Args>
	struct FunctionTemplateDecomposer<mfp, Return(Owner::*)(Args...)>
	{
		using Type = NativeReflectMemberFunction<mfp, Return, Owner, Args...>;
	};

	template <auto mfp, typename Return, typename Owner, typename... Args>
	struct FunctionTemplateDecomposer<mfp, Return(Owner::*)(Args...) const>
	{
		using Type = NativeReflectMemberFunction<mfp, Return, Owner, Args...>;
	};

	template <auto fp, typename Return, typename... Args>
	struct FunctionTemplateDecomposer<fp, Return(*)(Args...)>
	{
		using Type = NativeReflectFunction<fp, Return, Args...>;
	};


	template <typename Type>
	class NativeReflectType
	{
	public:
		static_assert(Traits::IsComplete<Traits::RemoveAll<Type>>, "type cannot be incomplete");

		SK_NO_COPY_CONSTRUCTOR(NativeReflectType);

		NativeReflectType(ReflectTypeBuilder builder, bool registerHandlers = true) : builder(builder), registerHandlers(registerHandlers)
		{
			if (registerHandlers)
			{
				if constexpr (std::is_constructible_v<Type>)
				{
					this->Constructor();
				}

				builder.SetFnDestroy(&DestroyImpl);
				builder.SetFnCopy(&CopyImpl);
				builder.SetFnDestructor(&DestructorImpl);
				builder.SetFnBatchDestructor(&BatchDestructorImpl);
			}
		}

		void Constructor()
		{
			if (registerHandlers)
			{
				NativeReflectConstructor<Type>(builder.AddConstructor(nullptr, nullptr, 0));
			}
		}

		template <typename... Args, typename... TName>
		void Constructor(TName&&... tnames)
		{
			if (registerHandlers)
			{
				static_assert(sizeof...(Args) == sizeof...(TName), "args and names should be equals");
				FieldProps props[] = {GetFieldProps<Type, Args>()...,};
				StringView names[] = {tnames...,};
				NativeReflectConstructor<Type, Args...>(builder.AddConstructor(props, names, sizeof...(Args)));
			}
		}

		template <auto mfp, typename... TNames>
		decltype(auto) Function(StringView name, TNames&&... tnames)
		{
			using Type = typename FunctionTemplateDecomposer<mfp, decltype(mfp)>::Type;
			static_assert(Type::params == sizeof...(TNames), "args and names should be equals");
			ReflectFunctionBuilder funcBuilder = builder.AddFunction(name);
			Type::AddParams(funcBuilder, Traits::Forward<TNames>(tnames)...);
			return Type(funcBuilder);
		}

		template <auto mfp>
		decltype(auto) Field(StringView name)
		{
			using Decomposer = FieldTemplateDecomposer<mfp, nullptr, nullptr, decltype(mfp)>;
			using FieldType = typename Decomposer::Type;
			using Type = typename Decomposer::DescType;
			return Type(builder.AddField(GetFieldProps<Type, FieldType>(), name));
		}

		template <auto mfp, auto getFp, auto setFp>
		decltype(auto) Field(StringView name)
		{
			using Decomposer = FieldTemplateDecomposer<mfp, getFp, setFp, decltype(mfp)>;
			using FieldType = typename Decomposer::Type;
			using Type = typename Decomposer::DescType;
			return Type(builder.AddField(GetFieldProps<Type, FieldType>(), name));
		}

		template <typename AttrType, typename... Args>
		decltype(auto) Attribute(Args&&... args)
		{
			NativeReflectAttribute<Type, AttrType>::s_Value = AttrType{Traits::Forward<Args>(args)...,};
			NativeReflectAttribute<Type, AttrType>(builder.AddAttribute(TypeInfo<AttrType>::GetProps()));

			return *this;
		}

		template <auto value>
		auto Value(StringView valueName)
		{
			return NativeReflectValue<value, decltype(value), Type>(builder.AddValue(valueName));
		}

		template <auto value>
		auto Value()
		{
			return NativeReflectValue<value, decltype(value), Type>(builder.AddValue(GetEnumValueName<value>()));
		}

		friend class Reflection;

	private:
		ReflectTypeBuilder builder;
		bool               registerHandlers;

		static void DestroyImpl(const ReflectType* type, Allocator* allocator, VoidPtr instance)
		{
			if constexpr (Traits::IsDestructible<Type>)
			{
				static_cast<Type*>(instance)->~Type();
			}
			allocator->MemFree(allocator->allocator, instance);
		}

		static void CopyImpl(const ReflectType* type, ConstPtr source, VoidPtr dest)
		{
			if constexpr (Traits::IsCopyConstructible<Type>)
			{
				new(PlaceHolder(), dest) Type(*static_cast<const Type*>(source));
			}
		}

		static void DestructorImpl(const ReflectType* type, VoidPtr instance)
		{
			if constexpr (Traits::IsDestructible<Type>)
			{
				static_cast<Type*>(instance)->~Type();
			}
		};

		static void BatchDestructorImpl(const ReflectType* type, VoidPtr data, usize count)
		{
			if constexpr (Traits::IsDestructible<Type>)
			{
				Type* arr = static_cast<Type*>(data);
				for (int i = 0; i < count; ++i)
				{
					arr[i].~Type();
				}
			}
		}
	};

	template <class T, class = void>
	struct HasBase : Traits::FalseType {};

	template <class T>
	struct HasBase<T, Traits::VoidType<typename T::Base>> : Traits::TrueType {};

	template <typename DerivedType, typename Base>
	struct AddBaseType
	{
		static void Register(ReflectTypeBuilder& builder)
		{
			builder.AddBaseType(TypeInfo<Base>::ID());

			if constexpr (HasRegisterType<Base>)
			{
				NativeReflectType<Base> desc = NativeReflectType<Base>(builder, false);
				Base::RegisterType(desc);
			}

			if constexpr (HasBase<Base>::value)
			{
				AddBaseType<DerivedType, typename Base::Base>::Register(builder);
			}
		}
	};

	template <typename T>
	decltype(auto) Reflection::Type(StringView name)
	{
		NativeReflectType<T> desc = NativeReflectType<T>(RegisterType(name, TypeInfo<T>::GetProps()));

		if constexpr (HasBase<T>::value)
		{
			AddBaseType<T, typename T::Base>::Register(desc.builder);
		}

		if constexpr (HasRegisterType<T>)
		{
			T::RegisterType(desc);
		}

		return desc;
	}

	template <typename T>
	decltype(auto) Reflection::Type()
	{
		return Type<T>(TypeInfo<T>::Name());
	}
}
