#ifndef SK_COMMON_HLSLI
#define SK_COMMON_HLSLI


static const float PI				= 3.14159265359;

#define SK_PI			 (3.1415926535897932384626433832795)
#define SK_PI_HALF (1.5707963267948966192313216916398)

static const float TwoPI = 2 * PI;
static const float Epsilon = 0.0001f;
static const float minGGXAlpha = 0.0064f;

#define SK_COL32_R_SHIFT    0
#define SK_COL32_G_SHIFT    8
#define SK_COL32_B_SHIFT    16
#define SK_COL32_A_SHIFT    24
#define SK_MAX_BONES        200
#define SK_MAX_LODS         10

#define SHADOW_MAP_CASCADE_COUNT 4
#define SK_USE_COMPARISON_STATE 1
#define SK_PCF_RANGE 1
#define MAX_LIGHTS 64

float4 ColorFromU32(uint value)
{
	return float4(
			((value >> SK_COL32_R_SHIFT) & 0xFF) / 255.0,
			((value >> SK_COL32_G_SHIFT) & 0xFF) / 255.0,
			((value >> SK_COL32_B_SHIFT) & 0xFF) / 255.0,
			((value >> SK_COL32_A_SHIFT) & 0xFF) / 255.0
	);
}

typedef float           lpfloat;
typedef float2          lpfloat2;
typedef float3          lpfloat3;
typedef float4          lpfloat4;
typedef float3x3        lpfloat3x3;

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

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN, https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
lpfloat FastSqrt( float x )
{
	return (lpfloat)(asfloat( 0x1fbd1df5 + ( asint( x ) >> 1 ) ));
}

// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
lpfloat FastACos( lpfloat inX )
{
	const lpfloat PI = 3.141593;
	const lpfloat HALF_PI = 1.570796;
	lpfloat x = abs(inX);
	lpfloat res = -0.156583 * x + HALF_PI;
	res *= FastSqrt(1.0 - x);
	return (inX >= 0) ? res : PI - res;
}

float3 OctohedralDecode(float2 e)
{
	float3 v = float3(e, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0.0)
	{
		v.xy = (1.0 - abs(v.yx)) * (step(0.0, v.xy) * 2.0 - float2(1.0, 1.0));
	}
	return normalize(v);
}

float2 OctohedralEncode(float3 normal)
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

float3 GetViewPositionFromDepth(float2 uv, float depth, float4x4 projInverse)
{
	float2 screenPos = uv * 2.0f - 1.0f;
	screenPos.y *= -1;

	float4 ndcDepth = float4(screenPos, depth, 1.0f);

	float4 viewPos = mul(projInverse, ndcDepth);

	return viewPos.xyz / viewPos.w;
}

// Reconstruct view-space position from linear depth (no matrix inverse needed).
// proj[0][0] = 1/(aspect*tanHalfFov), proj[1][1] = 1/tanHalfFov
// Returns RH view-space position (negative z in front of camera).
float3 GetViewPositionFromLinearDepth(float2 uv, float linearDepth, float4x4 proj)
{
	float2 screenPos = uv * 2.0f - 1.0f;
	screenPos.y *= -1;
	float3 viewPos;
	viewPos.x = screenPos.x * (linearDepth / proj[0][0]);
	viewPos.y = screenPos.y * (linearDepth / proj[1][1]);
	viewPos.z = -linearDepth;
	return viewPos;
}


float Squared(const float value)
{
	return value * value;
}

float2 UVNearest( int2 pixel, float2 textureSize )
{
	float2 uv = floor(pixel) + .5;
	return uv / textureSize;
}

float2 ViewToScreenUV(float4x4 proj, float3 viewPos)
{
    float4 clip = mul(proj, float4(viewPos, 1.0));
    float3 ndc  = clip.xyz / clip.w;
    return float2(ndc.x * 0.5 + 0.5,
                  ndc.y * 0.5 + 0.5);
}

float3 NDCFromUvRawDepth( float2 uv, float rawDepth )
{
	return float3( uv.x * 2 - 1, (1 - uv.y) * 2 - 1, rawDepth );
}

float2 CalculateMotionVector(float4 currentPos, float4 previousPos, float2 currentJitter, float2 previousJitter)
{
	//TODO - be careful, TAA is different.
	float2 cancelJitter = previousJitter - currentJitter;
	float2 motionVectors = (previousPos.xy / previousPos.w) - (currentPos.xy / currentPos.w) - cancelJitter;
	// Transform motion vectors from NDC space to UV space (+Y is top-down).
	motionVectors *= float2(0.5f, -0.5f);
	return motionVectors;
}

float Luminance( float3 rgb )
{
	float l = 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
	return l;
}

float3 ApplySharpening(in float3 center, in float3 top, in float3 left, in float3 right, in float3 bottom)
{
	float sharpenAmount = 0.25f;

	float accum  = 0.0f;
	float weight = 0.0f;
	float result = sqrt(Luminance(center));

	{
		float n0 = sqrt(Luminance(top));
		float n1 = sqrt(Luminance(bottom));
		float n2 = sqrt(Luminance(left));
		float n3 = sqrt(Luminance(right));

		float w0 = max(1.0f - 6.0f * (abs(result - n0) + abs(result - n1)), 0.0f);
		float w1 = max(1.0f - 6.0f * (abs(result - n2) + abs(result - n3)), 0.0f);

		w0 = min(1.25f * sharpenAmount * w0, w0);
		w1 = min(1.25f * sharpenAmount * w1, w1);

		accum += n0 * w0;
		accum += n1 * w0;
		accum += n2 * w1;
		accum += n3 * w1;

		weight += 2.0f * w0;
		weight += 2.0f * w1;
	}

	result = max(result * (weight + 1.0f) - accum, 0.0f);
	result = Squared(result);

	return min(center * result / max(Luminance(center), 1e-5f), 1.0f);
}

// https://software.intel.com/en-us/node/503873
float3 RGBToYCoCg(float3 c)
{
	// Y = R/4 + G/2 + B/4
	// Co = R/2 - B/2
	// Cg = -R/4 + G/2 - B/4
	return float3(c.x/4.0 + c.y/2.0 + c.z/4.0,
								c.x/2.0 - c.z/2.0,
								-c.x/4.0 + c.y/2.0 - c.z/4.0 );
}

// https://software.intel.com/en-us/node/503873
float3 YCoCgToRGB(float3 c)
{
	// R = Y + Co - Cg
	// G = Y + Cg
	// B = Y - Co - Cg
	return max(float3(c.x + c.y - c.z,
											c.x + c.z,
											c.x - c.y - c.z), float3(0, 0, 0));
}


#endif

