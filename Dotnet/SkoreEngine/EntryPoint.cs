using System;

namespace Skore
{

    public static class EntryPoint
    {
        private static readonly Logger? _logger = Logger.GetLogger("EntryPoint");

        public static int Bootstrap(IntPtr arg, int argSizeBytes)
        {
            Reflection.Initialize(arg);

            ReflectType[] types = Reflection.GetAllTypes();
            Console.WriteLine($"[SkoreEngine] Managed runtime initialized. {types.Length} reflected types, reflection version {Reflection.Version}.");


            _logger?.PrintLog(LogLevel.Info, "EntryPoint From logger");


            return 0;
        }
    }
}
