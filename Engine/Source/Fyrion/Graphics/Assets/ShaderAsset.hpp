#pragma once

#include "Fyrion/Common.hpp"
#include "Fyrion/Core/HashMap.hpp"
#include "Fyrion/Core/HashSet.hpp"
#include "Fyrion/Core/SharedPtr.hpp"
#include "Fyrion/Graphics/GraphicsTypes.hpp"
#include "Fyrion/IO/Asset.hpp"


namespace Fyrion
{
    enum class ShaderAssetType
    {
        None,
        Include,
        Graphics,
        Compute,
        Raytrace
    };


    struct FY_API ShaderState
    {
        ~ShaderState();

        ShaderAsset* shaderAsset;

        void AddPipelineDependency(PipelineState pipelineState);
        void AddShaderDependency(ShaderAsset* shaderAsset);
        void AddBindingSetDependency(BindingSet* bindingSet);
        void RemoveBindingSetDependency(BindingSet* bindingSet);

        ShaderInfo             shaderInfo;
        Array<ShaderStageInfo> stages{};
        u32                    streamSize{};
        u32                    streamOffset{};

        Array<PipelineState>  pipelineDependencies{};
        HashSet<ShaderAsset*> shaderDependencies{};
        HashSet<BindingSet*>  bindingSetDependencies{};

        static void RegisterType(NativeTypeHandler<ShaderState>& type);
    };


    struct FY_API ShaderAsset : Asset
    {
        FY_BASE_TYPES(Asset);

        ShaderAssetType type = ShaderAssetType::None;
        ShaderState     shaderState{this};

        Array<u8> bytes{};

        Array<u8> LoadStream(usize offset, usize size) const override;

        ShaderState* GetDefaultState();

        static void RegisterType(NativeTypeHandler<ShaderAsset>& type);
    };
}
