#include "Common.hlsli"


[[vk::image_format("rgba16f")]]
RWTexture2D<float4>     taaOutput       : register(u0);
Texture2D<float2>       motionVector    : register(t1);
Texture2D               historyTexture  : register(t2);
Texture2D               colorTexture    : register(t3);
Texture2D<float>        depthTexture    : register(t4);
SamplerState            linearSampler   : register(s5);
SamplerState            nearestSampler  : register(s6);


struct PushConstants
{
    int2 resolution;
    float resetHistory;
    float pad;
};

[[vk::push_constant]]
PushConstants pc;

//TODO - not working well
#define SK_USE_YCOCG 0

// Reversible tone map to suppress HDR fireflies during neighborhood operations
float3 HDRToneMap(float3 c)   { return c / (1.0 + Luminance(c)); }
float3 HDRUnToneMap(float3 c) { return c / max(1.0 - Luminance(c), 0.001); }

// https://www.gdcvault.com/play/1022970/Temporal-Reprojection-Anti-Aliasing-in
float2 GetClosestVelocity(in float2 uv, in float2 texelSize, out bool isSkyPixel)
{
    // Scale uv for lower resolution motion vector texture
    float2 velocity;
    // reverse-Z: closest = largest depth value; sky/far = 0.
    float closestDepth = -0.1f;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            const float2 st = uv + float2(x, y) * texelSize;
            const float depth = depthTexture.SampleLevel(nearestSampler, st, 0.0f).x;
            if (depth > closestDepth)
            {
                velocity = motionVector.SampleLevel(nearestSampler, st, 0.0f).xy;
                closestDepth = depth;
            }
        }
    isSkyPixel = (closestDepth == 0.0f);
    return velocity;
}

// Source: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// License: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
float3 SampleHistoryCatmullRom(in float2 uv, in float2 texelSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv / texelSize;
    float2 texPos1   = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12      = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0  = texPos1 - 1.0f;
    float2 texPos3  = texPos1 + 2.0f;
    float2 texPos12 = texPos1 + offset12;

    texPos0  *= texelSize;
    texPos3  *= texelSize;
    texPos12 *= texelSize;

    float3 result = float3(0.0f, 0.0f, 0.0f);

    result += historyTexture.SampleLevel(linearSampler, float2(texPos0.x,  texPos0.y),  0.0f).xyz * w0.x  * w0.y;
    result += historyTexture.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y),  0.0f).xyz * w12.x * w0.y;
    result += historyTexture.SampleLevel(linearSampler, float2(texPos3.x,  texPos0.y),  0.0f).xyz * w3.x  * w0.y;

    result += historyTexture.SampleLevel(linearSampler, float2(texPos0.x,  texPos12.y), 0.0f).xyz * w0.x  * w12.y;
    result += historyTexture.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0f).xyz * w12.x * w12.y;
    result += historyTexture.SampleLevel(linearSampler, float2(texPos3.x,  texPos12.y), 0.0f).xyz * w3.x  * w12.y;

    result += historyTexture.SampleLevel(linearSampler, float2(texPos0.x,  texPos3.y),  0.0f).xyz * w0.x  * w3.y;
    result += historyTexture.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y),  0.0f).xyz * w12.x * w3.y;
    result += historyTexture.SampleLevel(linearSampler, float2(texPos3.x,  texPos3.y),  0.0f).xyz * w3.x  * w3.y;

#if SK_USE_YCOCG
    result = RGBToYCoCg(result);
#endif

    return max(result, 0.0f);
}

// https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/Resolve.hlsl
float FilterCubic(in float x, in float B, in float C) {
    float y = 0.0f;
    float x2 = x * x;
    float x3 = x * x * x;
    if(x < 1)
    {
        y = (12 - 9 * B - 6 * C) * x3 + (-18 + 12 * B + 6 * C) * x2 + (6 - 2 * B);
    }

    else if (x <= 2)
    {
        y = (-B - 6 * C) * x3 + (6 * B + 30 * C) * x2 + (-12 * B - 48 * C) * x + (8 * B + 24 * C);
    }
    return y / 6.0f;
}


float FilterCatmullRom(float value)
{
    return FilterCubic(value, 0, 0.5f);
}


float SubsampleFilter(float value)
{
    return FilterCatmullRom( value * 2 );
}

