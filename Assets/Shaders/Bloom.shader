shaderFile: Bloom.hlsl
variants:
  - name : BloomPrefilter
    stages:
      - entryPoint: BloomPrefilter
        stage: Compute
  - name : BloomDownsample
    stages:
      - entryPoint: BloomDownsample
        stage: Compute
  - name : BloomUpsample
    stages:
      - entryPoint: BloomUpsample
        stage: Compute
