#include "Common.hlsli"

#define XE_GTAO_FP32_DEPTHS
#define XE_GTAO_OCCLUSION_TERM_SCALE    (1.5f)      // for packing in UNORM (because raw, pre-denoised occlusion term can overshoot 1 but will later average out to 1)
#define XE_GTAO_NUMTHREADS_X 8 // these can be changed
#define XE_GTAO_NUMTHREADS_Y 8 // these can be changed
struct GTAOConstants
{
    int2    ViewportSize;
    float2  ViewportPixelSize;                  // .zw == 1.0 / ViewportSize.xy

    float2  DepthUnpackConsts;
    float2  CameraTanHalfFOV;

    float2  NDCToViewMul;
    float2  NDCToViewAdd;

    float2  NDCToViewMul_x_PixelSize;
    float   EffectRadius;                       // world (viewspace) maximum size of the shadow
    float   EffectFalloffRange;

    float  RadiusMultiplier;
    int    DepthMipLevels;                      // mip count from the linear depth texture
    float  FinalValuePower;
    float  DenoiseBlurBeta;

    float  SampleDistributionPower;
    float  ThinOccluderCompensation;
    float  DepthMIPSamplingOffset;
    int    NoiseIndex;                         // frameIndex % 64 if using TAA or 0 otherwise

    float4x4 View;
};

// From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
#define XE_HILBERT_LEVEL    6U
#define XE_HILBERT_WIDTH    ( (1U << XE_HILBERT_LEVEL) )
#define XE_HILBERT_AREA     ( XE_HILBERT_WIDTH * XE_HILBERT_WIDTH )
inline uint HilbertIndex( uint posX, uint posY )
{
    uint index = 0U;
    for( uint curLevel = XE_HILBERT_WIDTH/2U; curLevel > 0U; curLevel /= 2U )
    {
        uint regionX = ( posX & curLevel ) > 0U;
        uint regionY = ( posY & curLevel ) > 0U;
        index += curLevel * curLevel * ( (3U * regionX) ^ regionY);
        if( regionY == 0U )
        {
            if( regionX == 1U )
            {
                posX = uint( (XE_HILBERT_WIDTH - 1U) ) - posX;
                posY = uint( (XE_HILBERT_WIDTH - 1U) ) - posY;
            }

            uint temp = posX;
            posX = posY;
            posY = temp;
        }
    }
    return index;
}

// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
lpfloat XeGTAO_PackEdges( lpfloat4 edgesLRTB )
{
    // integer version:
    // edgesLRTB = saturate(edgesLRTB) * 2.9.xxxx + 0.5.xxxx;
    // return (((uint)edgesLRTB.x) << 6) + (((uint)edgesLRTB.y) << 4) + (((uint)edgesLRTB.z) << 2) + (((uint)edgesLRTB.w));
    //
    // optimized, should be same as above
    edgesLRTB = round( saturate( edgesLRTB ) * 2.9 );
    return dot( edgesLRTB, lpfloat4( 64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0 ) ) ;
}

lpfloat4 XeGTAO_CalculateEdges( const lpfloat centerZ, const lpfloat leftZ, const lpfloat rightZ, const lpfloat topZ, const lpfloat bottomZ )
{
    lpfloat4 edgesLRTB = lpfloat4( leftZ, rightZ, topZ, bottomZ ) - (lpfloat)centerZ;

    lpfloat slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    lpfloat slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    lpfloat4 edgesLRTBSlopeAdjusted = edgesLRTB + lpfloat4( slopeLR, -slopeLR, slopeTB, -slopeTB );
    edgesLRTB = min( abs( edgesLRTB ), abs( edgesLRTBSlopeAdjusted ) );
    return lpfloat4(saturate( ( 1.25 - edgesLRTB / (centerZ * 0.011) ) ));
}

