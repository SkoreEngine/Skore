using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Skore
{
    internal sealed class ScriptLoadContext : AssemblyLoadContext
    {
        private static readonly AssemblyLoadContext EngineContext =
            GetLoadContext(typeof(ScriptLoadContext).Assembly) ?? Default;

        public ScriptLoadContext() : base(isCollectible: false)
        {
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            foreach (Assembly assembly in EngineContext.Assemblies)
            {
                if (assembly.GetName().Name == assemblyName.Name)
                {
                    return assembly;
                }
            }
            return null;
        }
    }

    internal sealed unsafe class ScriptAssembly
    {
        private readonly ScriptLoadContext _loadContext;
        private readonly Assembly _assembly;
        private readonly List<TypeId> _typeIds = new();

        private ScriptAssembly(ScriptLoadContext loadContext, Assembly assembly)
        {
            _loadContext = loadContext;
            _assembly = assembly;
        }

        public IReadOnlyList<TypeId> TypeIds => _typeIds;

        public static ScriptAssembly Load(string path, void* userData, delegate* unmanaged[Cdecl]<void*, TypeId, void> callback)
        {
            ScriptLoadContext loadContext = new ScriptLoadContext();

            byte[] bytes = File.ReadAllBytes(path);
            using MemoryStream stream = new MemoryStream(bytes);
            Assembly assembly = loadContext.LoadFromStream(stream);

            ScriptAssembly script = new ScriptAssembly(loadContext, assembly);

            foreach (Type type in assembly.GetTypes())
            {
                if (!IsRegistrable(type))
                {
                    continue;
                }

                ReflectType reflectType = TypeRegistration.Register(type);
                TypeId typeId = reflectType.TypeId;
                script._typeIds.Add(typeId);

                if (callback != null)
                {
                    callback(userData, typeId);
                }
            }

            return script;
        }

        private static bool IsRegistrable(Type type)
        {
            if (type.IsInterface || type.IsGenericTypeDefinition)
            {
                return false;
            }
            if (type.IsAbstract && type.IsSealed)
            {
                return false;
            }
            if (type == typeof(SkoreObject) || !typeof(SkoreObject).IsAssignableFrom(type))
            {
                return false;
            }
            return type.IsPublic || type.IsNestedPublic;
        }
    }
}
