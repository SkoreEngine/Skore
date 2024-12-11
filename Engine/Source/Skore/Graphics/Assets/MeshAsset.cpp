#include "MeshAsset.hpp"

#include "Skore/Core/Chronometer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/RenderUtils.hpp"

namespace Skore
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::MeshAsset");
    }

    Span<MeshPrimitive> MeshAsset::GetPrimitives() const
    {
        return primitives;
    }

    Span<MaterialAsset*> MeshAsset::GetMaterials() const
    {
        return materials;
    }

    Buffer MeshAsset::GetVertexBuffer()
    {
        if (!vertexBuffer)
        {
            Chronometer chronometer;
            Array<u8> data;
            this->LoadStream(0, verticesCount * sizeof(VertexStride), data);

            BufferCreation creation{
                .usage = BufferUsage::VertexBuffer,
                .size = data.Size(),
                .allocation = BufferAllocation::GPUOnly
            };

            vertexBuffer = Graphics::CreateBuffer(creation);


            Graphics::UpdateBufferData(BufferDataInfo{
                .buffer = vertexBuffer,
                .data = data.Data(),
                .size = data.Size(),
            });
            logger.Debug("time to load mesh {} - {}ms", GetName(), chronometer.Diff());
        }
        return vertexBuffer;
    }

    Buffer MeshAsset::GetIndexBuffer()
    {
        if (!indexBuffer)
        {
            Array<u8> data;
            this->LoadStream(verticesCount * sizeof(VertexStride), indicesCount * sizeof(u32), data);

            BufferCreation creation{
                .usage = BufferUsage::IndexBuffer,
                .size = data.Size(),
                .allocation = BufferAllocation::GPUOnly
            };

            indexBuffer = Graphics::CreateBuffer(creation);

            Graphics::UpdateBufferData(BufferDataInfo{
                .buffer = indexBuffer,
                .data = data.Data(),
                .size = data.Size(),
            });
        }
        return indexBuffer;
    }

    void MeshAsset::LoadIndexData(Array<u8>& data) const
    {
        this->LoadStream(verticesCount * sizeof(VertexStride), indicesCount * sizeof(u32), data);
    }

    void MeshAsset::LoadVertexData(Array<u8>& data) const
    {
        this->LoadStream(0, verticesCount * sizeof(VertexStride), data);
    }

    usize MeshAsset::GetIndexSize() const
    {
        return indicesCount * sizeof(u32);
    }

    usize MeshAsset::GetVertexSize() const
    {
        return verticesCount * sizeof(VertexStride);
    }

    MeshAsset::~MeshAsset()
    {
        if (vertexBuffer)
        {
            Graphics::DestroyBuffer(vertexBuffer);
        }

        if (indexBuffer)
        {
            Graphics::DestroyBuffer(indexBuffer);
        }
    }

    void MeshAsset::RegisterType(NativeTypeHandler<MeshAsset>& type)
    {
        type.Field<&MeshAsset::boundingBox>("boundingBox");
        type.Field<&MeshAsset::indicesCount>("indicesCount");
        type.Field<&MeshAsset::verticesCount>("verticesCount");
        type.Field<&MeshAsset::materials>("materials");
        type.Field<&MeshAsset::primitives>("primitives");

        // type.Field<&MeshAsset::vertices>("vertices");
        // type.Field<&MeshAsset::indices>("indices");
    }
}
