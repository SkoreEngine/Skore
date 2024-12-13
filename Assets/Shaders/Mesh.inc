struct VertexData
{
    float3 position;
    float3 normal;
    float3 color;
    float2 uv;
    float4 tangent;
};

VertexData GetVertexData(ByteAddressBuffer bytes, uint vertexId, uint vertexOffset)
{
    const uint positionOffset   = 0;
    const uint normalOffset     = positionOffset + sizeof(float3);
    const uint colorOffset      = normalOffset + sizeof(float3);
    const uint uvOffset         = colorOffset + sizeof(float3);
    const uint tangentOffset    = uvOffset + sizeof(float2);

    VertexData data = (VertexData)0;
    data.position = asfloat(bytes.Load3(vertexId * sizeof(VertexData) + vertexOffset + positionOffset));
    data.normal   = asfloat(bytes.Load3(vertexId * sizeof(VertexData) + vertexOffset + normalOffset));
    data.color    = asfloat(bytes.Load3(vertexId * sizeof(VertexData) + vertexOffset + colorOffset));
    data.uv       = asfloat(bytes.Load2(vertexId * sizeof(VertexData) + vertexOffset + uvOffset));
    data.tangent  = asfloat(bytes.Load4(vertexId * sizeof(VertexData) + vertexOffset + tangentOffset));
    return data;
}