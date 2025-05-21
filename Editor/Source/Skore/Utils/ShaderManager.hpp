// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"

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

	bool CompileShader(const ShaderCompileInfo& shaderCompileInfo, Array<u8>& bytes);
	bool GetPipelineLayout(GraphicsAPI api, Span<u8> bytes, Span<ShaderStageInfo> stages, PipelineDesc& pipelineDesc);
}
