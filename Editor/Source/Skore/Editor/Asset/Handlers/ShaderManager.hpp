#pragma once


#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"

namespace Skore
{
    struct ShaderCreation
    {
        ShaderAsset*  asset{};
        StringView    source{};
        StringView    entryPoint{};
        ShaderStage   shaderStage{};
        RenderApiType renderApi{};
        Array<String> macros;
    };
}

namespace Skore::ShaderManager
{
    SK_API bool       CompileShader(const ShaderCreation& shaderCreation, Array<u8>& bytes);
    SK_API ShaderInfo ExtractShaderInfo(const Span<u8>& bytes, const Span<ShaderStageInfo>& stages, RenderApiType renderApi);
}
