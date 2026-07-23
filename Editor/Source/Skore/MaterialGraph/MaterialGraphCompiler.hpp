#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	struct MaterialGraphCompileResult
	{
		bool   success = false;
		String hlsl{};      //the full spliced shader (template + generated body)
		String log{};       //compiler / codegen diagnostics
		u32    spirvSize = 0;
	};

	struct SK_API MaterialGraphCompiler
	{
		//Generates the full shader (template + body) and compiles it to SPIR-V for validation (editor side).
		static MaterialGraphCompileResult Compile(RID graph);

		static RID EnsureMaterialVariant(RID shader, RID material, StringView variantName, String& log);

		//Splices the generated body into a caller-provided template (token replacement).
		static String GenerateShader(RID graph, StringView templateText, String& log);

		//Generates only the material node network: the temporaries plus the surface.* output assignments
		//that get injected at the template's // @SK_MATERIAL_GRAPH@ marker.
		static String GenerateBody(RID graph, String& log);

		//Generates the vertex-stage network feeding the World Position Offset output pin, injected at the
		//template's // @SK_MATERIAL_VERTEX_GRAPH@ marker. Empty when the pin is unconnected; hosts without
		//the marker (compute / ray-tracing material shaders) ignore it entirely.
		static String GenerateVertexBody(RID graph, String& log);
	};

	SK_API void RegisterMaterialVariantResolver();
}
