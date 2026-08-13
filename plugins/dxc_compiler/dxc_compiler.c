/**
 * @file dxc_compiler.c
 * @brief Compile-only HLSL -> SPIR-V backend over the DirectX Shader Compiler.
 *
 * dxcapi.h (thirdparty/dxc) is a C++ header, so the C plugin hand-declares the
 * tiny COM surface it needs (IUnknown + IDxcBlob/IDxcBlobUtf8, IDxcUtils,
 * IDxcResult, IDxcCompiler3) as plain vtables whose slot order mirrors dxcapi.h
 * exactly. The runtime library is loaded dynamically through sk_platform_api
 * (lib_open / lib_symbol / lib_close) and the COM objects are driven through
 * those vtables; nothing is linked against DXC at build time.
 *
 * Compile arguments mirror skore main's ShaderManager::CompileShader SPIR-V
 * path (entry, profile, -spirv, vulkan1.2 target env, DX layout flags) without
 * porting GetPipelineLayout / spirv_reflect / stage detection.
 *
 * Singleton module state (one API table per loaded plugin, main-thread owned).
 */

#include "dxc_compiler.h"

#include "logger.h"
#include "path.h"
#include "platform.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Minimal C mirror of the DXC COM interfaces (slot order = dxcapi.h)  */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
/* DXC interfaces use __stdcall on Windows. On x64 the calling conventions
 * collapse to one ABI, but 32-bit Win32 still needs the annotation. */
#define SK_DXC_STDCALL __stdcall
#else
#define SK_DXC_STDCALL
#endif

/* HRESULT is signed 32-bit on every supported DXC build (long on Win32). */
typedef i32 sk_dxc_hr_t;

/* Wide character unit DXC expects for strings/args: UTF-16 on Win32 (wchar_t
 * is 16-bit), UTF-32 elsewhere (wchar_t is 32-bit on Linux/macOS). */
#if defined(_WIN32)
typedef u16 sk_dxc_wide_char_t;
#else
typedef u32 sk_dxc_wide_char_t;
#endif

enum {
	SK_DXC_WIDE_CAP = 64u,		/* wide units per arg / entry / profile string */
	SK_DXC_LIB_PATH_CAP = 256u, /* candidate runtime library path buffer */
};

/* GUID/CLSID/IID layout, matching the WinAdapter.h / Windows GUID struct. */
typedef struct sk_dxc_guid_t {
	u32 data1;
	u16 data2;
	u16 data3;
	u8 data4[8];
} sk_dxc_guid_t;

/* DxcBuffer: pointer + byte size + codepage of the source handed to Compile. */
typedef struct sk_dxc_buffer_t {
	const_ptr_t ptr;
	size_t size;
	u32 encoding;
} sk_dxc_buffer_t;

/* Codepages and DXC_OUT_* kinds used here (values from dxcapi.h). */
enum {
	SK_DXC_CP_UTF8 = 65001,
	SK_DXC_CP_ACP = 0,
	SK_DXC_OUT_OBJECT = 1,
	SK_DXC_OUT_ERRORS = 2,
};

/* IUnknown prefix shared by every COM interface. */
typedef struct sk_dxc_unknown_vtbl_t {
	sk_dxc_hr_t(SK_DXC_STDCALL* query_interface)(void_ptr_t self, const sk_dxc_guid_t* iid, void_ptr_t* out);
	u32(SK_DXC_STDCALL* add_ref)(void_ptr_t self);
	u32(SK_DXC_STDCALL* release)(void_ptr_t self);
} sk_dxc_unknown_vtbl_t;

typedef struct sk_dxc_unknown_t {
	const sk_dxc_unknown_vtbl_t* vtbl;
} sk_dxc_unknown_t;

/* IDxcBlob = IUnknown + GetBufferPointer + GetBufferSize. */
typedef struct sk_dxc_blob_vtbl_t {
	sk_dxc_hr_t(SK_DXC_STDCALL* query_interface)(void_ptr_t self, const sk_dxc_guid_t* iid, void_ptr_t* out);
	u32(SK_DXC_STDCALL* add_ref)(void_ptr_t self);
	u32(SK_DXC_STDCALL* release)(void_ptr_t self);
	void_ptr_t(SK_DXC_STDCALL* get_buffer_pointer)(void_ptr_t self);
	size_t(SK_DXC_STDCALL* get_buffer_size)(void_ptr_t self);
} sk_dxc_blob_vtbl_t;

typedef struct sk_dxc_blob_t {
	const sk_dxc_blob_vtbl_t* vtbl;
} sk_dxc_blob_t;

