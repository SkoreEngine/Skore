using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{

    public static class EntryPoint
    {
        private static readonly Logger? _logger = Logger.GetLogger("EntryPoint");

        private static readonly List<ScriptAssembly> _assemblies = new();

        public static int Bootstrap(IntPtr arg, int argSizeBytes)
        {
            Reflection.Initialize(arg);

            ReflectType[] types = Reflection.GetAllTypes();
            Console.WriteLine($"[SkoreEngine] Managed runtime initialized. {types.Length} reflected types, reflection version {Reflection.Version}.");

            _logger?.PrintLog(LogLevel.Info, "EntryPoint From logger");


            return 0;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static unsafe void LoadAssembly(StringView path, void* userData, delegate* unmanaged[Cdecl]<void*, TypeId, void> callback)
        {
            try
            {
                ScriptAssembly script = ScriptAssembly.Load(path.ToString(), userData, callback);
                _assemblies.Add(script);
            }
            catch (Exception e)
            {
                Console.WriteLine($"[SkoreEngine] LoadAssembly failed: {e}");
            }
        }
    }
}