void XeGTAO_OutputWorkingTerm( const uint2 pixCoord, lpfloat visibility, lpfloat3 bentNormal, RWTexture2D<uint> outWorkingAOTerm )
{
    visibility = saturate( visibility / lpfloat(XE_GTAO_OCCLUSION_TERM_SCALE) );
#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
    outWorkingAOTerm[pixCoord] = XeGTAO_EncodeVisibilityBentNormal( visibility, bentNormal );
#else
    outWorkingAOTerm[pixCoord] = uint(visibility * 255.0 + 0.5);
#endif
}

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 XeGTAO_ComputeViewspacePosition( const float2 screenPos, const float viewspaceDepth, const GTAOConstants consts )
{
    float3 ret;
    ret.xy = (consts.NDCToViewMul * screenPos.xy + consts.NDCToViewAdd) * viewspaceDepth;
    ret.z = viewspaceDepth;
    return ret;
}

void XeGTAO_MainPass( const uint2 pixCoord, lpfloat sliceCount, lpfloat stepsPerSlice, const lpfloat2 localNoise, lpfloat3 viewspaceNormal, const GTAOConstants consts,
    Texture2D<lpfloat> depthBuffer, SamplerState depthSampler, RWTexture2D<uint> outWorkingAOTerm, RWTexture2D<unorm float> outWorkingEdges )
{
    float2 normalizedScreenPos = (pixCoord + 0.5.xx) * consts.ViewportPixelSize;

    // Depth buffer is already linearized (view-space depth from SPD mip chain)
    lpfloat4 valuesUL   = depthBuffer.GatherRed( depthSampler, float2( pixCoord * consts.ViewportPixelSize )               );
    lpfloat4 valuesBR   = depthBuffer.GatherRed( depthSampler, float2( pixCoord * consts.ViewportPixelSize ), int2( 1, 1 ) );


    // viewspace Z at the center
    lpfloat viewspaceZ  = valuesUL.y; //depthBuffer.SampleLevel( depthSampler, normalizedScreenPos, 0 ).x;

    // viewspace Zs left top right bottom
    const lpfloat pixLZ = valuesUL.x;
    const lpfloat pixTZ = valuesUL.z;
    const lpfloat pixRZ = valuesBR.z;
    const lpfloat pixBZ = valuesBR.x;

    lpfloat4 edgesLRTB  = XeGTAO_CalculateEdges( (lpfloat)viewspaceZ, (lpfloat)pixLZ, (lpfloat)pixRZ, (lpfloat)pixTZ, (lpfloat)pixBZ );
    outWorkingEdges[pixCoord] = XeGTAO_PackEdges(edgesLRTB);

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
#ifdef XE_GTAO_FP32_DEPTHS
    viewspaceZ *= 0.99999;     // this is good for FP32 depth buffer
#else
    viewspaceZ *= 0.99920;     // this is good for FP16 depth buffer
#endif

    const float3 pixCenterPos   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, consts);
    const lpfloat3 viewVec      = (lpfloat3)normalize(-pixCenterPos);

    // prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases, but in Vanilla it seems rare so it's disabled by default
    // viewspaceNormal = normalize( viewspaceNormal + max( 0, -dot( viewspaceNormal, viewVec ) ) * viewVec );

    const lpfloat effectRadius              = (lpfloat)consts.EffectRadius * (lpfloat)consts.RadiusMultiplier;
    const lpfloat sampleDistributionPower   = (lpfloat)consts.SampleDistributionPower;
    const lpfloat thinOccluderCompensation  = (lpfloat)consts.ThinOccluderCompensation;
    const lpfloat falloffRange              = (lpfloat)consts.EffectFalloffRange * effectRadius;
    const lpfloat falloffFrom               = effectRadius * ((lpfloat)1-(lpfloat)consts.EffectFalloffRange);

    // fadeout precompute optimisation
    const lpfloat falloffMul        = (lpfloat)-1.0 / ( falloffRange );
    const lpfloat falloffAdd        = falloffFrom / ( falloffRange ) + (lpfloat)1.0;

    lpfloat visibility = 0;
#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
    lpfloat3 bentNormal = 0;
#else
    lpfloat3 bentNormal = viewspaceNormal;