/* IDxcBlobEncoding returned by IDxcUtils::CreateBlob; shares the IDxcBlob
 * layout for the GetBuffer* calls this plugin uses. */
typedef struct sk_dxc_blob_encoding_t {
	const sk_dxc_blob_vtbl_t* vtbl;
} sk_dxc_blob_encoding_t;

/* IDxcIncludeHandler — only Release is ever called, so IUnknown suffices. */
typedef struct sk_dxc_include_handler_t {
	const sk_dxc_unknown_vtbl_t* vtbl;
} sk_dxc_include_handler_t;

/* IDxcBlobUtf8 = IUnknown + IDxcBlob + GetEncoding + GetStringPointer/Length. */
typedef struct sk_dxc_blob_utf8_vtbl_t {
	sk_dxc_hr_t(SK_DXC_STDCALL* query_interface)(void_ptr_t self, const sk_dxc_guid_t* iid, void_ptr_t* out);
	u32(SK_DXC_STDCALL* add_ref)(void_ptr_t self);
	u32(SK_DXC_STDCALL* release)(void_ptr_t self);
	void_ptr_t(SK_DXC_STDCALL* get_buffer_pointer)(void_ptr_t self);
	size_t(SK_DXC_STDCALL* get_buffer_size)(void_ptr_t self);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_encoding)(void_ptr_t self, i32* known, u32* codepage);
	const_chr_t(SK_DXC_STDCALL* get_string_pointer)(void_ptr_t self);
	size_t(SK_DXC_STDCALL* get_string_length)(void_ptr_t self);
} sk_dxc_blob_utf8_vtbl_t;

typedef struct sk_dxc_blob_utf8_t {
	const sk_dxc_blob_utf8_vtbl_t* vtbl;
} sk_dxc_blob_utf8_t;

/* IDxcUtils = IUnknown + 13 methods (slot order from dxcapi.h). */
typedef struct sk_dxc_utils_vtbl_t {
	sk_dxc_hr_t(SK_DXC_STDCALL* query_interface)(void_ptr_t self, const sk_dxc_guid_t* iid, void_ptr_t* out);
	u32(SK_DXC_STDCALL* add_ref)(void_ptr_t self);
	u32(SK_DXC_STDCALL* release)(void_ptr_t self);
	sk_dxc_hr_t(SK_DXC_STDCALL* create_blob_from_blob)(void_ptr_t self, sk_dxc_blob_t* blob, u32 offset, u32 length, sk_dxc_blob_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* create_blob_from_pinned)(void_ptr_t self, const_ptr_t data, u32 size, u32 codepage, sk_dxc_blob_encoding_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* move_to_blob)(void_ptr_t self, const_ptr_t data, void_ptr_t alloc, u32 size, u32 codepage, sk_dxc_blob_encoding_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* create_blob)(void_ptr_t self, const_ptr_t data, u32 size, u32 codepage, sk_dxc_blob_encoding_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* load_file)(void_ptr_t self, const sk_dxc_wide_char_t* path, u32* codepage, sk_dxc_blob_encoding_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* create_read_only_stream_from_blob)(void_ptr_t self, sk_dxc_blob_t* blob, void_ptr_t* out);
	sk_dxc_hr_t(SK_DXC_STDCALL* create_default_include_handler)(void_ptr_t self, sk_dxc_include_handler_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_blob_as_utf8)(void_ptr_t self, sk_dxc_blob_t* blob, sk_dxc_blob_utf8_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_blob_as_wide)(void_ptr_t self, sk_dxc_blob_t* blob, void_ptr_t* out);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_dxil_container_part)(void_ptr_t self, const sk_dxc_buffer_t* shader, u32 part, void_ptr_t* part_data, u32* part_size);
	sk_dxc_hr_t(SK_DXC_STDCALL* create_reflection)(void_ptr_t self, const sk_dxc_buffer_t* data, const sk_dxc_guid_t* iid, void_ptr_t* out);
	sk_dxc_hr_t(SK_DXC_STDCALL* build_arguments)(void_ptr_t self, const sk_dxc_wide_char_t* name, const sk_dxc_wide_char_t* entry, const sk_dxc_wide_char_t* profile,
												 const sk_dxc_wide_char_t* const* args, u32 arg_count, const void* defines, u32 define_count, void_ptr_t* out);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_pdb_contents)(void_ptr_t self, sk_dxc_blob_t* blob, sk_dxc_blob_t** hash, sk_dxc_blob_t** container);
} sk_dxc_utils_vtbl_t;

typedef struct sk_dxc_utils_t {
	const sk_dxc_utils_vtbl_t* vtbl;
} sk_dxc_utils_t;

