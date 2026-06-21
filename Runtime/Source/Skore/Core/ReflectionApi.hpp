#pragma once

#include "Skore/Common.hpp"
#include "Span.hpp"
#include "StringView.hpp"
#include "TypeInfo.hpp"
#include "Reflection.hpp"

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
		VoidPtr       (*GetDefaultAllocator)();

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

		ReflectTypeBuilder        (*RegisterType)(StringView name, StringView simpleName, const TypeProps* props);

		ReflectFieldBuilder       (*TypeBuilderAddField)(ReflectTypeBuilder builder, const FieldProps* props, StringView name);
		ReflectFunctionBuilder    (*TypeBuilderAddFunction)(ReflectTypeBuilder builder, StringView name);
		ReflectConstructorBuilder (*TypeBuilderAddConstructor)(ReflectTypeBuilder builder, FieldProps* props, StringView* names, u32 size);
		ReflectAttributeBuilder   (*TypeBuilderAddAttribute)(ReflectTypeBuilder builder, const TypeProps* props);
		ReflectValueBuilder       (*TypeBuilderAddValue)(ReflectTypeBuilder builder, StringView valueDesc);
		void                      (*TypeBuilderSetFnDestroy)(ReflectTypeBuilder builder, VoidPtr fnDestroy);
		void                      (*TypeBuilderSetFnCopy)(ReflectTypeBuilder builder, VoidPtr fnCopy);
		void                      (*TypeBuilderSetFnDestructor)(ReflectTypeBuilder builder, VoidPtr fnDestructor);
		void                      (*TypeBuilderSetFnBatchDestructor)(ReflectTypeBuilder builder, VoidPtr fnBatchDestructor);
		void                      (*TypeBuilderAddBaseType)(ReflectTypeBuilder builder, TypeID typeId);

		void                    (*FieldBuilderSetSerializer)(ReflectFieldBuilder builder, VoidPtr serialize);
		void                    (*FieldBuilderSetDeserialize)(ReflectFieldBuilder builder, VoidPtr deserialize);
		void                    (*FieldBuilderSetCopy)(ReflectFieldBuilder builder, VoidPtr copy);
		void                    (*FieldBuilderSetGet)(ReflectFieldBuilder builder, VoidPtr get);
		void                    (*FieldBuilderSetGetObject)(ReflectFieldBuilder builder, VoidPtr getObject);
		void                    (*FieldBuilderSetFnSet)(ReflectFieldBuilder builder, VoidPtr set);
		void                    (*FieldBuilderSetFnToResource)(ReflectFieldBuilder builder, VoidPtr fnToResource);
		void                    (*FieldBuilderSetFnFromResource)(ReflectFieldBuilder builder, VoidPtr fnFromResource);
		void                    (*FieldBuilderSetFnGetResourceFieldInfo)(ReflectFieldBuilder builder, VoidPtr fnGetResourceFieldInfo);
		void                    (*FieldBuilderSetFunctionPointerSignature)(ReflectFieldBuilder builder, const FieldProps* returnProps, FieldProps* params, u32 count);
		ReflectAttributeBuilder (*FieldBuilderAddAttribute)(ReflectFieldBuilder builder, const TypeProps* props);

		void                    (*FunctionBuilderAddParams)(ReflectFunctionBuilder builder, StringView* names, FieldProps* props, u32 size);
		void                    (*FunctionBuilderSetFnInvoke)(ReflectFunctionBuilder builder, VoidPtr fnInvoke);
		void                    (*FunctionBuilderSetFunctionPointer)(ReflectFunctionBuilder builder, VoidPtr functionPointer);
		void                    (*FunctionBuilderSetReturnProps)(ReflectFunctionBuilder builder, const FieldProps* returnProps);
		void                    (*FunctionBuilderSetIsStatic)(ReflectFunctionBuilder builder, bool isStatic);
		ReflectAttributeBuilder (*FunctionBuilderAddAttribute)(ReflectFunctionBuilder builder, const TypeProps* props);

		void (*ConstructorBuilderSetPlacementNewFn)(ReflectConstructorBuilder builder, VoidPtr placementNew);
		void (*ConstructorBuilderSetNewObjectFn)(ReflectConstructorBuilder builder, VoidPtr newObject);
		void (*ConstructorBuilderSetUserData)(ReflectConstructorBuilder builder, VoidPtr userData);

		void (*ValueBuilderSetFnGetValue)(ReflectValueBuilder builder, VoidPtr fnGetValue);
		void (*ValueBuilderSetFnGetCode)(ReflectValueBuilder builder, VoidPtr fnGetCode);
		void (*ValueBuilderSetFnCompare)(ReflectValueBuilder builder, VoidPtr fnCompare);
		void (*ValueBuilderSetFnSetToPointer)(ReflectValueBuilder builder, VoidPtr fnSetToPointer);

		void (*AttributeBuilderSetGetValue)(ReflectAttributeBuilder builder, VoidPtr fnGetValue);

		VoidPtr (*FieldGetUserData)(const ReflectField* field);
		void    (*FieldBuilderSetUserData)(ReflectFieldBuilder builder, VoidPtr userData);
		VoidPtr (*ConstructorGetUserData)(const ReflectConstructor* constructor);
	};

	SK_API const ReflectionApi* GetReflectionApi();
}
