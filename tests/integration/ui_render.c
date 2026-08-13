/*
 * ui_render.c — offscreen UI draw-list GPU backend integration test (APX-134).
 *
 * Renders a fixture UI tree into an offscreen RGBA8 target via:
 *   sk-ui paint → sk_ui_renderer prepare/encode → sk-vulkan-render-device
 * then reads pixels back and compares against a checked-in golden PNG with a
 * channel tolerance. Mismatch artifacts (actual.png, diff.png) are written to
 * the build artifact directory as binary files; stdout only gets ASCII stats.
 *
 * Golden regeneration (deliberate): set environment variable
 *   SK_UI_REGEN_GOLDENS=1
 * and re-run this test to overwrite plugins/ui/testdata/ui_fixture_golden.png.
 *
 * Encoding safety: golden PNG, readback buffers, and SPIR-V are always handled
 * as raw bytes (fopen "rb"/"wb", memcpy). Never str()/UTF-8-decode binary.
 */

#include "app.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "test.h"
#include "ui.h"

#include "skore_test_font_ttf.h"

/*
 * Unity (via test.h) may include <stdnoreturn.h>, which defines `noreturn`
 * as `_Noreturn`. Windows UCRT <stdlib.h> uses `__declspec(noreturn)`, which
 * breaks under clang-tidy when the macro expands to `_Noreturn`.
 */
#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS

#ifndef SK_INTEGRATION_ARTIFACT_DIR
#define SK_INTEGRATION_ARTIFACT_DIR "."
#endif

#ifndef SK_UI_GOLDEN_DIR
#define SK_UI_GOLDEN_DIR "."
#endif

#define UI_RT_W 128u
#define UI_RT_H 128u
#define UI_RT_BPP 4u
#define UI_PIXEL_TOLERANCE 12u	  /* max |channel| delta per pixel component */
#define UI_MAX_FAIL_FRACTION 0.02 /* allow 2% of pixels slightly off (font AA) */

/* -------------------------------------------------------------------------- */
/* Paths / plugins                                                            */
/* -------------------------------------------------------------------------- */

static i32 ui_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];
	i32 n;

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static const sk_render_device_api_t* ui_load_vulkan_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-vulkan-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-vulkan-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-vulkan-render-device.so";
#endif
	if (ui_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)app_api->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

static const sk_dxc_compiler_api_t* ui_load_dxc_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-dxc-compiler.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-dxc-compiler.dylib";
#else
	const_chr_t plugin_name = "sk-dxc-compiler.so";
#endif
	if (ui_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_dxc_compiler_api_t*)app_api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
}

static const sk_ui_api_t* ui_load_ui_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	if (ui_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_ui_api_t*)app_api->get_api(ctx, SK_UI_API_TYPE_ID);
}