/* IDxcResult = IDxcOperationResult (GetStatus/GetResult/GetErrorBuffer) +
 * HasOutput/GetOutput/GetNumOutputs/GetOutputByIndex/PrimaryOutput. */
typedef struct sk_dxc_result_vtbl_t {
	sk_dxc_hr_t(SK_DXC_STDCALL* query_interface)(void_ptr_t self, const sk_dxc_guid_t* iid, void_ptr_t* out);
	u32(SK_DXC_STDCALL* add_ref)(void_ptr_t self);
	u32(SK_DXC_STDCALL* release)(void_ptr_t self);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_status)(void_ptr_t self, sk_dxc_hr_t* out);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_result)(void_ptr_t self, sk_dxc_blob_t** out);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_error_buffer)(void_ptr_t self, sk_dxc_blob_encoding_t** out);
	i32(SK_DXC_STDCALL* has_output)(void_ptr_t self, u32 kind);
	sk_dxc_hr_t(SK_DXC_STDCALL* get_output)(void_ptr_t self, u32 kind, const sk_dxc_guid_t* iid, void_ptr_t* out, void_ptr_t* out_name);
	u32(SK_DXC_STDCALL* get_num_outputs)(void_ptr_t self);
	u32(SK_DXC_STDCALL* get_output_by_index)(void_ptr_t self, u32 index);
	u32(SK_DXC_STDCALL* primary_output)(void_ptr_t self);
} sk_dxc_result_vtbl_t;

typedef struct sk_dxc_result_t {
	const sk_dxc_result_vtbl_t* vtbl;
} sk_dxc_result_t;

/* IDxcCompiler3 = IUnknown + Compile + Disassemble. */
typedef struct sk_dxc_compiler3_vtbl_t {
	sk_dxc_hr_t(SK_DXC_STDCALL* query_interface)(void_ptr_t self, const sk_dxc_guid_t* iid, void_ptr_t* out);
	u32(SK_DXC_STDCALL* add_ref)(void_ptr_t self);
	u32(SK_DXC_STDCALL* release)(void_ptr_t self);
	sk_dxc_hr_t(SK_DXC_STDCALL* compile)(void_ptr_t self, const sk_dxc_buffer_t* source, const sk_dxc_wide_char_t* const* args, u32 arg_count,
										 sk_dxc_include_handler_t* include_handler, const sk_dxc_guid_t* riid, void_ptr_t* out);
	sk_dxc_hr_t(SK_DXC_STDCALL* disassemble)(void_ptr_t self, const sk_dxc_buffer_t* object, const sk_dxc_guid_t* riid, void_ptr_t* out);
} sk_dxc_compiler3_vtbl_t;

typedef struct sk_dxc_compiler3_t {
	const sk_dxc_compiler3_vtbl_t* vtbl;
} sk_dxc_compiler3_t;

/* DxcCreateInstance export resolved from the shared library. */
typedef sk_dxc_hr_t(SK_DXC_STDCALL* sk_dxc_create_instance_proc_t)(const sk_dxc_guid_t* rclsid, const sk_dxc_guid_t* riid, void_ptr_t* out);

/* GUIDs / CLSIDs / IIDs used by this plugin (byte-identical to dxcapi.h). */
static const sk_dxc_guid_t IID_IDxcBlob = {0x8ba5fb08u, 0x5195u, 0x40e2u, {0xacu, 0x58u, 0x0du, 0x98u, 0x9cu, 0x3au, 0x01u, 0x02u}};
static const sk_dxc_guid_t IID_IDxcBlobUtf8 = {0x3da636c9u, 0xba71u, 0x4024u, {0xa3u, 0x01u, 0x30u, 0xcbu, 0xf1u, 0x25u, 0x30u, 0x5bu}};
static const sk_dxc_guid_t IID_IDxcUtils = {0x4605c4cbu, 0x2019u, 0x492au, {0xadu, 0xa4u, 0x65u, 0xf2u, 0x0bu, 0xb7u, 0xd6u, 0x7fu}};
static const sk_dxc_guid_t IID_IDxcResult = {0x58346cdau, 0xdde7u, 0x4497u, {0x94u, 0x61u, 0x6fu, 0x87u, 0xafu, 0x5eu, 0x06u, 0x59u}};
static const sk_dxc_guid_t IID_IDxcCompiler3 = {0x228b4687u, 0x5a6au, 0x4730u, {0x90u, 0x0cu, 0x97u, 0x02u, 0xb2u, 0x20u, 0x3fu, 0x54u}};
static const sk_dxc_guid_t CLSID_DxcUtils = {0x6245d6afu, 0x66e0u, 0x48fdu, {0x80u, 0xb4u, 0x4du, 0x27u, 0x17u, 0x96u, 0x74u, 0x8cu}};
static const sk_dxc_guid_t CLSID_DxcCompiler = {0x73e22d93u, 0xe6ceu, 0x47f3u, {0xb5u, 0xbfu, 0xf0u, 0x66u, 0x4fu, 0x39u, 0xc1u, 0xb0u}};

