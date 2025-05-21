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

#if 0

#include "doctest.h"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

#include "Skore/Utils/ShaderManager.hpp"

using namespace Skore;

namespace Skore
{
	void ShaderManagerInit();
	void ShaderManagerShutdown();

}

namespace
{
	TEST_CASE("Assets::ShaderSPRIVTest")
	{
		ShaderManagerInit();
		{
			#ifndef SK_EDITOR_TEST_FILES
			REQUIRE(false);
			#endif

			String shaderSource = FileSystem::ReadFileAsString(Path::Join(SK_EDITOR_TEST_FILES, "ShaderTest.hlsl"));
			REQUIRE(!shaderSource.Empty());

			ShaderCompileInfo shaderCompileInfo;
			shaderCompileInfo.source = shaderSource;
			shaderCompileInfo.api = GraphicsAPI::Vulkan;

			Array<ShaderStageInfo> stages;
			Array<u8> bytes;
			{
				shaderCompileInfo.shaderStage = ShaderStage::Vertex;
				shaderCompileInfo.entryPoint = "MainVS";

				CHECK(CompileShader(shaderCompileInfo, bytes));
				CHECK(!bytes.Empty());

				stages.EmplaceBack(ShaderStageInfo{
					.stage = ShaderStage::Vertex,
					.entryPoint = "MainVS",
					.offset = 0,
					.size = static_cast<u32>(bytes.Size()),
				});
			}

			{
				shaderCompileInfo.shaderStage = ShaderStage::Pixel;
				shaderCompileInfo.entryPoint = "MainPS";

				usize s = bytes.Size();
				CHECK(CompileShader(shaderCompileInfo, bytes));
				CHECK(s < bytes.Size());

				stages.EmplaceBack(ShaderStageInfo{
					.stage = ShaderStage::Pixel,
					.entryPoint = "MainPS",
					.offset = static_cast<u32>(s),
					.size = static_cast<u32>(bytes.Size()) - static_cast<u32>(s),
				});
			}


			PipelineDesc pipelineDesc;
			GetPipelineLayout(GraphicsAPI::Vulkan, bytes, stages, pipelineDesc);

			CHECK(pipelineDesc.inputVariables.Size() == 3);
			CHECK(pipelineDesc.inputVariables[0].format == TextureFormat::R32G32B32_FLOAT);
			CHECK(pipelineDesc.inputVariables[0].offset == 0);
			CHECK(pipelineDesc.descriptors.Size() == 2);
			CHECK(pipelineDesc.descriptors[0].bindings.Size() == 1);
			CHECK(pipelineDesc.descriptors[0].bindings[0].name == "CameraBuffer");
			CHECK(pipelineDesc.descriptors[0].bindings[0].descriptorType == DescriptorType::UniformBuffer);


			CHECK(pipelineDesc.descriptors[1].bindings.Size() == 2);
			CHECK(pipelineDesc.descriptors[1].bindings[0].name == "diffuseTexture");
			CHECK(pipelineDesc.descriptors[1].bindings[0].descriptorType == DescriptorType::SampledImage);

			CHECK(pipelineDesc.descriptors[1].bindings.Size() == 2);
			CHECK(pipelineDesc.descriptors[1].bindings[1].name == "textureSampler");
			CHECK(pipelineDesc.descriptors[1].bindings[1].descriptorType == DescriptorType::Sampler);

		}
		ShaderManagerShutdown();
	}
}

#endif