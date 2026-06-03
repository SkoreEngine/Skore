
#if SK_REFLECTION_PASS

#include "LightCommon.hlsli"
#include "PBR.hlsli"

[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outputTexture           : register(u0);
Texture2D           colorTexture            : register(t1);
Texture2D           albedoMetallicTexture   : register(t2);
Texture2D<float2>   roughnessAoTexture      : register(t3);
Texture2D<float2>   normalTexture           : register(t4);
Texture2D<float>    linearDepthPyramid      : register(t5);
TextureCube         specularMap             : register(t6);
Texture2D           brdfLUT                 : register(t7);
SamplerState        brdfLUTSampler          : register(s8);
SamplerState        linearSampler           : register(s9);
SamplerState        nearestSampler          : register(s10);

RaytracingAccelerationStructure tlas        : register(t11);

struct ReflectionPushConstants
{
    float3   cameraPosition;
    float    reflectionMultiplier;

    float2   outputSize;
    float    farClip;
    uint     flags;

    float4x4 proj;
    float4x4 viewProj;
    float4x4 invView;

    int   maxIterations; // e.g. 64  — max total cells crossed
    int   maxMipLevel;   // e.g. 6   — highest (coarsest) mip to use
    float thickness;     // e.g. 0.1 — depth tolerance for a hit
    float rayBias;       // e.g. 0.001
    float nearClip;
};

[[vk::push_constant]] ReflectionPushConstants pc;

uint QuerySpecularTextureLevels()
{
    uint width, height, levels;
    specularMap.GetDimensions(0, width, height, levels);
    return levels;
}

uint2 QueryLinearDepthSize()
{
    uint width, height, levels;
    linearDepthPyramid.GetDimensions(0, width, height, levels);
    return uint2(width, height);
}

[numthreads(8, 8, 1)]
void ReflectionCS(uint2 px : SV_DispatchThreadID)
{
    if (any(int2(px) >= pc.outputSize))
        return;

    float linearDepth = linearDepthPyramid[px].r;
    if (linearDepth >= pc.farClip - 0.1) //need to improve precision.
    {
        outputTexture[px] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float2 uv = (float2(px) + 0.5) / pc.outputSize;

    float3 baseColor = albedoMetallicTexture[px].rgb;
    float  metallic  = albedoMetallicTexture[px].a;
    float  roughness = roughnessAoTexture[px].r;
    float  ao        = roughnessAoTexture[px].g;
    float3 normal    = OctohedralDecode(normalTexture[px].rg);

    float3 viewPosition = GetViewPositionFromLinearDepth(uv, linearDepth, pc.proj);
    float3 worldPosition = mul(pc.invView, float4(viewPosition, 1.0)).xyz;

    float3 V        = normalize(pc.cameraPosition - worldPosition);
    float3 N        = normalize(normal);
    float  cosLo    = max(dot(N, V), 0.0);
    float3 Lr       = 2.0 * cosLo * N - V;

    float3 F0 = lerp(0.04, baseColor, metallic);
    float3 F = FresnelSchlickRoughness(cosLo, F0, roughness);

    float3 specularColor = 0.0;

    [branch]
    if (pc.flags & SK_LIGHT_FLAGS_HAS_REFLECTION_TEXTURE)
    {
        uint   specularTextureLevels = QuerySpecularTextureLevels();
        float3 specularIrradiance = specularMap.SampleLevel(linearSampler, Lr, roughness * specularTextureLevels).rgb;
        float2 specularBRDF = brdfLUT.SampleLevel(brdfLUTSampler, float2(cosLo, roughness), 0).rg;
        specularColor = (specularIrradiance * (F * specularBRDF.x + specularBRDF.y)) * pc.reflectionMultiplier;
    }

    [branch]
    if (pc.flags & SK_LIGHT_FLAGS_SSR_ENABLED && roughness < 0.3)
    {

        if (pc.maxIterations == 0)
        {
            RayDesc ray;
            ray.Origin    = worldPosition + N * pc.rayBias;
            ray.Direction = normalize(Lr);
            ray.TMin      = 0.001;
            ray.TMax      = pc.farClip;

            RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
            q.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);

            while (q.Proceed())
            {
                if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
                {
                }
            }

            if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                float  t= q.CommittedRayT();
                float3 hitPos = ray.Origin + t * ray.Direction;

                float4 clipPos = mul(pc.viewProj, float4(hitPos, 1.0));
                float3 ndc = clipPos.xyz / clipPos.w;

                float2 screenUV = float2(ndc.x, -ndc.y) * 0.5 + 0.5;

                float hitDepth   = clipPos.w;
                float sceneDepth = linearDepthPyramid.SampleLevel(nearestSampler, screenUV, 0).r;

                bool onScreen    = clipPos.w > 0.0 && all(screenUV == saturate(screenUV));
                bool notOccluded = hitDepth <= sceneDepth + pc.thickness;

                if (onScreen && notOccluded)
                {
                    float3 color = colorTexture.SampleLevel(linearSampler, screenUV, 0).rgb;
                    specularColor = color;
                }
            }
        }
        else
        {

        /*
            float3 normalViewSpace = normalize(mul((float3x3) pc.view, normal));
            float3 viewDir = normalize(viewPosition);
            float3 reflDir = reflect(viewDir, normalViewSpace);

            float3 samplePosInTS;
            float3 vReflDirInTS;
            float  maxTraceDistance;
            ComputePosAndReflection(uv, viewPosition, reflDir, pc.proj, pc.nearClip, pc.farClip, pc.rayBias, samplePosInTS, vReflDirInTS, maxTraceDistance);

            float3 intersection = 0;
            float intensity = FindIntersectionHiZ(
                samplePosInTS,
                vReflDirInTS,
                maxTraceDistance,
                linearDepthPyramid,
                pc.maxIterations,
                pc.outputSize,
                pc.nearClip,
                pc.farClip,
                pc.thickness,
                intersection
            );

            float3 reflColor = colorTexture.SampleLevel(linearSampler, intersection.xy, 0).xyz;
            specularColor = lerp(specularColor, reflColor, intensity);
           */
        }
    }
    outputTexture[px] = float4(specularColor, 1.0);
}

#endif

#if SK_IRRADIANCE_TRACE || SK_IRRADIANCE_BLEND || SK_IRRADIANCE_SAMPLE || SK_IRRADIANCE_DEBUG || SK_IRRADIANCE_RELOCATE || SK_IRRADIANCE_CLASSIFY

