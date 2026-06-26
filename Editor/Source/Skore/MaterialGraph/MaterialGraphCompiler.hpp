#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	struct MaterialGraphCompileResult
	{
		bool   success = false;
		String hlsl{};      //the generated HLSL pixel shader
		String log{};       //compiler / codegen diagnostics
		u32    spirvSize = 0;
	};

	struct SK_API MaterialGraphCompiler
	{
		//Generates HLSL from the graph and compiles it to SPIR-V (editor side).
		static MaterialGraphCompileResult Compile(RID graph);

		//Only generates the HLSL source without invoking the shader compiler.
		static String GenerateHlsl(RID graph, String& log);
	};
}
