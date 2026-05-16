#include "Skore/Editor.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Utils/ShaderManager.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::ShaderHandler");

	enum class ShaderAssetType
	{
		None,
		Graphics,
		Compute,
		Raytrace
	};

	struct ShaderConfigStage
	{
		String        entryPoint;
		ShaderStage   stage;
		Array<String> macros;

		static void RegisterType(NativeReflectType<ShaderConfigStage>& type)
		{
			type.Field<&ShaderConfigStage::entryPoint>("entryPoint");
			type.Field<&ShaderConfigStage::stage>("stage");
			type.Field<&ShaderConfigStage::macros>("macros");
		}
	};

	struct ShaderConfigVariant
	{
		String                   name;
		Array<ShaderConfigStage> stages;

		static void RegisterType(NativeReflectType<ShaderConfigVariant>& type)
		{
			type.Field<&ShaderConfigVariant::name>("name");
			type.Field<&ShaderConfigVariant::stages>("stages");
		}
	};

	struct ShaderConfig : Object
	{
		SK_CLASS(ShaderConfig, Object);

		String shaderFile;
		Array<ShaderConfigVariant> variants;
		Array<String> booleanStates;

		static void RegisterType(NativeReflectType<ShaderConfig>& type)
		{
			type.Field<&ShaderConfig::shaderFile>("shaderFile");
			type.Field<&ShaderConfig::variants>("variants");
			type.Field<&ShaderConfig::booleanStates>("booleanStates");
		}
	};


	struct ShaderHandler : ResourceAssetHandler
	{
		SK_CLASS(ShaderHandler, ResourceAssetHandler);

		void OpenAsset(RID asset) override
		{
			Platform::OpenURL(ResourceAssets::GetAbsolutePath(asset).CStr());
		}

		RID Load(RID asset, StringView absolutePath) override
		{
			GraphicsAPI graphicsApi = GraphicsAPI::Vulkan;
			RID shaderResource = Resources::Create<ShaderResource>(UUID::RandomUUID());
			ResourceObject shaderResourceObject = Resources::Write(shaderResource);
			StringView extension = Path::Extension(absolutePath);

			ShaderConfig config;
			String source;

			if (extension == ".shader")
			{
				if (FileSystem::GetFileStatus(absolutePath).exists)
				{
					String str = FileSystem::ReadFileAsString(absolutePath);
					if (!str.Empty())
					{
						String str = FileSystem::ReadFileAsString(absolutePath);
						YamlArchiveReader archiveReader(str);
						config.Deserialize(archiveReader);

						if (!config.shaderFile.Empty())
						{
							source = FileSystem::ReadFileAsString(Path::Join(Path::Parent(absolutePath), config.shaderFile));
						}
					}
				}
			}

			if (source.Empty())
			{
				source = FileSystem::ReadFileAsString(absolutePath);
			}

			std::string_view str = {source.CStr(), source.Size()};
			bool             hasDefaultGeometry = str.find("MainGS") != std::string_view::npos;
			bool             hasRaygen = str.find("[shader(\"raygeneration\")]") != std::string_view::npos;
			bool             hasMiss = str.find("[shader(\"miss\")]") != std::string_view::npos;
			bool             hasClosestHit = str.find("[shader(\"closesthit\")]") != std::string_view::npos;

			if (!config.booleanStates.Empty())
			{
				// Sort boolean states alphabetically for deterministic naming
				Array<String> sortedStates = config.booleanStates;
				for (usize i = 0; i < sortedStates.Size(); i++)
				{
					for (usize j = i + 1; j < sortedStates.Size(); j++)
					{
						if (sortedStates[j] < sortedStates[i])
						{
							String tmp = Traits::Move(sortedStates[i]);
							sortedStates[i] = Traits::Move(sortedStates[j]);
							sortedStates[j] = Traits::Move(tmp);
						}
					}
				}

				u32 stateCount = static_cast<u32>(sortedStates.Size());
				u32 combinationCount = 1u << stateCount;

				if (config.variants.Empty())
				{
					// Auto-detect stages from source when no explicit variants defined
					bool hasVS = str.find("MainVS") != std::string_view::npos;
					bool hasPS = str.find("MainPS") != std::string_view::npos;
					bool hasGS = hasDefaultGeometry;
					bool hasCS = str.find("MainCS") != std::string_view::npos;

					for (u32 mask = 0; mask < combinationCount; mask++)
					{
						Array<String> activeMacros;
						String suffix;

						for (u32 bit = 0; bit < stateCount; bit++)
						{
							if (mask & (1u << bit))
							{
								activeMacros.EmplaceBack(sortedStates[bit] + "=1");
								if (!suffix.Empty()) suffix += "_";
								suffix += sortedStates[bit];
							}
						}

						ShaderConfigVariant variant;
						variant.name = suffix.Empty() ? "Default" : suffix;

						if (hasVS)
						{
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "MainVS",
								.stage = ShaderStage::Vertex,
								.macros = activeMacros
							});
						}
						if (hasPS)
						{
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "MainPS",
								.stage = ShaderStage::Pixel,
								.macros = activeMacros
							});
						}
						if (hasGS)
						{
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "MainGS",
								.stage = ShaderStage::Geometry,
								.macros = activeMacros
							});
						}
						if (hasCS)
						{
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "MainCS",
								.stage = ShaderStage::Compute,
								.macros = activeMacros
							});
						}
						if (hasRaygen)
						{
							Array<String> rtMacros = activeMacros;
							rtMacros.EmplaceBack("RAY_GENERATION=1");
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "Main",
								.stage = ShaderStage::RayGen,
								.macros = rtMacros
							});
						}
						if (hasMiss)
						{
							Array<String> rtMacros = activeMacros;
							rtMacros.EmplaceBack("RAY_MISS=1");
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "Main",
								.stage = ShaderStage::Miss,
								.macros = rtMacros
							});
						}
						if (hasClosestHit)
						{
							Array<String> rtMacros = activeMacros;
							rtMacros.EmplaceBack("RAY_CLOSEST_HIT=1");
							variant.stages.EmplaceBack(ShaderConfigStage{
								.entryPoint = "Main",
								.stage = ShaderStage::ClosestHit,
								.macros = rtMacros
							});
						}

						config.variants.EmplaceBack(variant);
					}
				}
				else
				{
					// Expand each explicit variant by all boolean state combinations
					Array<ShaderConfigVariant> expandedVariants;

					for (const ShaderConfigVariant& baseVariant : config.variants)
					{
						for (u32 mask = 0; mask < combinationCount; mask++)
						{
							Array<String> stateMacros;
							String suffix;

							for (u32 bit = 0; bit < stateCount; bit++)
							{
								if (mask & (1u << bit))
								{
									stateMacros.EmplaceBack(sortedStates[bit] + "=1");
									if (!suffix.Empty()) suffix += "_";
									suffix += sortedStates[bit];
								}
							}

							ShaderConfigVariant expanded;
							expanded.name = suffix.Empty() ? baseVariant.name : baseVariant.name + "_" + suffix;

							for (const ShaderConfigStage& baseStage : baseVariant.stages)
							{
								ShaderConfigStage stage;
								stage.entryPoint = baseStage.entryPoint;
								stage.stage = baseStage.stage;
								stage.macros = baseStage.macros;
								for (const String& m : stateMacros)
								{
									stage.macros.EmplaceBack(m);
								}
								expanded.stages.EmplaceBack(stage);
							}

							expandedVariants.EmplaceBack(expanded);
						}
					}

					config.variants = expandedVariants;
				}
			}

			if (config.variants.Empty())
			{
				ShaderAssetType shaderType = GetShaderAssetType();
				if (shaderType == ShaderAssetType::Graphics)
				{
					ShaderConfigVariant configVariant = ShaderConfigVariant{
						.name = "Default",
						.stages = {
							ShaderConfigStage{
								.entryPoint = "MainVS",
								.stage = ShaderStage::Vertex
							},
							ShaderConfigStage{
								.entryPoint = "MainPS",
								.stage = ShaderStage::Pixel
							},
						}
					};

					if (hasDefaultGeometry)
					{
						configVariant.stages.EmplaceBack(ShaderConfigStage{
							.entryPoint = "MainGS",
							.stage = ShaderStage::Geometry
						});
					}

					config.variants.EmplaceBack(configVariant);
				}
				else if (shaderType == ShaderAssetType::Compute)
				{
					config.variants.EmplaceBack(ShaderConfigVariant{
						.name = "Default",
						.stages = {
							ShaderConfigStage{
								.entryPoint = "MainCS",
								.stage = ShaderStage::Compute
							}
						}
					});
				}
				else if (shaderType == ShaderAssetType::Raytrace)
				{
					ShaderConfigVariant shaderConfigVariant = ShaderConfigVariant{
						.name = "Default"
					};

					if (hasRaygen)
					{
						shaderConfigVariant.stages.EmplaceBack(ShaderConfigStage{
							.entryPoint = "Main",
							.stage = ShaderStage::RayGen,
							.macros = {"RAY_GENERATION=1"}
						});
					}
					if (hasMiss)
					{
						shaderConfigVariant.stages.EmplaceBack(ShaderConfigStage{
							.entryPoint = "Main",
							.stage = ShaderStage::Miss,
							.macros = {"RAY_MISS=1"}
						});
					}
					if (hasClosestHit)
					{
						shaderConfigVariant.stages.EmplaceBack(ShaderConfigStage{
							.entryPoint = "Main",
							.stage = ShaderStage::ClosestHit,
							.macros = {"RAY_CLOSEST_HIT=1"}
						});
					}
					config.variants.EmplaceBack(shaderConfigVariant);
				}
			}

			for (ShaderConfigVariant& shaderConfigVariant : config.variants)
			{
				Array<u8> bytes{};
				Array<ShaderStageInfo> stages{};

				u32 stageOffset = 0;
				for (ShaderConfigStage& configStage : shaderConfigVariant.stages)
				{
					struct ShaderIncludeUserData
					{
						StringView absolutePath;
					};

					ShaderIncludeUserData userData = {
						.absolutePath = absolutePath
					};

					ShaderCompileInfo shaderCompileInfo;
					shaderCompileInfo.entryPoint = configStage.entryPoint;
					shaderCompileInfo.source = source;
					shaderCompileInfo.shaderStage = configStage.stage;
					shaderCompileInfo.macros = configStage.macros;
					shaderCompileInfo.api = graphicsApi;
					shaderCompileInfo.userData = &userData;
					shaderCompileInfo.getShaderInclude = [](StringView include, void* userData, String& source) -> bool
					{
						const ShaderIncludeUserData& shaderIncludeUserData = *static_cast<ShaderIncludeUserData*>(userData);

						//TODO - add shader dependencies for hot reloading
						if (Contains(include, StringView(":/")))
						{
							String absolutePath = ResourceAssets::GetAbsolutePathFromPathId(include);
							source = FileSystem::ReadFileAsString(absolutePath);
							return true;
						}

						if (FileSystem::GetFileStatus(Path::Join(Path::Parent(shaderIncludeUserData.absolutePath), include)).exists)
						{
							source = FileSystem::ReadFileAsString(Path::Join(Path::Parent(shaderIncludeUserData.absolutePath), include));
							return true;
						}

						return false;
					};


					String log = {};
					if (!CompileShader(shaderCompileInfo, bytes, log))
					{
						Platform::ShowSimpleMessageBox(MessageBoxType::Error, "Shader compilation failed", log.CStr());
						std::exit(1);
					}

					u32 shaderSize = static_cast<u32>(bytes.Size()) - stageOffset;

					stages.EmplaceBack(ShaderStageInfo{
						.stage = configStage.stage,
						.entryPoint = configStage.entryPoint,
						.offset = stageOffset,
						.size = shaderSize
					});
					stageOffset += shaderSize;
				}

				PipelineDesc pipelineDesc;
				GetPipelineLayout(graphicsApi, bytes, stages, pipelineDesc);

				RID pipelineDescRID = Resources::Create<PipelineDesc>(UUID::RandomUUID());
				Resources::ToResource(pipelineDescRID, &pipelineDesc, nullptr);

				RID shaderVariant = Resources::Create<ShaderVariantResource>(UUID::RandomUUID());

				ResourceObject shaderVariantObject = Resources::Write(shaderVariant);
				shaderVariantObject.SetString(ShaderVariantResource::Name, shaderConfigVariant.name);
				shaderVariantObject.SetBlob(ShaderVariantResource::Spriv, bytes);
				shaderVariantObject.SetSubObject(ShaderVariantResource::PipelineDesc, pipelineDescRID);

				for (const ShaderStageInfo& stage: stages)
				{
					RID stageRID = Resources::Create<ShaderStageInfo>(UUID::RandomUUID());
					Resources::ToResource(stageRID, &stage, nullptr);
					shaderVariantObject.AddToSubObjectList(ShaderVariantResource::Stages, stageRID);
				}

				shaderVariantObject.Commit();
				shaderResourceObject.AddToSubObjectList(ShaderResource::Variants, shaderVariant);

				logger.Debug("shader {} variant {} created successfully", ResourceAssets::GetPathId(asset), shaderConfigVariant.name);
			}

			shaderResourceObject.Commit();

			return shaderResource;
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<ShaderResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Shader";
		}

		virtual ShaderAssetType GetShaderAssetType() = 0;
	};


	struct RasterShaderHandler : ShaderHandler
	{
		SK_CLASS(RasterShaderHandler, ShaderHandler);

		StringView Extension() override
		{
			return ".raster";
		}

		ShaderAssetType GetShaderAssetType() override
		{
			return ShaderAssetType::Graphics;
		}
	};

	struct ComputeShaderHandler : ShaderHandler
	{
		SK_CLASS(ComputeShaderHandler, ShaderHandler);

		StringView Extension() override
		{
			return ".comp";
		}

		ShaderAssetType GetShaderAssetType() override
		{
			return ShaderAssetType::Compute;
		}
	};

	struct RaytraceShaderHandler : ShaderHandler
	{
		SK_CLASS(RaytraceShaderHandler, ShaderHandler);

		StringView Extension() override
		{
			return ".rt";
		}

		ShaderAssetType GetShaderAssetType() override
		{
			return ShaderAssetType::Raytrace;
		}
	};

	struct ConfigShaderHandler : ShaderHandler
	{
		SK_CLASS(ConfigShaderHandler, ShaderHandler);

		StringView Extension() override
		{
			return ".shader";
		}

		ShaderAssetType GetShaderAssetType() override
		{
			return ShaderAssetType::None;
		}
	};


	void RegisterShaderHandler()
	{
		Reflection::Type<ShaderHandler>();
		Reflection::Type<RasterShaderHandler>();
		Reflection::Type<ComputeShaderHandler>();
		Reflection::Type<RaytraceShaderHandler>();
		Reflection::Type<ConfigShaderHandler>();
		Reflection::Type<ShaderConfigStage>();
		Reflection::Type<ShaderConfigVariant>();
		Reflection::Type<ShaderConfig>();
	}
}