static sk_adapter_t ui_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev, u32* out_count) {
	u32 adapter_count = api->get_adapter_count(dev);
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	u32 i;

	if (adapter_count == 0u) {
		*out_count = 0u;
		return sk_adapter_t_zero();
	}
	for (i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	*out_count = adapter_count;
	return best;
}

/* -------------------------------------------------------------------------- */
/* Minimal RGBA8 PNG (store-only zlib) — binary I/O only                      */
/* -------------------------------------------------------------------------- */

static u32 ui_crc32_table[256];
static i32 ui_crc32_ready = 0;

static void ui_crc32_init(void) {
	u32 i;
	u32 j;
	if (ui_crc32_ready != 0) {
		return;
	}
	for (i = 0u; i < 256u; ++i) {
		u32 c = i;
		for (j = 0u; j < 8u; ++j) {
			c = (c & 1u) != 0u ? (0xedb88320u ^ (c >> 1u)) : (c >> 1u);
		}
		ui_crc32_table[i] = c;
	}
	ui_crc32_ready = 1;
}

static u32 ui_crc32(const u8* data, u32 len) {
	u32 c = 0xffffffffu;
	u32 i;
	ui_crc32_init();
	for (i = 0u; i < len; ++i) {
		c = ui_crc32_table[(c ^ data[i]) & 0xffu] ^ (c >> 8u);
	}
	return c ^ 0xffffffffu;
}

static u32 ui_adler32(const u8* data, u32 len) {
	u32 a = 1u;
	u32 b = 0u;
	u32 i;
	for (i = 0u; i < len; ++i) {
		a = (a + data[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	return (b << 16u) | a;
}

static void ui_write_be32(u8* p, u32 v) {
	p[0] = (u8)((v >> 24u) & 0xffu);
	p[1] = (u8)((v >> 16u) & 0xffu);
	p[2] = (u8)((v >> 8u) & 0xffu);
	p[3] = (u8)(v & 0xffu);
}

static u32 ui_read_be32(const u8* p) {
	return ((u32)p[0] << 24u) | ((u32)p[1] << 16u) | ((u32)p[2] << 8u) | (u32)p[3];
}

static void ui_write_le16(u8* p, u16 v) {
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8u) & 0xffu);
}

static u16 ui_read_le16(const u8* p) {
	return (u16)((u16)p[0] | ((u16)p[1] << 8u));
}

/**
 * Write RGBA8 image as a store-only PNG (filter 0 per row).
 * Always uses fopen(..., "wb") — binary mode.
 * @return 0 on success.
 */
static i32 ui_png_write_rgba8(const_chr_t path, const u8* rgba, u32 width, u32 height) {
	FILE* f;
	const u8 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	u8 ihdr[13];
	u8 chunk_hdr[8];
	u8 crc_buf[4];
	u32 row_bytes;
	u32 raw_size;
	u8* raw = NULL;
	u8* zlib = NULL;
	u32 zlib_cap;
	u32 zlib_len = 0u;
	u32 y;
	u32 off;
	u32 adler;
	u32 crc;
	u32 remaining;
	u32 chunk_len;
	i32 rc = -1;

	if (path == NULL || rgba == NULL || width == 0u || height == 0u) {
		return -1;
	}

	row_bytes = width * 4u;
	raw_size = height * (1u + row_bytes);
	raw = (u8*)malloc((size_t)raw_size);
	if (raw == NULL) {
		return -1;
	}
	for (y = 0u; y < height; ++y) {
		raw[y * (1u + row_bytes)] = 0u; /* filter None */
		memcpy(&raw[y * (1u + row_bytes) + 1u], &rgba[y * row_bytes], (size_t)row_bytes);
	}

	/* zlib: CMF/FLG + stored blocks + adler32 */
	zlib_cap = 2u + ((raw_size / 65535u) + 1u) * 5u + raw_size + 4u;
	zlib = (u8*)malloc((size_t)zlib_cap);
	if (zlib == NULL) {
		free(raw);
		return -1;
	}
	zlib[0] = 0x78u;
	zlib[1] = 0x01u;
	zlib_len = 2u;
	remaining = raw_size;
	off = 0u;
	while (remaining > 0u) {
		u32 block = remaining > 65535u ? 65535u : remaining;
		u8 bfinal = (remaining <= 65535u) ? 1u : 0u;
		zlib[zlib_len++] = bfinal; /* BTYPE = 00 stored */
		ui_write_le16(&zlib[zlib_len], (u16)block);
		zlib_len += 2u;
		ui_write_le16(&zlib[zlib_len], (u16)(block ^ 0xffffu));
		zlib_len += 2u;
		memcpy(&zlib[zlib_len], &raw[off], (size_t)block);
		zlib_len += block;
		off += block;
		remaining -= block;
	}
	adler = ui_adler32(raw, raw_size);
	ui_write_be32(&zlib[zlib_len], adler);
	zlib_len += 4u;

	f = fopen(path, "wb");
	if (f == NULL) {
		free(raw);
		free(zlib);
		return -1;
	}
	if (fwrite(sig, 1u, 8u, f) != 8u) {
		goto done;
	}

	/* IHDR */
	ui_write_be32(&ihdr[0], width);
	ui_write_be32(&ihdr[4], height);
	ihdr[8] = 8u;  /* bit depth */
	ihdr[9] = 6u;  /* RGBA */
	ihdr[10] = 0u; /* compression */
	ihdr[11] = 0u; /* filter */
	ihdr[12] = 0u; /* interlace */
	ui_write_be32(chunk_hdr, 13u);
	chunk_hdr[4] = 'I';
	chunk_hdr[5] = 'H';
	chunk_hdr[6] = 'D';
	chunk_hdr[7] = 'R';
	if (fwrite(chunk_hdr, 1u, 8u, f) != 8u || fwrite(ihdr, 1u, 13u, f) != 13u) {
		goto done;
	}
	{
		u8 tmp[4 + 13];
		memcpy(tmp, "IHDR", 4u);
		memcpy(tmp + 4u, ihdr, 13u);
		crc = ui_crc32(tmp, 17u);
		ui_write_be32(crc_buf, crc);
		if (fwrite(crc_buf, 1u, 4u, f) != 4u) {
			goto done;
		}
	}

	/* IDAT */
	ui_write_be32(chunk_hdr, zlib_len);
	chunk_hdr[4] = 'I';
	chunk_hdr[5] = 'D';
	chunk_hdr[6] = 'A';
	chunk_hdr[7] = 'T';
	if (fwrite(chunk_hdr, 1u, 8u, f) != 8u || fwrite(zlib, 1u, (size_t)zlib_len, f) != (size_t)zlib_len) {
		goto done;
	}
	{
		u8* tmp = (u8*)malloc((size_t)zlib_len + 4u);
		if (tmp == NULL) {
			goto done;
		}
		memcpy(tmp, "IDAT", 4u);
		memcpy(tmp + 4u, zlib, (size_t)zlib_len);
		crc = ui_crc32(tmp, zlib_len + 4u);
		free(tmp);
		ui_write_be32(crc_buf, crc);
		if (fwrite(crc_buf, 1u, 4u, f) != 4u) {
			goto done;
		}
	}

	/* IEND */
	ui_write_be32(chunk_hdr, 0u);
	chunk_hdr[4] = 'I';
	chunk_hdr[5] = 'E';
	chunk_hdr[6] = 'N';
	chunk_hdr[7] = 'D';
	if (fwrite(chunk_hdr, 1u, 8u, f) != 8u) {
		goto done;
	}
	crc = ui_crc32((const u8*)"IEND", 4u);
	ui_write_be32(crc_buf, crc);
	if (fwrite(crc_buf, 1u, 4u, f) != 4u) {
		goto done;
	}

	rc = 0;
done:
	fclose(f);
	free(raw);
	free(zlib);
	(void)chunk_len;
	return rc;
}

/**
 * Inflate store-only zlib stream into out (must be large enough).
 * Returns 0 on success. Rejects compressed streams (our goldens are store-only).
 */
static i32 ui_zlib_inflate_store(const u8* src, u32 src_len, u8* out, u32 out_cap, u32* out_len) {
	u32 pos = 0u;
	u32 written = 0u;

	if (src_len < 6u) {
		return -1;
	}
	/* skip CMF/FLG */
	pos = 2u;
	for (;;) {
		u8 header;
		u16 len;
		u16 nlen;
		u32 bfinal;
		if (pos >= src_len) {
			return -1;
		}
		header = src[pos++];
		bfinal = (u32)(header & 1u);
		if (((header >> 1u) & 3u) != 0u) {
			/* not a stored block — not our golden format */
			return -2;
		}
		if (pos + 4u > src_len) {
			return -1;
		}
		len = ui_read_le16(&src[pos]);
		nlen = ui_read_le16(&src[pos + 2u]);
		pos += 4u;
		if ((u16)(len ^ 0xffffu) != nlen) {
			return -1;
		}
		if (pos + len > src_len) {
			return -1;
		}
		if (written + len > out_cap) {
			return -1;
		}
		memcpy(out + written, src + pos, (size_t)len);
		written += len;
		pos += len;
		if (bfinal != 0u) {
			break;
		}
	}
	/* remaining should be adler32 (4 bytes); ignore value for compare path */
	if (out_len != NULL) {
		*out_len = written;
	}
	return 0;
}

/**
 * Load store-only RGBA8 PNG written by ui_png_write_rgba8 (or equivalent).
 * Opens path with "rb". On success *out_rgba is malloc'd (caller free).
 * @return 0 on success.
 */
static i32 ui_png_read_rgba8(const_chr_t path, u8** out_rgba, u32* out_w, u32* out_h) {
	FILE* f;
	u8 sig[8];
	u8* file = NULL;
	long fsize;
	u32 pos;
	u32 width = 0u;
	u32 height = 0u;
	u8* idat = NULL;
	u32 idat_len = 0u;
	u32 idat_cap = 0u;
	u8* raw = NULL;
	u32 raw_len = 0u;
	u32 raw_cap;
	u8* rgba = NULL;
	u32 y;
	u32 row_bytes;
	i32 rc = -1;

	if (path == NULL || out_rgba == NULL || out_w == NULL || out_h == NULL) {
		return -1;
	}
	*out_rgba = NULL;
	*out_w = 0u;
	*out_h = 0u;

	f = fopen(path, "rb");
	if (f == NULL) {
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	fsize = ftell(f);
	if (fsize < 33) {
		fclose(f);
		return -1;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	file = (u8*)malloc((size_t)fsize);
	if (file == NULL) {
		fclose(f);
		return -1;
	}
	if (fread(file, 1u, (size_t)fsize, f) != (size_t)fsize) {
		fclose(f);
		free(file);
		return -1;
	}
	fclose(f);

	memcpy(sig, file, 8u);
	if (sig[0] != 137u || sig[1] != 80u || sig[2] != 78u || sig[3] != 71u) {
		free(file);
		return -1;
	}

	pos = 8u;
	while (pos + 12u <= (u32)fsize) {
		u32 len = ui_read_be32(&file[pos]);
		const u8* type = &file[pos + 4u];
		const u8* data = &file[pos + 8u];
		if (pos + 12u + len > (u32)fsize) {
			break;
		}
		if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
			if (len < 13u) {
				break;
			}
			width = ui_read_be32(&data[0]);
			height = ui_read_be32(&data[4]);
			if (data[8] != 8u || data[9] != 6u) {
				/* only 8-bit RGBA */
				free(file);
				free(idat);
				return -1;
			}
		} else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
			if (idat_len + len > idat_cap) {
				u32 ncap = idat_cap == 0u ? (len + 64u) : idat_cap;
				u8* nbuf;
				while (ncap < idat_len + len) {
					ncap *= 2u;
				}
				nbuf = (u8*)realloc(idat, (size_t)ncap);
				if (nbuf == NULL) {
					free(file);
					free(idat);
					return -1;
				}
				idat = nbuf;
				idat_cap = ncap;
			}
			memcpy(idat + idat_len, data, (size_t)len);
			idat_len += len;
		} else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
			break;
		}
		pos += 12u + len;
	}

	if (width == 0u || height == 0u || idat == NULL || idat_len == 0u) {
		free(file);
		free(idat);
		return -1;
	}

	raw_cap = height * (1u + width * 4u) + 64u;
	raw = (u8*)malloc((size_t)raw_cap);
	if (raw == NULL) {
		free(file);
		free(idat);
		return -1;
	}
	if (ui_zlib_inflate_store(idat, idat_len, raw, raw_cap, &raw_len) != 0) {
		free(file);
		free(idat);
		free(raw);
		return -1;
	}
	free(idat);
	free(file);

	row_bytes = width * 4u;
	if (raw_len < height * (1u + row_bytes)) {
		free(raw);
		return -1;
	}
	rgba = (u8*)malloc((size_t)width * (size_t)height * 4u);
	if (rgba == NULL) {
		free(raw);
		return -1;
	}
	for (y = 0u; y < height; ++y) {
		const u8* row = &raw[y * (1u + row_bytes)];
		if (row[0] != 0u) {
			/* only filter 0 supported */
			free(raw);
			free(rgba);
			return -1;
		}
		memcpy(&rgba[y * row_bytes], row + 1u, (size_t)row_bytes);
	}
	free(raw);

	*out_rgba = rgba;
	*out_w = width;
	*out_h = height;
	rc = 0;
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Pixel compare (bytes only; ASCII summary to stdout)                        */
/* -------------------------------------------------------------------------- */

typedef struct ui_pixel_diff_stats_t {
	u32 fail_count;
	u32 max_channel_delta;
	u64 sum_channel_delta;
	u32 pixel_count;
} ui_pixel_diff_stats_t;

static u32 ui_abs_u8(u8 a, u8 b) {
	return a > b ? (u32)(a - b) : (u32)(b - a);
}

static void ui_compare_rgba(const u8* actual, const u8* golden, u32 width, u32 height, u32 tolerance, u8* diff_rgba, ui_pixel_diff_stats_t* stats) {
	u32 x;
	u32 y;
	memset(stats, 0, sizeof(*stats));
	stats->pixel_count = width * height;
	for (y = 0u; y < height; ++y) {
		for (x = 0u; x < width; ++x) {
			const u32 i = (y * width + x) * 4u;
			u32 d0 = ui_abs_u8(actual[i + 0u], golden[i + 0u]);
			u32 d1 = ui_abs_u8(actual[i + 1u], golden[i + 1u]);
			u32 d2 = ui_abs_u8(actual[i + 2u], golden[i + 2u]);
			u32 d3 = ui_abs_u8(actual[i + 3u], golden[i + 3u]);
			u32 maxd = d0;
			if (d1 > maxd) {
				maxd = d1;
			}
			if (d2 > maxd) {
				maxd = d2;
			}
			if (d3 > maxd) {
				maxd = d3;
			}
			stats->sum_channel_delta += (u64)d0 + (u64)d1 + (u64)d2 + (u64)d3;
			if (maxd > stats->max_channel_delta) {
				stats->max_channel_delta = maxd;
			}
			if (maxd > tolerance) {
				stats->fail_count += 1u;
				if (diff_rgba != NULL) {
					diff_rgba[i + 0u] = 255u;
					diff_rgba[i + 1u] = 0u;
					diff_rgba[i + 2u] = 0u;
					diff_rgba[i + 3u] = 255u;
				}
			} else if (diff_rgba != NULL) {
				/* dim match */
				diff_rgba[i + 0u] = (u8)(actual[i + 0u] / 4u);
				diff_rgba[i + 1u] = (u8)(actual[i + 1u] / 4u);
				diff_rgba[i + 2u] = (u8)(actual[i + 2u] / 4u);
				diff_rgba[i + 3u] = 255u;
			}
		}
	}
}

static i32 ui_env_regen_goldens(void) {
	const char* v = getenv("SK_UI_REGEN_GOLDENS");
	if (v == NULL || v[0] == '\0') {
		return 0;
	}
	if (v[0] == '0' && v[1] == '\0') {
		return 0;
	}
	return 1;
}

/* -------------------------------------------------------------------------- */
/* Fixture UI tree                                                            */
/* -------------------------------------------------------------------------- */

static void ui_build_fixture(const sk_ui_api_t* ui, sk_ui_context_t* ctx) {
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel;
	sk_ui_node_t red;
	sk_ui_node_t green;
	sk_ui_node_t label;
	sk_ui_style_props_t props;
	sk_ui_layout_style_t layout;

	/* Root: dark background full surface */
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_PADDING;
	props.background_color = sk_ui_rgba(0.08f, 0.10f, 0.14f, 1.0f);
	props.layout.width = sk_ui_pt((f32)UI_RT_W);
	props.layout.height = sk_ui_pt((f32)UI_RT_H);
	props.layout.flex_direction = SK_UI_FLEX_COLUMN;
	props.layout.padding.left = 8.0f;
	props.layout.padding.top = 8.0f;
	props.layout.padding.right = 8.0f;
	props.layout.padding.bottom = 8.0f;
	ui->node_set_inline_style(ctx, root, &props);

	panel = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui->node_set_id(ctx, panel, "panel");
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP;
	props.background_color = sk_ui_rgba(0.18f, 0.22f, 0.30f, 1.0f);
	props.corner_radius = 6.0f;
	props.layout.width = sk_ui_pt(112.0f);
	props.layout.height = sk_ui_pt(112.0f);
	props.layout.flex_direction = SK_UI_FLEX_COLUMN;
	props.layout.padding.left = 8.0f;
	props.layout.padding.top = 8.0f;
	props.layout.padding.right = 8.0f;
	props.layout.padding.bottom = 8.0f;
	props.layout.row_gap = 6.0f;
	ui->node_set_inline_style(ctx, panel, &props);

	red = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, panel);
	ui->node_set_id(ctx, red, "red");
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.85f, 0.20f, 0.15f, 1.0f);
	props.layout.width = sk_ui_pt(96.0f);
	props.layout.height = sk_ui_pt(28.0f);
	ui->node_set_inline_style(ctx, red, &props);

	green = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, panel);
	ui->node_set_id(ctx, green, "green");
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.15f, 0.75f, 0.35f, 0.70f);
	props.layout.width = sk_ui_pt(72.0f);
	props.layout.height = sk_ui_pt(28.0f);
	ui->node_set_inline_style(ctx, green, &props);

	label = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, panel);
	ui->node_set_id(ctx, label, "label");
	ui->node_set_prop_str(ctx, label, "text", "UI");
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH;
	props.color = sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
	props.font_size = 18.0f;
	props.layout.width = sk_ui_pt(96.0f);
	props.layout.height = sk_ui_pt(24.0f);
	ui->node_set_inline_style(ctx, label, &props);

	/* Ensure layout styles are defaults where needed */
	ui->node_get_layout_style(ctx, root, &layout);
	(void)layout;
}

