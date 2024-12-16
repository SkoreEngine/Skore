#pragma once
#include "GraphicsTypes.hpp"
#include "Skore/Common.hpp"


namespace Skore
{
    class MeshAsset;
    class MaterialAsset;
}

namespace Skore::RenderGlobals
{
    SK_API DescriptorSet   GetBindlessResources();
    SK_API DescriptorSet   GetMaterialDescriptor();
    SK_API Buffer          GetGlobalVertexBuffer();
    SK_API Buffer          GetGlobalIndexBuffer();
    SK_API u32             FindOrCreateMaterialInstance(const MaterialAsset* materialAsset);
    SK_API MeshLookupData* GetMeshLookupData(const MeshAsset* meshAsset);
}
