#!/usr/bin/env bash
# Regenerate the frozen APX-176 cross-engine compatibility vectors.
#
# Compiles scripts/gen-compression-vectors.c against the vendored reference
# libraries (zstd, lz4, miniz -- the same sources the C++ engine on `main`
# links for zstd, and the reference implementations for lz4/zlib) and prints
# the C block to paste into foundation/compression.c (APX-176 test section).
#
# Usage: ./scripts/gen-compression-vectors.sh [output-file]
#   Without an argument the block goes to stdout.
#   With an argument the block replaces the APX-176 section in that file
#   between the BEGIN/END markers (default: foundation/compression.c).
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

GEN_BIN="$(mktemp "${TMPDIR:-/tmp}/gen-compression-vectors.XXXXXX")"
trap 'rm -f "$GEN_BIN"' EXIT

gcc -DZSTD_DISABLE_ASM \
    -I "$ROOT/thirdparty/zstd/src" \
    -I "$ROOT/thirdparty/lz4/src" \
    -I "$ROOT/thirdparty/miniz/src" \
    "$ROOT/scripts/gen-compression-vectors.c" \
    "$ROOT"/thirdparty/zstd/src/compress/*.c \
    "$ROOT"/thirdparty/zstd/src/decompress/*.c \
    "$ROOT"/thirdparty/zstd/src/common/*.c \
    "$ROOT"/thirdparty/lz4/src/lz4.c \
    "$ROOT"/thirdparty/miniz/src/miniz.c \
    -o "$GEN_BIN"

OUTPUT="${1:-}"

if [ -z "$OUTPUT" ]; then
    "$GEN_BIN"
    exit 0
fi

BEGIN="/* BEGIN APX-176 COMPATIBILITY VECTORS */"
END="/* END APX-176 COMPATIBILITY VECTORS */"

if ! grep -qF "$BEGIN" "$OUTPUT"; then
    echo "error: $BEGIN marker not found in $OUTPUT" >&2
    exit 1
fi

TMP="$(mktemp "${TMPDIR:-/tmp}/compression-vectors.XXXXXX")"
trap 'rm -f "$GEN_BIN" "$TMP"' EXIT

{
    grep -F -B9999 -m1 "$BEGIN" "$OUTPUT"
    "$GEN_BIN"
    grep -F -A9999 -m1 "$END" "$OUTPUT"
} > "$TMP"

mv "$TMP" "$OUTPUT"
if command -v clang-format >/dev/null 2>&1; then
    clang-format -i "$OUTPUT"
fi
echo "updated $OUTPUT" >&2
