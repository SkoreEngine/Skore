struct InstanceData
{
     float4x4 current;
     float4x4 previous;
     uint     materialIndex;
     uint     vertexOffset;
     uint     _pad0;
     uint     _pad1;
};

StructuredBuffer<InstanceData> instances;