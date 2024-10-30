static const float PI = 3.141592;
static const float TwoPI = 2 * PI;
static const float Epsilon = 0.001;
static const float minGGXAlpha = 0.0064f;

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT 2

static const float4x4 biasMat = float4x4(
	0.5, 0.0, 0.0, 0.5,
	0.0, 0.5, 0.0, 0.5,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
);

struct Light
{
    float4 directionType;
    float4 positionMultiplier;
    float4 color;
    float4 rangeCutoff;

    float3 GetDirection()
    {
        return directionType.xyz;
    }

    int GetType()
    {
        return int(directionType.w);
    }

    float3 GetPosition()
    {
        return positionMultiplier.xyz;
    }

    float GetIndirectMultiplier()
    {
        return positionMultiplier.w;
    }

    float3 GetColor()
    {
        return color.xyz;
    }

    float GetRange()
    {
        return rangeCutoff.x;
    }

    float GetInnerCutoff()
    {
        return rangeCutoff.y;
    }

    float GetaOuterCutoff()
    {
        return rangeCutoff.z;
    }
};


float3 GetSamplingVector(float outputWidth, float outputHeight, float outputDepth, uint3 threadID)
{
    float2 st = threadID.xy/float2(outputWidth, outputHeight);
    float2 uv = 2.0 * float2(st.x, 1.0-st.y) - 1.0;

    // Select vector based on cubemap face index.
    float3 ret;
    switch(threadID.z)
    {
        case 0: ret = float3(1.0,  uv.y, -uv.x); break;
        case 1: ret = float3(-1.0, uv.y,  uv.x); break;
        case 2: ret = float3(uv.x, 1.0, -uv.y); break;
        case 3: ret = float3(uv.x, -1.0, uv.y); break;
        case 4: ret = float3(uv.x, uv.y, 1.0); break;
        case 5: ret = float3(-uv.x, uv.y, -1.0); break;
    }
    return normalize(ret);
}

float2 SampleSphericalMap(float3 v)
{
    const float2 invAtan = float2(0.1591, 0.3183);
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    uv *= -1.0;
    return uv;
}

float3 GammaToLinear(float3 input)
{
    return pow(max(input,0.0f), 2.2f);
}

float3 LinearToGamma(float3 input)
{
    return pow(max(input,0.0f), 1.0f/2.2f);
}


float3 OctohedralToDirection(float2 e)
{
    float3 v = float3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
    {
        v.xy = (1.0 - abs(v.yx)) * (step(0.0, v.xy) * 2.0 - float2(1.0, 1.0));
    }
    return normalize(v);
}

float2 DirectionToOctohedral(float3 normal)
{
    float2 p = normal.xy * (1.0f / dot(abs(normal), float3(1.0f, 1.0f, 1.0f)));
    return normal.z > 0.0f ? p : (1.0f - abs(p.yx)) * (step(0.0f, p) * 2.0f - float2(1.0f, 1.0f));
}

float3 GetWorldPositionFromDepth(float2 uv, float depth, float4x4 viewProjInverse)
{
    // Take texture coordinate and remap to [-1.0, 1.0] range.
    float2 screenPos = uv * 2.0f - 1.0f;

    //invert y because of vulkan -.-
    screenPos.y *= -1;

    // Create NDC position.
    float4 ndcDepth = float4(screenPos, depth, 1.0f);

    // Transform back into world position.
    float4 worldPos = mul(viewProjInverse, ndcDepth);

    // Undo projection.
    return worldPos.xyz / worldPos.w;
}

