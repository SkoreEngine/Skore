#pragma once

/**
 * @file dxc_compiler.h
 * @brief Compile-only HLSL -> SPIR-V plugin backed by the DirectX Shader Compiler.
 *
 * Implemented by the sk-dxc-compiler plugin. The plugin dynamically loads the
 * DXC shared library (dxcompiler.dll / libdxcompiler.so / libdxcompiler.dylib)
 * at runtime via sk_platform_api->lib_open and drives IDxcUtils + IDxcCompiler3
 * through hand-declared COM vtables (dxcapi.h is a C++ header and cannot be
 * included from C). The runtime is copied beside the built plugin by
 * sk_copy_dxc_shared_library (see cmake/cmake_functions.cmake).
 *
 * The plugin registers a static sk_dxc_compiler_api_t on the app context.
 * Hosts obtain it **only** via the app registry:
 *
 *   const sk_dxc_compiler_api_t* dxc =
 *       (const sk_dxc_compiler_api_t*)app_api->get_api(
 *           ctx, SK_DXC_COMPILER_API_TYPE_ID);
 *   if (dxc->init() == 0) {
 *       dxc->compile("MainVS", "vs_6_8", hlsl, hlsl_size,
 *                    spirv, spirv_capacity, &spirv_size, log, log_cap);
 *   }
 *   dxc->shutdown();
 *
 * This is a compile-only plugin: it does not reflect on the produced SPIR-V
 * (no pipeline layout / stage detection). Compilation always targets SPIR-V
 * with the vulkan1.2 target environment and the DX layout flags, mirroring
 * skore main's ShaderManager::CompileShader SPIR-V path.
 */

#include "app.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_dxc_compiler_api_t in the app registry. */
#define SK_DXC_COMPILER_API_TYPE_ID SK_TYPE_ID("sk.dxc_compiler_api", 0x07cb322faa0f3388ULL, 0xf2999aee835a82adULL)

/**
 * Global compile-only DXC module API (one table per process after plugin load).
 *
 * init loads the DXC runtime (idempotent) and must succeed before compile.
 * compile is safe to call only after a successful init; otherwise it returns
 * an error code and writes a message into @p log.
 */
typedef struct sk_dxc_compiler_api_t {
	/**
	 * Load the platform DXC shared library and create IDxcUtils / IDxcCompiler3.
	 * The library is resolved from the OS loader search path (bare name) and,
	 * as a fallback, from a cwd-relative "plugins" folder where the build copies
	 * the runtime (sk_copy_dxc_shared_library). Idempotent.
	 * @return 0 on success, non-zero if the runtime could not be loaded.
	 */
	i32 (*init)(void);

	/**
	 * Release the DXC interfaces and unload the shared library. Safe to call
	 * multiple times; no-op when init never succeeded.
	 */
	void (*shutdown)(void);

	/**
	 * Compile HLSL @p source (UTF-8, @p source_size bytes) into SPIR-V bytes.
	 * Uses the same compiler arguments as main's CompileShader SPIR-V path:
	 * -E @p entry_point, -T @p profile, -spirv, -fspv-target-env=vulkan1.2,
	 * -fvk-use-dx-layout, -fvk-use-dx-position-w, -Wno-ignored-attributes,
	 * -disable-payload-qualifiers. Includes are resolved with DXC's default
	 * filesystem include handler.
	 *
	 * @param entry_point     HLSL entry point name (non-NULL, <= 63 bytes UTF-8).
	 * @param profile         Shader profile, e.g. "vs_6_8" (non-NULL, <= 63 bytes).
	 * @param source          HLSL source bytes (non-NULL; may be 0-length with a
	 *                        NULL pointer only when source_size == 0).
	 * @param source_size     Source length in bytes.
	 * @param spirv           Destination buffer for the compiled SPIR-V.
	 * @param spirv_capacity  Capacity of @p spirv in bytes.
	 * @param out_spirv_size  Receives the number of bytes written (may be NULL).
	 * @param log             Optional buffer for compiler errors/warnings
	 *                        (may be NULL when log_capacity == 0).
	 * @param log_capacity    Capacity of @p log in bytes.
	 * @return 0 on success; -1 runtime not loaded, -2 bad entry/profile encoding
	 *         or too long, -3 DXC compile failed (errors in @p log), -4 output
	 *         buffer too small, -5 DXC API call failed (errors in @p log).
	 */
	i32 (*compile)(const_chr_t entry_point, const_chr_t profile, const_chr_t source, u32 source_size, u8* spirv, u32 spirv_capacity, u32* out_spirv_size, char_ptr_t log,
				   u32 log_capacity);
} sk_dxc_compiler_api_t;

#ifdef __cplusplus
}
#endif
