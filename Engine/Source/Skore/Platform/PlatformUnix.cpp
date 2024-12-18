#include "Platform.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Core/Logger.hpp"

#ifdef SK_UNIX

#include <dlfcn.h>
#include <ctime>

namespace Skore
{

    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::Platform");
    }

    namespace Platform
    {
        void InitStyle();
        void ApplyDarkStyle(VoidPtr internal);
    }

    void Platform::InitStyle()
    {

    }

    void Platform::ApplyDarkStyle(VoidPtr internal)
    {

    }

    void Platform::ShowInExplorer(const StringView& path)
    {
#ifdef SK_LINUX
        if (FileSystem::GetFileStatus(path).isDirectory)
        {
            String command = "xdg-open " + String{path};
            system(command.CStr());
        }
        else
        {
            String command = "xdg-open " + Path::Parent(path);
            system(command.CStr());
        }
#else
#endif
    }

    VoidPtr Platform::LoadDynamicLib(const StringView& library)
    {
        VoidPtr ptr;
        if (Path::Extension(library).Empty())
        {
            String libName = "lib" + String{library} + SK_SHARED_EXT;
            ptr = dlopen(libName.CStr(), RTLD_NOW);
        }
        else
        {
            ptr = dlopen(library.CStr(), RTLD_NOW);
        }

        if (ptr == nullptr)
        {
            const char* err = dlerror();
            logger.Error("error on load dynamic lib {} ", err);
        }

        return ptr;
    }

    VoidPtr Platform::GetFunctionAddress(VoidPtr library, const StringView& functionName)
    {
        return dlsym(library, functionName.CStr());
    }

    void Platform::FreeDynamicLib(VoidPtr library)
    {
        dlclose(library);
    }
}

#endif