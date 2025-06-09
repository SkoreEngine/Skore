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

#include "Reflection.hpp"

#include "HashSet.hpp"
#include "Logger.hpp"
#include "Ref.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "yyjson.h"

namespace Skore
{
	bool                                            reflectionReadOnly = false;
	static HashMap<String, Array<Ref<ReflectType>>> typesByName;
	static HashMap<TypeID, Array<Ref<ReflectType>>> typesById;
	static HashMap<TypeID, HashSet<TypeID>>         derivedTypes;
	static HashMap<TypeID, Array<TypeID>>           typesByAttribute{};
	static Logger&                                  logger = Logger::GetLogger("Skore::Reflection");

	static Array<String> groupStack;

	namespace
	{
		String GetCurrentScope()
		{
			String result;
			for (int i = 0; i < groupStack.Size(); ++i)
			{
				result += groupStack[i];
				if (i != groupStack.Size() - 1)
				{
					result += ".";
				}
			}
			return result;
		}
	}

	StringView ReflectFunction::GetName() const
	{
		return name;
	}

	FieldProps ReflectFunction::GetReturn() const
	{
		return returnProps;
	}

	Span<ReflectParam*> ReflectFunction::GetParams() const
	{
		return params;
	}

	void ReflectFunction::Invoke(VoidPtr instance, VoidPtr ret, VoidPtr* params) const
	{
		if (invoke)
		{
			invoke(this, instance, ret, params);
		}
	}

	ReflectFunction::~ReflectFunction()
	{
		for (ReflectParam* param : params)
		{
			DestroyAndFree(param);
		}
	}

	const TypeProps& ReflectAttribute::GetProps() const
	{
		return typeProps;
	}

	ConstPtr ReflectAttribute::GetPointer() const
	{
		if (fnGetValue)
		{
			return fnGetValue(this);
		}
		return nullptr;
	}

	ReflectAttributeHolder::~ReflectAttributeHolder()
	{
		for (ReflectAttribute* attr : attributeArray)
		{
			DestroyAndFree(attr);
		}
	}

	ConstPtr ReflectAttributeHolder::GetAttribute(TypeID attributeId) const
	{
		for (ReflectAttribute* attr : attributeArray)
		{
			if (attr->GetProps().typeId == attributeId)
			{
				return attr->GetPointer();
			}
		}
		return nullptr;
	}

	StringView ReflectValue::GetDesc() const
	{
		return valueDesc;
	}

	i64 ReflectValue::GetCode() const
	{
		if (fnGetCode)
		{
			return fnGetCode(this);
		}
		return I64_MIN;
	}

	ConstPtr ReflectValue::GetValue() const
	{
		if (fnGetValue)
		{
			return fnGetValue(this);
		}
		return nullptr;
	}

	bool ReflectValue::Compare(ConstPtr value) const
	{
		if (fnCompare)
		{
			return fnCompare(this, value);
		}
		return false;
	}

	const FieldProps& ReflectParam::GetProps() const
	{
		return props;
	}

	StringView ReflectParam::GetName() const
	{
		return name;
	}

	Span<ReflectParam*> ReflectConstructor::GetParams() const
	{
		return params;
	}

	void ReflectConstructor::Construct(VoidPtr memory, VoidPtr* params) const
	{
		if (placementNewFn)
		{
			placementNewFn(this, memory, params);
		}
	}

	Object* ReflectConstructor::NewObject(Allocator* allocator, VoidPtr* params) const
	{
		if (newInstanceFn)
		{
			return newInstanceFn(this, allocator, params);
		}
		return nullptr;
	}

	ReflectConstructor::~ReflectConstructor()
	{
		for (ReflectParam* param : params)
		{
			DestroyAndFree(param);
		}
	}

	StringView ReflectField::GetName() const
	{
		return m_name;
	}

	u32 ReflectField::GetIndex() const
	{
		return m_index;
	}

	ResourceFieldInfo ReflectField::GetResourceFieldInfo() const
	{
		if (m_getResourceFieldInfo)
		{
			return m_getResourceFieldInfo(this);
		}
		return {ResourceFieldType::None};
	}

	void ReflectField::ToResource(ResourceObject& resourceObject, u32 index, ConstPtr instance, UndoRedoScope* scope) const
	{
		if (m_toResource)
		{
			m_toResource(this, resourceObject, index, instance, scope);
		}
	}