float4 ClipAABB(float3 aabbMin, float3 aabbMax, float4 previousSample, float averageAlpha) {
    // Note: Only clips towards AABB center (but fast!)
    float3 pClip = 0.5f * (aabbMax + aabbMin);
    float3 eClip = 0.5f * (aabbMax - aabbMin) + 0.000000001f;

    float4 vClip = previousSample - float4(pClip, averageAlpha);
    float3 vUnit = vClip.xyz / eClip;
    float3 aUnit = abs(vUnit);
    float maUnit = max(aUnit.x, max(aUnit.y, aUnit.z));

    if (maUnit > 1.0f) {
        return float4(pClip, averageAlpha) + vClip / maUnit;
    } else {
        // Point inside AABB
        return previousSample;
    }
}


[numthreads(8, 8, 1)]
void ResolveTemporal(in uint2 globalID : SV_DispatchThreadID)
{
    int2 colorOutputSize = pc.resolution;

    if (any(int2(globalID.xy) >= colorOutputSize))
        return;

    const float2 texelSize = 1.0f / float2(colorOutputSize.x, colorOutputSize.y);
    const float2 uv = (globalID.xy + 0.5f) * texelSize;


    bool isSkyPixel;
    const float2 velocity = GetClosestVelocity(uv, texelSize, isSkyPixel);
    float3 history = SampleHistoryCatmullRom(uv - velocity, texelSize);

    // Apply tone map to history to suppress fireflies
    history = HDRToneMap(history);

    // Current sampling: read a 3x3 neighborhood and cache color and other data to process history and final resolve.
    // Accumulate current sample and weights.
    float3 currentSampleTotal = float3(0, 0, 0);
    float currentSampleWeight = 0.0f;
    // Min and Max used for history clipping
    float3 neighborhoodMin = float3(10000, 10000, 10000);
    float3 neighborhoodMax = float3(-10000, -10000, -10000);
    // Cache of moments used in the resolve phase
    float3 m1 = float3(0, 0, 0);
    float3 m2 = float3(0, 0, 0);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            int2 pixelPosition = globalID + int2(x, y);
            pixelPosition = clamp(pixelPosition, int2(0, 0), int2(colorOutputSize.x - 1, colorOutputSize.y - 1));

            float3 neighborSample = colorTexture[pixelPosition].rgb;

            // Tone map before neighborhood operations to suppress HDR fireflies
            neighborSample = HDRToneMap(neighborSample);

#if SK_USE_YCOCG
            neighborSample = RGBToYCoCg(neighborSample);
#endif

            float2 subsamplePosition = float2(x * 1.0f, y * 1.0f);
            float subsampleDistance = length(subsamplePosition);
            float subsampleWeight = SubsampleFilter(subsampleDistance);

            currentSampleTotal += neighborSample * subsampleWeight;
            currentSampleWeight += subsampleWeight;

            neighborhoodMin = min(neighborhoodMin, neighborSample);
            neighborhoodMax = max(neighborhoodMax, neighborSample);

            m1 += neighborSample;
            m2 += neighborSample * neighborSample;
        }
    }

    float3 currentSample = currentSampleTotal / currentSampleWeight;

    float rcpSampleCount = 1.0f / 9.0f;
    float gamma = 1.4f;
    float3 mu = m1 * rcpSampleCount;
    float3 sigma = sqrt(abs((m2 * rcpSampleCount) - (mu * mu)));
    float3 minC = clamp(mu - gamma * sigma, neighborhoodMin, neighborhoodMax);
    float3 maxC = clamp(mu + gamma * sigma, neighborhoodMin, neighborhoodMax);

    // Clip history towards AABB center (no hard clamp before — let ClipAABB do its job)
    history.rgb = ClipAABB(minC, maxC, float4(history.rgb, 1), 1.0f).rgb;

    float3 result;

    // On first frame (or after resize), history is uninitialized — skip it entirely
    // (can't use lerp with alpha=1 because lerp propagates NaN from the history operand)
    if (pc.resetHistory > 0.5)
    {
        result = currentSample;
    }
    else
    {
        float speed = saturate(length(velocity) * 100.0);
        float baseAlpha = lerp(0.1, 0.4, speed);

        float lastLuminance = Luminance(history);
        float luminanceRange = Luminance(maxC) - Luminance(minC);
        float antiFlickerWeight = lastLuminance / (lastLuminance + luminanceRange + 0.00001f);
        float alpha = isnan(antiFlickerWeight) ? 0.1 : baseAlpha * antiFlickerWeight;

        result = lerp(history, currentSample, alpha);
    }

    // Undo tone map to return to HDR
    result = HDRUnToneMap(result);

#if SK_USE_YCOCG
    result = YCoCgToRGB(result);
#endif

    taaOutput[globalID] = float4(result, 1.0);
}