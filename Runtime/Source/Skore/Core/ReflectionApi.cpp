#include "ReflectionApi.hpp"

#include "Reflection.hpp"
#include "Array.hpp"

namespace Skore
{
	namespace
	{
		ReflectType** ApiGetAllTypes(u32* outCount)
		{
			static Array<ReflectType*> cache;
			cache = Reflection::GetAllTypes();
			if (outCount)
			{
				*outCount = cache.Size();
			}
			return cache.Data();
		}

		TypeID* ApiGetDerivedTypes(TypeID baseType, u32* outCount)
		{
			static Array<TypeID> cache;
			cache = Reflection::GetDerivedTypes(baseType);
			if (outCount)
			{
				*outCount = cache.Size();
			}
			return cache.Data();
		}

		const TypeProps* ApiTypeGetProps(const ReflectType* type)
		{
			return &type->GetProps();
		}

		const FieldProps* ApiFieldGetProps(const ReflectField* field)
		{
			return &field->GetProps();
		}

		void ApiFunctionGetReturn(const ReflectFunction* function, FieldProps* outProps)
		{
			if (outProps)
			{
				*outProps = function->GetReturn();
			}
		}

		const FieldProps* ApiParamGetProps(const ReflectParam* param)
		{
			return &param->GetProps();
		}

		Object* ApiTypeNewObject(const ReflectType* type)
		{
			return type->NewObject();
		}

		void ApiTypeDestroy(const ReflectType* type, VoidPtr instance)
		{
			type->Destroy(instance);
		}

		Object* ApiConstructorNewObject(const ReflectConstructor* constructor, VoidPtr* params)
		{
			return constructor->NewObject(MemoryGlobals::GetDefaultAllocator(), params);
		}

		ConstPtr ApiTypeGetAttribute(const ReflectType* type, TypeID attributeId)
		{
			return type->GetAttribute(attributeId);
		}

		ConstPtr ApiFieldGetAttribute(const ReflectField* field, TypeID attributeId)
		{
			return field->GetAttribute(attributeId);
		}

		ConstPtr ApiFunctionGetAttribute(const ReflectFunction* function, TypeID attributeId)
		{
			return function->GetAttribute(attributeId);
		}

