#pragma once

#include "Skore/Common.hpp"
#include "Span.hpp"
#include "StringView.hpp"
#include "TypeInfo.hpp"

namespace Skore
{
	class Object;
	class ReflectType;
	class ReflectField;
	class ReflectFunction;
	class ReflectParam;
	class ReflectValue;
	class ReflectConstructor;

	struct ReflectionApi
	{
		ReflectType*  (*FindTypeByName)(StringView name);
		ReflectType*  (*FindTypeById)(TypeID typeId);
		ReflectType** (*GetAllTypes)(u32* outCount);
		TypeID*       (*GetDerivedTypes)(TypeID baseType, u32* outCount);
		Span<TypeID>  (*GetTypesAnnotatedWith)(TypeID attributeId);
		u32           (*GetReflectionVersion)();

		StringView                 (*TypeGetName)(const ReflectType* type);
		StringView                 (*TypeGetSimpleName)(const ReflectType* type);
		StringView                 (*TypeGetScope)(const ReflectType* type);
		const TypeProps*           (*TypeGetProps)(const ReflectType* type);
		u32                        (*TypeGetVersion)(const ReflectType* type);
		bool                       (*TypeIsDerivedOf)(const ReflectType* type, TypeID typeId);
		Span<TypeID>               (*TypeGetBaseTypes)(const ReflectType* type);
		Span<ReflectField*>        (*TypeGetFields)(const ReflectType* type);
		ReflectField*              (*TypeFindField)(const ReflectType* type, StringView name);
		Span<ReflectFunction*>     (*TypeGetFunctions)(const ReflectType* type);
		ReflectFunction*           (*TypeFindFunction)(const ReflectType* type, StringView name);
		Span<ReflectConstructor*>  (*TypeGetConstructors)(const ReflectType* type);
		ReflectConstructor*        (*TypeGetDefaultConstructor)(const ReflectType* type);
		Span<ReflectValue*>        (*TypeGetValues)(const ReflectType* type);
		ReflectValue*              (*TypeFindValueByName)(const ReflectType* type, StringView name);
		ReflectValue*              (*TypeFindValueByCode)(const ReflectType* type, i64 code);
		Object*                    (*TypeNewObject)(const ReflectType* type);
		void                       (*TypeDestroy)(const ReflectType* type, VoidPtr instance);
		void                       (*TypeDestructor)(const ReflectType* type, VoidPtr instance);
		void                       (*TypeCopy)(const ReflectType* type, ConstPtr source, VoidPtr dest);
		ConstPtr                   (*TypeGetAttribute)(const ReflectType* type, TypeID attributeId);

		StringView        (*FieldGetName)(const ReflectField* field);
		u32               (*FieldGetIndex)(const ReflectField* field);
		const FieldProps* (*FieldGetProps)(const ReflectField* field);
		void              (*FieldGet)(const ReflectField* field, ConstPtr instance, VoidPtr dest, usize destSize);
		void              (*FieldSet)(const ReflectField* field, VoidPtr instance, ConstPtr src, usize srcSize);
		const Object*     (*FieldGetObject)(const ReflectField* field, ConstPtr instance);
		ConstPtr          (*FieldGetAttribute)(const ReflectField* field, TypeID attributeId);

		StringView          (*FunctionGetName)(const ReflectFunction* function);
		StringView          (*FunctionGetSimpleName)(const ReflectFunction* function);
		void                (*FunctionGetReturn)(const ReflectFunction* function, FieldProps* outProps);
		Span<ReflectParam*> (*FunctionGetParams)(const ReflectFunction* function);
		bool                (*FunctionIsStatic)(const ReflectFunction* function);
		void                (*FunctionInvoke)(const ReflectFunction* function, VoidPtr instance, VoidPtr ret, VoidPtr* params);
		VoidPtr             (*FunctionGetFunctionPointer)(const ReflectFunction* function);
		ConstPtr            (*FunctionGetAttribute)(const ReflectFunction* function, TypeID attributeId);

		StringView        (*ParamGetName)(const ReflectParam* param);
		const FieldProps* (*ParamGetProps)(const ReflectParam* param);

		Span<ReflectParam*> (*ConstructorGetParams)(const ReflectConstructor* constructor);
		Object*             (*ConstructorNewObject)(const ReflectConstructor* constructor, VoidPtr* params);
		void                (*ConstructorConstruct)(const ReflectConstructor* constructor, VoidPtr memory, VoidPtr* params);

		StringView (*ValueGetDesc)(const ReflectValue* value);
		i64        (*ValueGetCode)(const ReflectValue* value);
		ConstPtr   (*ValueGetValue)(const ReflectValue* value);
		bool       (*ValueCompare)(const ReflectValue* value, ConstPtr compareValue);
		void       (*ValueSetToPointer)(const ReflectValue* value, VoidPtr pointer);
	};

	SK_API const ReflectionApi* GetReflectionApi();
}
