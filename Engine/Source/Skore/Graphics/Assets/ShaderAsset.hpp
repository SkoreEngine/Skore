#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/SharedPtr.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/IO/Asset.hpp"


namespace Skore
{
    enum class ShaderAssetType
    {
        None,
        Include,
        Graphics,
        Compute,
        Raytrace
    };


    struct SK_API ShaderState
    {
        ~ShaderState();

        ShaderAsset* shaderAsset;

        void AddPipelineDependency(PipelineState pipelineState);
        void RemovePipelineDependency(PipelineState pipelineState);
        void AddShaderDependency(ShaderAsset* shaderAsset);
        void AddBindingSetDependency(BindingSet* bindingSet);
        void RemoveBindingSetDependency(BindingSet* bindingSet);


        String                 name;
        ShaderInfo             shaderInfo;
        Array<ShaderStageInfo> stages{};
        u32                    streamSize{};
        u32                    streamOffset{};

        Array<PipelineState>  pipelineDependencies{};
        HashSet<ShaderAsset*> shaderDependencies{};
        HashSet<BindingSet*>  bindingSetDependencies{};

        static void RegisterType(NativeTypeHandler<ShaderState>& type);
    };


    struct SK_API ShaderAsset : Asset
    {
        SK_BASE_TYPES(Asset);

        ShaderAssetType type = ShaderAssetType::None;

        Array<SharedPtr<ShaderState>> states;

        Array<u8> bytes{};

        usize LoadStream(usize offset, usize size, Array<u8>& bytes) const override;

        ShaderState* GetDefaultState();
        ShaderState* GetState(StringView name);

        ShaderState* FindOrCreateState(StringView name);

        static void RegisterType(NativeTypeHandler<ShaderAsset>& type);
    };
}
