shaderFile: TAA.hlsl
variants:
  - name : ResolveTemporal
    stages:
      - entryPoint: ResolveTemporal
        stage: Compute