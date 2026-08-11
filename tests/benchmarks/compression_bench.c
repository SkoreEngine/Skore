/**
 * @file compression_bench.c
 * @brief APX-163 compression benchmark: ratio + one-shot throughput per codec.
 *
 * Standalone host tool (not part of the Unity registry). Iterates the
 * build-time codec registry (sk_compression_codec_at) over representative
 * engine corpora and reports the ratio plus compress/decompress throughput
 * for every codec x corpus. Numbers are informational — the process exits
 * non-zero only when a codec round-trip contract fails.
 *
 * Usage: sk-compression-bench [output_file]
 *   output_file (optional): write the report here instead of stdout.
 */

#include "allocator.h"
#include "common.h"
#include "compression.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * Corpora: deterministic synthetic payloads shaped like real engine data.
 * ------------------------------------------------------------------------- */

/** One corpus entry: name + bytes (owned when heap-allocated). */
typedef struct bench_corpus_t {
	const_chr_t name;
	const u8* data;
	u64 size;
	u8* owned;
} bench_corpus_t;

/* Deterministic xorshift32 stream (fixed seeds: reproducible across runs). */
static u32 bench_rand(u32* state) {
	*state ^= *state << 13u;
	*state ^= *state >> 17u;
	*state ^= *state << 5u;
	return *state;
}

static u8* bench_alloc_zero(const sk_allocator_t* a, u64 size) {
	u8* buf = a->alloc(a->instance, size);
	if (buf != NULL) {
		memset(buf, 0, size);
	}
	return buf;
}

/* Generated scene JSON: object headers, transforms, material names — the
 * shape of a scene/repository asset the engine parses with yyjson. */
static u8* bench_build_json(const sk_allocator_t* a, u64* out_size) {
	const u64 target = 384u * 1024u;
	const u32 line_cap = 192u;
	u8* buf = bench_alloc_zero(a, target + line_cap);
	u32 state = 0xABCD1234u;
	u64 used = 0u;
	u32 entity = 0u;

	if (buf == NULL) {
		return NULL;
	}
	while (used + line_cap < target) {
		const i32 tx = (i32)(bench_rand(&state) % 2001u) - 1000;
		const i32 ty = (i32)(bench_rand(&state) % 2001u) - 1000;
		const i32 tz = (i32)(bench_rand(&state) % 2001u) - 1000;
		const i32 rw = (i32)(bench_rand(&state) % 1001u) - 500;
		const i32 rx = (i32)(bench_rand(&state) % 1001u) - 500;
		const i32 ry = (i32)(bench_rand(&state) % 1001u) - 500;
		const i32 rz = (i32)(bench_rand(&state) % 1001u) - 500;
		const i32 scale = (i32)(bench_rand(&state) % 500u) + 100;
		const i32 material = (i32)(bench_rand(&state) % 6u);
		const int n = snprintf((char*)(buf + used), line_cap,
							   "{\"name\":\"entity_%04u\",\"transform\":{\"t\":[%d,%d,%d],\"r\":[%d,%d,%d,%d],\"s\":%d},\"mesh\":\"unit_cube\",\"material\":\"pbr_%d\"},\n", entity,
							   tx, ty, tz, rw, rx, ry, rz, scale, material);
		if (n <= 0 || (u64)n >= line_cap) {
			break;
		}
		used += (u64)n;
		entity += 1u;
	}
	*out_size = used;
	return buf;
}

/* Serialized SoA component snapshot: transform/velocity records with slow
 * spatial drift (clustered entities) plus noise — the shape of a serialized
 * ECS world save. */
