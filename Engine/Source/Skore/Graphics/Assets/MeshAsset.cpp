#include "MeshAsset.hpp"

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
            logger.Debug("starting mesh texture {}", GetName());
            Array<u8> data = this->LoadStream(0, verticesCount * sizeof(VertexStride));
            logger.Debug("mesh data loaded {}", GetName());

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
            logger.Debug("mesh data uploaded {}", GetName());
        }
        return vertexBuffer;
    }

    Buffer MeshAsset::GetIndexBuffer()
    {
        if (!indexBuffer)
        {
            Array<u8> data = this->LoadStream(verticesCount * sizeof(VertexStride), indicesCount * sizeof(u32));

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
