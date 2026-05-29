float LinearDepthToDeviceZ(float linearDepth, float nearClip, float farClip)
{
    return farClip * (linearDepth - nearClip) / (linearDepth * (farClip - nearClip));
}

float DeviceZToLinearDepth(float deviceZ, float nearClip, float farClip)
{
    return nearClip * farClip / (farClip - deviceZ * (farClip - nearClip));
}

void ComputePosAndReflection(float2   uv,
                             float3   viewPosition,
                             float3   reflDirVS,
                             float4x4 proj,
                             float    nearClip,
                             float    farClip,
                             float    rayBias,
                             out float3 outSamplePosInTS,
                             out float3 outReflDirInTS,
                             out float  outMaxDistance)
{
    float3 startVS = viewPosition + reflDirVS * rayBias;

    float4 startCS = mul(proj, float4(startVS, 1.0));
    startCS.xyz /= startCS.w;

    float  maxDist = farClip;
    float3 endVS   = startVS + reflDirVS * maxDist;
    if (endVS.z > -nearClip)
    {
        maxDist = (-nearClip - startVS.z) / reflDirVS.z;
        endVS   = startVS + reflDirVS * maxDist;
    }

    float4 endCS = mul(proj, float4(endVS, 1.0));
    endCS.xyz /= endCS.w;

    float3 dirCS = endCS.xyz - startCS.xyz;

    outSamplePosInTS = float3(uv, startCS.z);
    outReflDirInTS   = normalize(float3(dirCS.x * 0.5, dirCS.y * -0.5, dirCS.z));



    outMaxDistance = outReflDirInTS.x >= 0 ? (1 - outSamplePosInTS.x) / outReflDirInTS.x
                                           :    -outSamplePosInTS.x  / outReflDirInTS.x;
    outMaxDistance = min(outMaxDistance, outReflDirInTS.y < 0 ? (-outSamplePosInTS.y / outReflDirInTS.y)
                                                              : ((1 - outSamplePosInTS.y) / outReflDirInTS.y));
    outMaxDistance = min(outMaxDistance, outReflDirInTS.z < 0 ? (-outSamplePosInTS.z / outReflDirInTS.z)
                                                              : ((1 - outSamplePosInTS.z) / outReflDirInTS.z));
}

float2 GetCell(float2 pos, float2 cellCount)
{
    return floor(pos * cellCount);
}

bool CrossedCellBoundary(float2 oldCellIdx, float2 newCellIdx)
{
    return (oldCellIdx.x != newCellIdx.x) || (oldCellIdx.y != newCellIdx.y);
}

float2 GetCellCount(int mipLevel, Texture2D<float> texHiZ)
{
    uint width, height, numLevels;
    texHiZ.GetDimensions((uint)mipLevel, width, height, numLevels);
    return float2(width, height);
}

float3 IntersectDepthPlane(float3 o, float3 d, float z)
{
    return o + d * z;
}

float GetMinimumDepthPlane(float2 p, int mipLevel, Texture2D<float> texHiZ, float nearClip, float farClip)
{
    return LinearDepthToDeviceZ(texHiZ.SampleLevel(nearestSampler, p, mipLevel), nearClip, farClip);
}

float3 IntersectCellBoundary(float3 o, float3 d, float2 cell, float2 cellCount, float2 crossStep, float2 crossOffset)
{
    float3 intersection = 0;

    float2 index = cell + crossStep;
    float2 boundary = index / cellCount;
    boundary += crossOffset;

    float2 delta = boundary - o.xy;
    delta /= d.xy;
    float t = min(delta.x, delta.y);

    intersection = IntersectDepthPlane(o, d, t);

    return intersection;
}


float FindIntersectionHiZ(float3 samplePosInTS,
                          float3 vReflDirInTS,
                          float maxTraceDistance,
                          Texture2D<float> texHiZ,
                          int maxIterations,
                          float2 viewSize,
                          float nearClip,
                          float farClip,
                          float thicknessWorld,
                          inout float3 intersection)
{
    uint hizWidth, hizHeight, hizMips;
    texHiZ.GetDimensions(0, hizWidth, hizHeight, hizMips);
    const int maxLevel = (int)hizMips - 1;

    float2 crossStep = float2(vReflDirInTS.x >= 0 ? 1 : -1, vReflDirInTS.y >= 0 ? 1 : -1);
    float2 crossOffset = crossStep / viewSize / 128;
    crossStep = saturate(crossStep);

    float3 ray  = samplePosInTS.xyz;
    float  minZ = ray.z;
    float  maxZ = ray.z + vReflDirInTS.z * maxTraceDistance;
    float  deltaZ = (maxZ - minZ);

    float3 o = ray;
    float3 d = vReflDirInTS * maxTraceDistance;

    int startLevel = 6;
    int stopLevel  = 0;
    float2 startCellCount = GetCellCount(startLevel, texHiZ);

    float2 rayCell = GetCell(ray.xy, startCellCount);
    ray = IntersectCellBoundary(o, d, rayCell, startCellCount, crossStep, crossOffset * 64);

    int  level = startLevel;
    uint iter  = 0;
    bool isBackwardRay = vReflDirInTS.z < 0;
    float rayDir = isBackwardRay ? -1 : 1;

    while (level >= stopLevel && ray.z * rayDir <= maxZ * rayDir && iter < maxIterations)
    {
        const float2 cellCount  = GetCellCount(level, texHiZ);
        const float2 oldCellIdx = GetCell(ray.xy, cellCount);

        float cellMinZ = GetMinimumDepthPlane((oldCellIdx + 0.5f) / cellCount, level, texHiZ, nearClip, farClip);

        float3 tmpRay = ((cellMinZ > ray.z) && !isBackwardRay) ? IntersectDepthPlane(o, d, (cellMinZ - minZ) / deltaZ) : ray;

        const float2 newCellIdx = GetCell(tmpRay.xy, cellCount);

        float thickness = level == 0 ? (DeviceZToLinearDepth(ray.z, nearClip, farClip) - DeviceZToLinearDepth(cellMinZ, nearClip, farClip)) : 0;

        bool crossed = (isBackwardRay && (cellMinZ > ray.z)) || (thickness > thicknessWorld) || CrossedCellBoundary(oldCellIdx, newCellIdx);

        ray   = crossed ? IntersectCellBoundary(o, d, oldCellIdx, cellCount, crossStep, crossOffset) : tmpRay;
        level = crossed ? min((float)maxLevel, level + 1.0f) : level - 1;

        ++iter;
    }

    bool intersected = (level < stopLevel);

    if (intersected) {
        //printf("intersected on level %i", level);
    }

    intersection = ray;

    float intensity = intersected ? 1 : 0;
    return intensity;
}