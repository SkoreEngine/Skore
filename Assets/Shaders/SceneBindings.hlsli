#ifndef SK_SCENE_BINDINGS_HLSLI
#define SK_SCENE_BINDINGS_HLSLI

#include "Common.hlsli"

cbuffer GlobalSceneBuffer : register(b0, space1)
{
	matrix viewProjection;
	matrix view;
	matrix projection;
	matrix viewInv;
	matrix projectionInv;
	matrix viewProjInv;

	float3 cameraPosition;
	float  farClip;

	int2   outputSize;
	float2 pad1;

	float2 jitter;
	float2 prevJitter;

	uint   instanceCount;
	uint   pad2;
	uint2  cullingMask;

	// Plane stored as (a, b, c, d); a point p is inside when dot(plane.xyz, p) + plane.w >= 0.
	// Order: Left, Right, Bottom, Top, Near, Far. Derived from viewProjection on the CPU.
	float4 frustumPlanes[6];
};

struct InstanceData
{
    matrix  transform;
    uint    materialIndex;
    uint    vertexByteOffset;
    uint    vertexLayoutIndex;
    uint    indexCount;

    float3  aabbMin;
    uint    firstIndex;

    float3  aabbMax;
    uint    pipelineIndex;

    uint    drawcallIndex;
    uint    transparent;
    uint2   layerMask;
};

StructuredBuffer<InstanceData> instances : register(t1, space1);

struct Light
{
	uint   type;
	float3 position;
	float4 direction;
	float4 color;
	float  intensity;
	float  range;
	float  innerConeAngle;
	float  outerConeAngle;
};

cbuffer LightBuffer : register(b2, space1)
{
	uint     lightCount;
	uint	 shadowLightIndex;
    float2   pad0;
	float4   cascadeSplits;
	float4x4 cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];
	Light    lights[MAX_LIGHTS];
};

Texture2DArray         shadowMapTexture     : register(t3, space1);
#if SK_USE_COMPARISON_STATE
SamplerComparisonState shadowMapSampler     : register(s4, space1);
#else
SamplerState   		   shadowMapSampler     : register(s5, space1);
#endif


#endif