/* ------------------------------------------------------------------ */
/* Wide (UTF-16 on Win32 / UTF-32 elsewhere) string helpers             */
/* ------------------------------------------------------------------ */

/* Decode null-terminated UTF-8 @p src into @p out (wide units) and
 * NUL-terminate. Returns the unit count excluding NUL, or UINT32_MAX on
 * invalid UTF-8 or a buffer that cannot hold the result + NUL. */
static u32 dxc_utf8_to_wide(const_chr_t src, sk_dxc_wide_char_t* out, u32 out_cap) {
	const size_t len = strlen(src);
	size_t i = 0u;
	u32 o = 0u;

	while (i < len) {
		const u8 c = (u8)src[i];
		u32 cp = 0u;
		u32 extra = 0u;

		if (c < 0x80u) {
			cp = (u32)c;
		} else if ((c & 0xE0u) == 0xC0u) {
			cp = (u32)(c & 0x1Fu);
			extra = 1u;
		} else if ((c & 0xF0u) == 0xE0u) {
			cp = (u32)(c & 0x0Fu);
			extra = 2u;
		} else if ((c & 0xF8u) == 0xF0u) {
			cp = (u32)(c & 0x07u);
			extra = 3u;
		} else {
			return UINT32_MAX;
		}

		if (extra > 0u) {
			if (i + (size_t)extra >= len) {
				return UINT32_MAX;
			}
			for (u32 k = 1u; k <= extra; ++k) {
				const u8 cc = (u8)src[i + (size_t)k];
				if ((cc & 0xC0u) != 0x80u) {
					return UINT32_MAX;
				}
				cp = (cp << 6u) | (u32)(cc & 0x3Fu);
			}
		}

		/* Reject overlong encodings, surrogates, and out-of-range scalars. */
		if (extra == 1u && cp < 0x80u) {
			return UINT32_MAX;
		}
		if (extra == 2u && cp < 0x800u) {
			return UINT32_MAX;
		}
		if (extra == 3u && (cp < 0x10000u || cp > 0x10FFFFu)) {
			return UINT32_MAX;
		}
		if (cp >= 0xD800u && cp <= 0xDFFFu) {
			return UINT32_MAX;
		}

		i += (size_t)extra + 1u;

#if defined(_WIN32)
		if (cp > 0xFFFFu) {
			if (o + 2u >= out_cap) {
				return UINT32_MAX;
			}
			cp -= 0x10000u;
			out[o++] = (sk_dxc_wide_char_t)(0xD800u + (cp >> 10u));
			out[o++] = (sk_dxc_wide_char_t)(0xDC00u + (cp & 0x3FFu));
		} else {
			if (o + 1u >= out_cap) {
				return UINT32_MAX;
			}
			out[o++] = (sk_dxc_wide_char_t)cp;
		}
#else
		if (o + 1u >= out_cap) {
			return UINT32_MAX;
		}
		out[o++] = cp;
#endif
	}

	if (o >= out_cap) {
		return UINT32_MAX;
	}
	out[o] = (sk_dxc_wide_char_t)0;
	return o;
}