#include "Common.hlsli"

#define SK_MAX_IRRADIANCE_VOLUMES 16

#define SK_IRRADIANCE_FIXED_RAYS 32
#define SK_IRRADIANCE_RELOCATION_ENABLED (1 << 0)
#define SK_IRRADIANCE_CLASSIFICATION_ENABLED (1 << 1)
#define SK_IRRADIANCE_PROBE_ACTIVE 0.0
#define SK_IRRADIANCE_PROBE_INACTIVE 1.0
#define SK_IRRADIANCE_BACKFACE_THRESHOLD 0.25
#define SK_IRRADIANCE_CLASSIFY_CELL_SCALE 1.0

struct IrradianceVolumeGPU
{
    float4 origin;
    float4 probeSpacing;
    float4 probeRayRotation;
    int4   probeCounts;
    int4   scrollOffsets;
    int4   scrollDelta;
    int    irradianceTextureWidth;
    int    irradianceTextureHeight;
    int    irradianceSideLength;
    int    raysPerProbe;
    int    visibilityTextureWidth;
    int    visibilityTextureHeight;
    int    visibilitySideLength;
    int    frame;
    float  hysteresis;
    float  normalBias;
    float  viewBias;
    float  maxRayDistance;
    float  irradianceGamma;
    float  energyConservation;
    float  distanceExponent;
    float  pad0;
};

