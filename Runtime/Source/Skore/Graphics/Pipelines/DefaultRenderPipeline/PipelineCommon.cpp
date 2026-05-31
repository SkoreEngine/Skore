#include "PipelineCommon.hpp"

#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void LightPassInstanceData::RegisterType(NativeReflectType<LightPassInstanceData>& type)
	{
		type.Field<&LightPassInstanceData::lightBuffer>("lightBuffer");
	}
}
