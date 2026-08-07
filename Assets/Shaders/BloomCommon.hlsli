#ifndef SK_BLOOM_COMMON_HLSLI
#define SK_BLOOM_COMMON_HLSLI

struct BloomPushConstants
{
	float2 texelSize;
	float  threshold;
	float  softKnee;
	float  bloomRadius;
	float  pad0;
	float  pad1;
	float  pad2;
};

[[vk::push_constant]]
ConstantBuffer<BloomPushConstants> pc;

                                   Texture2D           srcTexture    : register(t0);
                                   SamplerState        linearSampler : register(s1);
[[vk::image_format("r11g11b10f")]] RWTexture2D<float4> dstTexture    : register(u2);

float BloomLuminance(float3 rgb)
{
	return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
}

float3 SoftThreshold(float3 color)
{
	float brightness = max(color.r, max(color.g, color.b));
	float knee = pc.threshold * pc.softKnee;
	float soft = brightness - pc.threshold + knee;
	soft = clamp(soft, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee + 0.00001);
	float contribution = max(soft, brightness - pc.threshold) / max(brightness, 0.00001);
	return color * max(contribution, 0.0);
}

float KarisWeight(float3 color)
{
	return 1.0 / (1.0 + BloomLuminance(color));
}

//the sampler defaults to repeat addressing, so taps are clamped to the source interior
float3 SampleSrc(float2 uv)
{
	uv = clamp(uv, pc.texelSize * 0.5, 1.0 - pc.texelSize * 0.5);
	return srcTexture.SampleLevel(linearSampler, uv, 0).rgb;
}

#endif