	void ReflectField::FromResource(const ResourceObject& resourceObject, u32 index, VoidPtr instance) const
	{
		if (m_fromResource)
		{
			m_fromResource(this, resourceObject, index, instance);
		}
	}

	void ReflectField::CopyFromType(ConstPtr src, VoidPtr dest) const
	{
		if (m_props.isPointer || m_props.isReference)
		{
			VoidPtr ptr = nullptr;
			Get(src, &ptr, sizeof(VoidPtr));
			Set(dest, &ptr, sizeof(VoidPtr));
		}
		else if (m_copy)
		{
			m_copy(this, src, dest);
		}
	}

	void ReflectField::Get(ConstPtr instance, VoidPtr dest, usize destSize) const
	{
		if (m_get)
		{
			m_get(this, instance, dest, destSize);
		}
	}

	const Object* ReflectField::GetObject(ConstPtr instance) const
	{
		if (m_getObject)
		{
			return m_getObject(this, instance);
		}
		return nullptr;
	}

	void ReflectField::Set(VoidPtr instance, ConstPtr src, usize srcSize) const
	{
		if (m_set)
		{
			m_set(this, instance, src, srcSize);
		}
	}

	const FieldProps& ReflectField::GetProps() const
	{
		return m_props;
	}

	void ReflectField::Serialize(ArchiveWriter& writer, ConstPtr instance) const
	{
		if (m_serialize)
		{
			m_serialize(writer, this, instance);
		}
	}

	void ReflectField::Deserialize(ArchiveReader& reader, VoidPtr instance) const
	{
		if (m_deserialize)
		{
			m_deserialize(reader, this, instance);
		}
	}

	StringView ReflectType::GetName() const
	{
		return name;
	}

	StringView ReflectType::GetSimpleName() const
	{
		return simpleName;
	}

	StringView ReflectType::GetScope() const
	{
		return scope;
	}

	u32 ReflectType::GetVersion() const
	{
		return version;
	}

	const TypeProps& ReflectType::GetProps() const
	{
		return props;
	}

	bool ReflectType::IsDerivedOf(TypeID typeId) const
	{
		for (TypeID baseType : baseTypes)
		{
			if (baseType == typeId)
			{
				return true;
			}
		}
		return false;
	}

	ReflectConstructor* ReflectType::FindConstructor(Span<TypeID> ids) const
	{
		for (ReflectConstructor* constructor : constructors)
		{
			Span<ReflectParam*> params = constructor->GetParams();
			if (params.Size() != ids.Size())
			{
				continue;
			}

			bool valid = true;
			for (int i = 0; i < ids.Size(); ++i)
			{
				if (params[i]->GetProps().typeId != ids[i])
				{
					valid = false;
					break;
				}
			}
			if (valid)
			{
				return constructor;
			}
		}
		return nullptr;
	}

	Span<ReflectConstructor*> ReflectType::GetConstructors() const
	{
		return constructors;
	}

	ReflectConstructor* ReflectType::GetDefaultConstructor() const
	{
		return defaultConstructor;
	}

	Span<ReflectField*> ReflectType::GetFields() const
	{
		return fields;
	}

	ReflectField* ReflectType::FindField(StringView fieldName) const
	{
		for (auto& field : fields)
		{
			if (field->GetName() == fieldName)
			{
				return field;
			}
		}
		return nullptr;
	}

	Span<ReflectFunction*> ReflectType::FindFunctionByName(StringView functionName) const
	{
		if (auto it = functionsByName.Find(functionName))
		{
			return it->second;
		}
		return {};
	}

	ReflectFunction* ReflectType::FindFunction(StringView functionName, Span<TypeID> ids) const
	{
		if (auto it = functionsByName.Find(functionName))
		{
			for (ReflectFunction* func : it->second)
			{
				Span<ReflectParam*> params = func->GetParams();
				if (params.Size() != ids.Size())
				{
					continue;
				}

				bool valid = true;
				for (int i = 0; i < ids.Size(); ++i)
				{
					if (params[i]->GetProps().typeId != ids[i])
					{
						valid = false;
						break;
					}
				}
				if (valid)
				{
					return func;
				}
			}
		}
		return nullptr;
	}

	ReflectFunction* ReflectType::FindFunction(StringView functionName) const
	{
		if (auto it = functionsByName.Find(functionName))
		{
			if (!it->second.Empty())
			{
				return it->second[0];
			}
		}
		return nullptr;
	}

	Span<ReflectFunction*> ReflectType::GetFunctions() const
	{
		return functions;
	}

