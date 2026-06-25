#pragma once
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"

namespace Skore
{
	typedef bool (*FnGetShaderInclude)(StringView include, void* userData, String& source);

	struct ShaderCompileInfo
	{
		StringView         source;
		StringView         entryPoint;
		ShaderStage        shaderStage;
		GraphicsAPI        api;
		Array<String>      macros;
		void*              userData = nullptr;
		FnGetShaderInclude getShaderInclude = nullptr;
	};

	SK_API void ShaderManagerInit();
	SK_API void ShaderManagerShutdown();

	SK_API bool CompileShader(const ShaderCompileInfo& shaderCompileInfo, Array<u8>& bytes, String& log);
	SK_API bool GetPipelineLayout(GraphicsAPI api, Span<u8> bytes, Span<ShaderStageInfo> stages, PipelineDesc& pipelineDesc);
}