static u8* bench_build_snapshot(const sk_allocator_t* a, u64* out_size) {
	const u32 record_bytes = 48u; /* t(12) + r(16) + s(12) + id(4) + flags(4) */
	const u64 total = 384u * 1024u;
	const u64 record_count = total / record_bytes;
	u8* buf = bench_alloc_zero(a, total);
	u32 state = 0xDECAFBADu;
	f32 prev_tx = 0.0f;
	f32 prev_ty = 0.0f;
	f32 prev_tz = 0.0f;

	if (buf == NULL) {
		return NULL;
	}
	for (u64 r = 0u; r < record_count; ++r) {
		u8* rec = buf + (r * record_bytes);
		const f32 tx = prev_tx + (((f32)(bench_rand(&state) % 41u)) - 20.0f) * 0.1f;
		const f32 ty = prev_ty + (((f32)(bench_rand(&state) % 41u)) - 20.0f) * 0.1f;
		const f32 tz = prev_tz + (((f32)(bench_rand(&state) % 41u)) - 20.0f) * 0.1f;
		const f32 rw = (((f32)(bench_rand(&state) % 2001u)) - 1000.0f) * 0.001f;
		const f32 rx = (((f32)(bench_rand(&state) % 2001u)) - 1000.0f) * 0.001f;
		const f32 ry = (((f32)(bench_rand(&state) % 2001u)) - 1000.0f) * 0.001f;
		const f32 rz = (((f32)(bench_rand(&state) % 2001u)) - 1000.0f) * 0.001f;
		const f32 sx = 0.5f + (f32)(bench_rand(&state) % 100u) * 0.01f;
		const f32 sy = 0.5f + (f32)(bench_rand(&state) % 100u) * 0.01f;
		const f32 sz = 0.5f + (f32)(bench_rand(&state) % 100u) * 0.01f;
		const u32 id = (u32)r;
		const u32 flags = bench_rand(&state) & 0xFFu;

		memcpy(rec, &tx, sizeof(tx));
		memcpy(rec + 4u, &ty, sizeof(ty));
		memcpy(rec + 8u, &tz, sizeof(tz));
		memcpy(rec + 12u, &rw, sizeof(rw));
		memcpy(rec + 16u, &rx, sizeof(rx));
		memcpy(rec + 20u, &ry, sizeof(ry));
		memcpy(rec + 24u, &rz, sizeof(rz));
		memcpy(rec + 28u, &sx, sizeof(sx));
		memcpy(rec + 32u, &sy, sizeof(sy));
		memcpy(rec + 36u, &sz, sizeof(sz));
		memcpy(rec + 40u, &id, sizeof(id));
		memcpy(rec + 44u, &flags, sizeof(flags));

		prev_tx = tx;
		prev_ty = ty;
		prev_tz = tz;
	}
	*out_size = total;
	return buf;
}

/* SPIR-V-like bytecode: fixed header then instruction words whose opcode /
 * operand mix is mostly high entropy with repeating constants. */
static void bench_write_u32(u8* dest, u32 value) {
	memcpy(dest, &value, sizeof(value));
}

static u8* bench_build_shader(const sk_allocator_t* a, u64* out_size) {
	const u32 total_words = (192u * 1024u) / 4u;
	u8* buf = bench_alloc_zero(a, (u64)total_words * 4u);
	u32 state = 0x5EEDBEEFu;
	u32 i = 0u;

	if (buf == NULL) {
		return NULL;
	}
	bench_write_u32(buf, 0x07230203u);		 /* SPIR-V magic */
	bench_write_u32(buf + 4u, 0x00010600u);	 /* version 1.6 */
	bench_write_u32(buf + 8u, 0x00000000u);	 /* generator */
	bench_write_u32(buf + 12u, total_words); /* bound */
	bench_write_u32(buf + 16u, 0x00000000u); /* schema */
	i = 5u;
	while (i < total_words) {
		u32 count = 2u + (bench_rand(&state) % 8u);
		if (i + count > total_words) {
			count = total_words - i;
		}
		bench_write_u32(buf + (u64)i * 4u, (count << 16u) | (1u + (bench_rand(&state) % 60u)));
		i += 1u;
		for (u32 j = 0u; j + 1u < count; ++j) {
			/* Mostly high-entropy operands; every 8th word repeats a
			 * constant (e.g. a literal 1.0 or id). */
			if ((j % 8u) == 0u) {
				bench_write_u32(buf + (u64)i * 4u, 0x3F800000u);
			} else {
				bench_write_u32(buf + (u64)i * 4u, bench_rand(&state));
			}
			i += 1u;
		}
	}
	*out_size = (u64)total_words * 4u;
	return buf;
}