	ReflectValue* ReflectType::FindValueByName(StringView valueName) const
	{
		for (ReflectValue* value : values)
		{
			if (value->GetDesc() == valueName)
			{
				return value;
			}
		}
		return nullptr;
	}

	ReflectValue* ReflectType::FindValueByCode(i64 code) const
	{
		for (ReflectValue* value : values)
		{
			if (value->GetCode() == code)
			{
				return value;
			}
		}

		return nullptr;
	}

	ReflectValue* ReflectType::FindValue(ConstPtr value) const
	{
		for (ReflectValue* reflectValue : values)
		{
			if (reflectValue->Compare(value))
			{
				return reflectValue;
			}
		}

		return nullptr;
	}

	Span<ReflectValue*> ReflectType::GetValues() const
	{
		return values;
	}


	void ReflectType::Destroy(VoidPtr instance, Allocator* allocator) const
	{
		if (fnDestroy)
		{
			fnDestroy(this, allocator, instance);
		}
	}

	void ReflectType::Destructor(VoidPtr instance) const
	{
		if (fnDestructor)
		{
			fnDestructor(this, instance);
		}
	}

	void ReflectType::BatchDestructor(VoidPtr data, usize count) const
	{
		if (fnBatchDestructor)
		{
			fnBatchDestructor(this, data, count);
		}
	}

	void ReflectType::Copy(ConstPtr source, VoidPtr dest) const
	{
		if (fnCopy)
		{
			fnCopy(this, source, dest);
		}
	}

	void ReflectType::DeepCopy(ConstPtr source, VoidPtr dest) const
	{
		SK_ASSERT(source, "source cannot be null");
		SK_ASSERT(dest, "dest cannot be null");
		SK_ASSERT(source != dest, "source and dest cannot be the same");

		if (!fields.Empty())
		{
			for (ReflectField* field : fields)
			{
				field->CopyFromType(source, dest);
			}
		}
		else
		{
			Copy(source, dest);
		}
	}

	void ReflectValueBuilder::SetFnGetValue(ReflectValue::FnGetValue fnGetValue)
	{
		reflectValue->fnGetValue = fnGetValue;
	}

	void ReflectValueBuilder::SerFnGetCode(ReflectValue::FnGetCode fnGetCode)
	{
		reflectValue->fnGetCode = fnGetCode;
	}

	void ReflectValueBuilder::SetFnCompare(ReflectValue::FnCompare fnCompare)
	{
		reflectValue->fnCompare = fnCompare;
	}

	void ReflectAttributeBuilder::SetGetValue(ReflectAttribute::FnGetValue value)
	{
		attribute->fnGetValue = value;
	}

	ReflectAttributeBuilder::ReflectAttributeBuilder(ReflectAttribute* attribute) : attribute(attribute) {}

	void ReflectConstructorBuilder::SetPlacementNewFn(ReflectConstructor::PlacementNewFn placementNew)
	{
		constructor->placementNewFn = placementNew;
	}

	void ReflectConstructorBuilder::SetNewObjectFn(ReflectConstructor::NewObjectFn newInstance)
	{
		constructor->newInstanceFn = newInstance;
	}

	ReflectType::ReflectType(StringView name, TypeProps props) : name(name), props(props)
	{
		simpleName = MakeSimpleName(name);
	}

	ReflectType::~ReflectType()
	{
		for (ReflectConstructor* constructor : constructors)
		{
			DestroyAndFree(constructor);
		}

		for (ReflectFunction* function : functions)
		{
			DestroyAndFree(function);
		}

		for (ReflectField* field : fields)
		{
			DestroyAndFree(field);
		}
	}

	void ReflectFunctionBuilder::AddParams(StringView* names, FieldProps* props, u32 size)
	{
		function->params.Reserve(size);
		for (u32 i = 0; i < size; ++i)
		{
			function->params.EmplaceBack(Alloc<ReflectParam>(names[i], props[i]));
		}
	}

	void ReflectFunctionBuilder::SetFnInvoke(ReflectFunction::FnInvoke fnInvoke)
	{
		function->invoke = fnInvoke;
	}

	void ReflectFunctionBuilder::SetFunctionPointer(VoidPtr functionPointer)
	{
		function->functionPointer = functionPointer;
	}

	void ReflectFunctionBuilder::SetReturnProps(FieldProps returnProps)
	{
		function->returnProps = returnProps;
	}

