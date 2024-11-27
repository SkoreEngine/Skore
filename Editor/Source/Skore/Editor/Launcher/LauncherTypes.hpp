#pragma once
#include "Skore/IO/Asset.hpp"

namespace Skore
{
    struct ProjectLauncherSettings : Asset
    {
        SK_BASE_TYPES(Asset);

        String        defaultPath{};
        Array<String> recentProjects{};

        static void RegisterType(NativeTypeHandler<ProjectLauncherSettings>& type);
    };
}
