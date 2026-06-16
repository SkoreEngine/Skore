using System;

namespace Skore
{
    public static class EntryPoint
    {
        public static int Bootstrap(IntPtr arg, int argSizeBytes)
        {
            Console.WriteLine("[SkoreEngine] Managed runtime initialized.");
            return 0;
        }
    }
}