	void ReflectFieldBuilder::SetSerializer(ReflectField::FnSerialize serialize)
	{
		field->m_serialize = serialize;
	}

	void ReflectFieldBuilder::SetDeserialize(ReflectField::FnDeserialize deserialize)
	{
		field->m_deserialize = deserialize;
	}

	void ReflectFieldBuilder::SetCopy(ReflectField::FnCopy copy)
	{
		field->m_copy = copy;
	}

	void ReflectFieldBuilder::SetGet(ReflectField::FnGet get)
	{
		field->m_get = get;
	}

	void ReflectFieldBuilder::SetGetObject(ReflectField::FnGetObject getObject)
	{
		field->m_getObject = getObject;
	}

	void ReflectFieldBuilder::SetFnSet(ReflectField::FnSet set)
	{
		field->m_set = set;
	}

	void ReflectFieldBuilder::SetFnToResource(ReflectField::FnToResource fnToResource)
	{
		field->m_toResource = fnToResource;
	}

	void ReflectFieldBuilder::SetFnFromResource(ReflectField::FnFromResource fnGetFromResource)
	{
		field->m_fromResource = fnGetFromResource;
	}

	void ReflectFieldBuilder::SetFnGetResourceFieldInfo(ReflectField::FnGetResourceFieldInfo fnGetResourceField)
	{
		field->m_getResourceFieldInfo = fnGetResourceField;
	}

	ReflectAttributeBuilder ReflectFieldBuilder::AddAttribute(const TypeProps& props)
	{
		ReflectAttribute* reflectAttribute = Alloc<ReflectAttribute>(props);
		field->attributeArray.EmplaceBack(reflectAttribute);
		return {reflectAttribute};
	}

	ReflectFieldBuilder ReflectTypeBuilder::AddField(const FieldProps& props, StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		SK_ASSERT(type, "type cannot be null");

		ReflectField* field = Alloc<ReflectField>(props, name, type->fields.Size());
		type->fields.EmplaceBack(field);
		return field;
	}

	ReflectFunctionBuilder ReflectTypeBuilder::AddFunction(StringView name)
	{
		SK_ASSERT(!name.Empty(), "name cannot be empty");
		SK_ASSERT(type, "type cannot be null");

		ReflectFunction* function = Alloc<ReflectFunction>(name);
		type->functions.EmplaceBack(function);

		auto it = type->functionsByName.Find(name);
		if (it == type->functionsByName.end())
		{
			it = type->functionsByName.Emplace(String(name), Array<ReflectFunction*>{}).first;
		}
		it->second.EmplaceBack(function);

		return {function};
	}

	ReflectConstructorBuilder ReflectTypeBuilder::AddConstructor(FieldProps* props, StringView* names, u32 size)
	{
		ReflectConstructor* reflectConstructor = Alloc<ReflectConstructor>();

		for (u32 i = 0; i < size; ++i)
		{
			reflectConstructor->params.EmplaceBack(Alloc<ReflectParam>(names[i], props[i]));
		}

		type->constructors.EmplaceBack(reflectConstructor);

		if (size == 0)
		{
			type->defaultConstructor = reflectConstructor;
		}


		return {reflectConstructor};
	}

	ReflectAttributeBuilder ReflectTypeBuilder::AddAttribute(const TypeProps& props)
	{
		ReflectAttribute* reflectAttribute = Alloc<ReflectAttribute>(props);
		type->attributeArray.EmplaceBack(reflectAttribute);

		auto fIt = typesByAttribute.Find(props.typeId);
		if (fIt == typesByAttribute.end())
		{
			fIt = typesByAttribute.Emplace(props.typeId, Array<TypeID>{}).first;
		}
		fIt->second.EmplaceBack(type->GetProps().typeId);

		return {reflectAttribute};
	}

	ReflectValueBuilder ReflectTypeBuilder::AddValue(StringView valueDesc)
	{
		ReflectValue* reflectValue = Alloc<ReflectValue>(valueDesc);
		type->values.EmplaceBack(reflectValue);
		return {reflectValue};
	}

	void ReflectTypeBuilder::SetFnDestroy(ReflectType::FnDestroy fnDestroy)
	{
		type->fnDestroy = fnDestroy;
	}

	void ReflectTypeBuilder::SetFnCopy(ReflectType::FnCopy fnCopy)
	{
		type->fnCopy = fnCopy;
	}

