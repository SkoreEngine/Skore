#include "Skore/Graphics/GraphicsCommon.hpp"

#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void ShaderStageInfo::RegisterType(NativeReflectType<ShaderStageInfo>& type)
	{
		type.Field<&ShaderStageInfo::stage>("stage");
		type.Field<&ShaderStageInfo::entryPoint>("entryPoint");
		type.Field<&ShaderStageInfo::offset>("offset");
		type.Field<&ShaderStageInfo::size>("size");
		type.Field<&ShaderStageInfo::hitGroup>("hitGroup");
	}
}