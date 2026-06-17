using System;

namespace Skore
{
    public static class EntryPoint
    {
        public static int Bootstrap(IntPtr arg, int argSizeBytes)
        {
            Reflection.Initialize(arg);

            ReflectType[] types = Reflection.GetAllTypes();
            Console.WriteLine($"[SkoreEngine] Managed runtime initialized. {types.Length} reflected types, reflection version {Reflection.Version}.");

            return 0;
        }
    }
}
