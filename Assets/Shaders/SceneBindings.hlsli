#ifndef SK_SCENE_BINDINGS_HLSLI
#define SK_SCENE_BINDINGS_HLSLI

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
	float  time;
	float  scenePad0;

	float2 jitter;
	float2 prevJitter;

	uint   instanceCount;
	uint   scenePad1;
	uint2  cullingMask;

	float4 frustumPlanes[6];
};

//mirrors Skore::InstanceData in Runtime/Source/Skore/Graphics/RenderSceneObjects.hpp
struct InstanceData
{
	matrix transform;
	uint   materialIndex;
	uint   vertexByteOffset;
	uint   vertexLayoutIndex;
	uint   primitiveInfoIndex;

	float3 aabbMin;
	uint   pad0;

	float3 aabbMax;
	uint   pipelineIndex;

	uint  drawcallIndex;
	uint  transparent;
	uint2 layerMask;

	// 0xFFFFFFFF when this instance does not cast a shadow.
	uint  shadowPipelineIndex;
	// 0xFFFFFFFF when this instance has no scene skinning buffer.
	uint  boneBufferIndex;
	uint2 pad3;
};

StructuredBuffer<InstanceData> instances : register(t1, space1);

#endif
