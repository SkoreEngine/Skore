#pragma once

namespace Skore
{
	struct AudioResource
	{
		enum
		{
			Name,
			Bytes
		};
	};

	enum class AttenuationModel
	{
		Inverse,
		Linear,
		Exponential
	};
}