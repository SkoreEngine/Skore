shaderFile: DepthLinearize_SPD.hlsl
variants:
  - name : Default
    stages:
      - entryPoint: main
        stage: Compute
        macros:
          - SK_IMAGE_FORMAT="r32f"
