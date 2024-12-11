#pragma once

#include "MaterialAsset.hpp"
#include "Skore/Common.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/IO/Asset.hpp"

namespace Skore
{
    class SK_API MeshAsset : public Asset
    {
    public:
        SK_BASE_TYPES(Asset);

        static void RegisterType(NativeTypeHandler<MeshAsset>& type);

        Span<MeshPrimitive>  GetPrimitives() const;
        Span<MaterialAsset*> GetMaterials() const;

        AABB                  boundingBox;
        u32                   indicesCount = 0;
        usize                 verticesCount = 0;
        Array<MaterialAsset*> materials;
        Array<MeshPrimitive>  primitives{};

        Buffer GetVertexBuffer();
        Buffer GetIndexBuffer();

        void LoadVertexData(Array<u8>& data) const;
        void LoadIndexData(Array<u8>& data) const;

        usize GetVertexSize() const;
        usize GetIndexSize() const;

        ~MeshAsset() override;

    private:
        Buffer vertexBuffer{};
        Buffer indexBuffer{};
    };
}