/* -------------------------------------------------------------------------- */
/* Test                                                                       */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_offscreen_draw_list_golden) {
	sk_app_context_t* ctx = NULL;
	const sk_render_device_api_t* api = NULL;
	const sk_dxc_compiler_api_t* dxc = NULL;
	const sk_ui_api_t* ui = NULL;
	sk_render_device_t dev = sk_render_device_t_zero();
	sk_ui_renderer_t* renderer = NULL;
	sk_ui_context_t* ui_ctx = NULL;
	sk_ui_font_system_t* fonts = NULL;
	sk_ui_font_t* font = NULL;
	sk_texture_t color_tex = sk_texture_t_zero();
	sk_texture_view_t color_view = sk_texture_view_t_zero();
	sk_render_pass_t pass = sk_render_pass_t_zero();
	sk_framebuffer_t fb = sk_framebuffer_t_zero();
	sk_buffer_t rb = sk_buffer_t_zero();
	sk_command_buffer_t cmd = sk_command_buffer_t_zero();
	sk_queue_t queue = sk_queue_t_zero();
	sk_fence_t fence = sk_fence_t_zero();
	const sk_ui_draw_list_t* dl = NULL;
	u8* actual = NULL;
	u8* golden = NULL;
	u8* diff = NULL;
	u32 gw = 0u;
	u32 gh = 0u;
	char golden_path[SK_FS_PATH_MAX];
	char actual_path[SK_FS_PATH_MAX];
	char diff_path[SK_FS_PATH_MAX];
	ui_pixel_diff_stats_t stats;
	i32 regen;

	sk_app_boot_t boot = sk_app_init(0, NULL);

	ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	api = ui_load_vulkan_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan render device API required");
	dxc = ui_load_dxc_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(dxc, "dxc compiler API required");
	ui = ui_load_ui_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui API required");
	if (api == NULL || dxc == NULL || ui == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, ui->init());
	{
		const i32 dxc_rc = dxc->init();
		if (dxc_rc != 0) {
			ui->shutdown();
			sk_app_shutdown(ctx);
		}
		TEST_ASSERT_EQUAL_INT32_MESSAGE(0, dxc_rc, "DXC runtime must load");
	}

	dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		dxc->shutdown();
		ui->shutdown();
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI golden render");
		sk_app_shutdown(ctx);
		return;
	}

	{
		u32 adapter_count = 0u;
		sk_adapter_t best = ui_select_adapter(api, dev, &adapter_count);
		if (adapter_count == 0u || !sk_adapter_t_is_valid(best)) {
			api->destroy(dev);
			dxc->shutdown();
			ui->shutdown();
			TEST_IGNORE_MESSAGE("no suitable adapter; skipping UI golden render");
			sk_app_shutdown(ctx);
			return;
		}
		TEST_ASSERT_EQUAL_INT(0, api->select_adapter(dev, best));
	}

	/* Offscreen color target */
	{
		sk_texture_desc_t tdesc;
		sk_texture_view_desc_t vdesc;
		memset(&tdesc, 0, sizeof(tdesc));
		tdesc.extent.width = UI_RT_W;
		tdesc.extent.height = UI_RT_H;
		tdesc.extent.depth = 1u;
		tdesc.mip_levels = 1u;
		tdesc.array_layers = 1u;
		tdesc.sample_count = 1u;
		tdesc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
		tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_RENDER_TARGET | (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
		tdesc.debug_name = "ui-offscreen-color";
		color_tex = api->create_texture(dev, &tdesc);
		TEST_ASSERT_TRUE(sk_texture_t_is_valid(color_tex));

		memset(&vdesc, 0, sizeof(vdesc));
		vdesc.texture = color_tex;
		vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
		vdesc.mip_level_count = 1u;
		vdesc.array_layer_count = 1u;
		vdesc.debug_name = "ui-offscreen-view";
		color_view = api->create_texture_view(dev, &vdesc);
		TEST_ASSERT_TRUE(sk_texture_view_t_is_valid(color_view));
	}

	{
		sk_attachment_desc_t attachment;
		sk_render_pass_desc_t pass_desc;
		sk_framebuffer_desc_t fb_desc;
		memset(&attachment, 0, sizeof(attachment));
		attachment.initial_state = SK_RESOURCE_STATE_UNDEFINED;
		attachment.final_state = SK_RESOURCE_STATE_COPY_SOURCE;
		attachment.load_op = SK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.store_op = SK_ATTACHMENT_STORE_OP_STORE;
		attachment.sample_count = 1u;
		attachment.format = SK_PIXEL_FORMAT_RGBA8_UNORM;

		memset(&pass_desc, 0, sizeof(pass_desc));
		pass_desc.attachments = &attachment;
		pass_desc.attachment_count = 1u;
		pass_desc.debug_name = "ui-offscreen-pass";
		pass = api->create_render_pass(dev, &pass_desc);
		TEST_ASSERT_TRUE(sk_render_pass_t_is_valid(pass));

		memset(&fb_desc, 0, sizeof(fb_desc));
		fb_desc.render_pass = pass;
		fb_desc.attachments = &color_view;
		fb_desc.attachment_count = 1u;
		fb_desc.debug_name = "ui-offscreen-fb";
		fb = api->create_framebuffer(dev, &fb_desc);
		TEST_ASSERT_TRUE(sk_framebuffer_t_is_valid(fb));
	}

	{
		sk_buffer_desc_t rb_desc;
		memset(&rb_desc, 0, sizeof(rb_desc));
		rb_desc.size = (u64)UI_RT_W * UI_RT_H * UI_RT_BPP;
		rb_desc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_DEST;
		rb_desc.host_visible = true;
		rb_desc.debug_name = "ui-readback";
		rb = api->create_buffer(dev, &rb_desc);
		TEST_ASSERT_TRUE(sk_buffer_t_is_valid(rb));
	}

	/* GPU renderer (HLSL via DXC → SPIR-V → create_shader) */
	{
		sk_ui_renderer_desc_t rdesc;
		memset(&rdesc, 0, sizeof(rdesc));
		rdesc.device_api = api;
		rdesc.device = dev;
		rdesc.dxc = dxc;
		rdesc.render_pass = pass;
		renderer = ui->renderer_create(&rdesc);
		TEST_ASSERT_NOT_NULL_MESSAGE(renderer, "UI renderer create must succeed");
		if (renderer == NULL) {
			goto cleanup;
		}
	}

	/* Fixture tree + paint */
	ui_ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ui_ctx);
	fonts = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(fonts);
	font = ui->font_load_memory(fonts, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	ui_build_fixture(ui, ui_ctx);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ui_ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ui_ctx, (f32)UI_RT_W, (f32)UI_RT_H));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ui_ctx, 1.0f, 1.0f));
	{
		sk_ui_paint_params_t paint_params;
		memset(&paint_params, 0, sizeof(paint_params));
		paint_params.font_system = fonts;
		paint_params.font = font;
		TEST_ASSERT_EQUAL_INT(0, ui->paint(ui_ctx, &paint_params));
	}
	dl = ui->get_draw_list(ui_ctx);
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->vertex_count > 0u);
	TEST_ASSERT_TRUE(dl->index_count > 0u);
	TEST_ASSERT_TRUE(dl->command_count > 0u);
	/* Upload CB submitted+waited, then draw CB (encode + readback). */
	{
		sk_command_buffer_desc_t cb_desc;
		sk_command_buffer_begin_info_t begin_info;
		sk_ui_renderer_prepare_info_t prep;
		sk_ui_renderer_encode_info_t enc;
		sk_clear_values_t clear;
		sk_begin_render_pass_info_t rp_info;
		sk_buffer_texture_copy_t copy;
		sk_queue_desc_t q_desc;
		sk_fence_desc_t f_desc;
		sk_submit_info_t submit;
		sk_command_buffer_t upload_cmd = sk_command_buffer_t_zero();
		sk_fence_t upload_fence = sk_fence_t_zero();

		memset(&q_desc, 0, sizeof(q_desc));
		q_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
		queue = api->create_queue(dev, &q_desc);
		TEST_ASSERT_TRUE(sk_queue_t_is_valid(queue));

		memset(&cb_desc, 0, sizeof(cb_desc));
		cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
		cb_desc.debug_name = "ui-golden-upload-cmd";
		upload_cmd = api->create_command_buffer(dev, &cb_desc);
		TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(upload_cmd));

		memset(&begin_info, 0, sizeof(begin_info));
		begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
		TEST_ASSERT_EQUAL_INT(0, api->begin_command_buffer(dev, upload_cmd, &begin_info));

		memset(&prep, 0, sizeof(prep));
		prep.cmd = upload_cmd;
		prep.draw_list = dl;
		prep.font_system = fonts;
		TEST_ASSERT_EQUAL_INT(0, ui->renderer_prepare(renderer, &prep));
		api->end_command_buffer(dev, upload_cmd);

		memset(&f_desc, 0, sizeof(f_desc));
		f_desc.debug_name = "ui-golden-upload-fence";
		upload_fence = api->create_fence(dev, &f_desc);
		TEST_ASSERT_TRUE(sk_fence_t_is_valid(upload_fence));
		memset(&submit, 0, sizeof(submit));
		submit.command_buffers = &upload_cmd;
		submit.command_buffer_count = 1u;
		submit.signal_fence = upload_fence;
		TEST_ASSERT_EQUAL_INT(0, api->submit(dev, queue, &submit));
		TEST_ASSERT_EQUAL_INT(0, api->wait_fences(dev, &upload_fence, 1u, true, UINT64_MAX));
		api->destroy_fence(dev, upload_fence);
		api->destroy_command_buffer(dev, upload_cmd);

		memset(&cb_desc, 0, sizeof(cb_desc));
		cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
		cb_desc.debug_name = "ui-golden-draw-cmd";
		cmd = api->create_command_buffer(dev, &cb_desc);
		TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));

		memset(&begin_info, 0, sizeof(begin_info));
		begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
		TEST_ASSERT_EQUAL_INT(0, api->begin_command_buffer(dev, cmd, &begin_info));

		memset(&clear, 0, sizeof(clear));
		clear.color.r = 0.0f;
		clear.color.g = 0.0f;
		clear.color.b = 0.0f;
		clear.color.a = 1.0f;
		memset(&rp_info, 0, sizeof(rp_info));
		rp_info.render_pass = pass;
		rp_info.framebuffer = fb;
		rp_info.clear_values = &clear;
		api->begin_render_pass(dev, cmd, &rp_info);

		memset(&enc, 0, sizeof(enc));
		enc.cmd = cmd;
		enc.draw_list = dl;
		enc.target_width = UI_RT_W;
		enc.target_height = UI_RT_H;
		TEST_ASSERT_EQUAL_INT(0, ui->renderer_encode(renderer, &enc));
		api->end_render_pass(dev, cmd);

		api->memory_barrier(dev, cmd);
		memset(&copy, 0, sizeof(copy));
		copy.buffer = rb;
		copy.texture = color_tex;
		copy.texture_extent.width = UI_RT_W;
		copy.texture_extent.height = UI_RT_H;
		copy.texture_extent.depth = 1u;
		api->copy_texture_to_buffer(dev, cmd, &copy);
		api->end_command_buffer(dev, cmd);

		memset(&f_desc, 0, sizeof(f_desc));
		f_desc.debug_name = "ui-golden-draw-fence";
		fence = api->create_fence(dev, &f_desc);
		TEST_ASSERT_TRUE(sk_fence_t_is_valid(fence));
		memset(&submit, 0, sizeof(submit));
		submit.command_buffers = &cmd;
		submit.command_buffer_count = 1u;
		submit.signal_fence = fence;
		TEST_ASSERT_EQUAL_INT(0, api->submit(dev, queue, &submit));
		TEST_ASSERT_EQUAL_INT(0, api->wait_fences(dev, &fence, 1u, true, UINT64_MAX));
	}

	/* Map readback as bytes (never decode as text). */
	{
		void_ptr_t mapped = api->buffer_map(dev, rb);
		TEST_ASSERT_NOT_NULL_MESSAGE(mapped, "readback map failed");
		if (mapped == NULL) {
			goto cleanup;
		}
		actual = (u8*)malloc((size_t)UI_RT_W * UI_RT_H * UI_RT_BPP);
		TEST_ASSERT_NOT_NULL(actual);
		if (actual == NULL) {
			api->buffer_unmap(dev, rb);
			goto cleanup;
		}
		memcpy(actual, mapped, (size_t)UI_RT_W * UI_RT_H * UI_RT_BPP);
		api->buffer_unmap(dev, rb);
	}

	snprintf(golden_path, sizeof(golden_path), "%s/ui_fixture_golden.png", SK_UI_GOLDEN_DIR);
	snprintf(actual_path, sizeof(actual_path), "%s/ui_fixture_actual.png", SK_INTEGRATION_ARTIFACT_DIR);
	snprintf(diff_path, sizeof(diff_path), "%s/ui_fixture_diff.png", SK_INTEGRATION_ARTIFACT_DIR);

	regen = ui_env_regen_goldens();
	if (regen != 0) {
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, ui_png_write_rgba8(golden_path, actual, UI_RT_W, UI_RT_H), "failed to write golden PNG");
		/* Also drop a copy in artifact dir for convenience. */
		(void)ui_png_write_rgba8(actual_path, actual, UI_RT_W, UI_RT_H);
		printf("ui_golden: regenerated %s (%ux%u)\n", golden_path, UI_RT_W, UI_RT_H);
	} else {
		i32 load_rc = ui_png_read_rgba8(golden_path, &golden, &gw, &gh);
		if (load_rc != 0) {
			/* First-time bootstrap: write golden when missing so CI can regen deliberately. */
			printf("ui_golden: missing or unreadable golden at %s (rc=%d); write actual artifact only\n", golden_path, load_rc);
			TEST_ASSERT_EQUAL_INT(0, ui_png_write_rgba8(actual_path, actual, UI_RT_W, UI_RT_H));
			TEST_FAIL_MESSAGE("golden PNG missing — run with SK_UI_REGEN_GOLDENS=1 to create it");
		} else {
			TEST_ASSERT_EQUAL_UINT(UI_RT_W, gw);
			TEST_ASSERT_EQUAL_UINT(UI_RT_H, gh);
			diff = (u8*)malloc((size_t)UI_RT_W * UI_RT_H * UI_RT_BPP);
			TEST_ASSERT_NOT_NULL(diff);
			ui_compare_rgba(actual, golden, UI_RT_W, UI_RT_H, UI_PIXEL_TOLERANCE, diff, &stats);

			{
				const f32 mean = stats.pixel_count > 0u ? (f32)stats.sum_channel_delta / (f32)(stats.pixel_count * 4u) : 0.0f;
				const f32 fail_frac = stats.pixel_count > 0u ? (f32)stats.fail_count / (f32)stats.pixel_count : 0.0f;
				/* ASCII-only summary — no binary on stdout. */
				printf("ui_golden: max_channel_delta=%u mean_channel_delta=%.3f fail_pixels=%u/%u (%.2f%%)\n", stats.max_channel_delta, (double)mean, stats.fail_count,
					   stats.pixel_count, (double)(fail_frac * 100.0f));

				if (fail_frac > (f32)UI_MAX_FAIL_FRACTION || stats.max_channel_delta > UI_PIXEL_TOLERANCE * 4u) {
					(void)ui_png_write_rgba8(actual_path, actual, UI_RT_W, UI_RT_H);
					(void)ui_png_write_rgba8(diff_path, diff, UI_RT_W, UI_RT_H);
					printf("ui_golden: mismatch artifacts written to %s and %s\n", actual_path, diff_path);
					TEST_FAIL_MESSAGE("UI golden image mismatch beyond tolerance");
				}
			}
		}
	}