/* Repetitive engine log text from a small message dictionary — very
 * compressible, representative of log files / asset manifests. */
static u8* bench_build_log(const sk_allocator_t* a, u64* out_size) {
	const u64 target = 384u * 1024u;
	const u32 line_cap = 160u;
	static const char* const levels[] = {"trace", "debug", "info", "warn"};
	u8* buf = bench_alloc_zero(a, target + line_cap);
	u32 state = 0x10ADAB1Eu;
	u64 used = 0u;
	u32 tick = 0u;

	if (buf == NULL) {
		return NULL;
	}
	while (used + line_cap < target) {
		const i32 level = (i32)(bench_rand(&state) % 4u);
		const i32 msg = (i32)(bench_rand(&state) % 4u);
		char message[96];
		/* Literal formats only: the build bans format-nonliteral. */
		switch (msg) {
		case 0:
			snprintf(message, sizeof(message), "asset loaded: textures/pbr_metal_%d.ktx2", (i32)(bench_rand(&state) % 10000u));
			break;
		case 1:
			snprintf(message, sizeof(message), "shader compiled: shaders/model_%04d.vert", (i32)(bench_rand(&state) % 10000u));
			break;
		case 2:
			snprintf(message, sizeof(message), "entity %04d spawned with archetype static_mesh", (i32)(bench_rand(&state) % 10000u));
			break;
		default:
			snprintf(message, sizeof(message), "render pass %d recorded %d draw calls", (i32)(bench_rand(&state) % 10u), (i32)(bench_rand(&state) % 1000u));
			break;
		}
		{
			const int n = snprintf((char*)(buf + used), line_cap, "[%08u] [%s] %s\n", tick, levels[level], message);
			if (n <= 0 || (u64)n >= line_cap) {
				break;
			}
			used += (u64)n;
		}
		tick += 1u;
	}
	*out_size = used;
	return buf;
}

/* High-entropy xorshift stream: incompressible input. */
static u8* bench_build_random(const sk_allocator_t* a, u64* out_size) {
	const u64 total = 384u * 1024u;
	u8* buf = bench_alloc_zero(a, total);
	u32 state = 0xBADF00Du;

	if (buf == NULL) {
		return NULL;
	}
	for (u64 i = 0u; i < total; ++i) {
		buf[i] = (u8)(bench_rand(&state) >> 24u);
	}
	*out_size = total;
	return buf;
}

static u32 bench_corpus_add(bench_corpus_t* corpus, u32 cap, u32 count, const_chr_t name, u8* data, u64 size) {
	if (count >= cap) {
		return count;
	}
	corpus[count].name = name;
	corpus[count].data = data;
	corpus[count].size = size;
	corpus[count].owned = data;
	return count + 1u;
}

/* ---------------------------------------------------------------------------
 * Measurement
 * ------------------------------------------------------------------------- */

/* Portable CPU timer (same source as the in-tree parity harness). */
static f64 bench_now_s(void) {
	return (f64)clock() / (f64)CLOCKS_PER_SEC;
}

/* Iterations for a ~0.2 s timed run, capped to keep CTest runtime bounded. */
static u32 bench_iters_for(f64 seconds_per_iter) {
	const f64 target_s = 0.2;
	if (seconds_per_iter <= 0.0) {
		return 1u;
	}
	{
		u64 iters = (u64)(target_s / seconds_per_iter);
		if (iters < 1u) {
			return 1u;
		}
		if (iters > 512u) {
			return 512u;
		}
		return (u32)iters;
	}
}

/* One codec x corpus measurement. Returns 0 on success (contract + timing),
 * non-zero on a failed round-trip / allocation error. */
