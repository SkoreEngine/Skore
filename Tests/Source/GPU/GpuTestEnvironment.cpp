#ifdef SK_GPU_TESTS

#include "GpuTestEnvironment.hpp"

#include <mutex>

#include "doctest.h"

#include "Skore/RegisterTypes.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Utils/ShaderManager.hpp"

namespace Skore
{
	void SK_API ResourceInit();
	void SK_API ResourceShutdown();
}

namespace Skore::GpuTest
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::GpuTest");

		bool initialized = false;
		bool available = false;

		std::mutex    validationMutex;
		Array<String> validationMessages;

		void ClearValidationMessages()
		{
			std::lock_guard lock(validationMutex);
			validationMessages.Clear();
		}

		void EnsureInitialized()
		{
			if (initialized)
			{
				return;
			}
			initialized = true;

			// Register every engine reflection type once for the whole process. RegisterTypes also
			// builds resource types, but those are dropped right away: each ResourceScope re-creates
			// the handful it needs, keeping the global resource database clean between test cases.
			ResourceInit();
			RegisterTypes();
			ResourceShutdown();

			// Validation layers are forced on so tests fail on any Vulkan warning/error.
			if (!Graphics::InitHeadless(true))
			{
				logger.Warn("no Vulkan device available; GPU tests will be skipped");
				return;
			}

			Graphics::SetMessageCallback([](GpuMessageSeverity severity, StringView message)
			{
				if (severity == GpuMessageSeverity::Warning || severity == GpuMessageSeverity::Error)
				{
					std::lock_guard lock(validationMutex);
					validationMessages.EmplaceBack(String(message));
				}
			});

			// Loaded only once a device exists so machines without a GPU don't require the DXC dll.
			ShaderManagerInit();

			available = true;
		}

		void RegisterShaderResourceTypes()
		{
			Resources::Type<ShaderVariantResource>()
				.Field<ShaderVariantResource::Name>(ResourceFieldType::String)
				.Field<ShaderVariantResource::Spriv>(ResourceFieldType::Blob)
				.Field<ShaderVariantResource::PipelineDesc>(ResourceFieldType::SubObject)
				.Field<ShaderVariantResource::Stages>(ResourceFieldType::SubObjectList)
				.Field<ShaderVariantResource::Material>(ResourceFieldType::Reference, TypeInfo<MaterialGraphResource>::ID())
				.Build();

			Resources::Type<ShaderResource>()
				.Field<ShaderResource::Name>(ResourceFieldType::String)
				.Field<ShaderResource::Variants>(ResourceFieldType::SubObjectList)
				.Field<ShaderResource::RayHitGroup>(ResourceFieldType::UInt)
				.Field<ShaderResource::IsMaterial>(ResourceFieldType::Bool)
				.Build();
		}

		// Wraps already-compiled SPIR-V (one blob, one entry per stage) into a single-variant
		// ShaderResource RID, mirroring Editor/.../Handlers/ShaderHandler.cpp.
		RID BuildShaderResource(const Array<u8>& bytes, const Array<ShaderStageInfo>& stages, StringView name)
		{
			PipelineDesc pipelineDesc;
			GetPipelineLayout(GraphicsAPI::Vulkan, bytes, stages, pipelineDesc);

			RID pipelineDescRID = Resources::Create<PipelineDesc>();
			Resources::ToResource(pipelineDescRID, &pipelineDesc, nullptr);

			RID shaderVariant = Resources::Create<ShaderVariantResource>();
			{
				ResourceObject variantObject = Resources::Write(shaderVariant);
				variantObject.SetString(ShaderVariantResource::Name, "Default");
				variantObject.SetBlob(ShaderVariantResource::Spriv, bytes);
				variantObject.SetSubObject(ShaderVariantResource::PipelineDesc, pipelineDescRID);

				for (const ShaderStageInfo& stage : stages)
				{
					RID stageRID = Resources::Create<ShaderStageInfo>();
					Resources::ToResource(stageRID, &stage, nullptr);
					variantObject.AddToSubObjectList(ShaderVariantResource::Stages, stageRID);
				}

				variantObject.Commit();
			}

			RID shaderResource = Resources::Create<ShaderResource>();
			{
				ResourceObject shaderObject = Resources::Write(shaderResource);
				shaderObject.SetString(ShaderResource::Name, name);
				shaderObject.SetUInt(ShaderResource::RayHitGroup, 0);
				shaderObject.AddToSubObjectList(ShaderResource::Variants, shaderVariant);
				shaderObject.Commit();
			}

			return shaderResource;
		}
	}

	bool IsAvailable()
	{
		EnsureInitialized();
		return available;
	}

	Array<String> ValidationMessages()
	{
		std::lock_guard lock(validationMutex);
		return validationMessages;
	}

	ResourceScope::ResourceScope()
	{
		EnsureInitialized();
		ClearValidationMessages();
		ResourceInit();
		RegisterShaderResourceTypes();
	}

	ResourceScope::~ResourceScope()
	{
		// Every GPU test validates automatically: any Vulkan validation warning/error captured while
		// the scope was alive fails the running test.
		Array<String> messages = ValidationMessages();
		for (const String& message : messages)
		{
			MESSAGE("Vulkan validation: ", message.CStr());
		}
		CHECK(messages.Empty());

		ResourceShutdown();
	}

	RID CompileGraphicsShader(StringView absolutePath, StringView vsEntry, StringView psEntry)
	{
		String source = FileSystem::ReadFileAsString(absolutePath);
		if (source.Empty())
		{
			logger.Error("shader file is empty or missing: {}", absolutePath);
			return {};
		}

		struct StageDef
		{
			StringView  entryPoint;
			ShaderStage stage;
		};

		const StageDef stageDefs[] = {
			{vsEntry, ShaderStage::Vertex},
			{psEntry, ShaderStage::Pixel},
		};

		Array<u8>              bytes;
		Array<ShaderStageInfo> stages;
		u32                    stageOffset = 0;

		for (const StageDef& stageDef : stageDefs)
		{
			ShaderCompileInfo compileInfo;
			compileInfo.source = source;
			compileInfo.entryPoint = stageDef.entryPoint;
			compileInfo.shaderStage = stageDef.stage;
			compileInfo.api = GraphicsAPI::Vulkan;

			String log;
			if (!CompileShader(compileInfo, bytes, log))
			{
				logger.Error("failed to compile shader stage '{}': {}", stageDef.entryPoint, log);
				return {};
			}

			u32 stageSize = static_cast<u32>(bytes.Size()) - stageOffset;
			stages.EmplaceBack(ShaderStageInfo{
				.stage = stageDef.stage,
				.entryPoint = String(stageDef.entryPoint),
				.offset = stageOffset,
				.size = stageSize,
			});
			stageOffset += stageSize;
		}

		return BuildShaderResource(bytes, stages, "TestShader");
	}

	RID CompileComputeShader(StringView absolutePath, StringView csEntry)
	{
		String source = FileSystem::ReadFileAsString(absolutePath);
		if (source.Empty())
		{
			logger.Error("shader file is empty or missing: {}", absolutePath);
			return {};
		}

		ShaderCompileInfo compileInfo;
		compileInfo.source = source;
		compileInfo.entryPoint = csEntry;
		compileInfo.shaderStage = ShaderStage::Compute;
		compileInfo.api = GraphicsAPI::Vulkan;

		Array<u8> bytes;
		String    log;
		if (!CompileShader(compileInfo, bytes, log))
		{
			logger.Error("failed to compile compute shader '{}': {}", csEntry, log);
			return {};
		}

		Array<ShaderStageInfo> stages;
		stages.EmplaceBack(ShaderStageInfo{
			.stage = ShaderStage::Compute,
			.entryPoint = String(csEntry),
			.offset = 0,
			.size = static_cast<u32>(bytes.Size()),
		});

		return BuildShaderResource(bytes, stages, "TestComputeShader");
	}
}

#endif
