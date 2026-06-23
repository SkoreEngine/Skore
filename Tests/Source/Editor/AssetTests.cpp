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
			CHECK(pipelineDesc.inputVariables[0].format == Format::RGB32_FLOAT);
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