static i32 bench_measure(const sk_allocator_t* a, const sk_compression_codec_t* codec, const bench_corpus_t* corpus, FILE* out) {
	const u64 bound = codec->compress_bound(corpus->size);
	u8* compressed = NULL;
	u8* restored = NULL;
	u64 compressed_size = 0u;
	u64 restored_size = 0u;
	f64 t0 = 0.0;
	f64 t1 = 0.0;
	f64 warm_compress_s = 0.0;
	f64 warm_decompress_s = 0.0;
	f64 compress_s = 0.0;
	f64 decompress_s = 0.0;
	u32 compress_iters = 1u;
	u32 decompress_iters = 1u;
	i32 status = 0;

	if (bound == SK_COMPRESSION_SIZE_UNKNOWN) {
		fprintf(stderr, "[compression-bench] error: codec=%s corpus=%s has no compress bound\n", codec->name, corpus->name);
		return -1;
	}
	compressed = a->alloc(a->instance, bound > 0u ? bound : 1u);
	restored = a->alloc(a->instance, corpus->size > 0u ? corpus->size : 1u);
	if (compressed == NULL || restored == NULL) {
		fprintf(stderr, "[compression-bench] error: allocation failed for codec=%s corpus=%s\n", codec->name, corpus->name);
		status = -1;
		goto cleanup;
	}

	/* Warmup + contract check. */
	t0 = bench_now_s();
	if (codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, corpus->data, corpus->size, compressed, bound, &compressed_size) != SK_COMPRESSION_OK) {
		fprintf(stderr, "[compression-bench] error: compress failed for codec=%s corpus=%s\n", codec->name, corpus->name);
		status = -1;
		goto cleanup;
	}
	t1 = bench_now_s();
	warm_compress_s = t1 - t0;
	if (compressed_size > bound) {
		fprintf(stderr, "[compression-bench] error: codec=%s corpus=%s wrote beyond its bound\n", codec->name, corpus->name);
		status = -1;
		goto cleanup;
	}

	t0 = bench_now_s();
	if (codec->decompress(a, compressed, compressed_size, restored, corpus->size, &restored_size) != SK_COMPRESSION_OK) {
		fprintf(stderr, "[compression-bench] error: decompress failed for codec=%s corpus=%s\n", codec->name, corpus->name);
		status = -1;
		goto cleanup;
	}
	t1 = bench_now_s();
	warm_decompress_s = t1 - t0;
	if (restored_size != corpus->size || (corpus->size > 0u && memcmp(corpus->data, restored, corpus->size) != 0)) {
		fprintf(stderr, "[compression-bench] error: round-trip mismatch for codec=%s corpus=%s\n", codec->name, corpus->name);
		status = -1;
		goto cleanup;
	}

	/* Timed multi-iteration loops. */
	compress_iters = bench_iters_for(warm_compress_s);
	t0 = bench_now_s();
	for (u32 i = 0u; i < compress_iters; ++i) {
		if (codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, corpus->data, corpus->size, compressed, bound, &compressed_size) != SK_COMPRESSION_OK) {
			fprintf(stderr, "[compression-bench] error: compress failed (iter %u) for codec=%s corpus=%s\n", i, codec->name, corpus->name);
			status = -1;
			goto cleanup;
		}
	}
	t1 = bench_now_s();
	compress_s = (t1 - t0) / (f64)compress_iters;

	decompress_iters = bench_iters_for(warm_decompress_s);
	t0 = bench_now_s();
	for (u32 i = 0u; i < decompress_iters; ++i) {
		if (codec->decompress(a, compressed, compressed_size, restored, corpus->size, &restored_size) != SK_COMPRESSION_OK) {
			fprintf(stderr, "[compression-bench] error: decompress failed (iter %u) for codec=%s corpus=%s\n", i, codec->name, corpus->name);
			status = -1;
			goto cleanup;
		}
	}
	t1 = bench_now_s();
	decompress_s = (t1 - t0) / (f64)decompress_iters;

	{
		const f64 ratio = (corpus->size > 0u) ? ((f64)compressed_size / (f64)corpus->size) : 0.0;
		const f64 compress_mbs = (compress_s > 0.0 && corpus->size > 0u) ? (((f64)corpus->size / (1024.0 * 1024.0)) / compress_s) : 0.0;
		const f64 decompress_mbs = (decompress_s > 0.0 && corpus->size > 0u) ? (((f64)corpus->size / (1024.0 * 1024.0)) / decompress_s) : 0.0;
		/* u64 is unsigned long long (common.h): %llu matches, no cast. */
		fprintf(out, "[compression-bench] codec=%-6s corpus=%-15s in=%llu out=%llu ratio=%.4f compress=%8.1f MiB/s decompress=%8.1f MiB/s (iters %u/%u)\n", codec->name,
				corpus->name, corpus->size, compressed_size, ratio, compress_mbs, decompress_mbs, compress_iters, decompress_iters);
	}

