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

float4 SK_R8G8B8A8_UNORM_to_FLOAT4( uint packedInput )
{
	float4 unpackedOutput;
	unpackedOutput.x = (float)( packedInput & 0x000000ff ) / 255;
	unpackedOutput.y = (float)( ( ( packedInput >> 8 ) & 0x000000ff ) ) / 255;
	unpackedOutput.z = (float)( ( ( packedInput >> 16 ) & 0x000000ff ) ) / 255;
	unpackedOutput.w = (float)( packedInput >> 24 ) / 255;
	return unpackedOutput;
}

uint SK_FLOAT4_to_R8G8B8A8_UNORM( float4 unpackedInput )
{
	return ( ( uint( saturate( unpackedInput.x ) * 255 + 0.5 ) ) |
	         ( uint( saturate( unpackedInput.y ) * 255 + 0.5 ) << 8 ) |
	         ( uint( saturate( unpackedInput.z ) * 255 + 0.5 ) << 16 ) |
	         ( uint( saturate( unpackedInput.w ) * 255 + 0.5 ) << 24 ) );
}

void SK_DecodeVisibilityBentNormal( const uint packedValue, out float visibility, out float3 bentNormal )
{
	float4 decoded = SK_R8G8B8A8_UNORM_to_FLOAT4( packedValue );
	bentNormal = decoded.xyz * 2.0.xxx - 1.0.xxx;   // could normalize - don't want to since it's done so many times, better to do it at the final step only
	visibility = decoded.w;
}

float SK_DecodeVisibilityBentNormal_VisibilityOnly( const uint packedValue )
{
	float visibility; float3 bentNormal;
	SK_DecodeVisibilityBentNormal( packedValue, visibility, bentNormal );
	return visibility;
}

float3 SK_R11G11B10_UNORM_to_FLOAT3( uint packedInput )
{
	float3 unpackedOutput;
	unpackedOutput.x = (float)( ( packedInput       ) & 0x000007ff ) / 2047.0f;
	unpackedOutput.y = (float)( ( packedInput >> 11 ) & 0x000007ff ) / 2047.0f;
	unpackedOutput.z = (float)( ( packedInput >> 22 ) & 0x000003ff ) / 1023.0f;
	return unpackedOutput;
}

// 'unpackedInput' is float3 and not float3 on purpose as half float lacks precision for below!
uint SK_FLOAT3_to_R11G11B10_UNORM( float3 unpackedInput )
{
	uint packedOutput;
	packedOutput =( ( uint( saturate( unpackedInput.x ) * 2047 + 0.5f ) ) |
	                ( uint( saturate( unpackedInput.y ) * 2047 + 0.5f ) << 11 ) |
	                ( uint( saturate( unpackedInput.z ) * 1023 + 0.5f ) << 22 ) );
	return packedOutput;
}

inline uint Align(uint value, uint alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}
inline uint2 Align(uint2 value, uint2 alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}
inline uint3 Align(uint3 value, uint3 alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}
inline uint4 Align(uint4 value, uint4 alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}

float2 GetUV(uint2 px, float2 size)
{
    return (float2(px) + 0.5) / size;
}

float2 CalculateMotionVector(float4 currentPos, float4 previousPos, float2 currentJitter, float2 previousJitter)
{
    float2 cancelJitter = previousJitter - currentJitter;
    float2 motionVectors = (previousPos.xy / previousPos.w) - (currentPos.xy / currentPos.w) - cancelJitter;
    // Transform motion vectors from NDC space to UV space (+Y is top-down).
    motionVectors *= float2(0.5f, -0.5f);
    return motionVectors;
}

float Luminance( float3 rgb ) {
    float l = 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
    return l;
}

float Squared(const float value)
{
    return value * value;
}