	void ReflectTypeBuilder::SetFnDestructor(ReflectType::FnDestructor destructor)
	{
		type->fnDestructor = destructor;
	}

	void ReflectTypeBuilder::SetFnBatchDestructor(ReflectType::FnBatchDestructor batchDestructor)
	{
		type->fnBatchDestructor = batchDestructor;
	}

	void ReflectTypeBuilder::AddBaseType(TypeID typeId)
	{
		type->baseTypes.EmplaceBack(typeId);

		auto it = derivedTypes.Find(typeId);
		if (it == derivedTypes.end())
		{
			it = derivedTypes.Emplace(typeId, {}).first;
		}
		it->second.Emplace(type->props.typeId);
	}

	ReflectTypeBuilder Reflection::RegisterType(StringView name, TypeProps props)
	{
		if (reflectionReadOnly)
		{
			logger.Error("reflection is in readonly mode, types cannot be registered in this stage, please use registration callbacks");
			return {nullptr};
		}

		Ref<ReflectType> reflectType = MakeRef<ReflectType>(name, props);
		reflectType->scope = GetCurrentScope();

		{
			auto it = typesByName.Find(name);
			if (it == typesByName.end())
			{
				it = typesByName.Emplace(String(name), Array<Ref<ReflectType>>()).first;
			}

			it->second.EmplaceBack(reflectType);
			reflectType->version = it->second.Size();
		}

		{
			auto it = typesById.Find(props.typeId);
			if (it == typesById.end())
			{
				it = typesById.Emplace(props.typeId, Array<Ref<ReflectType>>()).first;
			}
			it->second.EmplaceBack(reflectType);
		}

		logger.Debug("Type {} Registered, version {} ", name, reflectType->version);

		return {reflectType.Get()};
	}

	ReflectType* Reflection::FindTypeByName(StringView name)
	{
		if (const auto it = typesByName.Find(name); it && !it->second.Empty())
		{
			return it->second.Back().Get();
		}
		return nullptr;
	}

	ReflectType* Reflection::FindTypeById(TypeID typeId)
	{
		if (const auto it = typesById.Find(typeId); it && !it->second.Empty())
		{
			return it->second.Back().Get();
		}
		return nullptr;
	}

	Array<TypeID> Reflection::GetDerivedTypes(TypeID typeId)
	{
		Array<TypeID> types;

		if (auto it = derivedTypes.Find(typeId))
		{
			types.Reserve(it->second.Size());
			for (TypeID derived : it->second)
			{
				types.EmplaceBack(derived);
			}
		}

		return types;
	}

	Span<TypeID> Reflection::GetTypesAnnotatedWith(TypeID typeId)
	{
		if (auto it = typesByAttribute.Find(typeId))
		{
			return it->second;
		}
		return {};
	}

	void Reflection::PushGroup(StringView name)
	{
		groupStack.EmplaceBack(name);
	}

	void Reflection::PopGroup()
	{
		groupStack.PopBack();
	}

	namespace
	{
		VoidPtr Malloc(VoidPtr ctx, usize size)
		{
			return MemAlloc(size);
		}

		VoidPtr Realloc(VoidPtr ctx, VoidPtr ptr, usize oldSize, usize size)
		{
			return MemRealloc(ptr, size);
		}

		void Free(VoidPtr ctx, VoidPtr ptr)
		{
			MemFree(ptr);
		}

		yyjson_alc alloc{
			.malloc = Malloc,
			.realloc = Realloc,
			.free = Free,
			.ctx = nullptr
		};
	}

	void SerializeTypeProps(yyjson_mut_doc* doc, yyjson_mut_val* obj, const TypeProps& props)
	{
		yyjson_mut_obj_add_uint(doc, obj, "size", props.size);
		yyjson_mut_obj_add_uint(doc, obj, "alignment", props.alignment);
		if (props.isEnum)
		{
			yyjson_mut_obj_add_bool(doc, obj, "isEnum", true);
		}
	}

	bool ValidProps(const FieldProps& props)
	{
		return props.isConst || props.isPointer || props.isReference;
	}

	void SerializeFieldProps(yyjson_mut_doc* doc, yyjson_mut_val* obj, const FieldProps& props)
	{
		if (props.isConst)
		{
			yyjson_mut_obj_add_bool(doc, obj, "isConst", true);
		}
		if (props.isPointer)
		{
			yyjson_mut_obj_add_bool(doc, obj, "isPointer", true);
		}

		if (props.isReference)
		{
			yyjson_mut_obj_add_bool(doc, obj, "isReference", true);
		}
	}

