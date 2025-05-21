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

#include "Math.hpp"

#include <random>
#include <SDL3/SDL.h>

#include "Reflection.hpp"


namespace Skore
{
	inline u64 GetTime()
	{
		SDL_Time time = {};
		SDL_GetCurrentTime(&time);
		return time;
	}

	u64 Random::Xorshift64star()
	{
		static u64 x = GetTime();
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		return x * 0x2545F4914F6CDD1DULL;
	}

	i64 Random::NextInt(i64 max)
	{
		static i64 x = static_cast<i64>(GetTime());
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		x *= 0x2545F4914F6CDD1DULL;
		return (x % max);
	}

	f32 Random::NextFloat32(f32 min, f32 max)
	{
		static std::default_random_engine     e;
		static std::uniform_real_distribution dis(min, max);
		return dis(e);
	}

	u64 Random::NextUInt(u64 max)
	{
		static u64 x = GetTime();
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		x *= 0x2545F4914F6CDD1DULL;
		return (x % max);
	}

	void Transform::RegisterType(NativeReflectType<Transform>& type)
	{
		type.Field<&Transform::position>("position");
		type.Field<&Transform::rotation>("rotation");
		type.Field<&Transform::scale>("scale");
	}
}
