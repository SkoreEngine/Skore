struct DrawIndexedIndirectArguments
{
     uint indexCountPerInstance;
     uint instanceCount;
     uint startIndexLocation;
     int baseVertexLocation;
     uint startInstanceLocation;
};

struct InstanceData
{
     uint     materialIndex;
     uint     vertexOffset;
     uint     _pad0;
     uint     _pad1;
};
