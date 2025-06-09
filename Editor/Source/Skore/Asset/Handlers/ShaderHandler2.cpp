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

#include "Skore/Asset/AssetFileOld.hpp"
#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"

#include "SDL3/SDL.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Utils/ShaderManager.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::ShaderHandler2");

	enum class ShaderType
	{
		None,
		Include,
		Graphics,
		Compute,
		Raytrace
	};

	struct ShaderConfigStage2
	{
		String        entryPoint;
		ShaderStage   stage;
		Array<String> macros;

		static void RegisterType(NativeReflectType<ShaderConfigStage2>& type)
		{
			type.Field<&ShaderConfigStage2::entryPoint>("entryPoint");
			type.Field<&ShaderConfigStage2::stage>("stage");
			type.Field<&ShaderConfigStage2::macros>("macros");
		}
	};

	struct ShaderConfigVariant2
	{
		String                   name;
		Array<ShaderConfigStage2> stages;

		static void RegisterType(NativeReflectType<ShaderConfigVariant2>& type)
		{
			type.Field<&ShaderConfigVariant2::name>("name");
			type.Field<&ShaderConfigVariant2::stages>("stages");
		}
	};

	struct ShaderConfig2 : Object
	{
		SK_CLASS(ShaderConfig2, Object);

		Array<ShaderConfigVariant2> variants;

		static void RegisterType(NativeReflectType<ShaderConfig2>& type)
		{
			type.Field<&ShaderConfig2::variants>("variants");
		}
	};


	struct ShaderHandler2 : AssetHandler
	{
		SK_CLASS(ShaderHandler2, AssetHandler);

		TypeID GetAssetTypeId() override
		{
			return TypeInfo<ShaderAsset>::ID();
		}

		void LoadInstance(AssetFileOld* assetFile, Asset* asset) override
		{
			ShaderAsset* shaderAsset = asset->SafeCast<ShaderAsset>();

			GraphicsAPI graphicsApi = GraphicsAPI::Vulkan;

			ShaderType shaderType = GetShaderType();
			ShaderConfig2 config;

			String configPath = Path::Join(Path::Parent(assetFile->GetAbsolutePath()), Path::Name(assetFile->GetFileName()) + ".shader");
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

			String source = FileSystem::ReadFileAsString(assetFile->GetAbsolutePath());

			std::string_view str = {source.CStr(), source.Size()};
			bool             hasDefaultGeometry = str.find("MainGS") != std::string_view::npos;
			bool             hasRaygen = str.find("[shader(\"raygeneration\")]") != std::string_view::npos;
			bool             hasMiss = str.find("[shader(\"miss\")]") != std::string_view::npos;
			bool             hasClosestHit = str.find("[shader(\"closesthit\")]") != std::string_view::npos;

			if (config.variants.Empty())
			{
				if (shaderType == ShaderType::Graphics)
				{
					ShaderConfigVariant2 configVariant = ShaderConfigVariant2{
						.name = "Default",
						.stages = {
							ShaderConfigStage2{
								.entryPoint = "MainVS",
								.stage = ShaderStage::Vertex
							},
							ShaderConfigStage2{
								.entryPoint = "MainPS",
								.stage = ShaderStage::Pixel
							},
						}
					};

					if (hasDefaultGeometry)
					{
						configVariant.stages.EmplaceBack(ShaderConfigStage2{
							.entryPoint = "MainGS",
							.stage = ShaderStage::Geometry
						});
					}

					config.variants.EmplaceBack(configVariant);
				}
				else if (shaderType == ShaderType::Compute)
				{
					config.variants.EmplaceBack(ShaderConfigVariant2{
						.name = "Default",
						.stages = {
							ShaderConfigStage2{
								.entryPoint = "MainCS",
								.stage = ShaderStage::Compute
							}
						}
					});
				}
				else if (shaderType == ShaderType::Raytrace)
				{
					ShaderConfigVariant2 shaderConfigVariant = ShaderConfigVariant2{
						.name = "Default"
					};

					if (hasRaygen)
					{
						shaderConfigVariant.stages.EmplaceBack(ShaderConfigStage2{
							.entryPoint = "Main",
							.stage = ShaderStage::RayGen,
							.macros = {"RAY_GENERATION=1"}
						});
					}
					if (hasMiss)
					{
						shaderConfigVariant.stages.EmplaceBack(ShaderConfigStage2{
							.entryPoint = "Main",
							.stage = ShaderStage::Miss,
							.macros = {"RAY_MISS=1"}
						});
					}
					if (hasClosestHit)
					{
						shaderConfigVariant.stages.EmplaceBack(ShaderConfigStage2{
							.entryPoint = "Main",
							.stage = ShaderStage::ClosestHit,
							.macros = {"RAY_CLOSEST_HIT=1"}
						});
					}
					config.variants.EmplaceBack(shaderConfigVariant);
				}
			}

            for (ShaderConfigVariant2& shaderConfigVariant : config.variants)
            {
                Array<u8> bytes{};
                Array<ShaderStageInfo> tempStages{};

                u32 stageOffset = 0;
                for (ShaderConfigStage2& configStage : shaderConfigVariant.stages)
                {
                	ShaderCompileInfo shaderCompileInfo;
                	shaderCompileInfo.entryPoint = configStage.entryPoint;
                	shaderCompileInfo.source = source;
                	shaderCompileInfo.shaderStage = configStage.stage;
                	shaderCompileInfo.macros = configStage.macros;
                	shaderCompileInfo.api = graphicsApi;
                	shaderCompileInfo.userData = assetFile;
                	shaderCompileInfo.getShaderInclude = [](StringView include, void* userData, String& source) -> bool
                	{
                		//TODO - add shader dependencies for hot reloading
                		AssetFileOld* assetFile = static_cast<AssetFileOld*>(userData);
						if (Contains(include, StringView(":/")))
						{
							if (AssetInterface* interface = Assets::GetInterfaceByPath(include))
							{
								source = FileSystem::ReadFileAsString(Assets::GetInterfaceByPath(include)->GetAbsolutePath());
								return true;
							}
						}

                		if (FileSystem::GetFileStatus(Path::Join(Path::Parent(assetFile->GetAbsolutePath()), include)).exists)
                		{
                			source = FileSystem::ReadFileAsString(Path::Join(Path::Parent(assetFile->GetAbsolutePath()), include));
                			return true;
                		}

                		return false;
                	};

	                if (!CompileShader(shaderCompileInfo, bytes))
	                {
		                return;
	                }

                    u32 shaderSize =  static_cast<u32>(bytes.Size()) - stageOffset;

                    tempStages.EmplaceBack(ShaderStageInfo{
                        .stage = configStage.stage,
                        .entryPoint = configStage.entryPoint,
                        .offset = stageOffset,
                        .size = shaderSize
                    });
                    stageOffset += shaderSize;
                }

                ShaderVariant* variant = shaderAsset->FindOrCreateVariant(shaderConfigVariant.name);
                variant->stages = Traits::Move(tempStages);
                variant->spriv = Traits::Move(bytes);
            	GetPipelineLayout(graphicsApi, variant->spriv, variant->stages, variant->pipelineDesc);


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

                logger.Debug("shader {} variant {} created successfully", assetFile->GetPath(), variant->name);
            }
		}

		void OpenAsset(AssetFileOld* assetFile) override
		{
			SDL_OpenURL(assetFile->GetAbsolutePath().CStr());
		}

		String Name() override
		{
			return "Shader";
		}

		Array<String> AssociatedExtensions() override
		{
			return {".shader"};
		}

		virtual ShaderType GetShaderType() = 0;
	};

	struct RasterShaderHandler2 : ShaderHandler2
	{
		SK_CLASS(RasterShaderHandler2, ShaderHandler2);

		String Extension() override
		{
			return ".raster";
		}

		ShaderType GetShaderType() override
		{
			return ShaderType::Graphics;
		}
	};

	struct ComputeShaderHandler2 : ShaderHandler2
	{
		SK_CLASS(ComputeShaderHandler2, ShaderHandler2);

		String Extension() override
		{
			return ".comp";
		}

		ShaderType GetShaderType() override
		{
			return ShaderType::Compute;
		}
	};


	void RegisterShaderHandler2()
	{
		Reflection::Type<ShaderHandler2>();

		Reflection::Type<RasterShaderHandler2>();
		Reflection::Type<ComputeShaderHandler2>();

		Reflection::Type<ShaderConfigStage2>();
		Reflection::Type<ShaderConfigVariant2>();
		Reflection::Type<ShaderConfig2>();
	}
}
