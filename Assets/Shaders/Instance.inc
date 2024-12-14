struct InstanceData
{
     uint     materialIndex;
     uint     vertexOffset;
     uint     _pad0;
     uint     _pad1;
};

StructuredBuffer<InstanceData> instances;
StructuredBuffer<float4x4>     transformBuffer;
StructuredBuffer<float4x4>     prevTransformBuffer;
