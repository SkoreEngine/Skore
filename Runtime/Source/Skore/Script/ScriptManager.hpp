#pragma once
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	class SK_API ScriptEngine : public Object
	{
	public:
		SK_CLASS(ScriptEngine, Object);

		virtual void Init() = 0;
		virtual String GetExtension() = 0;
		virtual bool LoadScript(StringView source) = 0;
		virtual bool LoadScript(Span<u8> bytecode) = 0;
		virtual ByteBuffer BuildScript(StringView source) = 0;
	};

	struct SK_API ScriptManager
	{
		static void Init();
		static bool LoadScript(StringView source, StringView extension);
		static bool LoadScript(Span<u8> bytecode, StringView extension);
		static ByteBuffer BuildScript(StringView source, StringView extension);
		static Span<ScriptEngine*> GetEngines();
	};
}
