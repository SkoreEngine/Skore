#pragma once
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Object.hpp"

namespace Skore
{
	class SK_API ScriptEngine : public Object
	{
	public:
		SK_CLASS(ScriptEngine, Object);

		virtual void Init() = 0;
	};

	struct SK_API ScriptManager
	{
		static void Init();
	};
}
