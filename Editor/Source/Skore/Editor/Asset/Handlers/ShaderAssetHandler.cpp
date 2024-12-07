#include "ShaderManager.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Editor/Asset/AssetEditor.hpp"
#include "Skore/Editor/Asset/AssetTypes.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::ShaderAssetHandler");
    }

    struct ShaderConfigStage
    {
        String        entryPoint;
        ShaderStage   stage;
        Array<String> macros;

        static void RegisterType(NativeTypeHandler<ShaderConfigStage>& type)
        {
            type.Field<&ShaderConfigStage::entryPoint>("entryPoint");
            type.Field<&ShaderConfigStage::stage>("stage");
            type.Field<&ShaderConfigStage::macros>("macros");
        }
    };

    struct ShaderPermutation
    {
        String name;
        Array<ShaderConfigStage> stages;

        static void RegisterType(NativeTypeHandler<ShaderPermutation>& type)
        {
            type.Field<&ShaderPermutation::name>("name");
            type.Field<&ShaderPermutation::stages>("stages");
        }
    };

    struct ShaderConfig
    {
        Array<ShaderPermutation> permutations;

        static void RegisterType(NativeTypeHandler<ShaderConfig>& type)
        {
            type.Field<&ShaderConfig::permutations>("permutations");
        }
    };

    class ShaderAssetHandler : public AssetHandler
    {
    public:
        SK_BASE_TYPES(AssetHandler);

        TypeID GetAssetTypeID() override
        {
            return GetTypeID<ShaderAsset>();
        }

        void Save(StringView newPath, AssetFile* assetFile) override {}

        virtual ShaderAssetType GetType() = 0;

        //TODO: report shader compilation error.

        void Load(AssetFile* assetFile, TypeHandler* typeHandler, VoidPtr instance) override
        {
            //TODO - Shader Temp Cache

            // String name = assetFile->MakePathName();
            // String shaderPath = Path::Join(AssetEditor::GetTempFolder(), "ShaderCache");
            // if (!FileSystem::GetFileStatus(shaderPath).exists)
            // {
            //     FileSystem::CreateDirectory(shaderPath);
            // }
            //
            // String shaderFile = Path::Join(shaderPath, name, ".bin");
            // String shaderData = Path::Join(shaderPath, name, ".info");
            //
            // FileSystem::GetFileStatus(assetFile->absolutePath);



            ShaderAsset* shaderAsset = static_cast<ShaderAsset*>(instance);
            shaderAsset->type = GetType();
            shaderAsset->bytes.Clear();

            RenderApiType   renderApi = Graphics::GetRenderApi();
            String          source = FileSystem::ReadFileAsString(assetFile->absolutePath);

            ShaderConfig config{};

            String configPath = Path::Join(Path::Parent(assetFile->absolutePath), Path::Name(assetFile->fileName), ".shader");
            if (FileSystem::GetFileStatus(configPath).exists)
            {
                String str = FileSystem::ReadFileAsString(configPath);
                if (!str.Empty())
                {
                    JsonArchiveReader reader(str);
                    Serialization::Deserialize(GetTypeID<ShaderConfig>(), reader, reader.GetRoot(), &config);
                }
            }

            std::string_view str = {source.CStr(), source.Size()};
            bool hasDefaultGeometry = str.find("MainGS") != std::string_view::npos;

            if (config.permutations.Empty())
            {
                if (shaderAsset->type == ShaderAssetType::Graphics)
                {
                    ShaderPermutation shaderPermutation = ShaderPermutation{
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
                        shaderPermutation.stages.EmplaceBack(ShaderConfigStage{
                            .entryPoint = "MainGS",
                            .stage = ShaderStage::Geometry
                        });
                    }

                    config.permutations.EmplaceBack(shaderPermutation);
                }
                else if (shaderAsset->type == ShaderAssetType::Compute)
                {
                    config.permutations.EmplaceBack(ShaderPermutation{
                        .name = "Default",
                        .stages = {
                            ShaderConfigStage{
                                .entryPoint = "MainCS",
                                .stage = ShaderStage::Compute
                            }
                        }
                    });
                }
            }


            u32 permOffset = 0;

            for (ShaderPermutation& shaderPermutation : config.permutations)
            {
                Array<u8> bytes{};
                Array<ShaderStageInfo> tempStages{};

                u32 stageOffset = 0;
                for (ShaderConfigStage& configStage : shaderPermutation.stages)
                {
                    if (!ShaderManager::CompileShader(ShaderCreation{
                                                          .asset = shaderAsset,
                                                          .source = source,
                                                          .entryPoint = configStage.entryPoint,
                                                          .shaderStage = configStage.stage,
                                                          .renderApi = renderApi,
                                                          .macros = configStage.macros
                                                      },
                                                      bytes))
                    {
                        return;
                    }

                    tempStages.EmplaceBack(ShaderStageInfo{
                        .stage = configStage.stage,
                        .entryPoint = configStage.entryPoint,
                        .offset = stageOffset,
                        .size = (u32)bytes.Size() - stageOffset
                    });
                    stageOffset += static_cast<u32>(bytes.Size());
                }

                ShaderState* state = shaderAsset->FindOrCreateState(shaderPermutation.name);

                state->stages = Traits::Move(tempStages);
                state->shaderInfo = ShaderManager::ExtractShaderInfo(bytes, state->stages, renderApi);
                state->streamOffset = permOffset;
                state->streamSize = bytes.Size();

                shaderAsset->bytes.Insert(shaderAsset->bytes.end(), bytes.begin(), bytes.end());

                for (PipelineState pipelineState : state->pipelineDependencies)
                {
                    if (shaderAsset->type == ShaderAssetType::Graphics)
                    {
                        Graphics::CreateGraphicsPipelineState({
                            .shaderState = state,
                            .pipelineState = pipelineState
                        });
                    }
                    else if (shaderAsset->type == ShaderAssetType::Compute)
                    {
                        Graphics::CreateComputePipelineState({
                            .shaderState = state,
                            .pipelineState = pipelineState
                        });
                    }
                }

                for (const auto it : state->shaderDependencies)
                {
                    Assets::Reload(it.first->GetUUID());
                }

                for (const auto it : state->bindingSetDependencies)
                {
                    it.first->Reload();
                }

                permOffset += (u32)bytes.Size();
            }

            logger.Debug("shader {} compiled sucessfully", assetFile->path);
        }

        void OpenAsset(AssetFile* assetFile) override
        {
            Assets::Load<ShaderAsset>(assetFile->uuid);
        }

        Image GenerateThumbnail(AssetFile* assetFile) override
        {
            return Image{};
        }
    };

    class ShaderIncludeHandler : public AssetHandler
    {
    public:
        SK_BASE_TYPES(AssetHandler);

        TypeID GetAssetTypeID() override
        {
            return GetTypeID<ShaderAsset>();
        }

        void Save(StringView newPath, AssetFile* assetFile) override {}
        void Load(AssetFile* assetFile, TypeHandler* typeHandler, VoidPtr instance) override {}
        void OpenAsset(AssetFile* assetFile) override {}

        Image GenerateThumbnail(AssetFile* assetFile) override
        {
            return {};
        }
    };

    class RasterShaderAssetHandler : public ShaderAssetHandler
    {
    public:
        SK_BASE_TYPES(ShaderAssetHandler);

        StringView Extension() override
        {
            return ".raster";
        }

        ShaderAssetType GetType() override
        {
            return ShaderAssetType::Graphics;
        }
    };

    class ComputeShaderAssetHandler : public ShaderAssetHandler
    {
    public:
        SK_BASE_TYPES(ShaderAssetHandler);

        StringView Extension() override
        {
            return ".comp";
        }

        ShaderAssetType GetType() override
        {
            return ShaderAssetType::Compute;
        }
    };

    #define FY_SHADER_INCLUDE(Name, StrExtension)                       \
        class Name##ShaderIncludeHandler : public ShaderIncludeHandler  \
        {                                                               \
        public:                                                         \
            SK_BASE_TYPES(ShaderIncludeHandler);                        \
                                                                        \
            StringView Extension() override                             \
            {                                                           \
                return StrExtension;                                    \
            }                                                           \
        }

    FY_SHADER_INCLUDE(HLSL, ".hlsl");
    FY_SHADER_INCLUDE(Inc, ".inc");
    FY_SHADER_INCLUDE(CHeader, ".h");
    FY_SHADER_INCLUDE(CPPHeader, ".hpp");

    void RegisterShaderAssetHandlers()
    {
        Registry::Type<ShaderAssetHandler>();
        Registry::Type<RasterShaderAssetHandler>();
        Registry::Type<ComputeShaderAssetHandler>();
        Registry::Type<ShaderIncludeHandler>();
        Registry::Type<HLSLShaderIncludeHandler>();
        Registry::Type<IncShaderIncludeHandler>();
        Registry::Type<CHeaderShaderIncludeHandler>();
        Registry::Type<CPPHeaderShaderIncludeHandler>();
        Registry::Type<ShaderConfig>();
        Registry::Type<ShaderPermutation>();
        Registry::Type<ShaderConfigStage>();
    }
}
