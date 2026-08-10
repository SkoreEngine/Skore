# Compression benchmark (APX-163)

`sk-compression-bench` measures the one-shot compression surface of every
registered v2 codec over representative engine corpora. It reports the ratio
(compressed / uncompressed bytes) and compress / decompress throughput in
MiB/s per codec × corpus. Numbers are informational; the process exits
non-zero only if a codec round-trip contract fails.

## Running

```bash
cmake --build build --target sk-compression-bench
./build/bin/sk-compression-bench            # stdout report
./build/bin/sk-compression-bench report.txt # or write to a file
```

The target is registered with CTest under the `benchmark` label, so
`ctest --test-dir build -L benchmark` runs it as part of the suite.

## Corpora

Deterministic synthetic payloads shaped like real engine data (fixed seeds;
identical across runs and platforms):

| Corpus | Size | Shape |
|--------|------|-------|
| `scene-json` | ~384 KiB | Generated scene JSON (yyjson-style repository asset) |
| `entity-snapshot` | 384 KiB | Serialized SoA component snapshot (ECS world save) |
| `shader-bytecode` | 192 KiB | SPIR-V-like instruction stream (mostly high-entropy) |
| `log-stream` | ~384 KiB | Repetitive engine log text (very compressible) |
| `incompressible` | 384 KiB | Deterministic xorshift noise |

Each codec × corpus is compressed once for ratio, then timed over a
self-calibrating iteration count (~0.2 s per direction).

## Results (recorded 2026-08-11)

Linux x86-64, GCC 13, Release build (`sk-compression-bench` from `build-rel`),
all codecs enabled (`none`, `zstd`, `lz4`, `zlib`).

| Codec | Corpus | Ratio | Compress (MiB/s) | Decompress (MiB/s) |
|-------|--------|------:|-----------------:|-------------------:|
| none | scene-json | 1.0000 | 90714 | 93045 |
| none | entity-snapshot | 1.0000 | 89261 | 95570 |
| none | shader-bytecode | 1.0000 | 87993 | 84656 |
| none | log-stream | 1.0000 | 92952 | 100233 |
| none | incompressible | 1.0000 | 90310 | 101641 |
| zstd | scene-json | 0.1912 | 403.1 | 1838.0 |
| zstd | entity-snapshot | 0.7098 | 137.2 | 850.8 |
| zstd | shader-bytecode | 0.7504 | 142.7 | 2813.2 |
| zstd | log-stream | 0.1408 | 554.7 | 2541.5 |
| zstd | incompressible | 1.0000 | 8013.8 | 61029.9 |
| lz4 | scene-json | 0.3110 | 1034.0 | 8079.1 |
| lz4 | entity-snapshot | 0.9017 | 672.3 | 10064.5 |
| lz4 | shader-bytecode | 0.8257 | 801.7 | 7614.8 |
| lz4 | log-stream | 0.2419 | 1271.1 | 7442.1 |
| lz4 | incompressible | 1.0039 | 33350.7 | 87272.7 |
| zlib | scene-json | 0.1843 | 35.3 | 644.9 |
| zlib | entity-snapshot | 0.5966 | 26.0 | 299.0 |
| zlib | shader-bytecode | 0.7595 | 21.4 | 404.0 |
| zlib | log-stream | 0.1205 | 52.0 | 793.4 |
| zlib | incompressible | 1.0002 | 39.7 | 3667.8 |

Readings: `zstd` and `zlib` produce the best ratios on text-like data
(`log-stream` ~0.12–0.14, `scene-json` ~0.18–0.19); `lz4` trades ratio for
throughput; `zlib` compress is the slowest of the three; `none` is the raw
identity baseline. Ratios above 1.0 on incompressible data are frame
overhead (zstd header / LZ4 and zlib size prefixes).
