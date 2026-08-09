#pragma once

#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long int ul32;

typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

typedef float f32;
typedef double f64;

typedef void* void_ptr_t;
typedef const void* const_ptr_t;
typedef char* char_ptr_t;
typedef const char* const_chr_t;

/*
 * ISO C forbids object-pointer ↔ function-pointer conversion (-Wpedantic).
 * Round-trip through uintptr_t is the portable engine convention for dlsym /
 * GetProcAddress results and similar host/plugin boundaries.
 */
#define SK_PTR_TO_FN(fn_type, p) ((fn_type)(uintptr_t)(p))

/* Drop const from a data pointer via uintptr_t (avoids -Wcast-qual). */
#define SK_CONST_CAST(type, p) ((type)(uintptr_t)(const void*)(p))

/*
 * Shared-library export.
 * - Windows (32/64): __declspec(dllexport). SK_WIN is set on Win64 only
 *   (historical; keep tests that gate on it working).
 * - ELF/Mach-O: default visibility.
 */
#if defined(_WIN32)
#if defined(_WIN64)
#define SK_WIN 1
#endif
#define SK_API __declspec(dllexport)
#else
#ifndef SK_API
#define SK_API __attribute__((visibility("default")))
#endif
#endif

/* Force-inline helpers (hot math / accessors). Falls back to static inline. */
#ifndef SK_FINLINE
/*
 * clang defines _MSC_VER when it targets the Windows/MSVC ABI (GNU mode and
 * clang-cl). __forceinline is an MS extension token there that fails under
 * -Wpedantic; clang accepts __attribute__((always_inline)) on every platform,
 * so reserve __forceinline for the genuine MSVC compiler.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#define SK_FINLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SK_FINLINE static inline __attribute__((always_inline))
#else
#define SK_FINLINE static inline
#endif
#endif

/*
 * 128-bit compile-time type identifier.
 *
 * Write type IDs as SK_TYPE_ID with a single string name; CMake
 * (cmake/cmake_functions.cmake) rewrites that call to add two u64 MD5 halves:
 * SK_TYPE_ID(name-string, lo-hex-ULL, hi-hex-ULL).
 *
 * @field lo  First 64 bits of MD5(name) (hex digits 0..15).
 * @field hi  Second 64 bits of MD5(name) (hex digits 16..31).
 */
typedef struct sk_type_id_t {
	u64 lo;
	u64 hi;
} sk_type_id_t;

/** Construct a type id from the two 64-bit halves produced by CMake. @param _name kept for readability only. */
#define SK_TYPE_ID(_name, _lo, _hi) ((sk_type_id_t){(u64)(_lo), (u64)(_hi)})

/** Zero / invalid type id. */
#define SK_TYPE_ID_ZERO ((sk_type_id_t){0ull, 0ull})

/** Equality: non-zero if both 64-bit halves match. */
#define SK_TYPE_ID_EQ(a, b) (((a).lo == (b).lo) && ((a).hi == (b).hi))

/* ------------------------------------------------------------------ */
/*  Lightweight opaque-handle wrapper (C port of the C++ SK_HANDLER)   */
/* ------------------------------------------------------------------ */

/**
 * Generate an opaque handle type backed by a u64.
 *
 * Patterned after the C++ SK_HANDLER macro: each invocation produces a
 * distinct typedef and a family of helpers scoped to that name via
 * token-pasting.  The C++ version has constructors / operator bool; this
 * generates a zero-value literal, static-inline factory functions, and
 * boolean/comparison macros (C has no operators overloading).
 *
 * Usage:
 *     SK_HANDLER(MyRenderDevice);
 *
 *     MyRenderDevice h0 = MyRenderDevice_zero();          // invalid handle
 *     MyRenderDevice hp = MyRenderDevice_from_ptr(p);     // from void*
 *     MyRenderDevice hu = MyRenderDevice_from_u64(42);    // raw u64
 *
 *     if (MyRenderDevice_is_valid(h)) { ... }            // truthiness
 *     if (MyRenderDevice_eq(a, b)) { ... }               // equality
 *
 * @param Name  The handle type name (used as struct tag / typedef tag
 *              and prefix for every generated helper).
 */
#define SK_HANDLER(Name)                            \
	struct sk_handler_##Name {                      \
		u64 handler;                                \
	};                                              \
	typedef struct sk_handler_##Name Name;          \
                                                    \
	SK_FINLINE Name Name##_zero(void) {             \
		return (Name){0};                           \
	}                                               \
                                                    \
	SK_FINLINE Name Name##_from_ptr(void_ptr_t p) { \
		return (Name){(uintptr_t)p};                  \
	}                                               \
                                                    \
	SK_FINLINE Name Name##_from_u64(u64 v) {        \
		return (Name){v};                           \
	}                                               \
                                                    \
	SK_FINLINE int Name##_is_valid(Name h) {        \
		return (h.handler != 0u);                   \
	}                                               \
                                                    \
	SK_FINLINE int Name##_eq(Name a, Name b) {      \
		return (a.handler == b.handler);            \
	}                                               \
                                                    \
	SK_FINLINE void_ptr_t Name##_to_ptr(Name h) {   \
		return (void_ptr_t)(uintptr_t)h.handler;    \
	}

/** Zero / invalid handle for the given generated type. */
#define SK_HANDLER_ZERO(Name) Name##_zero()
