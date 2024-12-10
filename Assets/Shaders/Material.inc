#include "Bindings.h"

struct MaterialSample
{
    float3     baseColor;
    float3     normal;
    float      roughness;
    float      metallic;
    float3     emissive;
    float      occlusion;
};

struct MaterialConstants
{
    float4 baseColorAlphaCutOff;
    float4 uvScaleNormalMultiplier;
    float4 metallicRoughness;
    float4 emissiveFactor;
    uint   baseColorIndex;
    uint   normalIndex;
    uint   roughnessIndex;
    uint   metallicIndex;
    uint   metallicRoughnessIndex;
    uint   emissiveIndex;
    uint   occlusionIndex;
    uint   _pad1;    
};

StructuredBuffer<MaterialConstants> materials : register(t0, space1);
SamplerState    materialSampler               : register(s1, space1); //TODO replace material sampler for proper texture samplers.


MaterialSample SampleMaterial(in uint materialIndex, in float2 uv, in float3 normal, in float4 tangent)
{
    MaterialConstants materialConstants = materials[NonUniformResourceIndex(materialIndex)];

    float3 baseColor    = materialConstants.baseColorAlphaCutOff.rgb;
    float  alphaCutOff  = materialConstants.baseColorAlphaCutOff.a;

    MaterialSample materialSample = (MaterialSample)0;

    float4 color = 1.0;
    if (materialConstants.baseColorIndex != 0)
    {
        color = bindlessTextures[NonUniformResourceIndex(materialConstants.baseColorIndex)].Sample(materialSampler, uv);
    }

    if (color.a < alphaCutOff)
    {
         discard;
    }

    materialSample.baseColor    = pow(color.rgb * baseColor, 2.2);
    materialSample.normal       = normal;
    materialSample.roughness    = materialConstants.metallicRoughness.x;
    materialSample.metallic     = materialConstants.metallicRoughness.y;
    materialSample.occlusion    = 1.0;

    if (materialConstants.normalIndex != 0)
    {
        float3 binormal = tangent.w * cross(normal, tangent.xyz);
        float3x3 TBN    = transpose(float3x3(tangent.xyz, binormal, normal));
        float3 n        = bindlessTextures[NonUniformResourceIndex(materialConstants.normalIndex)].Sample(materialSampler, uv).rgb * 2.0 - 1.0;
        //tangent space to world space.
        materialSample.normal = mul(TBN, n);
    }

    if (materialConstants.roughnessIndex != 0)
    {
        materialSample.roughness = bindlessTextures[NonUniformResourceIndex(materialConstants.roughnessIndex)].Sample(materialSampler, uv).r;
    }

    if (materialConstants.metallicIndex != 0)
    {
        materialSample.metallic = bindlessTextures[NonUniformResourceIndex(materialConstants.metallicIndex)].Sample(materialSampler, uv).r;
    }

    if (materialConstants.metallicRoughnessIndex != 0)
    {
        float2 metallicRoughness = bindlessTextures[NonUniformResourceIndex(materialConstants.metallicRoughnessIndex)].Sample(materialSampler, uv).zy;
        materialSample.metallic = metallicRoughness.x;
        materialSample.roughness = metallicRoughness.y;
    }

    if (materialConstants.emissiveIndex != 0)
    {
        materialSample.emissive = bindlessTextures[NonUniformResourceIndex(materialConstants.emissiveIndex)].Sample(materialSampler, uv).xyz;
    }

    if (materialConstants.occlusionIndex != 0)
    {
        //TODO
    }

    return materialSample;
}