	//how to use it, add to program arguments:
	//--export-api C:\dev\SkoreEngine\Skore
	void Reflection::Export(StringView path)
	{
		//TODO - replace by JsonArchiveWriter;
		//TODO - add base type
		//TODO - remove base type functions and attributes
		//TODO - add values
		//TODO - add constructors

		yyjson_mut_doc* doc = yyjson_mut_doc_new(&alloc);
		yyjson_mut_val* root = yyjson_mut_obj(doc);

		yyjson_mut_val* arr = yyjson_mut_obj_add_arr(doc, root, "types");

		for (auto itType: typesByName)
		{
			if (!itType.second.Empty())
			{
				yyjson_mut_val* typeObject = yyjson_mut_arr_add_obj(doc, arr);
				ReflectType* type = itType.second.Back().Get();
				yyjson_mut_obj_add_str(doc, typeObject, "name", type->GetName().CStr());
				yyjson_mut_obj_add_str(doc, typeObject, "scope", type->GetScope().CStr());
				SerializeTypeProps(doc, yyjson_mut_obj_add_obj(doc, typeObject, "props"), type->GetProps());

				if (!type->GetFields().Empty())
				{
					yyjson_mut_val* jsonFields = yyjson_mut_obj_add_arr(doc, typeObject, "fields");
					for (ReflectField* field : type->GetFields())
					{
						yyjson_mut_val* fieldObj = yyjson_mut_arr_add_obj(doc, jsonFields);
						yyjson_mut_obj_add_str(doc, fieldObj, "name", field->GetName().CStr());
						const FieldProps& fieldProps = field->GetProps();
						yyjson_mut_obj_add_strncpy(doc, fieldObj, "type", fieldProps.name.CStr(), fieldProps.name.Size());
						if (ValidProps(fieldProps))
						{
							SerializeFieldProps(doc, fieldObj, fieldProps);
						}

					}
				}

				if (!type->GetFunctions().Empty())
				{
					yyjson_mut_val* jsonFunctions = yyjson_mut_obj_add_arr(doc, typeObject, "functions");
					for (ReflectFunction* function : type->GetFunctions())
					{
						yyjson_mut_val* funcObj = yyjson_mut_arr_add_obj(doc, jsonFunctions);
						yyjson_mut_obj_add_str(doc, funcObj, "name", function->GetName().CStr());

						{
							FieldProps returnProps = function->GetReturn();
							yyjson_mut_val* returnObj = yyjson_mut_obj_add_obj(doc, funcObj, "return");
							yyjson_mut_obj_add_strncpy(doc, returnObj, "type", returnProps.name.CStr(), returnProps.name.Size());
							if (ValidProps(returnProps))
							{
								SerializeFieldProps(doc,  yyjson_mut_obj_add_obj(doc, returnObj, "props"), returnProps);
							}
						}

						if (!function->GetParams().Empty())
						{
							yyjson_mut_val* jsonFunctionParameters = yyjson_mut_obj_add_arr(doc, funcObj, "params");

							for (ReflectParam* param : function->GetParams())
							{
								yyjson_mut_val* paramObj = yyjson_mut_arr_add_obj(doc, jsonFunctionParameters);
								yyjson_mut_obj_add_str(doc, paramObj, "name", param->GetName().CStr());
								yyjson_mut_obj_add_strncpy(doc, paramObj, "type", param->GetProps().name.CStr(), param->GetProps().name.Size());
								if (ValidProps(param->GetProps()))
								{
									SerializeFieldProps(doc,  yyjson_mut_obj_add_obj(doc, paramObj, "props"), param->GetProps());
								}
							}
						}
					}
				}
			}
		}

		yyjson_write_flag flg = YYJSON_WRITE_ESCAPE_UNICODE | YYJSON_WRITE_PRETTY;

		usize                   len = 0;
		yyjson_write_err        err;
		if (char* json = yyjson_mut_val_write_opts(root, flg, &alloc, &len, &err))
		{
			FileSystem::SaveFileAsString(path, {json, len});
			Free(nullptr, json);
		}
		yyjson_mut_doc_free(doc);
	}

	SK_API void ReflectionSetReadOnly(bool readOnly)
	{
		reflectionReadOnly = readOnly;
	}

	void ReflectionResetContext()
	{
		typesByName.Clear();
		typesById.Clear();
		derivedTypes.Clear();
	}
}