cleanup:
	free(diff);
	free(golden);
	free(actual);
	if (sk_fence_t_is_valid(fence)) {
		api->destroy_fence(dev, fence);
	}
	if (sk_queue_t_is_valid(queue)) {
		api->destroy_queue(dev, queue);
	}
	if (sk_command_buffer_t_is_valid(cmd)) {
		api->destroy_command_buffer(dev, cmd);
	}
	if (renderer != NULL) {
		ui->renderer_destroy(renderer);
	}
	if (ui_ctx != NULL) {
		ui->context_destroy(ui_ctx);
	}
	if (fonts != NULL) {
		ui->font_system_destroy(fonts);
	}
	if (sk_buffer_t_is_valid(rb)) {
		api->destroy_buffer(dev, rb);
	}
	if (sk_framebuffer_t_is_valid(fb)) {
		api->destroy_framebuffer(dev, fb);
	}
	if (sk_render_pass_t_is_valid(pass)) {
		api->destroy_render_pass(dev, pass);
	}
	if (sk_texture_view_t_is_valid(color_view)) {
		api->destroy_texture_view(dev, color_view);
	}
	if (sk_texture_t_is_valid(color_tex)) {
		api->destroy_texture(dev, color_tex);
	}
	dxc->shutdown();
	ui->shutdown();
	if (sk_render_device_t_is_valid(dev)) {
		api->destroy(dev);
	}
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
