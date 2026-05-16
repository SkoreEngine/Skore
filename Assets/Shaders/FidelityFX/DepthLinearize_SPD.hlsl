// Depth Linearization + SPD Mip Chain Generation
// Combines depth linearization with AMD's Single Pass Downsampler
// Mip 0: linearized from the raw depth buffer
// Mip 1..N: average-reduction downsample via SPD

struct SpdConstants
{
    uint  mips;
    uint  numWorkGroups;
    uint2 workGroupOffset;
    float nearClip;
    float farClip;
    uint2 inputSize;
};

[[vk::push_constant]]
ConstantBuffer<SpdConstants> spdConstants;

Texture2D<float> depthInput : register(t0);

[[vk::image_format("r32f")]] [[vk::binding(1)]] RWTexture2DArray<float4> imgDst[13] : register(u1);
[[vk::image_format("r32f")]] [[vk::binding(2)]] globallycoherent RWTexture2DArray<float4> imgDst6 : register(u2);

struct SpdGlobalAtomicBuffer
{
    uint counter[6];
};
[[vk::binding(3)]] globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic;

#define A_GPU
#define A_HLSL

#include "ffx_a.h"

groupshared AU1 spdCounter;

groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];

float LinearizeDepth(float rawDepth, float nearClip, float farClip)
{
    return nearClip * farClip / (farClip - rawDepth * (farClip - nearClip));
}

AF4 SpdLoadSourceImage(ASU2 tex, AU1 slice)
{
    float rawDepth = depthInput.Load(int3(tex, 0)).r;
    float linearDepth = LinearizeDepth(rawDepth, spdConstants.nearClip, spdConstants.farClip);
    AF4 result = AF4(linearDepth, 0, 0, 0);
    imgDst[0][int3(tex, slice)] = result;
    return result;
}

AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    return imgDst6[int3(tex, slice)];
}

void SpdStore(ASU2 pix, AF4 outValue, AU1 index, AU1 slice)
{
    if (index == 5)
    {
        imgDst6[int3(pix, slice)] = outValue;
        return;
    }
    imgDst[index + 1][int3(pix, slice)] = outValue;
}

void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(spdGlobalAtomic[0].counter[slice], 1, spdCounter);
}

AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}

void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic[0].counter[slice] = 0;
}

AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
    return AF4(
        spdIntermediateR[x][y],
        spdIntermediateG[x][y],
        spdIntermediateB[x][y],
        spdIntermediateA[x][y]);
}

void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spdIntermediateR[x][y] = value.x;
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
}

AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    //return min(min(v0, v1), min(v2, v3));
    return max(max(v0, v1), max(v2, v3));
    //return (v0 + v1 + v2 + v3) * 0.25;
}

#include "ffx_spd.h"

[numthreads(256, 1, 1)]
void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    SpdDownsample(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(spdConstants.mips),
        AU1(spdConstants.numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(spdConstants.workGroupOffset));
}
