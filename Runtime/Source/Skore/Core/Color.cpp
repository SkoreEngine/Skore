#include "Skore/Core/Color.hpp"
#include "Skore/Core/Reflection.hpp"


namespace Skore
{
	template<> void TColor<u8>::RegisterType(NativeReflectType<Color>& type)
	{
		type.Field<&Color::red>("red");
		type.Field<&Color::green>("green");
		type.Field<&Color::blue>("blue");
		type.Field<&Color::alpha>("alpha");
	}
}