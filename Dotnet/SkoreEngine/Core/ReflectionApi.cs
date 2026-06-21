using System;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ReflectionApi
    {
        public delegate* unmanaged[Cdecl]<StringView, IntPtr> FindTypeByName;
        public delegate* unmanaged[Cdecl]<TypeId, IntPtr> FindTypeById;
        public delegate* unmanaged[Cdecl]<uint*, IntPtr> GetAllTypes;
        public delegate* unmanaged[Cdecl]<TypeId, uint*, IntPtr> GetDerivedTypes;
        public delegate* unmanaged[Cdecl]<TypeId, UnmanagedSpan<TypeId>> GetTypesAnnotatedWith;
        public delegate* unmanaged[Cdecl]<uint> GetReflectionVersion;
        public delegate* unmanaged[Cdecl]<IntPtr> GetDefaultAllocator;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> TypeGetName;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView> TypeGetSimpleName;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView> TypeGetScope;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeProps*> TypeGetProps;
        public delegate* unmanaged[Cdecl]<IntPtr, uint> TypeGetVersion;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, byte> TypeIsDerivedOf;
        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<TypeId>> TypeGetBaseTypes;
        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<IntPtr>> TypeGetFields;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeFindField;
        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<IntPtr>> TypeGetFunctions;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeFindFunction;
        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<IntPtr>> TypeGetConstructors;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> TypeGetDefaultConstructor;
        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<IntPtr>> TypeGetValues;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeFindValueByName;
        public delegate* unmanaged[Cdecl]<IntPtr, long, IntPtr> TypeFindValueByCode;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> TypeNewObject;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> TypeDestroy;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> TypeDestructor;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, void> TypeCopy;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, IntPtr> TypeGetAttribute;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> FieldGetName;
        public delegate* unmanaged[Cdecl]<IntPtr, uint> FieldGetIndex;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*> FieldGetProps;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, nuint, void> FieldGet;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, nuint, void> FieldSet;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr> FieldGetObject;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, IntPtr> FieldGetAttribute;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> FunctionGetName;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView> FunctionGetSimpleName;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*, void> FunctionGetReturn;
        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<IntPtr>> FunctionGetParams;
        public delegate* unmanaged[Cdecl]<IntPtr, byte> FunctionIsStatic;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, IntPtr*, void> FunctionInvoke;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> FunctionGetFunctionPointer;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, IntPtr> FunctionGetAttribute;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> ParamGetName;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*> ParamGetProps;

        public delegate* unmanaged[Cdecl]<IntPtr, UnmanagedSpan<IntPtr>> ConstructorGetParams;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr*, IntPtr> ConstructorNewObject;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr*, void> ConstructorConstruct;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> ValueGetDesc;
        public delegate* unmanaged[Cdecl]<IntPtr, long> ValueGetCode;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> ValueGetValue;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, byte> ValueCompare;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ValueSetToPointer;

        public delegate* unmanaged[Cdecl]<StringView, StringView, TypeProps*, IntPtr> RegisterType;

        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*, StringView, IntPtr> TypeBuilderAddField;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeBuilderAddFunction;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*, StringView*, uint, IntPtr> TypeBuilderAddConstructor;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeProps*, IntPtr> TypeBuilderAddAttribute;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeBuilderAddValue;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> TypeBuilderSetFnDestroy;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> TypeBuilderSetFnCopy;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> TypeBuilderSetFnDestructor;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> TypeBuilderSetFnBatchDestructor;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, void> TypeBuilderAddBaseType;

        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetSerializer;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetDeserialize;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetCopy;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetGet;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetGetObject;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetFnSet;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetFnToResource;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetFnFromResource;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetFnGetResourceFieldInfo;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*, FieldProps*, uint, void> FieldBuilderSetFunctionPointerSignature;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeProps*, IntPtr> FieldBuilderAddAttribute;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView*, FieldProps*, uint, void> FunctionBuilderAddParams;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FunctionBuilderSetFnInvoke;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FunctionBuilderSetFunctionPointer;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*, void> FunctionBuilderSetReturnProps;
        public delegate* unmanaged[Cdecl]<IntPtr, byte, void> FunctionBuilderSetIsStatic;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeProps*, IntPtr> FunctionBuilderAddAttribute;

        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ConstructorBuilderSetPlacementNewFn;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ConstructorBuilderSetNewObjectFn;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ConstructorBuilderSetUserData;

        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ValueBuilderSetFnGetValue;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ValueBuilderSetFnGetCode;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ValueBuilderSetFnCompare;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ValueBuilderSetFnSetToPointer;

        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> AttributeBuilderSetGetValue;

        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> FieldGetUserData;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> FieldBuilderSetUserData;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> ConstructorGetUserData;
    }
}
