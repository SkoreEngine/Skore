shaderFile: DefaultIndirectLighting.hlsl
variants:
  - name : Reflection
    stages:
      - entryPoint: ReflectionCS
        stage: Compute
        macros:
          - SK_REFLECTION_PASS=1
  - name : IrradianceProbeTrace
    stages:
      - entryPoint: IrradianceProbeRayGen
        stage: RayGen
        macros:
          - SK_IRRADIANCE_TRACE=1
      - entryPoint: IrradianceProbeClosestHit
        stage: ClosestHit
        macros:
          - SK_IRRADIANCE_TRACE=1
      - entryPoint: IrradianceProbeMiss
        stage: Miss
        macros:
          - SK_IRRADIANCE_TRACE=1
  - name : IrradianceProbeBlendIrradiance
    stages:
      - entryPoint: IrradianceProbeBlendCS
        stage: Compute
        macros:
          - SK_IRRADIANCE_BLEND=1
          - SK_IRRADIANCE_BLEND_RADIANCE=1
  - name : IrradianceProbeBlendDistance
    stages:
      - entryPoint: IrradianceProbeBlendCS
        stage: Compute
        macros:
          - SK_IRRADIANCE_BLEND=1
          - SK_IRRADIANCE_BLEND_RADIANCE=0
  - name : IrradianceVolumeSample
    stages:
      - entryPoint: IrradianceVolumeSampleCS
        stage: Compute
        macros:
          - SK_IRRADIANCE_SAMPLE=1
  - name : IrradianceProbeDebug
    stages:
      - entryPoint: IrradianceProbeDebugVS
        stage: Vertex
        macros:
          - SK_IRRADIANCE_DEBUG=1
      - entryPoint: IrradianceProbeDebugPS
        stage: Pixel
        macros:
          - SK_IRRADIANCE_DEBUG=1