#endif


    // see "Algorithm 1" in https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    {
        const lpfloat noiseSlice  = (lpfloat)localNoise.x;
        const lpfloat noiseSample = (lpfloat)localNoise.y;

        // quality settings / tweaks / hacks
        const lpfloat pixelTooCloseThreshold  = 1.3;      // if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance

        // approx viewspace pixel size at pixCoord; approximation of NDCToViewspace( normalizedScreenPos.xy + consts.ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * consts.NDCToViewMul_x_PixelSize;

        lpfloat screenspaceRadius   = effectRadius / (lpfloat)pixelDirRBViewspaceSizeAtCenterZ.x;

        // fade out for small screen radii
        visibility += saturate((10 - screenspaceRadius)/100)*0.5;

#if 0   // sensible early-out for even more performance; disabled because not yet tested
        [branch]
        if( screenspaceRadius < pixelTooCloseThreshold )
        {
            XeGTAO_OutputWorkingTerm( pixCoord, 1, viewspaceNormal, outWorkingAOTerm );
            return;
        }
#endif

        // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
        const lpfloat minS = (lpfloat)pixelTooCloseThreshold / screenspaceRadius;

        //[unroll]
        for( lpfloat slice = 0; slice < sliceCount; slice++ )
        {
            lpfloat sliceK = (slice+noiseSlice) / sliceCount;
            // lines 5, 6 from the paper
            lpfloat phi = sliceK * SK_PI;
            lpfloat cosPhi = cos(phi);
            lpfloat sinPhi = sin(phi);
            lpfloat2 omega = lpfloat2(cosPhi, -sinPhi);       //lpfloat2 on omega causes issues with big radii

            // convert to screen units (pixels) for later use
            omega *= screenspaceRadius;

            // line 8 from the paper
            const lpfloat3 directionVec = lpfloat3(cosPhi, sinPhi, 0);

            // line 9 from the paper
            const lpfloat3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

            // line 10 from the paper
            //axisVec is orthogonal to directionVec and viewVec, used to define projectedNormal
            const lpfloat3 axisVec = normalize( cross(orthoDirectionVec, viewVec) );

            // alternative line 9 from the paper
            // float3 orthoDirectionVec = cross( viewVec, axisVec );

            // line 11 from the paper
            lpfloat3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            // line 13 from the paper
            lpfloat signNorm = (lpfloat)sign( dot( orthoDirectionVec, projectedNormalVec ) );

            // line 14 from the paper
            lpfloat projectedNormalVecLength = length(projectedNormalVec);
            lpfloat cosNorm = (lpfloat)saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength);

            // line 15 from the paper
            lpfloat n = signNorm * FastACos(cosNorm);

            // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
            const lpfloat lowHorizonCos0  = cos(n+SK_PI_HALF);
            const lpfloat lowHorizonCos1  = cos(n-SK_PI_HALF);

            // lines 17, 18 from the paper, manually unrolled the 'side' loop
            lpfloat horizonCos0           = lowHorizonCos0; //-1;
            lpfloat horizonCos1           = lowHorizonCos1; //-1;

            [unroll]
            for( lpfloat step = 0; step < stepsPerSlice; step++ )
            {
                // R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
                const lpfloat stepBaseNoise = lpfloat(slice + step * stepsPerSlice) * 0.6180339887498948482; // <- this should unroll
                lpfloat stepNoise = frac(noiseSample + stepBaseNoise);

                // approx line 20 from the paper, with added noise
                lpfloat s = (step+stepNoise) / (stepsPerSlice); // + (lpfloat2)1e-6f);

                // additional distribution modifier
                s = (lpfloat)pow( s, (lpfloat)sampleDistributionPower );

                // avoid sampling center pixel
                s += minS;

                // approx lines 21-22 from the paper, unrolled
                lpfloat2 sampleOffset = s * omega;

                lpfloat sampleOffsetLength = length( sampleOffset );

                // note: when sampling, using point_point_point or point_point_linear sampler works, but linear_linear_linear will cause unwanted interpolation between neighbouring depth values on the same MIP level!
                const lpfloat mipLevel    = (lpfloat)clamp( log2( sampleOffsetLength ) - consts.DepthMIPSamplingOffset, 0, consts.DepthMipLevels - 1 );

                // Snap to pixel center (more correct direction math, avoids artifacts due to sampling pos not matching depth texel center - messes up slope - but adds other
                // artifacts due to them being pushed off the slice). Also use full precision for high res cases.
                sampleOffset = round(sampleOffset) * (lpfloat2)consts.ViewportPixelSize;

                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float  SZ0 = depthBuffer.SampleLevel( depthSampler, sampleScreenPos0, mipLevel ).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0, consts);

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float  SZ1 = depthBuffer.SampleLevel( depthSampler, sampleScreenPos1, mipLevel ).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1, consts);

                float3 sampleDelta0     = (samplePos0 - float3(pixCenterPos)); // using lpfloat for sampleDelta causes precision issues
                float3 sampleDelta1     = (samplePos1 - float3(pixCenterPos)); // using lpfloat for sampleDelta causes precision issues
                lpfloat sampleDist0     = (lpfloat)length( sampleDelta0 );
                lpfloat sampleDist1     = (lpfloat)length( sampleDelta1 );

                // approx lines 23, 24 from the paper, unrolled
                lpfloat3 sampleHorizonVec0 = (lpfloat3)(sampleDelta0 / sampleDist0);
                lpfloat3 sampleHorizonVec1 = (lpfloat3)(sampleDelta1 / sampleDist1);

                // this is our own thickness heuristic that relies on sooner discarding samples behind the center
                lpfloat falloffBase0    = length( lpfloat3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1+thinOccluderCompensation) ) );
                lpfloat falloffBase1    = length( lpfloat3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1+thinOccluderCompensation) ) );
                lpfloat weight0         = saturate( falloffBase0 * falloffMul + falloffAdd );
                lpfloat weight1         = saturate( falloffBase1 * falloffMul + falloffAdd );

                // sample horizon cos
                lpfloat shc0 = (lpfloat)dot(sampleHorizonVec0, viewVec);
                lpfloat shc1 = (lpfloat)dot(sampleHorizonVec1, viewVec);

                // discard unwanted samples
                shc0 = lerp( lowHorizonCos0, shc0, weight0 ); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos0), acos(shc0), weight0 ));
                shc1 = lerp( lowHorizonCos1, shc1, weight1 ); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos1), acos(shc1), weight1 ));

                // thickness heuristic - see "4.3 Implementation details, Height-field assumption considerations"
