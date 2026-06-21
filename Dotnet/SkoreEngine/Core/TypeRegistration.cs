using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{
    public static unsafe class TypeRegistration
    {
        private sealed class FieldAccessor
        {
            public Func<object, object> Get = null!;
            public Action<object, object> Set = null!;
            public Type FieldType = null!;
            public int Size;
        }

        private static readonly Dictionary<string, IntPtr> _names = new();

        private static readonly IntPtr FieldGetPtr = (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, nuint, void>)&FieldGetThunk;
        private static readonly IntPtr FieldSetPtr = (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, nuint, void>)&FieldSetThunk;
        private static readonly IntPtr NewInstancePtr = (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, IntPtr>)&NewInstanceThunk;
        private static readonly IntPtr DestroyInstancePtr = (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr, void>)&DestroyInstanceThunk;

        public static IntPtr AllocInstance(object instance) => GCHandle.ToIntPtr(GCHandle.Alloc(instance));

        public static object? ResolveInstance(IntPtr handle) => handle == IntPtr.Zero ? null : GCHandle.FromIntPtr(handle).Target;

        public static void FreeInstance(IntPtr handle)
        {
            if (handle == IntPtr.Zero)
            {
                return;
            }
            GCHandle gcHandle = GCHandle.FromIntPtr(handle);
            if (gcHandle.IsAllocated)
            {
                gcHandle.Free();
            }
        }

        public static ReflectType Register(Type type)
        {
            string cppName = CppName(type);

            TypeProps props = MakeTypeProps(type, cppName);
            ReflectTypeBuilder builder = Reflection.RegisterType(cppName, type.Name, props);

            builder.SetFnDestroy(DestroyInstancePtr);

            AddBaseTypes(type, builder);
            RegisterMembers(type, builder, props.TypeId);

            if (type.IsValueType || type.GetConstructor(Type.EmptyTypes) != null)
            {
                ReflectConstructorBuilder constructorBuilder = builder.AddConstructor(null, null, 0);
                constructorBuilder.SetNewObjectFn(NewInstancePtr);
                constructorBuilder.SetUserData(GCHandle.ToIntPtr(GCHandle.Alloc(type)));
            }

            return Reflection.FindTypeByName(cppName)!.Value;
        }

        private static void AddBaseTypes(Type type, ReflectTypeBuilder builder)
        {
            Type? baseType = type.BaseType;
            while (baseType != null && baseType != typeof(object) && baseType != typeof(ValueType))
            {
                ReflectType? reflectBase = Reflection.FindTypeByName(CppName(baseType));
                if (reflectBase.HasValue)
                {
                    builder.AddBaseType(reflectBase.Value.TypeId);
                }
                baseType = baseType.BaseType;
            }
        }

        private static void RegisterMembers(Type type, ReflectTypeBuilder builder, TypeId ownerId)
        {
            foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (field.IsInitOnly || field.IsLiteral || field.DeclaringType == typeof(SkoreObject))
                {
                    continue;
                }
                FieldInfo captured = field;
                AddMember(builder, ownerId, captured.FieldType, captured.Name,
                    instance => captured.GetValue(instance)!,
                    (instance, value) => captured.SetValue(instance, value));
            }

            foreach (PropertyInfo property in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!property.CanRead || !property.CanWrite || property.GetIndexParameters().Length != 0)
                {
                    continue;
                }
                PropertyInfo captured = property;
                AddMember(builder, ownerId, captured.PropertyType, captured.Name,
                    instance => captured.GetValue(instance)!,
                    (instance, value) => captured.SetValue(instance, value));
            }
        }

        private static void AddMember(ReflectTypeBuilder builder, TypeId ownerId, Type memberType, string name, Func<object, object> get, Action<object, object> set)
        {
            if (!IsBlittable(memberType))
            {
                Console.WriteLine($"[SkoreEngine] TypeRegistration: skipping non-blittable member '{name}' of type '{memberType}'");
                return;
            }

            FieldProps props = MakeFieldProps(memberType, ownerId);
            ReflectFieldBuilder fieldBuilder = builder.AddField(props, name);
            fieldBuilder.SetGet(FieldGetPtr);
            fieldBuilder.SetFnSet(FieldSetPtr);

            FieldAccessor accessor = new FieldAccessor
            {
                Get = get,
                Set = set,
                FieldType = memberType,
                Size = UnmanagedSizeOf(memberType)
            };
            fieldBuilder.SetUserData(GCHandle.ToIntPtr(GCHandle.Alloc(accessor)));
        }

        private static FieldProps MakeFieldProps(Type memberType, TypeId ownerId)
        {
            return new FieldProps
            {
                Type = MakeTypeProps(memberType, null),
                OwnerId = ownerId
            };
        }

        private static TypeProps MakeTypeProps(Type type, string? overrideName)
        {
            string[] candidates = Candidates(type);
            if (overrideName == null)
            {
                foreach (string candidate in candidates)
                {
                    ReflectType? engineType = Reflection.FindTypeByName(candidate);
                    if (engineType.HasValue)
                    {
                        return engineType.Value.Props;
                    }
                }
            }

            string cppName = overrideName ?? candidates[0];
            bool blittable = type.IsValueType && IsBlittable(type);

            return new TypeProps
            {
                TypeId = TypeId.FromName(cppName),
                Name = MakeStringView(cppName),
                Size = (uint)(type.IsValueType ? UnmanagedSizeOf(type) : IntPtr.Size),
                Alignment = (uint)IntPtr.Size,
                IsTriviallyCopyableRaw = (byte)(blittable ? 1 : 0),
                IsEnumRaw = (byte)(type.IsEnum ? 1 : 0)
            };
        }

        private static string CppName(Type type) => Candidates(type)[0];

        private static string[] Candidates(Type type)
        {
            if (type == typeof(sbyte)) return new[] { "signed char", "i8", "int8_t" };
            if (type == typeof(byte)) return new[] { "unsigned char", "u8", "uint8_t" };
            if (type == typeof(short)) return new[] { "short", "i16", "int16_t" };
            if (type == typeof(ushort)) return new[] { "unsigned short", "u16", "uint16_t" };
            if (type == typeof(int)) return new[] { "int", "i32", "int32_t" };
            if (type == typeof(uint)) return new[] { "unsigned int", "u32", "uint32_t" };
            if (type == typeof(long)) return new[] { "long long", "__int64", "i64", "int64_t" };
            if (type == typeof(ulong)) return new[] { "unsigned long long", "unsigned __int64", "u64", "uint64_t" };
            if (type == typeof(float)) return new[] { "float", "f32" };
            if (type == typeof(double)) return new[] { "double", "f64" };
            if (type == typeof(bool)) return new[] { "bool" };

            string fullName = type.FullName ?? type.Name;
            return new[] { fullName.Replace("+", "::").Replace(".", "::") };
        }

        private static StringView MakeStringView(string value)
        {
            if (!_names.TryGetValue(value, out IntPtr buffer))
            {
                buffer = Marshal.StringToCoTaskMemUTF8(value);
                _names[value] = buffer;
            }
            int length = System.Text.Encoding.UTF8.GetByteCount(value);
            return new StringView((byte*)buffer, (ulong)length);
        }

        private static bool IsBlittable(Type type)
        {
            if (!type.IsValueType || type.IsGenericType)
            {
                return false;
            }
            try
            {
                object boxed = RuntimeHelpers.GetUninitializedObject(type);
                GCHandle handle = GCHandle.Alloc(boxed, GCHandleType.Pinned);
                handle.Free();
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static int UnmanagedSizeOf(Type type)
        {
            DynamicMethod method = new DynamicMethod($"SizeOf_{type.Name}", typeof(int), Type.EmptyTypes, typeof(TypeRegistration).Module, true);
            ILGenerator il = method.GetILGenerator();
            il.Emit(OpCodes.Sizeof, type);
            il.Emit(OpCodes.Ret);
            Func<int> sizeOf = (Func<int>)method.CreateDelegate(typeof(Func<int>));
            return sizeOf();
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void FieldGetThunk(IntPtr field, IntPtr instance, IntPtr dest, nuint destSize)
        {
            if (instance == IntPtr.Zero || dest == IntPtr.Zero)
            {
                return;
            }
            IntPtr context = Reflection.Api.FieldGetUserData(field);
            if (context == IntPtr.Zero || GCHandle.FromIntPtr(context).Target is not FieldAccessor accessor)
            {
                return;
            }
            object? target = GCHandle.FromIntPtr(instance).Target;
            if (target == null)
            {
                return;
            }

            object value = accessor.Get(target);
            long copy = Math.Min(accessor.Size, (long)destSize);
            GCHandle pinned = GCHandle.Alloc(value, GCHandleType.Pinned);
            try
            {
                Buffer.MemoryCopy((void*)pinned.AddrOfPinnedObject(), (void*)dest, (long)destSize, copy);
            }
            finally
            {
                pinned.Free();
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void FieldSetThunk(IntPtr field, IntPtr instance, IntPtr src, nuint srcSize)
        {
            if (instance == IntPtr.Zero || src == IntPtr.Zero)
            {
                return;
            }
            IntPtr context = Reflection.Api.FieldGetUserData(field);
            if (context == IntPtr.Zero || GCHandle.FromIntPtr(context).Target is not FieldAccessor accessor)
            {
                return;
            }
            object? target = GCHandle.FromIntPtr(instance).Target;
            if (target == null)
            {
                return;
            }

            object boxed = RuntimeHelpers.GetUninitializedObject(accessor.FieldType);
            long copy = Math.Min(accessor.Size, (long)srcSize);
            GCHandle pinned = GCHandle.Alloc(boxed, GCHandleType.Pinned);
            try
            {
                Buffer.MemoryCopy((void*)src, (void*)pinned.AddrOfPinnedObject(), accessor.Size, copy);
            }
            finally
            {
                pinned.Free();
            }
            accessor.Set(target, boxed);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static IntPtr NewInstanceThunk(IntPtr constructor, IntPtr allocator, IntPtr paramValues)
        {
            IntPtr context = Reflection.Api.ConstructorGetUserData(constructor);
            if (context == IntPtr.Zero || GCHandle.FromIntPtr(context).Target is not Type type)
            {
                return IntPtr.Zero;
            }
            if (Activator.CreateInstance(type) is not SkoreObject instance)
            {
                return IntPtr.Zero;
            }
            return instance.Handle;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void DestroyInstanceThunk(IntPtr type, IntPtr allocator, IntPtr instance)
        {
            FreeInstance(instance);
        }
    }
}