static i32 dxc_wide_from_utf8(const_chr_t src, sk_dxc_wide_char_t* out, u32 out_cap) {
	return (dxc_utf8_to_wide(src, out, out_cap) == UINT32_MAX) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* Module state and logging                                            */
/* ------------------------------------------------------------------ */

/* Singleton compile state, owned by the plugin's one API table. The
 * platform pointer is captured at registration; everything else is filled by
 * init and torn down by shutdown. Main-thread owned. */
typedef struct sk_dxc_compiler_state_t {
	const sk_platform_api_t* platform;
	sk_shared_lib_t library;
	sk_dxc_create_instance_proc_t create_instance;
	sk_dxc_utils_t* utils;
	sk_dxc_compiler3_t* compiler;
	i32 initialized;
} sk_dxc_compiler_state_t;

static sk_dxc_compiler_state_t dxc_state;
static sk_logger_t* dxc_logger;
static const sk_logger_api_t* dxc_logger_api;

static void dxc_log_error(const_chr_t fmt, ...) {
	va_list args;
	if (dxc_logger == NULL || dxc_logger_api == NULL) {
		return;
	}
	va_start(args, fmt);
	sk_log_messagev(dxc_logger_api, SK_LOGGER_TYPE_ERROR, dxc_logger, fmt, args);
	va_end(args);
}

/* Append @p text (up to @p text_len bytes) to the caller log buffer. */
static void dxc_log_append_sized(const_chr_t text, u32 text_len, char_ptr_t log, u32 log_capacity) {
	if (log == NULL || log_capacity == 0u || text == NULL || text_len == 0u) {
		return;
	}
	size_t used = 0u;
	while (used + 1u < (size_t)log_capacity && log[used] != '\0') {
		used += 1u;
	}
	u32 i = 0u;
	while (i < text_len && used + 1u < (size_t)log_capacity) {
		log[used] = text[i];
		used += 1u;
		i += 1u;
	}
	log[used] = '\0';
}

static void dxc_log_append(const_chr_t message, char_ptr_t log, u32 log_capacity) {
	dxc_log_append_sized(message, (u32)strlen(message), log, log_capacity);
}

/* Release a COM interface through its IUnknown prefix (NULL is a no-op). */
static u32 dxc_release(void_ptr_t iface) {
	if (iface == NULL) {
		return 0u;
	}
	return ((sk_dxc_unknown_t*)iface)->vtbl->release(iface);
}

/* ------------------------------------------------------------------ */
/* API implementations                                                 */
/* ------------------------------------------------------------------ */

static const_chr_t dxc_library_name(void) {
#if defined(_WIN32)
	return "dxcompiler.dll";
#elif defined(__APPLE__)
	return "libdxcompiler.dylib";
#else
	return "libdxcompiler.so";
#endif
}

static i32 dxc_init_impl(void) {
	const sk_platform_api_t* platform = dxc_state.platform;
	void_ptr_t raw = NULL;
	sk_dxc_hr_t hr = 0;

	if (dxc_state.initialized != 0) {
		return 0;
	}
	if (platform == NULL) {
		dxc_log_error("dxc-compiler: no platform API available (plugin not registered via sk_app_init?)");
		return -1;
	}

	/* The build copies the vendored runtime into the plugins output dir
	 * (sk_copy_dxc_shared_library), so prefer that deterministic copy over the
	 * OS loader search path: an unrelated system DXC (e.g. one bundled with
	 * the toolchain) may lack SPIR-V codegen. */
	char local_path[SK_DXC_LIB_PATH_CAP];
	dxc_state.library = NULL;
	if (sk_path_join(sk_str_view_cstr("plugins"), sk_str_view_cstr(dxc_library_name()), local_path, (u32)sizeof(local_path)) >= 0) {
		dxc_state.library = platform->lib_open(local_path);
	}
	if (dxc_state.library == NULL) {
		dxc_state.library = platform->lib_open(dxc_library_name());
	}
	if (dxc_state.library == NULL) {
		dxc_log_error("dxc-compiler: failed to load DXC runtime (%s)", platform->lib_error());
		return -1;
	}

	raw = platform->lib_symbol(dxc_state.library, "DxcCreateInstance");
	if (raw == NULL) {
		dxc_log_error("dxc-compiler: DxcCreateInstance symbol missing (%s)", platform->lib_error());
		platform->lib_close(dxc_state.library);
		dxc_state.library = NULL;
		return -1;
	}
	dxc_state.create_instance = SK_PTR_TO_FN(sk_dxc_create_instance_proc_t, raw);

	hr = dxc_state.create_instance(&CLSID_DxcUtils, &IID_IDxcUtils, (void_ptr_t*)&dxc_state.utils);
	if (hr < 0) {
		dxc_log_error("dxc-compiler: DxcCreateInstance(IDxcUtils) failed (0x%08x)", (u32)hr);
		goto fail;
	}
	hr = dxc_state.create_instance(&CLSID_DxcCompiler, &IID_IDxcCompiler3, (void_ptr_t*)&dxc_state.compiler);
	if (hr < 0) {
		dxc_log_error("dxc-compiler: DxcCreateInstance(IDxcCompiler3) failed (0x%08x)", (u32)hr);
		goto fail;
	}

	dxc_state.initialized = 1;
	return 0;

fail:
	dxc_release(dxc_state.compiler);
	dxc_release(dxc_state.utils);
	dxc_state.compiler = NULL;
	dxc_state.utils = NULL;
	platform->lib_close(dxc_state.library);
	dxc_state.library = NULL;
	return -1;
}

static void dxc_shutdown_impl(void) {
	if (dxc_state.initialized == 0) {
		return;
	}
	dxc_release(dxc_state.compiler);
	dxc_release(dxc_state.utils);
	dxc_state.compiler = NULL;
	dxc_state.utils = NULL;
	dxc_state.platform->lib_close(dxc_state.library);
	dxc_state.library = NULL;
	dxc_state.create_instance = NULL;
	dxc_state.initialized = 0;
}

static i32 dxc_compile_impl(const_chr_t entry_point, const_chr_t profile, const_chr_t source, u32 source_size, u8* spirv, u32 spirv_capacity, u32* out_spirv_size, char_ptr_t log,
							u32 log_capacity) {
	sk_dxc_utils_t* utils = dxc_state.utils;
	sk_dxc_compiler3_t* compiler = dxc_state.compiler;
	sk_dxc_blob_encoding_t* p_source = NULL;
	sk_dxc_include_handler_t* include_handler = NULL;
	sk_dxc_result_t* result = NULL;
	sk_dxc_blob_utf8_t* errors = NULL;
	sk_dxc_blob_t* shader = NULL;
	sk_dxc_wide_char_t wide[10u][SK_DXC_WIDE_CAP];
	const sk_dxc_wide_char_t* args[10u];
	u32 arg_count = 0u;
	i32 rc = -1;

	if (dxc_state.initialized == 0 || utils == NULL || compiler == NULL) {
		dxc_log_append("dxc-compiler: DXC runtime not loaded (init failed or not called)", log, log_capacity);
		return -1;
	}

	/* Arg list mirrors main CompileShader SPIR-V path (entry, profile, -spirv,
	 * vulkan target env, layout flags). */
	if (dxc_wide_from_utf8("-E", wide[0], SK_DXC_WIDE_CAP) != 0 || dxc_wide_from_utf8(entry_point, wide[1], SK_DXC_WIDE_CAP) != 0 ||
		dxc_wide_from_utf8("-T", wide[2], SK_DXC_WIDE_CAP) != 0 || dxc_wide_from_utf8(profile, wide[3], SK_DXC_WIDE_CAP) != 0 ||
		dxc_wide_from_utf8("-Wno-ignored-attributes", wide[4], SK_DXC_WIDE_CAP) != 0 || dxc_wide_from_utf8("-spirv", wide[5], SK_DXC_WIDE_CAP) != 0 ||
		dxc_wide_from_utf8("-fspv-target-env=vulkan1.2", wide[6], SK_DXC_WIDE_CAP) != 0 || dxc_wide_from_utf8("-fvk-use-dx-layout", wide[7], SK_DXC_WIDE_CAP) != 0 ||
		dxc_wide_from_utf8("-fvk-use-dx-position-w", wide[8], SK_DXC_WIDE_CAP) != 0 || dxc_wide_from_utf8("-disable-payload-qualifiers", wide[9], SK_DXC_WIDE_CAP) != 0) {
		dxc_log_append("dxc-compiler: entry/profile exceeds wide buffer", log, log_capacity);
		return -2;
	}
	args[arg_count++] = wide[0];
	args[arg_count++] = wide[1];
	args[arg_count++] = wide[2];
	args[arg_count++] = wide[3];
	args[arg_count++] = wide[4];
	args[arg_count++] = wide[5];
	args[arg_count++] = wide[6];
	args[arg_count++] = wide[7];
	args[arg_count++] = wide[8];
	args[arg_count++] = wide[9];

	if (utils->vtbl->create_blob(utils, (const_ptr_t)source, source_size, SK_DXC_CP_UTF8, &p_source) < 0) {
		dxc_log_append("dxc-compiler: IDxcUtils::CreateBlob failed", log, log_capacity);
		return -5;
	}
	if (utils->vtbl->create_default_include_handler(utils, &include_handler) < 0) {
		dxc_log_append("dxc-compiler: IDxcUtils::CreateDefaultIncludeHandler failed", log, log_capacity);
		goto cleanup;
	}

	{
		const sk_dxc_buffer_t buffer = {
			p_source->vtbl->get_buffer_pointer(p_source),
			p_source->vtbl->get_buffer_size(p_source),
			SK_DXC_CP_ACP,
		};
		if (compiler->vtbl->compile(compiler, &buffer, args, arg_count, include_handler, &IID_IDxcResult, (void_ptr_t*)&result) < 0) {
			dxc_log_append("dxc-compiler: IDxcCompiler3::Compile failed", log, log_capacity);
			goto cleanup;
		}
	}

	result->vtbl->get_output(result, SK_DXC_OUT_ERRORS, &IID_IDxcBlobUtf8, (void_ptr_t*)&errors, NULL);
	if (errors != NULL) {
		const_chr_t text = errors->vtbl->get_string_pointer(errors);
		const size_t len = errors->vtbl->get_string_length(errors);
		dxc_log_append_sized(text, (u32)len, log, log_capacity);
		if (text != NULL && len > 0u) {
			dxc_log_error("dxc-compiler: %.64s", text);
		}
	}

	{
		sk_dxc_hr_t compile_status = 0;
		result->vtbl->get_status(result, &compile_status);
		if (compile_status < 0) {
			rc = -3;
			goto cleanup;
		}
	}

	if (result->vtbl->get_output(result, SK_DXC_OUT_OBJECT, &IID_IDxcBlob, (void_ptr_t*)&shader, NULL) < 0 || shader == NULL) {
		dxc_log_append("dxc-compiler: no object output from DXC", log, log_capacity);
		rc = -3;
		goto cleanup;
	}

	{
		const size_t byte_size = shader->vtbl->get_buffer_size(shader);
		if (byte_size == 0u || (size_t)spirv_capacity < byte_size) {
			dxc_log_append("dxc-compiler: SPIR-V output buffer too small", log, log_capacity);
			rc = -4;
			goto cleanup;
		}
		memcpy(spirv, shader->vtbl->get_buffer_pointer(shader), byte_size);
		if (out_spirv_size != NULL) {
			*out_spirv_size = (u32)byte_size;
		}
	}
	rc = 0;

cleanup:
	dxc_release(shader);
	dxc_release(errors);
	dxc_release(result);
	dxc_release(include_handler);
	dxc_release(p_source);
	return rc;
}

static const sk_dxc_compiler_api_t dxc_compiler_api = {
	dxc_init_impl,
	dxc_shutdown_impl,
	dxc_compile_impl,
};

/* ------------------------------------------------------------------ */
/* Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_dxc_compiler_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	dxc_state.platform = (const sk_platform_api_t*)app_api->get_api(context, SK_PLATFORM_API_TYPE_ID);
	dxc_logger_api = app_api->logger_api(context);
	if (dxc_logger_api != NULL && app_api->logger_context(context) != NULL) {
		dxc_logger = dxc_logger_api->create_logger(app_api->logger_context(context), "dxc-compiler");
	}
	app_api->set_api(context, SK_DXC_COMPILER_API_TYPE_ID, (const_ptr_t)&dxc_compiler_api);
}

#ifdef SK_TESTS
#include "test.h"

SK_TEST(dxc_utf8_to_wide_ascii) {
	sk_dxc_wide_char_t out[16];
	const u32 n = dxc_utf8_to_wide("MainVS", out, 16u);
	TEST_ASSERT_EQUAL_UINT(6, n);
	TEST_ASSERT_EQUAL_INT('M', (int)out[0]);
	TEST_ASSERT_EQUAL_INT('S', (int)out[5]);
	TEST_ASSERT_EQUAL_INT(0, (int)out[6]);
}

SK_TEST(dxc_utf8_to_wide_multibyte) {
	sk_dxc_wide_char_t out[16];
	const u32 n = dxc_utf8_to_wide("h\xC3\xA9llo", out, 16u); /* é = U+00E9 */
	TEST_ASSERT_EQUAL_UINT(5, n);
	TEST_ASSERT_EQUAL_INT(0xE9, (int)out[1]);
	TEST_ASSERT_EQUAL_INT('o', (int)out[4]);
}