#if 0   // (disabled, not used) this should match the paper
                lpfloat newhorizonCos0 = max( horizonCos0, shc0 );
                lpfloat newhorizonCos1 = max( horizonCos1, shc1 );
                horizonCos0 = (horizonCos0 > shc0)?( lerp( newhorizonCos0, shc0, thinOccluderCompensation ) ):( newhorizonCos0 );
                horizonCos1 = (horizonCos1 > shc1)?( lerp( newhorizonCos1, shc1, thinOccluderCompensation ) ):( newhorizonCos1 );
#elif 0 // (disabled, not used) this is slightly different from the paper but cheaper and provides very similar results
                horizonCos0 = lerp( max( horizonCos0, shc0 ), shc0, thinOccluderCompensation );
                horizonCos1 = lerp( max( horizonCos1, shc1 ), shc1, thinOccluderCompensation );
#else   // this is a version where thicknessHeuristic is completely disabled
                horizonCos0 = max( horizonCos0, shc0 );
                horizonCos1 = max( horizonCos1, shc1 );
#endif
            }

#if 1       // I can't figure out the slight overdarkening on high slopes, so I'm adding this fudge - in the training set, 0.05 is close (PSNR 21.34) to disabled (PSNR 21.45)
            projectedNormalVecLength = lerp( projectedNormalVecLength, 1, 0.05 );
#endif

            // line ~27, unrolled
            lpfloat h0 = -FastACos((lpfloat)horizonCos1);
            lpfloat h1 = FastACos((lpfloat)horizonCos0);
#if 0       // we can skip clamping for a tiny little bit more performance
            h0 = n + clamp( h0-n, (lpfloat)-SK_PI_HALF, (lpfloat)SK_PI_HALF );
            h1 = n + clamp( h1-n, (lpfloat)-SK_PI_HALF, (lpfloat)SK_PI_HALF );
