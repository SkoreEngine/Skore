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

#include "SDL3/SDL_misc.h"
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

		Array<ShaderConfigVariant> variants;

		static void RegisterType(NativeReflectType<ShaderConfig>& type)
		{
			type.Field<&ShaderConfig::variants>("variants");
		}
	};


	struct ShaderHandler : ResourceAssetHandler
	{
		SK_CLASS(ShaderHandler, ResourceAssetHandler);

		void OpenAsset(RID asset) override
		{
			SDL_OpenURL(ResourceAssets::GetAbsolutePath(asset).CStr());
		}

		RID Load(StringView absolutePath) override
		{
			GraphicsAPI graphicsApi = GraphicsAPI::Vulkan;

			ShaderAssetType   shaderType = GetShaderAssetType();
			ShaderConfig config;

			String configPath = Path::Join(Path::Parent(absolutePath), Path::Name(absolutePath) + ".shader");
			if (FileSystem::GetFileStatus(configPath).exists)
			{
				String str = FileSystem::ReadFileAsString(configPath);
				if (!str.Empty())
				{
					String            str = FileSystem::ReadFileAsString(configPath);
					YamlArchiveReader archiveReader(str);
					config.Deserialize(archiveReader);
				}
			}

			String source = FileSystem::ReadFileAsString(absolutePath);

			std::string_view str = {source.CStr(), source.Size()};
			bool             hasDefaultGeometry = str.find("MainGS") != std::string_view::npos;
			bool             hasRaygen = str.find("[shader(\"raygeneration\")]") != std::string_view::npos;
			bool             hasMiss = str.find("[shader(\"miss\")]") != std::string_view::npos;
			bool             hasClosestHit = str.find("[shader(\"closesthit\")]") != std::string_view::npos;

			if (config.variants.Empty())
			{
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
				Array<u8>              bytes{};
				Array<ShaderStageInfo> tempStages{};

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
							if (AssetInterface* interface = Assets::GetInterfaceByPath(include))
							{
								source = FileSystem::ReadFileAsString(Assets::GetInterfaceByPath(include)->GetAbsolutePath());
								return true;
							}
						}

						if (FileSystem::GetFileStatus(Path::Join(Path::Parent(shaderIncludeUserData.absolutePath), include)).exists)
						{
							source = FileSystem::ReadFileAsString(Path::Join(Path::Parent(shaderIncludeUserData.absolutePath), include));
							return true;
						}

						return false;
					};

					if (!CompileShader(shaderCompileInfo, bytes))
					{
						return {};
					}

					u32 shaderSize = static_cast<u32>(bytes.Size()) - stageOffset;

					tempStages.EmplaceBack(ShaderStageInfo{
						.stage = configStage.stage,
						.entryPoint = configStage.entryPoint,
						.offset = stageOffset,
						.size = shaderSize
					});
					stageOffset += shaderSize;
				}

				// ShaderVariant* variant = shaderAsset->FindOrCreateVariant(shaderConfigVariant.name);
				// variant->stages = Traits::Move(tempStages);
				// variant->spriv = Traits::Move(bytes);
				// GetPipelineLayout(graphicsApi, variant->spriv, variant->stages, variant->pipelineDesc);


				//TODO - shader hot-reload
				/*
			    for (PipelineState pipelineState : variant->pipelineDependencies)
			    {
			        Graphics::ReloadPipelineState(variant, pipelineState);
			    }

			    for (const auto it : variant->shaderDependencies)
			    {
			        Assets::Reload(it.first->GetUUID());
			    }

			    for (const auto it : variant->bindingSetDependencies)
			    {
			        it.first->Reload();
			    }
			    */

				logger.Debug("shader {} compiled? ", absolutePath);

				//logger.Debug("shader {} variant {} created successfully", assetFile->GetPath(), variant->name);
			}

			return {};
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


	void RegisterShaderHandler()
	{
		Reflection::Type<ShaderHandler>();
		Reflection::Type<RasterShaderHandler>();
		Reflection::Type<ComputeShaderHandler>();
		Reflection::Type<ShaderConfigStage>();
		Reflection::Type<ShaderConfigVariant>();
		Reflection::Type<ShaderConfig>();
	}
}
