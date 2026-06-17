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
        public delegate* unmanaged[Cdecl]<TypeId, Span<TypeId>> GetTypesAnnotatedWith;
        public delegate* unmanaged[Cdecl]<uint> GetReflectionVersion;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> TypeGetName;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView> TypeGetSimpleName;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView> TypeGetScope;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeProps*> TypeGetProps;
        public delegate* unmanaged[Cdecl]<IntPtr, uint> TypeGetVersion;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, byte> TypeIsDerivedOf;
        public delegate* unmanaged[Cdecl]<IntPtr, Span<TypeId>> TypeGetBaseTypes;
        public delegate* unmanaged[Cdecl]<IntPtr, Span<IntPtr>> TypeGetFields;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeFindField;
        public delegate* unmanaged[Cdecl]<IntPtr, Span<IntPtr>> TypeGetFunctions;
        public delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> TypeFindFunction;
        public delegate* unmanaged[Cdecl]<IntPtr, Span<IntPtr>> TypeGetConstructors;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> TypeGetDefaultConstructor;
        public delegate* unmanaged[Cdecl]<IntPtr, Span<IntPtr>> TypeGetValues;
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
        public delegate* unmanaged[Cdecl]<IntPtr, Span<IntPtr>> FunctionGetParams;
        public delegate* unmanaged[Cdecl]<IntPtr, byte> FunctionIsStatic;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, IntPtr*, void> FunctionInvoke;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> FunctionGetFunctionPointer;
        public delegate* unmanaged[Cdecl]<IntPtr, TypeId, IntPtr> FunctionGetAttribute;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> ParamGetName;
        public delegate* unmanaged[Cdecl]<IntPtr, FieldProps*> ParamGetProps;

        public delegate* unmanaged[Cdecl]<IntPtr, Span<IntPtr>> ConstructorGetParams;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr*, IntPtr> ConstructorNewObject;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr*, void> ConstructorConstruct;

        public delegate* unmanaged[Cdecl]<IntPtr, StringView> ValueGetDesc;
        public delegate* unmanaged[Cdecl]<IntPtr, long> ValueGetCode;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> ValueGetValue;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, byte> ValueCompare;
        public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> ValueSetToPointer;
    }
}