SK_TEST(dxc_utf8_to_wide_astral_scalar) {
	sk_dxc_wide_char_t out[16];
	const u32 n = dxc_utf8_to_wide("\xF0\x9D\x84\x9E", out, 16u); /* U+1D11E */
#if defined(_WIN32)
	TEST_ASSERT_EQUAL_UINT(2, n); /* UTF-16 surrogate pair */
	TEST_ASSERT_EQUAL_INT(0xD834, (int)out[0]);
	TEST_ASSERT_EQUAL_INT(0xDD1E, (int)out[1]);
#else
	TEST_ASSERT_EQUAL_UINT(1, n); /* UTF-32 code point */
	TEST_ASSERT_EQUAL_INT(0x1D11E, (int)out[0]);
#endif
}

SK_TEST(dxc_utf8_to_wide_rejects_invalid) {
	sk_dxc_wide_char_t out[16];
	TEST_ASSERT_EQUAL_UINT(UINT32_MAX, dxc_utf8_to_wide("\xC0\x80", out, 16u));		/* overlong NUL */
	TEST_ASSERT_EQUAL_UINT(UINT32_MAX, dxc_utf8_to_wide("\xED\xA0\x80", out, 16u)); /* UTF-16 surrogate */
	TEST_ASSERT_EQUAL_UINT(UINT32_MAX, dxc_utf8_to_wide("\xFF", out, 16u));			/* stray byte */
	TEST_ASSERT_EQUAL_UINT(UINT32_MAX, dxc_utf8_to_wide("\xC3", out, 16u));			/* truncated */
}

