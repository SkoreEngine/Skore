#include "Skore/Core/Math.hpp"

#include <random>

#include "Skore/Core/Reflection.hpp"
#include "Skore/Platform/Platform.hpp"


namespace Skore
{
	u64 Random::Xorshift64star()
	{
		static u64 x = Platform::GetTime();
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		return x * 0x2545F4914F6CDD1DULL;
	}

	i64 Random::NextInt(i64 max)
	{
		static i64 x = static_cast<i64>(Platform::GetTime());
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		x *= 0x2545F4914F6CDD1DULL;
		return (x % max);
	}

	f32 Random::NextFloat32(f32 min, f32 max)
	{
		static std::default_random_engine     e;
		std::uniform_real_distribution dis(min, max);
		return dis(e);
	}

	u64 Random::NextUInt(u64 max)
	{
		static u64 x = Platform::GetTime();
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		x *= 0x2545F4914F6CDD1DULL;
		return (x % max);
	}

	void Random::RegisterType(NativeReflectType<Random>& type)
	{
		type.Function<&Random::Xorshift64star>("Xorshift64star");
		type.Function<&Random::NextInt>("NextInt", "max");
		type.Function<&Random::NextFloat32>("NextFloat32", "min", "max");
		type.Function<&Random::NextUInt>("NextUInt", "max");
	}
}