float3 QuatRotate(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float3 SphericalFibonacci(float sampleIndex, float numSamples)
{
    const float b = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    float phi = TwoPI * frac(sampleIndex * b);
    float cosTheta = 1.0 - (2.0 * sampleIndex + 1.0) * (1.0 / numSamples);
    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float2 NormalizedOctCoord(int2 coords, int side)
{
    int withBorder = side + 2;
    float2 c = float2((coords.x - 1) % withBorder, (coords.y - 1) % withBorder);
    c += 0.5;
    c *= (2.0 / float(side));
    c -= 1.0;
    return c;
}

int3 PositiveMod(int3 a, int3 n)
{
    return ((a % n) + n) % n;
}

int3 ProbeStorageCoords(IrradianceVolumeGPU v, int storageIndex)
{
    int cx = v.probeCounts.x;
    int cxy = v.probeCounts.x * v.probeCounts.y;
    return int3(storageIndex % cx, (storageIndex % cxy) / cx, storageIndex / cxy);
}

int ProbeStorageIndex(IrradianceVolumeGPU v, int3 storageCoords)
{
    return storageCoords.x + storageCoords.y * v.probeCounts.x + storageCoords.z * v.probeCounts.x * v.probeCounts.y;
}

int3 StorageToLogical(IrradianceVolumeGPU v, int3 storageCoords)
{
    return PositiveMod(storageCoords - v.scrollOffsets.xyz, v.probeCounts.xyz);
}

int3 LogicalToStorage(IrradianceVolumeGPU v, int3 logicalCoords)
{
    return PositiveMod(logicalCoords + v.scrollOffsets.xyz, v.probeCounts.xyz);
}

float3 ProbeWorldPosition(IrradianceVolumeGPU v, int3 storageCoords)
{
    int3 logical = StorageToLogical(v, storageCoords);
    return v.origin.xyz + float3(logical) * v.probeSpacing.xyz;
}

float2 GetProbeUV(float3 direction, int storageIndex, int texWidth, int texHeight, int side)
{
    float2 oct = OctohedralEncode(normalize(direction));
    float withBorder = float(side) + 2.0;
    int probesPerRow = texWidth / int(withBorder);
    int2 probeIndices = int2(storageIndex % probesPerRow, storageIndex / probesPerRow);
    float2 texel = float2(probeIndices) * withBorder + float2(1.0, 1.0) + float2(side * 0.5, side * 0.5) + oct * (side * 0.5);
    return texel / float2(float(texWidth), float(texHeight));
}

int GetProbeIndexFromPixels(int2 pixels, int probeWithBorderSide, int texWidth)
{
    int probesPerRow = texWidth / probeWithBorderSide;
    return (pixels.x / probeWithBorderSide) + probesPerRow * (pixels.y / probeWithBorderSide);
}

bool IrradianceRelocationEnabled(IrradianceVolumeGPU v)
{
    return (v.probeCounts.w & SK_IRRADIANCE_RELOCATION_ENABLED) != 0;
}

bool IrradianceClassificationEnabled(IrradianceVolumeGPU v)
{
    return (v.probeCounts.w & SK_IRRADIANCE_CLASSIFICATION_ENABLED) != 0;
}

bool IrradianceFixedRaysEnabled(IrradianceVolumeGPU v)
{
    return (v.probeCounts.w & (SK_IRRADIANCE_RELOCATION_ENABLED | SK_IRRADIANCE_CLASSIFICATION_ENABLED)) != 0;
}

int2 ProbeDataTexel(IrradianceVolumeGPU v, int storageIndex, int slice)
{
    int probesPerRow = v.probeCounts.x * v.probeCounts.y;
    return int2(storageIndex % probesPerRow, storageIndex / probesPerRow + slice * v.probeCounts.z);
}

float3 GetProbeRayDirection(IrradianceVolumeGPU v, int rayIndex)
{
    if (IrradianceFixedRaysEnabled(v))
    {
        if (rayIndex < SK_IRRADIANCE_FIXED_RAYS)
        {
            return normalize(SphericalFibonacci(float(rayIndex), float(SK_IRRADIANCE_FIXED_RAYS)));
        }
        return normalize(QuatRotate(v.probeRayRotation, SphericalFibonacci(float(rayIndex - SK_IRRADIANCE_FIXED_RAYS), float(v.raysPerProbe - SK_IRRADIANCE_FIXED_RAYS))));
    }
    return normalize(QuatRotate(v.probeRayRotation, SphericalFibonacci(float(rayIndex), float(v.raysPerProbe))));
}

bool IsProbeNewlyExposed(IrradianceVolumeGPU v, int3 logical)
{
    if (v.scrollDelta.w != 0)
    {
        return true;
    }
    [unroll]
    for (int a = 0; a < 3; ++a)
    {
        int d = v.scrollDelta[a];
        if (d > 0 && logical[a] >= v.probeCounts[a] - d) return true;
        if (d < 0 && logical[a] < -d) return true;
    }
    return false;
}

float3 GetVolumeIrradiance(IrradianceVolumeGPU v, Texture2D<float4> irradianceTex, Texture2D<float2> visibilityTex, RWTexture2D<float4> probeDataTex, SamplerState samp, int slice, int volumeCount,
                           float3 worldPosition, float3 normal, float3 cameraPosition)
{
    float3 spacing = v.probeSpacing.xyz;
    float3 Wo = normalize(cameraPosition - worldPosition);
    float3 biasedWorldPosition = worldPosition + (normal * v.normalBias) + (Wo * v.viewBias);

    int3   counts = v.probeCounts.xyz;
    float3 gridLocal = (biasedWorldPosition - v.origin.xyz) / spacing;
    int3   baseLogical = clamp(int3(floor(gridLocal)), int3(0, 0, 0), counts - int3(1, 1, 1));
    float3 baseLogicalWorld = v.origin.xyz + float3(baseLogical) * spacing;
    float3 alpha = clamp((biasedWorldPosition - baseLogicalWorld) / spacing, 0.0, 1.0);

    float3 sumIrradiance = float3(0.0, 0.0, 0.0);
    float  sumWeight = 0.0;

    for (int i = 0; i < 8; ++i)
    {
        int3 offset = int3(i, i >> 1, i >> 2) & int3(1, 1, 1);
        int3 logical = clamp(baseLogical + offset, int3(0, 0, 0), counts - int3(1, 1, 1));
        int3 storageCoords = LogicalToStorage(v, logical);
        int  storageIndex = ProbeStorageIndex(v, storageCoords);

        float4 probeMeta = probeDataTex[ProbeDataTexel(v, storageIndex, slice)];
        if (IrradianceClassificationEnabled(v) && probeMeta.w >= 0.5)
        {
            continue;
        }
        float3 probeOffset = float3(0.0, 0.0, 0.0);
        if (IrradianceRelocationEnabled(v))
        {
            probeOffset = probeMeta.xyz * spacing;
        }
        float3 probePos = v.origin.xyz + float3(logical) * spacing + probeOffset;

        float3 trilinear = lerp(1.0 - alpha, alpha, float3(offset));
        float  weight = 1.0;

        float3 dirToProbe = normalize(probePos - worldPosition);
        float  dirDotN = (dot(dirToProbe, normal) + 1.0) * 0.5;
        weight *= (dirDotN * dirDotN) + 0.2;

        float3 probeToPoint = biasedWorldPosition - probePos;
        float  distToPoint = length(probeToPoint);
        probeToPoint /= max(distToPoint, 1e-6);

        float2 visSubUV = GetProbeUV(probeToPoint, storageIndex, v.visibilityTextureWidth, v.visibilityTextureHeight, v.visibilitySideLength);
        float2 vis = visibilityTex.SampleLevel(samp, float2(visSubUV.x, (float(slice) + visSubUV.y) / float(volumeCount)), 0);
        float  meanDist = vis.x;
        float  cheb = 1.0;
        if (distToPoint > meanDist)
        {
            float variance = abs((vis.x * vis.x) - vis.y);
            float diff = distToPoint - meanDist;
            cheb = variance / (variance + (diff * diff));
            cheb = max(cheb * cheb * cheb, 0.0);
        }
        weight *= max(0.05, cheb);
        weight = max(0.000001, weight);

        const float crush = 0.2;
        if (weight < crush)
        {
            weight *= (weight * weight) * (1.0 / (crush * crush));
        }

        float2 irrSubUV = GetProbeUV(normal, storageIndex, v.irradianceTextureWidth, v.irradianceTextureHeight, v.irradianceSideLength);
        float3 probeIrradiance = irradianceTex.SampleLevel(samp, float2(irrSubUV.x, (float(slice) + irrSubUV.y) / float(volumeCount)), 0).rgb;
        probeIrradiance = pow(max(probeIrradiance, 0.0), v.irradianceGamma * 0.5);

        weight *= trilinear.x * trilinear.y * trilinear.z + 0.001;
        sumIrradiance += weight * probeIrradiance;
        sumWeight += weight;
    }

    if (sumWeight <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 net = sumIrradiance / sumWeight;
    net = net * net;
    return 0.5 * PI * net;
}

float VolumeBlendWeight(IrradianceVolumeGPU v, float3 worldPosition)
{
    float3 spacing = v.probeSpacing.xyz;
    float3 extent = spacing * float3(v.probeCounts.xyz - int3(1, 1, 1)) * 0.5;
    float3 center = v.origin.xyz + extent;
    float3 delta = abs(worldPosition - center) - extent;
    if (all(delta < 0.0))
    {
        return 1.0;
    }
    float w = 1.0;
    w *= 1.0 - saturate(delta.x / spacing.x);
    w *= 1.0 - saturate(delta.y / spacing.y);
    w *= 1.0 - saturate(delta.z / spacing.z);
    return w;
}

#endif

#if SK_IRRADIANCE_TRACE

#include "GlobalBindings.hlsli"
#include "SceneBindings.hlsli"
#include "Lights.hlsli"

StructuredBuffer<IrradianceVolumeGPU> volumes : register(t5, space2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> rayDataImage : register(u0, space2);
Texture2D<float4>                     prevIrradiance : register(t1, space2);
Texture2D<float2>                     prevDistance   : register(t2, space2);
TextureCube<float4>                   skyCube        : register(t3, space2);
SamplerState                          volumeSampler  : register(s4, space2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> probeData : register(u6, space2);

struct IrradianceTracePushConstants
{
    uint   volumeIndex;
    uint   enableMultibounce;
    uint   volumeCount;
    uint   ambientMode;
    float3 ambientColor;
    float  ambientIntensity;
};

[[vk::push_constant]] IrradianceTracePushConstants pc;

struct [raypayload] IrradiancePayload
{
    float3 radiance : read(caller) : write(closesthit, miss);
    float  hitT     : read(caller) : write(closesthit, miss);
};

[shader("raygeneration")]
void IrradianceProbeRayGen()
{
    IrradianceVolumeGPU v = volumes[pc.volumeIndex];

    int rayIndex = int(DispatchRaysIndex().x);
    int storageIndex = int(DispatchRaysIndex().y);
    int total = v.probeCounts.x * v.probeCounts.y * v.probeCounts.z;
    if (rayIndex >= v.raysPerProbe || storageIndex >= total)
    {
        return;
    }

    int3   storageCoords = ProbeStorageCoords(v, storageIndex);
    bool   newlyExposed = IsProbeNewlyExposed(v, StorageToLogical(v, storageCoords));

    if (IrradianceFixedRaysEnabled(v) && !newlyExposed)
    {
        float4 pd = probeData[ProbeDataTexel(v, storageIndex, int(pc.volumeIndex))];
        if (IrradianceClassificationEnabled(v) && pd.w >= 0.5 && rayIndex >= SK_IRRADIANCE_FIXED_RAYS)
        {
            return;
        }
    }

    float3 origin = ProbeWorldPosition(v, storageCoords);
    if (IrradianceRelocationEnabled(v) && !newlyExposed)
    {
        origin += probeData[ProbeDataTexel(v, storageIndex, int(pc.volumeIndex))].xyz * v.probeSpacing.xyz;
    }
    float3 direction = GetProbeRayDirection(v, rayIndex);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0001;
    ray.TMax = 10000.0;

    IrradiancePayload payload;
    payload.radiance = float3(0.0, 0.0, 0.0);
    payload.hitT = v.maxRayDistance;

    TraceRay(sceneTLAS, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);

    rayDataImage[int2(rayIndex, storageIndex + int(pc.volumeIndex) * total)] = float4(payload.radiance, payload.hitT);
}

float TraceSunShadow(float3 worldPos, float3 N)
{
    float3 L = normalize(-lights[shadowLightIndex].direction.xyz);
    if (dot(N, L) <= 0.0)
    {
        return 0.0;
    }

    RayDesc ray;
    ray.Origin    = worldPos + N * 0.05;
    ray.Direction = L;
    ray.TMin      = 0.01;
    ray.TMax      = 10000.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(sceneTLAS, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, ray);
    while (q.Proceed()) {}
    return (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
}

float ComputeSunShadow(float3 worldPos, float3 N)
{
    if (shadowLightIndex >= lightCount)
    {
        return 1.0;
    }

    [unroll]
    for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        float4 clip = mul(biasMat, mul(cascadeViewProjMat[i], float4(worldPos, 1.0)));
        float3 coord = clip.xyz / clip.w;
        if (all(coord.xy >= 0.02) && all(coord.xy <= 0.98) && coord.z >= 0.0 && coord.z <= 1.0)
        {
            return SampleShadowCascade(worldPos, N, i);
        }
    }

    return TraceSunShadow(worldPos, N);
}

float3 ShadeIrradianceSurface(IrradianceVolumeGPU v, float3 hitPos, float3 N, float3 baseColor, float roughness, float metallic)
{
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 V = -WorldRayDirection();
    float  shadow = ComputeSunShadow(hitPos, N);

    float3 lit = float3(0.0, 0.0, 0.0);
    for (uint l = 0; l < lightCount; ++l)
    {
        float3 contrib = EvaluateDirectLighting(lights[l], N, V, hitPos, baseColor, roughness, metallic, F0);
        if (l == shadowLightIndex)
        {
            contrib *= shadow;
        }
        lit += contrib;
    }

    if (pc.enableMultibounce != 0 && v.frame > 2)
    {
        float3 irradiance = GetVolumeIrradiance(v, prevIrradiance, prevDistance, probeData, volumeSampler, int(pc.volumeIndex), int(pc.volumeCount), hitPos, N, WorldRayOrigin());
        lit += (min(baseColor, 0.9) / PI) * irradiance;
    }

    return lit;
}

float4 UnpackTerrainLayerWeights(uint packed)
{
    return float4(
        (packed >>  0) & 0xFF,
        (packed >>  8) & 0xFF,
        (packed >> 16) & 0xFF,
        (packed >> 24) & 0xFF) * (1.0 / 255.0);
}

float3 TerrainTriplanarWeights(float3 worldNormal)
{
    float3 w = pow(abs(worldNormal), 4.0);
    return w / max(w.x + w.y + w.z, 0.0001);
}

float3 TerrainLayerAlbedo(uint materialIndex, float3 worldPos, float3 triWeights)
{
    MaterialData mat = MaterialDataBuffer[materialIndex];
    float3 albedo = mat.baseColor;
    if (mat.baseColorTexture >= 0)
    {
        float2 uvX = worldPos.zy * mat.uvScale;
        float2 uvY = worldPos.xz * mat.uvScale;
        float2 uvZ = worldPos.xy * mat.uvScale;
        float3 cx = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].SampleLevel(samplers[mat.GetBaseColorSamplerIndex()], uvX, 0).rgb;
        float3 cy = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].SampleLevel(samplers[mat.GetBaseColorSamplerIndex()], uvY, 0).rgb;
        float3 cz = BindlessTextures[NonUniformResourceIndex(mat.baseColorTexture)].SampleLevel(samplers[mat.GetBaseColorSamplerIndex()], uvZ, 0).rgb;
        float3 sampled = cx * triWeights.x + cy * triWeights.y + cz * triWeights.z;
        albedo = pow(sampled * mat.baseColor, 2.2);
    }
    return albedo;
}

[shader("closesthit")]
void IrradianceProbeClosestHit(inout IrradiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    IrradianceVolumeGPU v = volumes[pc.volumeIndex];
    float3 hitPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    if (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE)
    {
        payload.radiance = float3(0.0, 0.0, 0.0);
        payload.hitT = -RayTCurrent() * 0.2;
        return;
    }

    InstanceData inst = instances[InstanceID()];

    if (inst.primitiveInfoIndex == 0xFFFFFFFF)
    {
        payload.radiance = float3(0.0, 0.0, 0.0);
        payload.hitT = RayTCurrent();
        return;
    }

    MeshPrimitiveInfo info = MeshLODBuffer[inst.primitiveInfoIndex];
    uint lodIdx = min(1u, info.lodCount - 1u);
    uint firstIndex = info.lods[lodIdx].firstIndex;
    uint baseIdx = firstIndex + PrimitiveIndex() * 3;

    uint i0 = MeshDataBuffer.Load((baseIdx + 0) << 2);
    uint i1 = MeshDataBuffer.Load((baseIdx + 1) << 2);
    uint i2 = MeshDataBuffer.Load((baseIdx + 2) << 2);

    float3 n0 = GetVertexNormal(inst.vertexByteOffset, inst.vertexLayoutIndex, i0);
    float3 n1 = GetVertexNormal(inst.vertexByteOffset, inst.vertexLayoutIndex, i1);
    float3 n2 = GetVertexNormal(inst.vertexByteOffset, inst.vertexLayoutIndex, i2);

    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float3 nLocal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 N = normalize(mul((float3x3)inst.transform, nLocal));

    MaterialData mat = MaterialDataBuffer[inst.materialIndex];

    payload.radiance = ShadeIrradianceSurface(v, hitPos, N, mat.baseColor, mat.roughness, mat.metallic);
    payload.hitT = RayTCurrent();
}

[shader("closesthit")]
void IrradianceProbeTerrainClosestHit(inout IrradiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    IrradianceVolumeGPU v = volumes[pc.volumeIndex];
    float3 hitPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    if (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE)
    {
        payload.radiance = float3(0.0, 0.0, 0.0);
        payload.hitT = -RayTCurrent() * 0.2;
        return;
    }

    InstanceData inst = instances[InstanceID()];

    if (inst.primitiveInfoIndex == 0xFFFFFFFF)
    {
        payload.radiance = float3(0.0, 0.0, 0.0);
        payload.hitT = RayTCurrent();
        return;
    }

    MeshPrimitiveInfo info = MeshLODBuffer[inst.primitiveInfoIndex];
    uint lodIdx = min(1u, info.lodCount - 1u);
    uint firstIndex = info.lods[lodIdx].firstIndex;
    uint baseIdx = firstIndex + PrimitiveIndex() * 3;

    uint i0 = MeshDataBuffer.Load((baseIdx + 0) << 2);
    uint i1 = MeshDataBuffer.Load((baseIdx + 1) << 2);
    uint i2 = MeshDataBuffer.Load((baseIdx + 2) << 2);

    float3 n0 = GetVertexNormal(inst.vertexByteOffset, inst.vertexLayoutIndex, i0);
    float3 n1 = GetVertexNormal(inst.vertexByteOffset, inst.vertexLayoutIndex, i1);
    float3 n2 = GetVertexNormal(inst.vertexByteOffset, inst.vertexLayoutIndex, i2);

    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float3 nLocal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 N = normalize(mul((float3x3)inst.transform, nLocal));

    float4 w0 = UnpackTerrainLayerWeights(GetVertexCustom0Uint(inst.vertexByteOffset, inst.vertexLayoutIndex, i0));
    float4 w1 = UnpackTerrainLayerWeights(GetVertexCustom0Uint(inst.vertexByteOffset, inst.vertexLayoutIndex, i1));
    float4 w2 = UnpackTerrainLayerWeights(GetVertexCustom0Uint(inst.vertexByteOffset, inst.vertexLayoutIndex, i2));
    float4 w = w0 * bary.x + w1 * bary.y + w2 * bary.z;
    float total = w.x + w.y + w.z + w.w;
    w /= max(total, 0.0001);

    uint packed01 = GetVertexCustom1Uint(inst.vertexByteOffset, inst.vertexLayoutIndex, i0);
    uint packed23 = GetVertexCustom2Uint(inst.vertexByteOffset, inst.vertexLayoutIndex, i0);
    uint4 palette = uint4(packed01 & 0xFFFF, (packed01 >> 16) & 0xFFFF, packed23 & 0xFFFF, (packed23 >> 16) & 0xFFFF);

    float3 triWeights = TerrainTriplanarWeights(N);

    float3 baseColor = float3(0.0, 0.0, 0.0);
    float  roughness = 0.0;
    float  metallic = 0.0;

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float wi = w[i];
        if (wi <= 0.005) continue;

        MaterialData mat = MaterialDataBuffer[palette[i]];
        baseColor += TerrainLayerAlbedo(palette[i], hitPos, triWeights) * wi;
        roughness += mat.roughness * wi;
        metallic  += mat.metallic  * wi;
    }
    roughness = max(roughness, 0.002);

    payload.radiance = ShadeIrradianceSurface(v, hitPos, N, baseColor, roughness, metallic);
    payload.hitT = RayTCurrent();
}

[shader("miss")]
void IrradianceProbeMiss(inout IrradiancePayload payload)
{
    IrradianceVolumeGPU v = volumes[pc.volumeIndex];
    float3 dir = WorldRayDirection();
    float3 sky = float3(0.0, 0.0, 0.0);
    if (dir.y >= 0.0)
    {
        if (pc.ambientMode == 1)
        {
            sky = skyCube.SampleLevel(volumeSampler, dir, 0).rgb;
        }
        else if (pc.ambientMode == 2)
        {
            sky = pc.ambientColor;
        }
    }
    payload.radiance = sky * pc.ambientIntensity;
    payload.hitT = v.maxRayDistance;
}

#endif

#if SK_IRRADIANCE_BLEND

StructuredBuffer<IrradianceVolumeGPU> volumes : register(t0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> rayData : register(u1);

#if SK_IRRADIANCE_BLEND_RADIANCE
[[vk::image_format("rgba16f")]] RWTexture2D<float4> irradianceImage : register(u2);
#else
[[vk::image_format("rg16f")]] RWTexture2D<float2> visibilityImage : register(u2);
#endif

[[vk::image_format("rgba16f")]] RWTexture2D<float4> probeData : register(u3);

struct IrradianceBlendPushConstants
{
    uint volumeIndex;
    uint volumeCount;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] IrradianceBlendPushConstants bpc;

#define SK_IRRADIANCE_CASCADE_SEED 0

static const int kReadTable[6] = {5, 3, 1, -1, -3, -5};

[numthreads(8, 8, 1)]
void IrradianceProbeBlendCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    IrradianceVolumeGPU v = volumes[bpc.volumeIndex];
    int slice = int(bpc.volumeIndex);
    int total = v.probeCounts.x * v.probeCounts.y * v.probeCounts.z;

#if SK_IRRADIANCE_BLEND_RADIANCE
    int texWidth = v.irradianceTextureWidth;
    int texHeight = v.irradianceTextureHeight;
    int side = v.irradianceSideLength;
#else
    int texWidth = v.visibilityTextureWidth;
    int texHeight = v.visibilityTextureHeight;
    int side = v.visibilitySideLength;
#endif

    int yOff = slice * texHeight;

    int2 coords = int2(dispatchThreadID.xy);
    if (coords.x >= texWidth || coords.y >= texHeight)
    {
        return;
    }

    int withBorder = side + 2;
    int lastPixel = side + 1;
    int storageIndex = GetProbeIndexFromPixels(coords, withBorder, texWidth);

    bool border = ((dispatchThreadID.x % withBorder) == 0) || ((dispatchThreadID.x % withBorder) == lastPixel)
               || ((dispatchThreadID.y % withBorder) == 0) || ((dispatchThreadID.y % withBorder) == lastPixel);

    if (!border)
    {
        int3 storageCoords = ProbeStorageCoords(v, storageIndex);
        int3 logical = StorageToLogical(v, storageCoords);
        bool clearProbe = v.scrollDelta.w != 0;
        [unroll]
        for (int a = 0; a < 3; ++a)
        {
            int d = v.scrollDelta[a];
            if (d > 0 && logical[a] >= v.probeCounts[a] - d) clearProbe = true;
            if (d < 0 && logical[a] < -d) clearProbe = true;
        }

        float4 result = float4(0.0, 0.0, 0.0, 0.0);
        bool   keepPrevious = false;

        if (IrradianceClassificationEnabled(v) && !clearProbe && probeData[ProbeDataTexel(v, storageIndex, slice)].w >= 0.5)
        {
            keepPrevious = true;
        }

        int  firstRay = IrradianceFixedRaysEnabled(v) ? SK_IRRADIANCE_FIXED_RAYS : 0;
        uint backfaces = 0;
        uint maxBackfaces = uint(float(v.raysPerProbe - firstRay) * 0.1);

        for (int rayIndex = firstRay; rayIndex < v.raysPerProbe && !keepPrevious; ++rayIndex)
        {
            int2   samplePos = int2(rayIndex, storageIndex + slice * total);
            float3 rayDirection = GetProbeRayDirection(v, rayIndex);
            float3 texelDirection = OctohedralDecode(NormalizedOctCoord(coords, side));
            float  weight = max(0.0, dot(texelDirection, rayDirection));
            float  hitT = rayData[samplePos].w;

#if SK_IRRADIANCE_BLEND_RADIANCE
            if (hitT < 0.0)
            {
                ++backfaces;
                if (backfaces >= maxBackfaces)
                {
                    keepPrevious = true;
                    break;
                }
                continue;
            }
            if (weight >= Epsilon)
            {
                float3 radiance = rayData[samplePos].rgb * v.energyConservation;
                result += float4(radiance * weight, weight);
            }
#else
            weight = pow(weight, v.distanceExponent);
            if (weight >= Epsilon)
            {
                float dist = min(abs(hitT), v.maxRayDistance);
                result += float4(dist * weight, dist * dist * weight, 0.0, weight);
            }
#endif
        }

        if (!keepPrevious)
        {
            if (result.w > Epsilon)
            {
                result.xyz /= result.w;
                result.w = 1.0;
            }

            int2  prevTexel = int2(coords.x, coords.y + yOff);
            float hysteresis = v.hysteresis;
            bool  useBlack = false;

            if (v.frame <= 2)
            {
                useBlack = true;
                hysteresis = 0.0;
            }
            else if (clearProbe)
            {
#if SK_IRRADIANCE_CASCADE_SEED
                int coarse = slice + 1;
                if (coarse < int(bpc.volumeCount))
                {
                    IrradianceVolumeGPU cv = volumes[coarse];
                    float3 probeWorld = v.origin.xyz + float3(logical) * v.probeSpacing.xyz;
                    int3   cLogical = clamp(int3(floor((probeWorld - cv.origin.xyz) / cv.probeSpacing.xyz)), int3(0, 0, 0), cv.probeCounts.xyz - int3(1, 1, 1));
                    int    cIdx = ProbeStorageIndex(cv, LogicalToStorage(cv, cLogical));
                    int    probesPerRow = texWidth / withBorder;
                    int2   cTile = int2(cIdx % probesPerRow, cIdx / probesPerRow) * withBorder;
                    prevTexel = int2(cTile.x + (int(coords.x) % withBorder), cTile.y + (int(coords.y) % withBorder) + coarse * texHeight);
                }
                else
#endif
                {
                    useBlack = true;
                    hysteresis = 0.0;
                }
            }

#if SK_IRRADIANCE_BLEND_RADIANCE
            result.rgb = pow(max(result.rgb, 0.0), 1.0 / v.irradianceGamma);
            float4 previous = useBlack ? float4(0.0, 0.0, 0.0, 0.0) : irradianceImage[prevTexel];
            result = lerp(result, previous, hysteresis);
            irradianceImage[int2(coords.x, coords.y + yOff)] = result;
#else
            float2 previous = useBlack ? float2(0.0, 0.0) : visibilityImage[prevTexel];
            result.rg = lerp(result.rg, previous, hysteresis);
            visibilityImage[int2(coords.x, coords.y + yOff)] = result.rg;
#endif
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (!border)
    {
        return;
    }

    int probePixelX = int(dispatchThreadID.x) % withBorder;
    int probePixelY = int(dispatchThreadID.y) % withBorder;
    bool cornerPixel = (probePixelX == 0 || probePixelX == lastPixel) && (probePixelY == 0 || probePixelY == lastPixel);
    bool rowPixel = (probePixelX > 0 && probePixelX < lastPixel);

    int2 src = coords;
    if (cornerPixel)
    {
        src.x += (probePixelX == 0) ? side : -side;
        src.y += (probePixelY == 0) ? side : -side;
    }
    else if (rowPixel)
    {
        src.x += kReadTable[probePixelX - 1];
        src.y += (probePixelY > 0) ? -1 : 1;
    }
    else
    {
        src.x += (probePixelX > 0) ? -1 : 1;
        src.y += kReadTable[probePixelY - 1];
    }

#if SK_IRRADIANCE_BLEND_RADIANCE
    irradianceImage[int2(coords.x, coords.y + yOff)] = irradianceImage[int2(src.x, src.y + yOff)];
#else
    visibilityImage[int2(coords.x, coords.y + yOff)] = visibilityImage[int2(src.x, src.y + yOff)];
#endif
}

#endif

#if SK_IRRADIANCE_SAMPLE

#include "SceneBindings.hlsli"

[[vk::image_format("rgba16f")]] RWTexture2D<float4> outputLight : register(u0, space2);
StructuredBuffer<IrradianceVolumeGPU> volumes                   : register(t1, space2);
Texture2D<float4> irradianceArray                               : register(t2, space2);
Texture2D<float2> distanceArray                                 : register(t3, space2);
Texture2D         albedoMetallicTexture                          : register(t4, space2);
Texture2D<float2> normalTexture                                  : register(t5, space2);
Texture2D<float>  linearDepthTexture                             : register(t6, space2);
SamplerState      volumeSampler                                  : register(s7, space2);
SamplerState      gbufferSampler                                 : register(s8, space2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> probeData     : register(u9, space2);

struct IrradianceSamplePushConstants
{
    uint  volumeCount;
    float indirectIntensity;
    uint  pad0;
    uint  pad1;
};

[[vk::push_constant]] IrradianceSamplePushConstants spc;

[numthreads(8, 8, 1)]
void IrradianceVolumeSampleCS(uint2 px : SV_DispatchThreadID)
{
    uint outW, outH;
    outputLight.GetDimensions(outW, outH);
    if (px.x >= outW || px.y >= outH)
    {
        return;
    }

    float2 uv = (float2(px) + 0.5) / float2(outW, outH);

    float3 result = float3(0.0, 0.0, 0.0);
    float  linearDepth = linearDepthTexture.SampleLevel(gbufferSampler, uv, 0).r;

    if (linearDepth < farClip - 0.1)
    {
        float4 albedoMetallic = albedoMetallicTexture.SampleLevel(gbufferSampler, uv, 0);
        float3 baseColor = albedoMetallic.rgb;
        float  metallic = albedoMetallic.a;
        float3 normal = OctohedralDecode(normalTexture.SampleLevel(gbufferSampler, uv, 0).rg);

        float3 viewPosition = GetViewPositionFromLinearDepth(uv, linearDepth, projection);
        float3 worldPosition = mul(viewInv, float4(viewPosition, 1.0)).xyz;

        float3 irradiance = float3(0.0, 0.0, 0.0);
        float  totalWeight = 0.0;

        for (uint i = 0; i < spc.volumeCount; ++i)
        {
            if (totalWeight >= 1.0)
            {
                break;
            }

            IrradianceVolumeGPU v = volumes[i];
            float volumeWeight = VolumeBlendWeight(v, worldPosition);
            if (volumeWeight <= 0.0)
            {
                continue;
            }

            float3 sampled = GetVolumeIrradiance(v, irradianceArray, distanceArray, probeData, volumeSampler, int(i), int(spc.volumeCount), worldPosition, normal, cameraPosition);

            float contribution = min(volumeWeight, 1.0 - totalWeight);
            irradiance += sampled * contribution;
            totalWeight += contribution;
        }

        if (totalWeight > 0.0)
        {
            irradiance /= totalWeight;
            float3 kD = (1.0 - metallic);
            result = kD * baseColor * irradiance * spc.indirectIntensity;
        }
    }

    outputLight[px] = float4(result, 1.0);
}

#endif

#if SK_IRRADIANCE_DEBUG

#include "GlobalBindings.hlsli"
#include "SceneBindings.hlsli"

StructuredBuffer<IrradianceVolumeGPU> volumes : register(t0, space2);
Texture2D<float4> irradianceTex               : register(t1, space2);
SamplerState      volumeSampler               : register(s2, space2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> probeData : register(u3, space2);

struct IrradianceDebugPushConstants
{
    uint vertexByteOffset;
    uint vertexLayoutIndex;
    uint volumeCount;
    uint renderVolumeIndex;
};

[[vk::push_constant]] IrradianceDebugPushConstants dpc;

struct DebugVSOutput
{
    float4 pos      : SV_POSITION;
    float3 normal   : NORMAL0;
    float3 worldPos : TEXCOORD2;
    nointerpolation uint volumeIndex  : TEXCOORD0;
    nointerpolation uint storageIndex : TEXCOORD1;
    nointerpolation uint stateFlags   : TEXCOORD3;
};

DebugVSOutput IrradianceProbeDebugVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    uint volumeIndex = dpc.renderVolumeIndex;
    uint storageIndex = instanceId;

    IrradianceVolumeGPU v = volumes[volumeIndex];
    int3   storageCoords = ProbeStorageCoords(v, int(storageIndex));
    float3 probePos = ProbeWorldPosition(v, storageCoords);

    uint stateFlags = 0;
    if (IrradianceFixedRaysEnabled(v))
    {
        float4 pd = probeData[ProbeDataTexel(v, int(storageIndex), int(volumeIndex))];
        if (IrradianceRelocationEnabled(v))
        {
            probePos += pd.xyz * v.probeSpacing.xyz;
        }
        if (IrradianceClassificationEnabled(v) && pd.w >= 0.5)
        {
            stateFlags = 1;
        }
    }

    float  radius = min(v.probeSpacing.x, min(v.probeSpacing.y, v.probeSpacing.z)) * 5;
    float3 localPos = GetVertexPosition(dpc.vertexByteOffset, dpc.vertexLayoutIndex, vertexId);
    float3 worldPos = probePos + localPos * radius;

    DebugVSOutput o;
    o.pos = mul(viewProjection, float4(worldPos, 1.0));
    o.normal = normalize(localPos);
    o.worldPos = worldPos;
    o.volumeIndex = volumeIndex;
    o.storageIndex = storageIndex;
    o.stateFlags = stateFlags;
    return o;
}

float4 IrradianceProbeDebugPS(DebugVSOutput input) : SV_TARGET
{
    IrradianceVolumeGPU v = volumes[input.volumeIndex];
    float2 subUV = GetProbeUV(input.normal, int(input.storageIndex), v.irradianceTextureWidth, v.irradianceTextureHeight, v.irradianceSideLength);
    float2 uv = float2(subUV.x, (float(input.volumeIndex) + subUV.y) / float(dpc.volumeCount));
    float3 irr = irradianceTex.SampleLevel(volumeSampler, uv, 0).rgb;
    irr = pow(max(irr, 0.0), v.irradianceGamma * 0.5);
    irr = irr * irr;

    if (IrradianceClassificationEnabled(v))
    {
        float3 N = normalize(input.normal);
        float3 V = normalize(cameraPosition - input.worldPos);
        float  rim = 1.0 - saturate(dot(N, V));
        float  border = smoothstep(0.55, 0.75, rim);
        float3 stateColor = (input.stateFlags != 0) ? float3(1.0, 0.04, 0.04) : float3(0.04, 1.0, 0.04);
        irr = lerp(irr, stateColor, border);
    }

    return float4(irr, 1.0);
}

#endif

#if SK_IRRADIANCE_RELOCATE || SK_IRRADIANCE_CLASSIFY

StructuredBuffer<IrradianceVolumeGPU> volumes : register(t0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> rayData   : register(u1);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> probeData : register(u2);

struct IrradianceProbePushConstants
{
    uint volumeIndex;
    uint volumeCount;
    uint pad0;
    uint pad1;
};

[[vk::push_constant]] IrradianceProbePushConstants ipc;

#endif

#if SK_IRRADIANCE_RELOCATE

[numthreads(64, 1, 1)]
void IrradianceProbeRelocateCS(uint3 tid : SV_DispatchThreadID)
{
    IrradianceVolumeGPU v = volumes[ipc.volumeIndex];
    int total = v.probeCounts.x * v.probeCounts.y * v.probeCounts.z;
    int storageIndex = int(tid.x);
    if (storageIndex >= total)
    {
        return;
    }

    int slice = int(ipc.volumeIndex);
    int rowBase = storageIndex + slice * total;
    int numFixed = min(SK_IRRADIANCE_FIXED_RAYS, v.raysPerProbe);

    int   closestBackfaceIndex = -1;
    int   closestFrontfaceIndex = -1;
    int   farthestFrontfaceIndex = -1;
    float closestBackfaceDistance = 1e27;
    float closestFrontfaceDistance = 1e27;
    float farthestFrontfaceDistance = 0.0;
    int   backfaceCount = 0;

    for (int r = 0; r < numFixed; ++r)
    {
        float hitT = rayData[int2(r, rowBase)].w;
        if (hitT < 0.0)
        {
            ++backfaceCount;
            hitT *= -5.0;
            if (hitT < closestBackfaceDistance)
            {
                closestBackfaceDistance = hitT;
                closestBackfaceIndex = r;
            }
        }
        else
        {
            if (hitT < closestFrontfaceDistance)
            {
                closestFrontfaceDistance = hitT;
                closestFrontfaceIndex = r;
            }
            else if (hitT > farthestFrontfaceDistance)
            {
                farthestFrontfaceDistance = hitT;
                farthestFrontfaceIndex = r;
            }
        }
    }

    int2   pdTexel = ProbeDataTexel(v, storageIndex, slice);
    float4 pd = probeData[pdTexel];
    bool   newlyExposed = IsProbeNewlyExposed(v, StorageToLogical(v, ProbeStorageCoords(v, storageIndex)));
    float3 currentOffset = newlyExposed ? float3(0.0, 0.0, 0.0) : pd.xyz * v.probeSpacing.xyz;
    float  state = pd.w;

    float minSpacing = min(v.probeSpacing.x, min(v.probeSpacing.y, v.probeSpacing.z));
    float probeMinFrontfaceDistance = minSpacing;

    float3 fullOffset = float3(1e27, 1e27, 1e27);

    if (closestBackfaceIndex != -1 && (float(backfaceCount) / float(numFixed)) > SK_IRRADIANCE_BACKFACE_THRESHOLD)
    {
        float3 closestBackfaceDirection = GetProbeRayDirection(v, closestBackfaceIndex);
        fullOffset = currentOffset + closestBackfaceDirection * (closestBackfaceDistance + probeMinFrontfaceDistance * 0.5);
    }
    else if (closestFrontfaceDistance < probeMinFrontfaceDistance)
    {
        float3 closestFrontfaceDirection = GetProbeRayDirection(v, closestFrontfaceIndex);
        float3 farthestFrontfaceDirection = GetProbeRayDirection(v, farthestFrontfaceIndex);
        if (dot(closestFrontfaceDirection, farthestFrontfaceDirection) <= 0.0)
        {
            fullOffset = currentOffset + farthestFrontfaceDirection * minSpacing * 0.25;
        }
    }
    else if (closestFrontfaceDistance > probeMinFrontfaceDistance)
    {
        float  moveBackLength = length(currentOffset);
        float  moveBackMargin = min(closestFrontfaceDistance - probeMinFrontfaceDistance, moveBackLength);
        float3 moveBackDirection = (moveBackLength > 1e-6) ? (-currentOffset / moveBackLength) : float3(0.0, 0.0, 0.0);
        fullOffset = currentOffset + moveBackMargin * moveBackDirection;
    }

    float3 normalizedOffset = fullOffset / v.probeSpacing.xyz;
    if (dot(normalizedOffset, normalizedOffset) < 0.2025)
    {
        currentOffset = fullOffset;
    }

    probeData[pdTexel] = float4(currentOffset / v.probeSpacing.xyz, state);
}

#endif

#if SK_IRRADIANCE_CLASSIFY

[numthreads(64, 1, 1)]
void IrradianceProbeClassifyCS(uint3 tid : SV_DispatchThreadID)
{
    IrradianceVolumeGPU v = volumes[ipc.volumeIndex];
    int total = v.probeCounts.x * v.probeCounts.y * v.probeCounts.z;
    int storageIndex = int(tid.x);
    if (storageIndex >= total)
    {
        return;
    }

    int slice = int(ipc.volumeIndex);
    int rowBase = storageIndex + slice * total;
    int numFixed = min(SK_IRRADIANCE_FIXED_RAYS, v.raysPerProbe);

    int backfaceCount = 0;
    [loop]
    for (int r = 0; r < numFixed; ++r)
    {
        if (rayData[int2(r, rowBase)].w < 0.0)
        {
            ++backfaceCount;
        }
    }

    int2   pdTexel = ProbeDataTexel(v, storageIndex, slice);
    float4 pd = probeData[pdTexel];
    float  state = SK_IRRADIANCE_PROBE_INACTIVE;

    if ((float(backfaceCount) / float(numFixed)) <= SK_IRRADIANCE_BACKFACE_THRESHOLD)
    {
        float3 cellBounds = v.probeSpacing.xyz * SK_IRRADIANCE_CLASSIFY_CELL_SCALE;
        [loop]
        for (int r2 = 0; r2 < numFixed; ++r2)
        {
            float hitT = rayData[int2(r2, rowBase)].w;
            if (hitT < 0.0)
            {
                continue;
            }
            float3 hitRelative = GetProbeRayDirection(v, r2) * hitT;
            if (all(abs(hitRelative) <= cellBounds))
            {
                state = SK_IRRADIANCE_PROBE_ACTIVE;
                break;
            }
        }
    }

    probeData[pdTexel] = float4(pd.xyz, state);
}

#endif