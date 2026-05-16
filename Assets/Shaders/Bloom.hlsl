#include "Common.hlsli"

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

                                    Texture2D    srcTexture     : register(t0);
                                    SamplerState linearSampler  : register(s1);
[[vk::image_format("r11g11b10f")]]  RWTexture2D<float4> dstTexture : register(u2);


// ============================================================================
// Shared helpers
// ============================================================================

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
    return 1.0 / (1.0 + Luminance(color));
}


// ============================================================================
// BloomPrefilter - First downsample with threshold + Karis average
// ============================================================================
[numthreads(8, 8, 1)]
void BloomPrefilter(uint2 px : SV_DispatchThreadID)
{
    int2 dstDimensions;
    dstTexture.GetDimensions(dstDimensions.x, dstDimensions.y);
    if (any(px >= (uint2)dstDimensions)) return;

    float2 uv = (px + 0.5) / float2(dstDimensions);

    // 13-tap downsample filter (Jimenez 2014)
    float3 a = srcTexture.SampleLevel(linearSampler, uv + float2(-2, -2) * pc.texelSize, 0).rgb;
    float3 b = srcTexture.SampleLevel(linearSampler, uv + float2( 0, -2) * pc.texelSize, 0).rgb;
    float3 c = srcTexture.SampleLevel(linearSampler, uv + float2( 2, -2) * pc.texelSize, 0).rgb;
    float3 d = srcTexture.SampleLevel(linearSampler, uv + float2(-2,  0) * pc.texelSize, 0).rgb;
    float3 e = srcTexture.SampleLevel(linearSampler, uv,                                  0).rgb;
    float3 f = srcTexture.SampleLevel(linearSampler, uv + float2( 2,  0) * pc.texelSize, 0).rgb;
    float3 g = srcTexture.SampleLevel(linearSampler, uv + float2(-2,  2) * pc.texelSize, 0).rgb;
    float3 h = srcTexture.SampleLevel(linearSampler, uv + float2( 0,  2) * pc.texelSize, 0).rgb;
    float3 i = srcTexture.SampleLevel(linearSampler, uv + float2( 2,  2) * pc.texelSize, 0).rgb;
    float3 j = srcTexture.SampleLevel(linearSampler, uv + float2(-1, -1) * pc.texelSize, 0).rgb;
    float3 k = srcTexture.SampleLevel(linearSampler, uv + float2( 1, -1) * pc.texelSize, 0).rgb;
    float3 l = srcTexture.SampleLevel(linearSampler, uv + float2(-1,  1) * pc.texelSize, 0).rgb;
    float3 m = srcTexture.SampleLevel(linearSampler, uv + float2( 1,  1) * pc.texelSize, 0).rgb;

    // Apply threshold
    a = SoftThreshold(a); b = SoftThreshold(b); c = SoftThreshold(c);
    d = SoftThreshold(d); e = SoftThreshold(e); f = SoftThreshold(f);
    g = SoftThreshold(g); h = SoftThreshold(h); i = SoftThreshold(i);
    j = SoftThreshold(j); k = SoftThreshold(k); l = SoftThreshold(l); m = SoftThreshold(m);

    // Karis average: weight 5 overlapping box samples by 1/(1+luma) to prevent fireflies
    float3 group0 = (j + k + l + m) * 0.25;
    float3 group1 = (a + b + d + e) * 0.25;
    float3 group2 = (b + c + e + f) * 0.25;
    float3 group3 = (d + e + g + h) * 0.25;
    float3 group4 = (e + f + h + i) * 0.25;

    float w0 = KarisWeight(group0);
    float w1 = KarisWeight(group1);
    float w2 = KarisWeight(group2);
    float w3 = KarisWeight(group3);
    float w4 = KarisWeight(group4);

    float wSum = w0 + w1 + w2 + w3 + w4;
    float3 result = (group0 * w0 + group1 * w1 + group2 * w2 + group3 * w3 + group4 * w4) / wSum;

    dstTexture[px] = float4(result, 1.0);
}