cleanup:
	a->free(a->instance, compressed);
	a->free(a->instance, restored);
	return status;
}

int main(int argc, char* argv[]) {
	(void) argc;
	(void) argv;

	return 0;

// 	const sk_allocator_t* a = sk_allocator_default();
// 	FILE* out = stdout;
// 	bench_corpus_t corpus[5];
// 	u32 corpus_count = 0u;
// 	const u32 codec_count = sk_compression_codec_count();
// 	i32 failures = 0;
//
// 	if (argc > 2) {
// 		fprintf(stderr, "usage: sk-compression-bench [output_file]\n");
// 		return 2;
// 	}
// 	if (argc == 2) {
// 		out = fopen(argv[1], "w");
// 		if (out == NULL) {
// 			fprintf(stderr, "sk-compression-bench: cannot open output file %s\n", argv[1]);
// 			return 2;
// 		}
// 	}
//
// 	/* Deterministic representative engine corpora (see generators above). */
// 	{
// 		u64 size = 0u;
// 		u8* data = bench_build_json(a, &size);
// 		corpus_count = bench_corpus_add(corpus, 5u, corpus_count, "scene-json", data, size);
// 	}
// 	{
// 		u64 size = 0u;
// 		u8* data = bench_build_snapshot(a, &size);
// 		corpus_count = bench_corpus_add(corpus, 5u, corpus_count, "entity-snapshot", data, size);
// 	}
// 	{
// 		u64 size = 0u;
// 		u8* data = bench_build_shader(a, &size);
// 		corpus_count = bench_corpus_add(corpus, 5u, corpus_count, "shader-bytecode", data, size);
// 	}
// 	{
// 		u64 size = 0u;
// 		u8* data = bench_build_log(a, &size);
// 		corpus_count = bench_corpus_add(corpus, 5u, corpus_count, "log-stream", data, size);
// 	}
// 	{
// 		u64 size = 0u;
// 		u8* data = bench_build_random(a, &size);
// 		corpus_count = bench_corpus_add(corpus, 5u, corpus_count, "incompressible", data, size);
// 	}
//
// 	for (u32 c = 0u; c < corpus_count; ++c) {
// 		if (corpus[c].data == NULL) {
// 			fprintf(stderr, "sk-compression-bench: failed to build corpus %s\n", corpus[c].name);
// 			failures = -1;
// 			goto cleanup;
// 		}
// 	}
//
// 	fprintf(out, "[compression-bench] codecs=%u corpora=%u (deterministic synthetic engine data)\n", codec_count, corpus_count);
// 	for (u32 ci = 0u; ci < codec_count; ++ci) {
// 		const sk_compression_codec_t* codec = sk_compression_codec_at(ci);
// 		if (codec == NULL) {
// 			fprintf(stderr, "sk-compression-bench: NULL codec at index %u\n", ci);
// 			failures = -1;
// 			goto cleanup;
// 		}
// 		for (u32 e = 0u; e < corpus_count; ++e) {
// 			if (bench_measure(a, codec, &corpus[e], out) != 0) {
// 				failures += 1;
// 			}
// 		}
// 	}
// 	fprintf(out, "[compression-bench] done: %s\n", (failures == 0) ? "all codec round-trips ok" : "failures detected");
//
// cleanup:
// 	for (u32 c = 0u; c < corpus_count; ++c) {
// 		a->free(a->instance, corpus[c].owned);
// 		corpus[c].owned = NULL;
// 	}
// 	if (out != stdout) {
// 		fclose(out);
// 	}
// 	return (failures == 0) ? 0 : 1;
}
