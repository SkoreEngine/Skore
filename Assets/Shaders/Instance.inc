struct InstanceData
{
     float4x4 model;
     float4x4 prevModel;
     uint     materialIndex;
     uint3   _pad;
};

StructuredBuffer<InstanceData> instances : register(t0, space3);