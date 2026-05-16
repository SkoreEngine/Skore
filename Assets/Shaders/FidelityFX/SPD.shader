shaderFile: SPD.hlsl
variants:
  - name : VariantRGBA
    stages:
      - entryPoint: main
        stage: Compute
        macros:
          - SK_IMAGE_FORMAT="rgba8"
  - name : VariantRGBA32F
    stages:
      - entryPoint: main
        stage: Compute
        macros:
          - SK_IMAGE_FORMAT="rgba32f"
  - name : VariantRGBA16F
    stages:
      - entryPoint: main
        stage: Compute
        macros:
        - SK_IMAGE_FORMAT="rgba16f"