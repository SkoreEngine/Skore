#pragma once

namespace Skore
{
    typedef unsigned char       u8;
    typedef unsigned short      u16;
    typedef unsigned int        u32;
    typedef unsigned long long  u64;
    typedef unsigned long int   ul32;

    typedef signed char         i8;
    typedef signed short        i16;
    typedef signed int          i32;
    typedef signed long long    i64;

    typedef float               f32;
    typedef double              f64;

    typedef void*               VoidPtr;
    typedef const void*         ConstPtr;
    typedef char*               CharPtr;
    typedef u64                 TypeID;
    typedef decltype(sizeof(0)) usize;

    typedef f32                 Float;

    struct PlaceHolder
    {
    };

    static constexpr usize nPos = -1;

    #define SK_HANDLER(StructName) struct StructName {                                  \
        VoidPtr handler;                                                                \
     operator bool() const {return handler != nullptr; }                                \
     bool operator==(const StructName& b) const { return this->handler == b.handler; }  \
     bool operator!=(const StructName& b) const { return this->handler != b.handler; }  \
    }

    #define SK_NO_COPY_CONSTRUCTOR(TypeName)                    \
        TypeName(TypeName&& other) = default;                   \
        TypeName(const TypeName& other) = delete;               \
        TypeName& operator=(const TypeName& other) = delete;    \

    constexpr u64 Prime = 1099511628211ULL;
    constexpr u64 OffsetBias = 14695981039346656037ULL;

    template<typename Type>
    class NativeTypeHandler;

    class TypeHandler;

    template<typename ...Types>
    struct BaseTypes {};

    #define SK_BASE_TYPES(...) using Bases = BaseTypes<__VA_ARGS__>


    SK_HANDLER(ArchiveValue);

    template <typename T, typename Enable = void>
    struct ArchiveType
    {
        constexpr static bool hasArchiveImpl = false;
    };

    struct Object
    {
        virtual ~Object() = default;
    };

}

inline void* operator new(Skore::usize, Skore::PlaceHolder, Skore::VoidPtr ptr)
{
    return ptr;
}

inline void operator delete(void*, Skore::PlaceHolder, Skore::VoidPtr) noexcept
{
}
//defines

//--general defines
#define SK_STRING_BUFFER_SIZE 18
#define SK_FRAMES_IN_FLIGHT 2
#define SK_META_EXTENSION ".meta"
#define SK_ASSET_EXTENSION ".asset"
#define SK_BUFFER_EXTENSION ".buffer"
#define SK_PROJECT_EXTENSION ".skore"
#define SK_REPO_PAGE_SIZE 4096

//---platform defines
#if _WIN64
    #define SK_API __declspec(dllexport)
    #define SK_PATH_SEPARATOR '\\'
    #define SK_SHARED_EXT ".dll"
    #define SK_WIN
    #define SK_DESKTOP
#elif __linux__
    #define SK_API __attribute__ ((visibility ("default")))
    #define SK_PATH_SEPARATOR '/'
    #define SK_SHARED_EXT ".so"
    #define SK_LINUX
    #define SK_UNIX
    #define SK_DESKTOP  //TODO android?
#elif __APPLE__
    #define SK_API
    #define SK_APPLE
    #define SK_UNIX
    #define SK_PATH_SEPARATOR '/'
    #define SK_SHARED_EXT ".dylib"

    #include <TargetConditionals.h>

    #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
        #define SK_IOS
    #elif TARGET_OS_MAC
        #define SK_DESKTOP
        #define SK_MACOS
    #endif
#endif

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

#if defined _MSC_VER
    #define SK_FINLINE __forceinline
#elif defined __CLANG__
#       define SK_FINLINE [[clang::always_inline]]
#elif  defined __GNUC__
#define SK_FINLINE inline __attribute__((always_inline))
#else
static_assert(false, "Compiler not supported");
#endif

#define SK_COL32_R_SHIFT    0
#define SK_COL32_G_SHIFT    8
#define SK_COL32_B_SHIFT    16
#define SK_COL32_A_SHIFT    24


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
inline ENUMNAME operator ^ (ENUMNAME a, ENUMNAME b)    noexcept { return ENUMNAME(((ENUMTYPE)a) ^ (ENUMTYPE)b);        }  \

#ifdef NDEBUG
#  define SK_ASSERT(condition, message) ((void)0)
#else
#  include <cassert>
#  define SK_ASSERT(condition, message) assert(condition && message)
#  define SK_DEBUG
#endif

#define SK_ENABLE_TAA 0
#define SK_ENABLE_TEXTURE_COMPRESSION 0