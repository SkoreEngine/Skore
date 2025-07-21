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

#pragma once

#if defined(__x86_64__) || defined(_M_X64) || \
defined(__aarch64__) || defined(__ppc64__) || \
defined(__mips64) || defined(__ia64__) || \
defined(__powerpc64__) || defined(__64BIT__) || \
defined(_WIN64)
	#define SK_IS_64BIT_SYSTEM 1
#else
	/* Then handle the sizeof check in the compiled code, not preprocessor */
	#if (sizeof(void*) == 8)
		#define SK_IS_64BIT_SYSTEM 1
	#else
		#define SK_IS_64BIT_SYSTEM 0
	#endif
#endif

namespace Skore
{
	typedef unsigned char      u8;
	typedef unsigned short     u16;
	typedef unsigned int       u32;
	typedef unsigned long long u64;
	typedef unsigned long int  ul32;

	typedef signed char      i8;
	typedef signed short     i16;
	typedef signed int       i32;
	typedef signed long long i64;

	typedef float  f32;
	typedef double f64;

	typedef void*               VoidPtr;
	typedef const void*         ConstPtr;
	typedef char*               CharPtr;
	typedef u64                 TypeID;
	typedef decltype(sizeof(0)) usize;

	typedef f32 Float;

	struct PlaceHolder {};
	struct ProjectSettings{};

	static constexpr usize nPos = -1;
	constexpr u64          Prime = 1099511628211ULL;
	constexpr u64          OffsetBias = 14695981039346656037ULL;

	static const u32 MaxBindlessResources = 8192;

#if SK_IS_64BIT_SYSTEM
	typedef u64 uptr;
#else
	typedef u32 uptr;
#endif

	template<typename Type, typename Enable = void>
	struct TypeApi
	{
		static void GetApi(VoidPtr pointer)
		{
		}

		static constexpr TypeID GetApiId()
		{
			return 0;
		}
	};

	template <typename Type>
	class NativeReflectType;

	inline u64 PtrToInt(VoidPtr ptr)
	{
		// ReSharper disable once CppRedundantCastExpression
		return static_cast<u64>(reinterpret_cast<uptr>(ptr));
	}

	inline VoidPtr IntToPtr(u64 ptr)
	{
		return reinterpret_cast<VoidPtr>(static_cast<uptr>(ptr));
	}

	struct RID
	{
		u64 id{};

		operator bool() const noexcept
		{
			return this->id > 0;
		}

		bool operator==(const RID& rid) const
		{
			return this->id == rid.id;
		}

		bool operator!=(const RID& rid) const
		{
			return !(*this == rid);
		}
	};

#define SK_NO_COPY_CONSTRUCTOR(TypeName)                    \
        TypeName(TypeName&& other) = default;                   \
        TypeName(const TypeName& other) = delete;               \
        TypeName& operator=(const TypeName& other) = delete;


#define SK_HANDLER(StructName) struct StructName {                                  \
u64 handler;																	    \
StructName()			: handler(0) {  }											\
StructName(VoidPtr ptr) : handler(PtrToInt(ptr)) {  }								\
StructName(u64 handler) : handler(handler) {  }										\
operator bool() const {return handler != 0; }										\
bool operator==(const StructName& b) const { return this->handler == b.handler; }  \
bool operator!=(const StructName& b) const { return this->handler != b.handler; }  \
VoidPtr ToPtr() { return IntToPtr(handler); }								       \
}

#define SK_BUFFER_HANDLER(StructName, S) struct StructName {     \
char buffer[S];													 \
																 \
template<typename T, typename ...Args>							 \
void Construct(Args&&... args)									 \
{																 \
new(PlaceHolder{}, &As<T>()) T(Traits::Forward<Args>(args)...);	 \
}																 \
template<typename T>											 \
void Destructor() {  As<T>().~T(); }							 \
template<typename T>											 \
T& As() { return *reinterpret_cast<T*>(buffer); }				 \
}

}

#define SK_STRING_BUFFER_SIZE 18
#define SK_COL32_R_SHIFT    0
#define SK_COL32_G_SHIFT    8
#define SK_COL32_B_SHIFT    16
#define SK_COL32_A_SHIFT    24

#define SK_FRAMES_IN_FLIGHT 2
#define SK_PAGE_SIZE 4096

inline void* operator new(Skore::usize, Skore::PlaceHolder, Skore::VoidPtr ptr)
{
	return ptr;
}

inline void operator delete(void*, Skore::PlaceHolder, Skore::VoidPtr) noexcept {}

#ifndef SK_PRETTY_FUNCTION
#if defined _MSC_VER
#   define SK_PRETTY_FUNCTION __FUNCSIG__
#   define SK_PRETTY_FUNCTION_PREFIX '<'
#   define SK_PRETTY_FUNCTION_SUFFIX '>'
#elif defined __clang__ || defined __GNUC__
#   define SK_PRETTY_FUNCTION __PRETTY_FUNCTION__
#   define SK_PRETTY_FUNCTION_PREFIX '='
#   define SK_PRETTY_FUNCTION_SUFFIX ']'
#endif
#endif

#define SK_CONCAT_IMPL(x, y) x##y
#define SK_CONCAT(x, y) SK_CONCAT_IMPL(x, y)

