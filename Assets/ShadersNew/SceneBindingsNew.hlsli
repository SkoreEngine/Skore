#ifndef SK_SCENE_BINDINGS_NEW_HLSLI
#define SK_SCENE_BINDINGS_NEW_HLSLI

cbuffer SceneBuffer : register(b0, space1)
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
	float2 scenePad0;

	float2 jitter;
	float2 prevJitter;

	uint   instanceCount;
	uint   scenePad1;
	uint2  cullingMask;

	float4 frustumPlanes[6];
};

#endif
