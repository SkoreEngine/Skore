shaderFile: DefaultIndirectLighting.hlsl
variants:
  - name : Reflection
    stages:
      - entryPoint: ReflectionCS
        stage: Compute