SK_TEST(dxc_utf8_to_wide_insufficient_buffer) {
	sk_dxc_wide_char_t out[4];
	TEST_ASSERT_EQUAL_UINT(UINT32_MAX, dxc_utf8_to_wide("MainVS", out, 4u));
}

SK_TEST(dxc_compiler_api_table_shape) {
	TEST_ASSERT_NOT_NULL(dxc_compiler_api.init);
	TEST_ASSERT_NOT_NULL(dxc_compiler_api.shutdown);
	TEST_ASSERT_NOT_NULL(dxc_compiler_api.compile);
}

SK_TEST(dxc_compile_fails_cleanly_without_runtime) {
	char log[128];
	u8 spirv[256];
	u32 spirv_size = 0u;
	const_chr_t hlsl = "float4 main() : SV_Position { return float4(0, 0, 0, 1); }";
	const i32 rc = dxc_compile_impl("MainVS", "vs_6_8", hlsl, (u32)strlen(hlsl), spirv, (u32)sizeof(spirv), &spirv_size, log, (u32)sizeof(log));
	TEST_ASSERT_NOT_EQUAL(0, rc);
	TEST_ASSERT_NOT_EQUAL(0, (int)log[0]);
}

/* Minimal HLSL vertex shader fixture for the compile tests. Exercises the DX
 * layout / position-w flags the plugin passes to DXC. */
