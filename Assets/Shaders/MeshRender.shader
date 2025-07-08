variants:
  - name : StaticMesh
    stages:
      - entryPoint: MainVS
        stage: Vertex
      - entryPoint: MainPS
        stage: Pixel
  - name : SkinnedMesh
    stages:
      - entryPoint: MainVS
        stage: Vertex
        macros:
          - SK_SKINNED_MESH=1
      - entryPoint: MainPS
        stage: Pixel