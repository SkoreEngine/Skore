#pragma once
#include "Device.hpp"


namespace Skore
{
	constexpr static u32 MaxLights = 1000;
	constexpr static i8  MaxNumCascades = 4;
	constexpr static u32  MaxBones = 200;
	constexpr static u32 MaxLods = 10;
	constexpr static u32 BlasLod = 1;

	//temp
	constexpr i32 MAX_LIGHTS = 64;

	enum class LightType
	{
		Directional,
		Point,
		Spot,
		//Area
	};

	enum class AmbientLightSource
	{
		Skybox,
		Color,
		Disabled
	};

	enum class ReflectedLightSource
	{
		Skybox,
		Disabled
	};

	enum class Projection : i32
	{
		Perspective = 1,
		Orthogonal  = 2
	};


	struct SK_API ShaderStageInfo
	{
		ShaderStage stage{};
		String      entryPoint{};
		u32         offset{};
		u32         size{};

		static void RegisterType(NativeReflectType<ShaderStageInfo>& type);
	};

	struct GraphicsSettings
	{
		enum
		{
			EnableValidationLayers,		//BOOL
		};
	};
}
