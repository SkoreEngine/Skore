#include "ShaderAsset.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Registry.hpp"

#include <algorithm>


namespace Skore
{
    Logger& logger = Logger::GetLogger("Skore::ShaderState");

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

    void ShaderState::RemovePipelineDependency(PipelineState pipelineState)
    {
        pipelineDependencies.Erase(std::find(pipelineDependencies.begin(), pipelineDependencies.end(), pipelineState), pipelineDependencies.end());
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
    usize ShaderAsset::LoadStream(usize offset, usize size, Array<u8>& ret) const
    {
        if (!bytes.Empty())
        {
            if (size == 0)
            {
                size = bytes.Size();
            }
            if (ret.Size() < size)
            {
                ret.Resize(size);
            }
            MemCopy(ret.begin(), bytes.begin() + offset, size);
            return bytes.Size();
        }
        return Asset::LoadStream(offset, size, ret);
    }

    ShaderState* ShaderAsset::GetDefaultState()
    {
        return FindOrCreateState("Default");
    }

    ShaderState* ShaderAsset::GetState(StringView name)
    {
        for (auto& state : states)
        {
            if (state->name == name)
            {
                state->shaderAsset = this;
                return state.Get();
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

        logger.Debug("shader state {} created for shader {}", name, GetName());
        return states.EmplaceBack(MakeShared<ShaderState>(ShaderState{.shaderAsset = this, .name = name})).Get();
    }

    void ShaderAsset::RegisterType(NativeTypeHandler<ShaderAsset>& type)
    {
        type.Field<&ShaderAsset::type>("type");
        type.Field<&ShaderAsset::states>("states");
    }
}