#define SK_PAD(size) u8 SK_CONCAT(pad_, __LINE__)[size]

#if defined _MSC_VER
#define SK_FINLINE __forceinline
#elif defined __CLANG__
#       define SK_FINLINE [[clang::always_inline]]
#elif  defined __GNUC__
#define SK_FINLINE inline __attribute__((always_inline))
#else
static_assert(false, "Compiler not supported");
#endif

#if _WIN64
	#define SK_WIN 1
	#define SK_API __declspec(dllexport)
	#define SK_SHARED_EXT ".dll"
	#define SK_EXEC_EXT ".exe"
#elif __linux__
	#define SK_LINUX 1
	#define SK_SHARED_EXT ".so"
	#define SK_API __attribute__((visibility("default")))
#elif __APPLE__
	#define SK_APPLE 1
	#define SK_SHARED_EXT ".dylib"
    #define SK_API __attribute__((visibility("default")))
#else
	#define SK_API
#endif

#if SK_WIN
#define SK_PATH_SEPARATOR '\\'
#else
#define SK_PATH_SEPARATOR '/'
#endif

#define ENUM_FLAGS(ENUMNAME, ENUMTYPE) \
inline ENUMNAME& operator |= (ENUMNAME& a, ENUMNAME b)  noexcept { return (ENUMNAME&)(((ENUMTYPE&)a) |= ((ENUMTYPE)b)); } \
inline ENUMNAME& operator &= (ENUMNAME& a, ENUMNAME b)  noexcept { return (ENUMNAME&)(((ENUMTYPE&)a) &= ((ENUMTYPE)b)); } \
inline ENUMNAME& operator ^= (ENUMNAME& a, ENUMNAME b)  noexcept { return (ENUMNAME&)(((ENUMTYPE&)a) ^= ((ENUMTYPE)b)); } \
inline ENUMNAME& operator <<= (ENUMNAME& a, ENUMTYPE b)  noexcept { return (ENUMNAME&)(((ENUMTYPE&)a) <<= ((ENUMTYPE)b)); } \
inline ENUMNAME& operator >>= (ENUMNAME& a, ENUMTYPE b)  noexcept { return (ENUMNAME&)(((ENUMTYPE&)a) >>= ((ENUMTYPE)b)); } \
inline ENUMNAME operator | (ENUMNAME a, ENUMNAME b)    noexcept { return ENUMNAME(((ENUMTYPE)a) | ((ENUMTYPE)b));        } \
inline ENUMNAME operator & (ENUMNAME a, ENUMNAME b)    noexcept { return ENUMNAME(((ENUMTYPE)a) & ((ENUMTYPE)b));        } \
inline bool   operator && (ENUMNAME a, ENUMNAME b)    noexcept { return ((ENUMTYPE)a & ((ENUMTYPE)b));            } \
inline ENUMNAME operator ~ (ENUMNAME a)                noexcept { return ENUMNAME(~((ENUMTYPE)a));                        } \
inline ENUMNAME operator ^ (ENUMNAME a, ENUMNAME b)    noexcept { return ENUMNAME(((ENUMTYPE)a) ^ (ENUMTYPE)b);        }

#ifdef NDEBUG
#  define SK_ASSERT(condition, message) ((void)0)
#else
#  include <cassert>
#  define SK_ASSERT(condition, message) assert(condition && message)
#  define SK_DEBUG
#endif

#if defined _MSC_VER
//unsigned int MAX
#   define U8_MAX           0xffui8
#   define U16_MAX          0xffffui16
#   define U32_MAX          0xffffffffui32
#   define U64_MAX          0xffffffffffffffffui64

//signed int MIN
#   define I8_MIN           (-127i8 - 1)
#   define I16_MIN          (-32767i16 - 1)
#   define I32_MIN          (-2147483647i32 - 1)
#   define I64_MIN          (-9223372036854775807i64 - 1)

//signed int MAX
#   define I8_MAX           127i8
#   define I16_MAX          32767i16
#   define I32_MAX          2147483647i32
#   define I64_MAX          9223372036854775807i64

#   define F32_MAX          3.402823466e+38F
#   define F64_MAX          1.7976931348623158e+308

#   define F32_MIN          1.175494351e-38F
#   define F64_MIN          2.2250738585072014e-308

#   define F32_LOW          (-(F32_MAX))
#   define F64_LOW          (-(F64_MAX))

#elif defined __GNUC__
# define I8_MIN		    (-128)
# define I16_MIN		(-32767-1)
# define I32_MIN		(-2147483647-1)
# define I64_MIN	    INT64_MIN

# define I8_MAX		    (127)
# define I16_MAX		(32767)
# define I32_MAX		(2147483647)
# define I64_MAX		INT64_MAX

/* Maximum of unsigned integral types.  */
# define U8_MAX		    (255)
# define U16_MAX		(65535)
# define U32_MAX		(4294967295U)
# define U64_MAX		18446744073709551615UL

# define F32_MAX        __FLT_MAX__
# define F64_MAX        __DBL_MAX__

# define F32_MIN        __FLT_MIN__
# define F64_MIN        __DBL_MIN__

# define F32_LOW         (-(F32_MAX))
# define F64_LOW         (-(F64_MAX))
#endif
