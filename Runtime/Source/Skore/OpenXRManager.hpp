#pragma once

#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{

	static constexpr  size_t EyeCount = 2;

	struct OpenXRSettings
	{
		enum
		{
			EnableOpenXR,
			EnableValidationLayers
		};
	};

	class SK_API OpenXRManager
	{
	public:
		static void Init();
		static bool IsEnabled();
	private:
		static void Shutdown();
	};
}