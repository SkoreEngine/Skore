#pragma once

#pragma once

#define FMT_UNICODE 0
#define FMT_HEADER_ONLY

#include "fmt/format.h"
#include "fmt/chrono.h"

#include "Math.hpp"


template<>
struct fmt::formatter<Skore::Vec3> : formatter<std::string_view>
{
	auto format(const Skore::Vec3& v, format_context& ctx) const
	{
		return format_to(ctx.out(), "(x {}, y {}, z {})", v.x, v.y, v.z);
	}
};

template<>
struct fmt::formatter<Skore::Quat> : formatter<std::string_view>
{
	auto format(const Skore::Quat& q, format_context& ctx) const
	{
		return format_to(ctx.out(), "(x {}, y {}, z {}, w {})", q.x, q.y, q.z, q.w);
	}
};

template<>
struct fmt::formatter<Skore::Mat4> : formatter<std::string_view>
{
	auto format(const Skore::Mat4& m, format_context& ctx) const
	{
		return format_to(ctx.out(),
		                 "[{}, {}, {}, {}] [{}, {}, {}, {}] [{}, {}, {}, {}] [{}, {}, {}, {}]",
		                 m[0][0], m[0][1], m[0][2], m[0][3],
		                 m[1][0], m[1][1], m[1][2], m[1][3],
		                 m[2][0], m[2][1], m[2][2], m[2][3],
		                 m[3][0], m[3][1], m[3][2], m[3][3]);
	}
};


template<>
struct fmt::formatter<Skore::TypeID> : formatter<std::string_view>
{
	auto format(const Skore::TypeID& v, format_context& ctx) const
	{
		return format_to(ctx.out(), "{}", v.id);
	}
};


namespace Skore
{
	template <typename T>
	class Array;
}


template<typename T>
struct fmt::formatter<Skore::Array<T>> : formatter<std::string_view>
{
	auto format(const Skore::Array<T>& arr, format_context& ctx) const
	{
		auto out = ctx.out();
		format_to(out, "[");
		for (int i = 0; i < arr.Size(); ++i)
		{
			if (i > 0) format_to(out, ", ");
			fmt::format_to(out, "{}", arr[i]);
		}
		return format_to(out, "]");
	}
};