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

#include "ShaderManager.hpp"

#if defined(SK_WIN)
#include "Windows.h"
#else
#include "dxc/WinAdapter.h"
#endif

#include "dxc/dxcapi.h"
#include "Skore/Core/Logger.hpp"

#define SHADER_MODEL "6_8"

#define COMPUTE_SHADER_MODEL            L"cs_" SHADER_MODEL
#define DOMAIN_SHADER_MODEL             L"ds_" SHADER_MODEL
#define GEOMETRY_SHADER_MODEL           L"gs_" SHADER_MODEL
#define HULL_SHADER_MODEL               L"hs_" SHADER_MODEL
#define PIXEL_SHADER_MODELS             L"ps_" SHADER_MODEL
#define VERTEX_SHADER_MODEL             L"vs_" SHADER_MODEL
#define MESH_SHADER_MODEL               L"ms_" SHADER_MODEL
#define AMPLIFICATION_SHADER_MODEL      L"as_" SHADER_MODEL
#define LIB_SHADER_MODEL                L"lib_" SHADER_MODEL

#include "SDL3/SDL.h"

#if defined(SK_WIN)
#define DXC_LIBRARY "dxcompiler.dll"
#endif

#include <algorithm>
#include <iostream>

#include "spirv_reflect.h"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/IO/Path.hpp"


namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::ShaderManager", LogLevel::Debug);

		SDL_SharedObject*     library;
		IDxcUtils*            utils;
		IDxcCompiler3*        compiler;
		DxcCreateInstanceProc dxcCreateInstance;

		constexpr auto GetShaderStage(ShaderStage shader)
		{
			switch (shader)
			{
				case ShaderStage::Vertex: return VERTEX_SHADER_MODEL;
				case ShaderStage::Hull: return HULL_SHADER_MODEL;
				case ShaderStage::Domain: return DOMAIN_SHADER_MODEL;
				case ShaderStage::Geometry: return GEOMETRY_SHADER_MODEL;
				case ShaderStage::Pixel: return PIXEL_SHADER_MODELS;
				case ShaderStage::Compute: return COMPUTE_SHADER_MODEL;
				case ShaderStage::Amplification: return AMPLIFICATION_SHADER_MODEL;
				case ShaderStage::Mesh: return MESH_SHADER_MODEL;
				case ShaderStage::RayGen:
				case ShaderStage::Intersection:
				case ShaderStage::AnyHit:
				case ShaderStage::ClosestHit:
				case ShaderStage::Miss:
				case ShaderStage::Callable:
				case ShaderStage::All:
					return LIB_SHADER_MODEL;
				default:
					break;
			}
			SK_ASSERT(false, "[ShaderCompiler] shader stage not found");
			return L"";
		}

		std::wstring ToWString(String str)
		{
			return std::wstring{str.begin(), str.end()};
		}
	}


	class IncludeHandler : public IDxcIncludeHandler
	{
	public:
		void*              userData = nullptr;
		FnGetShaderInclude getShaderInclude = nullptr;

		IncludeHandler(void* userData, FnGetShaderInclude getShaderInclude) : userData(userData), getShaderInclude(getShaderInclude) {}

		static String FormatFilePath(LPCWSTR pFilename)
		{
			std::wstring widePath(pFilename);
			std::string  fileName{};
			std::transform(widePath.begin(), widePath.end(), std::back_inserter(fileName), [](wchar_t c)
			{
				return (char)c;
			});

			if (const auto c = fileName.find("./"); c != std::string::npos)
			{
				fileName.replace(c, sizeof("./") - 1, "");
			}

			if (const auto c = fileName.find(".\\"); c != std::string::npos)
			{
				fileName.replace(c, sizeof(".\\") - 1, "");
			}

			auto c = fileName.find('\\');
			while (c != std::string::npos)
			{
				fileName.replace(c, sizeof("\\") - 1, "/");
				c = fileName.find('\\');
			}

			if (c = fileName.find(":/"); c != std::string::npos)
			{
				fileName.replace(c, sizeof(":/") - 1, "://");
			}

			return {fileName.c_str(), fileName.size()};
		}

		HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override
		{
			String fileName = FormatFilePath(pFilename);

			if (getShaderInclude)
			{
				String source;
				if (getShaderInclude(fileName, userData, source))
				{
					IDxcBlobEncoding* pSource = {};
					utils->CreateBlob(source.CStr(), source.Size(), CP_UTF8, &pSource);
					*ppIncludeSource = pSource;
					return S_OK;
				}
			}
			return S_FALSE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return 0;
		}

		ULONG STDMETHODCALLTYPE  Release() override
		{
			return 0;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) override
		{
			return E_NOINTERFACE;
		}
	};


	void ShaderManagerInit()
	{
		String path = Path::Join(SDL_GetBasePath(), DXC_LIBRARY);
		library = SDL_LoadObject(path.CStr());
		SK_ASSERT(library, "Failed to load DxcCompiler");
		if (library)
		{
			dxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(SDL_LoadFunction(library, "DxcCreateInstance"));
			dxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
			dxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		}
	}

	void ShaderManagerShutdown()
	{
		if (library)
		{
			SDL_UnloadObject(library);
		}
	}


	bool CompileShader(const ShaderCompileInfo& shaderCompileInfo, Array<u8>& bytes)
	{
		bool shaderCompilerValid = utils && compiler;
		if (!shaderCompilerValid)
		{
			logger.Error("DxShaderCompiler not loaded");
			return false;
		}


		IDxcBlobEncoding* pSource = {};
		utils->CreateBlob(shaderCompileInfo.source.CStr(), shaderCompileInfo.source.Size(), CP_UTF8, &pSource);

		DxcBuffer source{};
		source.Ptr = pSource->GetBufferPointer();
		source.Size = pSource->GetBufferSize();
		source.Encoding = DXC_CP_ACP;

		std::wstring entryPoint = ToWString(shaderCompileInfo.entryPoint);

		Array<LPCWSTR> args;
		args.EmplaceBack(L"-E");
		args.EmplaceBack(entryPoint.c_str());
		args.EmplaceBack(L"-Wno-ignored-attributes");

		args.EmplaceBack(L"-T");
		args.EmplaceBack(GetShaderStage(shaderCompileInfo.shaderStage));

		if (shaderCompileInfo.api != GraphicsAPI::D3D12)
		{
			args.EmplaceBack(L"-spirv");
			args.EmplaceBack(L"-fspv-target-env=vulkan1.2");
			args.EmplaceBack(L"-fvk-use-dx-layout");
			args.EmplaceBack(L"-fvk-use-dx-position-w");
		}

		args.EmplaceBack(L"-disable-payload-qualifiers");

		Array<std::wstring> macrosCache;
		macrosCache.Reserve(shaderCompileInfo.macros.Size());
		for (const String& macro : shaderCompileInfo.macros)
		{
			args.EmplaceBack(macrosCache.EmplaceBack(std::wstring().append(L"-D").append(ToWString(macro))).c_str());
		}

		IDxcResult* pResults{};

		IncludeHandler includeHeader{shaderCompileInfo.userData, shaderCompileInfo.getShaderInclude};
		compiler->Compile(&source, args.Data(), args.Size(), &includeHeader, IID_PPV_ARGS(&pResults));

		IDxcBlobUtf8* pErrors = {};
		pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		if (pErrors != nullptr && pErrors->GetStringLength() != 0)
		{
			logger.Error("{} ", pErrors->GetStringPointer());
		}

		HRESULT hrStatus;
		pResults->GetStatus(&hrStatus);
		if (FAILED(hrStatus))
		{
			logger.Error("Compilation Failed");
		}

		IDxcBlob*     pShader = nullptr;
		IDxcBlobWide* pShaderName = nullptr;
		pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);

		if (pShader->GetBufferSize() == 0)
		{
			return false;
		}

		usize offset = bytes.Size();
		bytes.Resize(bytes.Size() + pShader->GetBufferSize());
		auto buffer = static_cast<u8*>(pShader->GetBufferPointer());

		for (usize i = 0; i < pShader->GetBufferSize(); ++i)
		{
			bytes[offset + i] = buffer[i];
		}

		pResults->Release();
		pShader->Release();
		if (pShaderName)
		{
			pShaderName->Release();
		}
		pSource->Release();

		return true;
	}

	namespace SpirvUtils
	{
		TextureFormat CastFormat(SpvReflectFormat format)
		{
			switch (format)
			{
				case SPV_REFLECT_FORMAT_UNDEFINED:
					return TextureFormat::Unknown;

				// R16 formats
				case SPV_REFLECT_FORMAT_R16_UINT: return TextureFormat::R16_UINT;
				case SPV_REFLECT_FORMAT_R16_SINT: return TextureFormat::R16_SINT;
				case SPV_REFLECT_FORMAT_R16_SFLOAT: return TextureFormat::R16_FLOAT;

				// R16G16 formats
				case SPV_REFLECT_FORMAT_R16G16_UINT: return TextureFormat::R16G16_UINT;
				case SPV_REFLECT_FORMAT_R16G16_SINT: return TextureFormat::R16G16_SINT;
				case SPV_REFLECT_FORMAT_R16G16_SFLOAT: return TextureFormat::R16G16_FLOAT;

				// R16G16B16 formats - now with proper mapping
				case SPV_REFLECT_FORMAT_R16G16B16_UINT:
					return TextureFormat::R16G16B16_UINT;
				case SPV_REFLECT_FORMAT_R16G16B16_SINT:
					return TextureFormat::R16G16B16_SINT;
				case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT:
					return TextureFormat::R16G16B16_FLOAT;

				// R16G16B16A16 formats
				case SPV_REFLECT_FORMAT_R16G16B16A16_UINT:
					return TextureFormat::R16G16B16A16_UINT;
				case SPV_REFLECT_FORMAT_R16G16B16A16_SINT:
					return TextureFormat::R16G16B16A16_SINT;
				case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT:
					return TextureFormat::R16G16B16A16_FLOAT;

				// R32 formats
				case SPV_REFLECT_FORMAT_R32_UINT:
					return TextureFormat::R32_UINT;
				case SPV_REFLECT_FORMAT_R32_SINT:
					return TextureFormat::R32_SINT;
				case SPV_REFLECT_FORMAT_R32_SFLOAT:
					return TextureFormat::R32_FLOAT;

				// R32G32 formats
				case SPV_REFLECT_FORMAT_R32G32_UINT:
					return TextureFormat::R32G32_UINT;
				case SPV_REFLECT_FORMAT_R32G32_SINT:
					return TextureFormat::R32G32_SINT;
				case SPV_REFLECT_FORMAT_R32G32_SFLOAT:
					return TextureFormat::R32G32_FLOAT;

				// R32G32B32 formats
				case SPV_REFLECT_FORMAT_R32G32B32_UINT:
					return TextureFormat::R32G32B32_UINT;
				case SPV_REFLECT_FORMAT_R32G32B32_SINT:
					return TextureFormat::R32G32B32_SINT;
				case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:
					return TextureFormat::R32G32B32_FLOAT;

				// R32G32B32A32 formats
				case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:
					return TextureFormat::R32G32B32A32_UINT;
				case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:
					return TextureFormat::R32G32B32A32_SINT;
				case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT:
					return TextureFormat::R32G32B32A32_FLOAT;

				// R64 formats - no direct mapping in TextureFormat
				case SPV_REFLECT_FORMAT_R64_UINT:
				case SPV_REFLECT_FORMAT_R64_SINT:
				case SPV_REFLECT_FORMAT_R64_SFLOAT:
				case SPV_REFLECT_FORMAT_R64G64_UINT:
				case SPV_REFLECT_FORMAT_R64G64_SINT:
				case SPV_REFLECT_FORMAT_R64G64_SFLOAT:
				case SPV_REFLECT_FORMAT_R64G64B64_UINT:
				case SPV_REFLECT_FORMAT_R64G64B64_SINT:
				case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT:
				case SPV_REFLECT_FORMAT_R64G64B64A64_UINT:
				case SPV_REFLECT_FORMAT_R64G64B64A64_SINT:
				case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT:
					return TextureFormat::Unknown;

				default:
					return TextureFormat::Unknown;
			}
		}

		RenderType CastRenderType(const SpvOp& spvOp)
		{
			switch (spvOp)
			{
				case SpvOpTypeVoid: return RenderType::Void;
				case SpvOpTypeBool: return RenderType::Bool;
				case SpvOpTypeInt: return RenderType::Int;
				case SpvOpTypeFloat: return RenderType::Float;
				case SpvOpTypeVector: return RenderType::Vector;
				case SpvOpTypeMatrix: return RenderType::Matrix;
				case SpvOpTypeImage: return RenderType::Image;
				case SpvOpTypeSampler: return RenderType::Sampler;
				case SpvOpTypeSampledImage: return RenderType::SampledImage;
				case SpvOpTypeArray: return RenderType::Array;
				case SpvOpTypeRuntimeArray: return RenderType::RuntimeArray;
				case SpvOpTypeStruct: return RenderType::Struct;
				default: return RenderType::None;
			}
		}

		u32 GetAttributeSize(SpvReflectFormat reflectFormat)
		{
			switch (reflectFormat)
			{
				case SPV_REFLECT_FORMAT_UNDEFINED: return 0;
				case SPV_REFLECT_FORMAT_R32_UINT: return sizeof(u32);
				case SPV_REFLECT_FORMAT_R32_SINT: return sizeof(i32);
				case SPV_REFLECT_FORMAT_R32_SFLOAT: return sizeof(f32);
				case SPV_REFLECT_FORMAT_R32G32_UINT: return sizeof(u32) * 2;
				case SPV_REFLECT_FORMAT_R32G32_SINT: return sizeof(i32) * 2;
				case SPV_REFLECT_FORMAT_R32G32_SFLOAT: return sizeof(f32) * 2;
				case SPV_REFLECT_FORMAT_R32G32B32_UINT: return sizeof(u32) * 3;
				case SPV_REFLECT_FORMAT_R32G32B32_SINT: return sizeof(i32) * 3;
				case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT: return sizeof(f32) * 3;
				case SPV_REFLECT_FORMAT_R32G32B32A32_UINT: return sizeof(u32) * 4;
				case SPV_REFLECT_FORMAT_R32G32B32A32_SINT: return sizeof(i32) * 4;
				case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT: return sizeof(f32) * 4;
				case SPV_REFLECT_FORMAT_R16_UINT: break;
				case SPV_REFLECT_FORMAT_R16_SINT: break;
				case SPV_REFLECT_FORMAT_R16_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R16G16_UINT: break;
				case SPV_REFLECT_FORMAT_R16G16_SINT: break;
				case SPV_REFLECT_FORMAT_R16G16_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R16G16B16_UINT: break;
				case SPV_REFLECT_FORMAT_R16G16B16_SINT: break;
				case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R16G16B16A16_UINT: break;
				case SPV_REFLECT_FORMAT_R16G16B16A16_SINT: break;
				case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R64_UINT: break;
				case SPV_REFLECT_FORMAT_R64_SINT: break;
				case SPV_REFLECT_FORMAT_R64_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R64G64_UINT: break;
				case SPV_REFLECT_FORMAT_R64G64_SINT: break;
				case SPV_REFLECT_FORMAT_R64G64_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R64G64B64_UINT: break;
				case SPV_REFLECT_FORMAT_R64G64B64_SINT: break;
				case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT: break;
				case SPV_REFLECT_FORMAT_R64G64B64A64_UINT: break;
				case SPV_REFLECT_FORMAT_R64G64B64A64_SINT: break;
				case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT: break;
			}
			logger.FatalError("GetAttributeSize format not found");
			return 0;
		}

		DescriptorType GetDescriptorType(SpvReflectDescriptorType descriptorType)
		{
			switch (descriptorType)
			{
				case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return DescriptorType::Sampler;
				case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return DescriptorType::SampledImage;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return DescriptorType::StorageImage;
				case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return DescriptorType::UniformBuffer;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: return DescriptorType::StorageBuffer;
				case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return DescriptorType::AccelerationStructure;
				case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: break;
			}
			logger.FatalError("GetDescriptorType DescriptorType not found");
			return {};
		}

		TextureViewType DimToViewType(SpvDim dim, u32 arrayed)
		{
			if (arrayed)
			{
				switch (dim)
				{
					case SpvDim1D: return TextureViewType::Type1DArray;
					case SpvDim2D: return TextureViewType::Type2DArray;
					case SpvDim3D: return TextureViewType::Type3D;
					case SpvDimCube: return TextureViewType::TypeCubeArray;
					case SpvDimRect: break;
					case SpvDimBuffer: break;
					case SpvDimSubpassData: break;
					case SpvDimMax: break;
					case SpvDimTileImageDataEXT: break;
				}
			}
			else
			{
				switch (dim)
				{
					case SpvDim1D: return TextureViewType::Type1D;
					case SpvDim2D: return TextureViewType::Type2D;
					case SpvDim3D: return TextureViewType::Type3D;
					case SpvDimCube: return TextureViewType::TypeCube;
					case SpvDimRect: break;
					case SpvDimBuffer: break;
					case SpvDimSubpassData: break;
					case SpvDimMax: break;
					case SpvDimTileImageDataEXT: break;
				}
			}

			logger.FatalError("DimToViewType SpvDim not found");

			return TextureViewType::Undefined;
		}

		u32 CalcTypeSize(SpvReflectTypeDescription* reflectTypeDescription)
		{
			switch (reflectTypeDescription->op)
			{
				case SpvOpTypeInt: return sizeof(i32);
				case SpvOpTypeFloat: return sizeof(f32);
				case SpvOpTypeVector: return reflectTypeDescription->traits.numeric.vector.component_count * sizeof(f32);
				case SpvOpTypeMatrix: return reflectTypeDescription->traits.numeric.matrix.row_count * reflectTypeDescription->traits.numeric.matrix.stride;
				case SpvOpTypeArray:
				{
					u32 size{};
					for (int d = 0; d < reflectTypeDescription->traits.array.dims_count; ++d)
					{
						size += reflectTypeDescription->traits.array.dims[d] * reflectTypeDescription->traits.numeric.scalar.width;
					}
					return size;
				}
				case SpvOpTypeRuntimeArray: return 0; //runtime arrays has size?
				case SpvOpTypeStruct: return 0;       //ignore, size will be calculated using the fields
			}

			logger.FatalError("CalcTypeSize type not found");

			return 0;
		}


		void SortAndAddDescriptors(PipelineDesc& shaderInfo, const HashMap<u32, HashMap<u32, DescriptorSetLayoutBinding>>& descriptors)
		{
			//sorting descriptors
			Array<u32> sortDescriptors{};
			sortDescriptors.Reserve(descriptors.Size());
			for (auto& descriptorIt : descriptors)
			{
				sortDescriptors.EmplaceBack(descriptorIt.first);
			}
			std::ranges::sort(sortDescriptors);

			for (auto& set : sortDescriptors)
			{
				const HashMap<u32, DescriptorSetLayoutBinding>& bindings = descriptors.Find(set)->second;

				DescriptorSetLayout descriptorLayout = DescriptorSetLayout{set};

				Array<u32> sortBindings{};
				sortBindings.Reserve(bindings.Size());
				for (auto& bindingIt : bindings)
				{
					sortBindings.EmplaceBack(bindingIt.first);
				}
				std::ranges::sort(sortBindings);

				for (auto& binding : sortBindings)
				{
					descriptorLayout.bindings.EmplaceBack(bindings.Find(binding)->second);
				}
				shaderInfo.descriptors.EmplaceBack(descriptorLayout);
			}
		}
	}

	bool GetPipelineLayout(GraphicsAPI api, Span<u8> bytes, Span<ShaderStageInfo> stages, PipelineDesc& pipelineLayout)
	{
		if (api != GraphicsAPI::D3D12)
		{
			u32                                                    varCount = 0;
			HashMap<u32, HashMap<u32, DescriptorSetLayoutBinding>> descriptors{};

			for (const ShaderStageInfo& stageInfo : stages)
			{
				Array<u32> data;
				data.Resize(stageInfo.size);
				MemCopy(data.Data(), bytes.Data() + stageInfo.offset, stageInfo.size);

				SpvReflectShaderModule module{};
				spvReflectCreateShaderModule(data.Size(), data.Data(), &module);

				if (stageInfo.stage == ShaderStage::Vertex)
				{
					spvReflectEnumerateInputVariables(&module, &varCount, nullptr);
					Array<SpvReflectInterfaceVariable*> inputVariables;
					inputVariables.Resize(varCount);
					spvReflectEnumerateInputVariables(&module, &varCount, inputVariables.Data());

					u32 offset = 0;
					for (SpvReflectInterfaceVariable* variable : inputVariables)
					{
						if (variable->location < U32_MAX)
						{
							InterfaceVariable interfaceVariable{
								.location = variable->location,
								.offset = offset,
								.name = variable->name,
								.format = SpirvUtils::CastFormat(variable->format),
								.size = SpirvUtils::GetAttributeSize(variable->format),
							};


							offset += interfaceVariable.size;
							pipelineLayout.inputVariables.EmplaceBack(interfaceVariable);
						}
					}
					pipelineLayout.stride = offset;
				}
				else if (stageInfo.stage == ShaderStage::Pixel)
				{
					Array<SpvReflectInterfaceVariable*> outputVariables;
					spvReflectEnumerateOutputVariables(&module, &varCount, nullptr);
					outputVariables.Resize(varCount);
					spvReflectEnumerateOutputVariables(&module, &varCount, outputVariables.Data());

					for (SpvReflectInterfaceVariable* variable : outputVariables)
					{
						InterfaceVariable interfaceVariable{
							.location = variable->location,
							.format = SpirvUtils::CastFormat(variable->format),
							.size = SpirvUtils::GetAttributeSize(variable->format),
						};
						pipelineLayout.outputVariables.EmplaceBack(interfaceVariable);
					}
				}

				spvReflectEnumeratePushConstantBlocks(&module, &varCount, nullptr);
				Array<SpvReflectBlockVariable*> blockVariables;
				blockVariables.Resize(varCount);
				spvReflectEnumeratePushConstantBlocks(&module, &varCount, blockVariables.Data());

				for (auto block : blockVariables)
				{
					pipelineLayout.pushConstants.EmplaceBack(PushConstantRange{
						.name = block->name,
						.offset = block->offset,
						.size = block->size,
						.stages = stageInfo.stage
					});
				}

				u32 varCount{0};
				spvReflectEnumerateDescriptorSets(&module, &varCount, nullptr);
				Array<SpvReflectDescriptorSet*> descriptorSets{};
				descriptorSets.Resize(varCount);
				spvReflectEnumerateDescriptorSets(&module, &varCount, descriptorSets.Data());

				spvReflectEnumerateDescriptorBindings(&module, &varCount, nullptr);
				Array<SpvReflectDescriptorBinding*> descriptorBinds{};
				descriptorBinds.Resize(varCount);
				spvReflectEnumerateDescriptorBindings(&module, &varCount, descriptorBinds.Data());

				for (const auto descriptorSet : descriptorSets)
				{
					auto setIt = descriptors.Find(descriptorSet->set);
					if (setIt == descriptors.end())
					{
						setIt = descriptors.Insert({descriptorSet->set, HashMap<u32, DescriptorSetLayoutBinding>{}}).first;
					}
					HashMap<u32, DescriptorSetLayoutBinding>& bindings = setIt->second;

					for (const auto descriptorBind : descriptorBinds)
					{
						if (descriptorBind->set == descriptorSet->set)
						{
							if (bindings.Find(descriptorBind->binding) == bindings.end())
							{
								DescriptorSetLayoutBinding descriptorBinding = DescriptorSetLayoutBinding{
									.binding = descriptorBind->binding,
									.count = descriptorBind->count,
									.name = descriptorBind->name,
									.descriptorType = SpirvUtils::GetDescriptorType(descriptorBind->descriptor_type),
									.renderType = SpirvUtils::CastRenderType(descriptorBind->type_description->op),
									.viewType = SpirvUtils::DimToViewType(descriptorBind->image.dim, descriptorBind->image.arrayed),
								};

								bindings.Emplace(descriptorBind->binding, Traits::Move(descriptorBinding));
							}
						}
					}
				}

				spvReflectDestroyShaderModule(&module);
			}
			SpirvUtils::SortAndAddDescriptors(pipelineLayout, descriptors);
		}
		return false;
	}
}
