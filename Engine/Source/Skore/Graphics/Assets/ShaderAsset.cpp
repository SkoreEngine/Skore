#include "ShaderAsset.hpp"

#include "Skore/Core/Registry.hpp"


namespace Skore
{
    ShaderState::~ShaderState()
    {
        for(auto& it : bindingSetDependencies)
        {
            it.first->RemoveShaderDependency();
        }
    }

    void ShaderState::AddPipelineDependency(PipelineState pipelineState)
    {
        pipelineDependencies.EmplaceBack(pipelineState);
    }

    void ShaderState::AddShaderDependency(ShaderAsset* shaderAsset)
    {
        shaderDependencies.Emplace(shaderAsset);
    }

    void ShaderState::AddBindingSetDependency(BindingSet* bindingSet)
    {
        bindingSetDependencies.Insert(bindingSet);
    }

    void ShaderState::RemoveBindingSetDependency(BindingSet* bindingSet)
    {
        bindingSetDependencies.Erase(bindingSet);
    }

    void ShaderState::RegisterType(NativeTypeHandler<ShaderState>& type)
    {
        type.Field<&ShaderState::name>("name");
        type.Field<&ShaderState::shaderInfo>("shaderInfo");
        type.Field<&ShaderState::stages>("stages");
        type.Field<&ShaderState::streamSize>("streamSize");
        type.Field<&ShaderState::streamOffset>("streamOffset");
    }

    //TODO: temp it should read from the temp ShaderCache folder.
    Array<u8> ShaderAsset::LoadStream(usize offset, usize size) const
    {
        if (bytes.Size() >= offset + size)
        {
            if (size == 0)
            {
                size = bytes.Size();
            }

            Array<u8> ret;
            ret.Resize(size);
            MemCopy(ret.begin(), bytes.begin() + offset, size);
            return ret;
        }
        return Asset::LoadStream(offset, size);
    }

    ShaderState* ShaderAsset::GetDefaultState()
    {
        return FindOrCreateState("Default");
    }

    ShaderState* ShaderAsset::GetState(StringView name)
    {
        for (auto& state : states)
        {
            if (state.name == name)
            {
                state.shaderAsset = this;
                return &state;
            }
        }
        return nullptr;
    }

    ShaderState* ShaderAsset::FindOrCreateState(StringView name)
    {
        if (ShaderState* state = GetState(name))
        {
            return state;
        }
        return &states.EmplaceBack(ShaderState{.shaderAsset = this, .name = name});
    }

    void ShaderAsset::RegisterType(NativeTypeHandler<ShaderAsset>& type)
    {
        type.Field<&ShaderAsset::type>("type");
        type.Field<&ShaderAsset::states>("states");
    }
}