		const ReflectionApi api = {
			.FindTypeByName = &Reflection::FindTypeByName,
			.FindTypeById = &Reflection::FindTypeById,
			.GetAllTypes = ApiGetAllTypes,
			.GetDerivedTypes = ApiGetDerivedTypes,
			.GetTypesAnnotatedWith = &Reflection::GetTypesAnnotatedWith,
			.GetReflectionVersion = &Reflection::GetVersion,
			.GetDefaultAllocator = []() -> VoidPtr { return MemoryGlobals::GetDefaultAllocator(); },

			.TypeGetName = [](const ReflectType* type) { return type->GetName(); },
			.TypeGetSimpleName = [](const ReflectType* type) { return type->GetSimpleName(); },
			.TypeGetScope = [](const ReflectType* type) { return type->GetScope(); },
			.TypeGetProps = ApiTypeGetProps,
			.TypeGetVersion = [](const ReflectType* type) { return type->GetVersion(); },
			.TypeIsDerivedOf = [](const ReflectType* type, TypeID typeId) { return type->IsDerivedOf(typeId); },
			.TypeGetBaseTypes = [](const ReflectType* type) { return type->GetBaseTypes(); },
			.TypeGetFields = [](const ReflectType* type) { return type->GetFields(); },
			.TypeFindField = [](const ReflectType* type, StringView name) { return type->FindField(name); },
			.TypeGetFunctions = [](const ReflectType* type) { return type->GetFunctions(); },
			.TypeFindFunction = [](const ReflectType* type, StringView name) { return type->FindFunction(name); },
			.TypeGetConstructors = [](const ReflectType* type) { return type->GetConstructors(); },
			.TypeGetDefaultConstructor = [](const ReflectType* type) { return type->GetDefaultConstructor(); },
			.TypeGetValues = [](const ReflectType* type) { return type->GetValues(); },
			.TypeFindValueByName = [](const ReflectType* type, StringView name) { return type->FindValueByName(name); },
			.TypeFindValueByCode = [](const ReflectType* type, i64 code) { return type->FindValueByCode(code); },
			.TypeNewObject = ApiTypeNewObject,
			.TypeDestroy = ApiTypeDestroy,
			.TypeDestructor = [](const ReflectType* type, VoidPtr instance) { type->Destructor(instance); },
			.TypeCopy = [](const ReflectType* type, ConstPtr source, VoidPtr dest) { type->Copy(source, dest); },
			.TypeGetAttribute = ApiTypeGetAttribute,

			.FieldGetName = [](const ReflectField* field) { return field->GetName(); },
			.FieldGetIndex = [](const ReflectField* field) { return field->GetIndex(); },
			.FieldGetProps = ApiFieldGetProps,
			.FieldGet = [](const ReflectField* field, ConstPtr instance, VoidPtr dest, usize destSize) { field->Get(instance, dest, destSize); },
			.FieldSet = [](const ReflectField* field, VoidPtr instance, ConstPtr src, usize srcSize) { field->Set(instance, src, srcSize); },
			.FieldGetObject = [](const ReflectField* field, ConstPtr instance) { return field->GetObject(instance); },
			.FieldGetAttribute = ApiFieldGetAttribute,

			.FunctionGetName = [](const ReflectFunction* function) { return function->GetName(); },
			.FunctionGetSimpleName = [](const ReflectFunction* function) { return function->GetSimpleName(); },
			.FunctionGetReturn = ApiFunctionGetReturn,
			.FunctionGetParams = [](const ReflectFunction* function) { return function->GetParams(); },
			.FunctionIsStatic = [](const ReflectFunction* function) { return function->IsStatic(); },
			.FunctionInvoke = [](const ReflectFunction* function, VoidPtr instance, VoidPtr ret, VoidPtr* params) { function->Invoke(instance, ret, params); },
			.FunctionGetFunctionPointer = [](const ReflectFunction* function) { return function->GetFunctionPointer(); },
			.FunctionGetAttribute = ApiFunctionGetAttribute,

			.ParamGetName = [](const ReflectParam* param) { return param->GetName(); },
			.ParamGetProps = ApiParamGetProps,

			.ConstructorGetParams = [](const ReflectConstructor* constructor) { return constructor->GetParams(); },
			.ConstructorNewObject = ApiConstructorNewObject,
			.ConstructorConstruct = [](const ReflectConstructor* constructor, VoidPtr memory, VoidPtr* params) { constructor->Construct(memory, params); },

			.ValueGetDesc = [](const ReflectValue* value) { return value->GetDesc(); },
			.ValueGetCode = [](const ReflectValue* value) { return value->GetCode(); },
			.ValueGetValue = [](const ReflectValue* value) { return value->GetValue(); },
			.ValueCompare = [](const ReflectValue* value, ConstPtr compareValue) { return value->Compare(compareValue); },
			.ValueSetToPointer = [](const ReflectValue* value, VoidPtr pointer) { value->SetToPointer(pointer); },

			.RegisterType = [](StringView name, StringView simpleName, const TypeProps* props) { return Reflection::RegisterType(name, simpleName, *props); },

			.TypeBuilderAddField = [](ReflectTypeBuilder builder, const FieldProps* props, StringView name) { return builder.AddField(*props, name); },
			.TypeBuilderAddFunction = [](ReflectTypeBuilder builder, StringView name) { return builder.AddFunction(name); },
			.TypeBuilderAddConstructor = [](ReflectTypeBuilder builder, FieldProps* props, StringView* names, u32 size) { return builder.AddConstructor(props, names, size); },
			.TypeBuilderAddAttribute = [](ReflectTypeBuilder builder, const TypeProps* props) { return builder.AddAttribute(*props); },
			.TypeBuilderAddValue = [](ReflectTypeBuilder builder, StringView valueDesc) { return builder.AddValue(valueDesc); },
			.TypeBuilderSetFnDestroy = [](ReflectTypeBuilder builder, VoidPtr fnDestroy) { builder.SetFnDestroy(reinterpret_cast<ReflectType::FnDestroy>(fnDestroy)); },
			.TypeBuilderSetFnCopy = [](ReflectTypeBuilder builder, VoidPtr fnCopy) { builder.SetFnCopy(reinterpret_cast<ReflectType::FnCopy>(fnCopy)); },
			.TypeBuilderSetFnDestructor = [](ReflectTypeBuilder builder, VoidPtr fnDestructor) { builder.SetFnDestructor(reinterpret_cast<ReflectType::FnDestructor>(fnDestructor)); },
			.TypeBuilderSetFnBatchDestructor = [](ReflectTypeBuilder builder, VoidPtr fnBatchDestructor) { builder.SetFnBatchDestructor(reinterpret_cast<ReflectType::FnBatchDestructor>(fnBatchDestructor)); },
			.TypeBuilderAddBaseType = [](ReflectTypeBuilder builder, TypeID typeId) { builder.AddBaseType(typeId); },

			.FieldBuilderSetSerializer = [](ReflectFieldBuilder builder, VoidPtr serialize) { builder.SetSerializer(reinterpret_cast<ReflectField::FnSerialize>(serialize)); },
			.FieldBuilderSetDeserialize = [](ReflectFieldBuilder builder, VoidPtr deserialize) { builder.SetDeserialize(reinterpret_cast<ReflectField::FnDeserialize>(deserialize)); },
			.FieldBuilderSetCopy = [](ReflectFieldBuilder builder, VoidPtr copy) { builder.SetCopy(reinterpret_cast<ReflectField::FnCopy>(copy)); },
			.FieldBuilderSetGet = [](ReflectFieldBuilder builder, VoidPtr get) { builder.SetGet(reinterpret_cast<ReflectField::FnGet>(get)); },
			.FieldBuilderSetGetObject = [](ReflectFieldBuilder builder, VoidPtr getObject) { builder.SetGetObject(reinterpret_cast<ReflectField::FnGetObject>(getObject)); },
			.FieldBuilderSetFnSet = [](ReflectFieldBuilder builder, VoidPtr set) { builder.SetFnSet(reinterpret_cast<ReflectField::FnSet>(set)); },
			.FieldBuilderSetFnToResource = [](ReflectFieldBuilder builder, VoidPtr fnToResource) { builder.SetFnToResource(reinterpret_cast<ReflectField::FnToResource>(fnToResource)); },
			.FieldBuilderSetFnFromResource = [](ReflectFieldBuilder builder, VoidPtr fnFromResource) { builder.SetFnFromResource(reinterpret_cast<ReflectField::FnFromResource>(fnFromResource)); },
			.FieldBuilderSetFnGetResourceFieldInfo = [](ReflectFieldBuilder builder, VoidPtr fnGetResourceFieldInfo) { builder.SetFnGetResourceFieldInfo(reinterpret_cast<ReflectField::FnGetResourceFieldInfo>(fnGetResourceFieldInfo)); },
			.FieldBuilderSetFunctionPointerSignature = [](ReflectFieldBuilder builder, const FieldProps* returnProps, FieldProps* params, u32 count) { builder.SetFunctionPointerSignature(*returnProps, params, count); },
			.FieldBuilderAddAttribute = [](ReflectFieldBuilder builder, const TypeProps* props) { return builder.AddAttribute(*props); },

			.FunctionBuilderAddParams = [](ReflectFunctionBuilder builder, StringView* names, FieldProps* props, u32 size) { builder.AddParams(names, props, size); },
			.FunctionBuilderSetFnInvoke = [](ReflectFunctionBuilder builder, VoidPtr fnInvoke) { builder.SetFnInvoke(reinterpret_cast<ReflectFunction::FnInvoke>(fnInvoke)); },
			.FunctionBuilderSetFunctionPointer = [](ReflectFunctionBuilder builder, VoidPtr functionPointer) { builder.SetFunctionPointer(functionPointer); },
			.FunctionBuilderSetReturnProps = [](ReflectFunctionBuilder builder, const FieldProps* returnProps) { builder.SetReturnProps(*returnProps); },
			.FunctionBuilderSetIsStatic = [](ReflectFunctionBuilder builder, bool isStatic) { builder.SetIsStatic(isStatic); },
			.FunctionBuilderAddAttribute = [](ReflectFunctionBuilder builder, const TypeProps* props) { return builder.AddAttribute(*props); },

			.ConstructorBuilderSetPlacementNewFn = [](ReflectConstructorBuilder builder, VoidPtr placementNew) { builder.SetPlacementNewFn(reinterpret_cast<ReflectConstructor::PlacementNewFn>(placementNew)); },
			.ConstructorBuilderSetNewObjectFn = [](ReflectConstructorBuilder builder, VoidPtr newObject) { builder.SetNewObjectFn(reinterpret_cast<ReflectConstructor::NewObjectFn>(newObject)); },
			.ConstructorBuilderSetUserData = [](ReflectConstructorBuilder builder, VoidPtr userData) { builder.SetUserData(userData); },

			.ValueBuilderSetFnGetValue = [](ReflectValueBuilder builder, VoidPtr fnGetValue) { builder.SetFnGetValue(reinterpret_cast<ReflectValue::FnGetValue>(fnGetValue)); },
			.ValueBuilderSetFnGetCode = [](ReflectValueBuilder builder, VoidPtr fnGetCode) { builder.SerFnGetCode(reinterpret_cast<ReflectValue::FnGetCode>(fnGetCode)); },
			.ValueBuilderSetFnCompare = [](ReflectValueBuilder builder, VoidPtr fnCompare) { builder.SetFnCompare(reinterpret_cast<ReflectValue::FnCompare>(fnCompare)); },
			.ValueBuilderSetFnSetToPointer = [](ReflectValueBuilder builder, VoidPtr fnSetToPointer) { builder.SetFnSetToPointer(reinterpret_cast<ReflectValue::FnSetToPointer>(fnSetToPointer)); },

			.AttributeBuilderSetGetValue = [](ReflectAttributeBuilder builder, VoidPtr fnGetValue) { builder.SetGetValue(reinterpret_cast<ReflectAttribute::FnGetValue>(fnGetValue)); },

			.FieldGetUserData = [](const ReflectField* field) { return field->GetUserData(); },
			.FieldBuilderSetUserData = [](ReflectFieldBuilder builder, VoidPtr userData) { builder.SetUserData(userData); },
			.ConstructorGetUserData = [](const ReflectConstructor* constructor) { return constructor->GetUserData(); },
		};
	}

	const ReflectionApi* GetReflectionApi()
	{
		return &api;
	}
}
