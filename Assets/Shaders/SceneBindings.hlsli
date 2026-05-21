#ifndef SK_SCENE_BINDINGS_HLSLI
#define SK_SCENE_BINDINGS_HLSLI

// space1 — scene data: camera UBO, lights, shadow tex/sampler.
// LightBuffer + shadow bindings live in Lights.hlsli (also at space1).

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
};

#endif