static const_chr_t dxc_test_vertex_hlsl = "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
										  "VSOut mainVS(float3 pos : POSITION, float2 uv : TEXCOORD0) {\n"
										  "  VSOut o; o.pos = float4(pos, 1.0); o.uv = uv; return o;\n"
										  "}\n";

/* Vendored DXC runtime is required on every supported platform (win-x64 /
 * macOS / linux-x64 under thirdparty/dxc/bin). Missing copy or failed load is
 * a hard test failure — do not soft-skip. */
#define DXC_RUNTIME_REQUIRED_MSG "DXC runtime must load (vendored lib missing or not copied to plugins/)"

SK_TEST(dxc_compiler_hlsl_compiles_to_spirv) {
	char log[512];
	u8 spirv[8192];
	u32 spirv_size = 0u;

	TEST_ASSERT_EQUAL_INT32_MESSAGE(0, dxc_compiler_api.init(), DXC_RUNTIME_REQUIRED_MSG);
	{
		const i32 rc = dxc_compiler_api.compile("mainVS", "vs_6_8", dxc_test_vertex_hlsl, (u32)strlen(dxc_test_vertex_hlsl), spirv, (u32)sizeof(spirv), &spirv_size, log,
												(u32)sizeof(log));
		TEST_ASSERT_EQUAL_INT32_MESSAGE(0, rc, log);
	}
	TEST_ASSERT_TRUE(spirv_size >= 4u);
	{
		const u32 magic = ((u32)spirv[0]) | ((u32)spirv[1] << 8u) | ((u32)spirv[2] << 16u) | ((u32)spirv[3] << 24u);
		TEST_ASSERT_EQUAL_UINT32(0x07230203u, magic);
	}
	dxc_compiler_api.shutdown();
}

SK_TEST(dxc_compiler_surfaces_compile_errors) {
	char log[512];
	u8 spirv[8192];
	u32 spirv_size = 0u;
	const_chr_t broken = "void mainVS() { float x = ; }\n";

	TEST_ASSERT_EQUAL_INT32_MESSAGE(0, dxc_compiler_api.init(), DXC_RUNTIME_REQUIRED_MSG);
	{
		const i32 rc = dxc_compiler_api.compile("mainVS", "vs_6_8", broken, (u32)strlen(broken), spirv, (u32)sizeof(spirv), &spirv_size, log, (u32)sizeof(log));
		TEST_ASSERT_NOT_EQUAL(0, rc);
		TEST_ASSERT_TRUE(spirv_size == 0u);
		TEST_ASSERT_TRUE(log[0] != '\0');
	}
	dxc_compiler_api.shutdown();
}
#endif /* SK_TESTS */