#endif
            lpfloat iarc0 = ((lpfloat)cosNorm + (lpfloat)2 * (lpfloat)h0 * (lpfloat)sin(n)-(lpfloat)cos((lpfloat)2 * (lpfloat)h0-n))/(lpfloat)4;
            lpfloat iarc1 = ((lpfloat)cosNorm + (lpfloat)2 * (lpfloat)h1 * (lpfloat)sin(n)-(lpfloat)cos((lpfloat)2 * (lpfloat)h1-n))/(lpfloat)4;
            lpfloat localVisibility = (lpfloat)projectedNormalVecLength * (lpfloat)(iarc0+iarc1);
            visibility += localVisibility;

#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
            // see "Algorithm 2 Extension that computes bent normals b."
            lpfloat t0 = (6*sin(h0-n)-sin(3*h0-n)+6*sin(h1-n)-sin(3*h1-n)+16*sin(n)-3*(sin(h0+n)+sin(h1+n)))/12;
            lpfloat t1 = (-cos(3 * h0-n)-cos(3 * h1-n) +8 * cos(n)-3 * (cos(h0+n) +cos(h1+n)))/12;
            lpfloat3 localBentNormal = lpfloat3( directionVec.x * (lpfloat)t0, directionVec.y * (lpfloat)t0, -lpfloat(t1) );
            localBentNormal = (lpfloat3)mul( XeGTAO_RotFromToMatrix( lpfloat3(0,0,-1), viewVec ), localBentNormal ) * projectedNormalVecLength;
            bentNormal += localBentNormal;
#endif
        }
        visibility /= (lpfloat)sliceCount;
        visibility = pow( visibility, (lpfloat)consts.FinalValuePower );
        visibility = max( (lpfloat)0.03, visibility ); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)

#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
        bentNormal = normalize(bentNormal) ;
#endif
    }

    XeGTAO_OutputWorkingTerm( pixCoord, visibility, bentNormal, outWorkingAOTerm );
}

//shader defs
[[vk::image_format("r8u")]] RWTexture2D<uint>               g_outWorkingAOTerm      : register(u0);
[[vk::image_format("r8")]]  RWTexture2D<unorm float>        g_outWorkingEdges       : register(u1);

ConstantBuffer<GTAOConstants>   g_GTAOConsts            : register(b2);
Texture2D<lpfloat>              g_srcWorkingDepth       : register(t3);
Texture2D                       g_srcNormalMap          : register(t4);
SamplerState                    g_samplerPointClamp     : register(s5);

// Engine-specific normal map loader
lpfloat3 LoadNormal( int2 pos )
{
    float3 normal = normalize(mul((float3x3)g_GTAOConsts.View, OctohedralDecode(g_srcNormalMap.Load(int3(pos, 0)).rg)));
    normal.z *= -1.0f;
    return normal;
}

// Engine-specific screen & temporal noise loader
lpfloat2 SpatioTemporalNoise( uint2 pixCoord, uint temporalIndex )    // without TAA, temporalIndex is always 0
{
#ifdef XE_GTAO_HILBERT_LUT_AVAILABLE // load from lookup texture...
    uint index = g_srcHilbertLUT.Load( uint3( pixCoord % 64, 0 ) ).x;
#else // ...or generate in-place?
    uint index = HilbertIndex( pixCoord.x, pixCoord.y );
#endif
    index += 288*(temporalIndex%64); // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)
    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    return lpfloat2( frac( 0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114) ) );
}


// Engine-specific entry point for the second pass
[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOUltra(const uint2 pixCoord : SV_DispatchThreadID )
{
    // g_samplerPointClamp is a sampler with D3D12_FILTER_MIN_MAG_MIP_POINT filter and D3D12_TEXTURE_ADDRESS_MODE_CLAMP addressing mode
    XeGTAO_MainPass(
        pixCoord,
        3,
        3,
        SpatioTemporalNoise( pixCoord, g_GTAOConsts.NoiseIndex ),
        LoadNormal( pixCoord ),
        g_GTAOConsts,
        g_srcWorkingDepth,
        g_samplerPointClamp,
        g_outWorkingAOTerm,
        g_outWorkingEdges
    );
}



