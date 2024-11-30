#pragma once
#include "Skore/Core/StringView.hpp"
#include "Skore/Resource/ResourceTypes.hpp"


namespace Skore
{
    struct ResourceFile : ResourceHandler
    {
        RID info;
        RID asset;

        u64 currentVersion;
        u64 persistedVersion;

        void Load(ResourceObject* resourceObject) override;
        void OnChange(ResourceObject* resourceObject) override;
    };

    struct SK_API ResourceEditor
    {
        static void AddPackage(StringView name, StringView directory);
        static void SetProject(StringView name, StringView directory);
    };
}