// ============================================================================
// BloomDownsample - Subsequent mip downsample with 13-tap filter
// ============================================================================
[numthreads(8, 8, 1)]
void BloomDownsample(uint2 px : SV_DispatchThreadID)
{
    int2 dstDimensions;
    dstTexture.GetDimensions(dstDimensions.x, dstDimensions.y);
    if (any(px >= (uint2)dstDimensions)) return;

    float2 uv = (px + 0.5) / float2(dstDimensions);

    // 13-tap downsample filter (Jimenez 2014)
    float3 a = srcTexture.SampleLevel(linearSampler, uv + float2(-2, -2) * pc.texelSize, 0).rgb;
    float3 b = srcTexture.SampleLevel(linearSampler, uv + float2( 0, -2) * pc.texelSize, 0).rgb;
    float3 c = srcTexture.SampleLevel(linearSampler, uv + float2( 2, -2) * pc.texelSize, 0).rgb;
    float3 d = srcTexture.SampleLevel(linearSampler, uv + float2(-2,  0) * pc.texelSize, 0).rgb;
    float3 e = srcTexture.SampleLevel(linearSampler, uv,                                  0).rgb;
    float3 f = srcTexture.SampleLevel(linearSampler, uv + float2( 2,  0) * pc.texelSize, 0).rgb;
    float3 g = srcTexture.SampleLevel(linearSampler, uv + float2(-2,  2) * pc.texelSize, 0).rgb;
    float3 h = srcTexture.SampleLevel(linearSampler, uv + float2( 0,  2) * pc.texelSize, 0).rgb;
    float3 i = srcTexture.SampleLevel(linearSampler, uv + float2( 2,  2) * pc.texelSize, 0).rgb;
    float3 j = srcTexture.SampleLevel(linearSampler, uv + float2(-1, -1) * pc.texelSize, 0).rgb;
    float3 k = srcTexture.SampleLevel(linearSampler, uv + float2( 1, -1) * pc.texelSize, 0).rgb;
    float3 l = srcTexture.SampleLevel(linearSampler, uv + float2(-1,  1) * pc.texelSize, 0).rgb;
    float3 m = srcTexture.SampleLevel(linearSampler, uv + float2( 1,  1) * pc.texelSize, 0).rgb;

    float3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    dstTexture[px] = float4(result, 1.0);
}


// ============================================================================
// BloomUpsample - Progressive upsample with 9-tap tent filter
// ============================================================================
[numthreads(8, 8, 1)]
void BloomUpsample(uint2 px : SV_DispatchThreadID)
{
    int2 dstDimensions;
    dstTexture.GetDimensions(dstDimensions.x, dstDimensions.y);
    if (any(px >= (uint2)dstDimensions)) return;

    float2 uv = (px + 0.5) / float2(dstDimensions);
    float radius = pc.bloomRadius;

    // 9-tap tent filter (1-2-1 in each direction)
    float3 a = srcTexture.SampleLevel(linearSampler, uv + float2(-1, -1) * pc.texelSize * radius, 0).rgb;
    float3 b = srcTexture.SampleLevel(linearSampler, uv + float2( 0, -1) * pc.texelSize * radius, 0).rgb;
    float3 c = srcTexture.SampleLevel(linearSampler, uv + float2( 1, -1) * pc.texelSize * radius, 0).rgb;
    float3 d = srcTexture.SampleLevel(linearSampler, uv + float2(-1,  0) * pc.texelSize * radius, 0).rgb;
    float3 e = srcTexture.SampleLevel(linearSampler, uv,                                          0).rgb;
    float3 f = srcTexture.SampleLevel(linearSampler, uv + float2( 1,  0) * pc.texelSize * radius, 0).rgb;
    float3 g = srcTexture.SampleLevel(linearSampler, uv + float2(-1,  1) * pc.texelSize * radius, 0).rgb;
    float3 h = srcTexture.SampleLevel(linearSampler, uv + float2( 0,  1) * pc.texelSize * radius, 0).rgb;
    float3 i = srcTexture.SampleLevel(linearSampler, uv + float2( 1,  1) * pc.texelSize * radius, 0).rgb;

    float3 upsample = e * 4.0;
    upsample += (b + d + f + h) * 2.0;
    upsample += (a + c + g + i);
    upsample *= (1.0 / 16.0);

    // Additive blend with existing downsample data at this mip (load from RWTexture to avoid layout conflict)
    float3 existing = dstTexture[px].rgb;

    dstTexture[px] = float4(existing + upsample, 1.0);
}
