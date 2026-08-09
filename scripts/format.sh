#!/usr/bin/env bash
# Run clang-format on all project C/C++/ObjC sources.
# Skips thirdparty/ and build/ (and common out-of-tree build dirs).
#
# Usage:
#   ./scripts/format.sh          # format in place
#   ./scripts/format.sh --check  # report files that need formatting (exit 1 if any)
#   ./scripts/format.sh --dry-run

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
	echo "error: clang-format not found in PATH" >&2
	exit 1
fi

MODE="format"
case "${1:-}" in
	"" ) MODE="format" ;;
	--check | -n | --dry-run ) MODE="check" ;;
	-h | --help )
		sed -n '2,12p' "$0"
		exit 0
		;;
	* )
		echo "usage: $0 [--check|--dry-run]" >&2
		exit 2
		;;
esac

# Source extensions used in this tree (and future C++/ObjC).
EXTENSIONS=(c h cc cxx cpp hpp hxx m mm)

# Directories to skip entirely (relative to repo root).
PRUNE_DIRS=(
	thirdparty
	build
	cmake-build-debug
	cmake-build-release
	cmake-build-relwithdebinfo
	cmake-build-minsizerel
	.git
)

# Build find prune expression: \( -path '*/thirdparty' -o -path '*/build' ... \)
# Prune matches at any depth so nested vendored/build dirs (e.g.
# skore-ecs-benchmark/thirdparty) are skipped too.
prune_args=()
for d in "${PRUNE_DIRS[@]}"; do
	if ((${#prune_args[@]})); then
		prune_args+=(-o)
	fi
	prune_args+=(-path "*/${d}")
done

# Build -name filters for extensions.
name_args=()
for ext in "${EXTENSIONS[@]}"; do
	if ((${#name_args[@]})); then
		name_args+=(-o)
	fi
	name_args+=(-name "*.${ext}")
done

mapfile -t files < <(
	find . \
		\( "${prune_args[@]}" \) -prune -o \
		-type f \( "${name_args[@]}" \) -print |
		sed 's|^\./||' |
		sort
)

if ((${#files[@]} == 0)); then
	echo "no source files found"
	exit 0
fi

echo "clang-format $(clang-format --version | head -n1)"
echo "root: $ROOT"
echo "files: ${#files[@]}"
echo "mode: $MODE"
echo

if [[ "$MODE" == "check" ]]; then
	bad=()
	for f in "${files[@]}"; do
		# Compare on-disk file to what clang-format would emit.
		if ! clang-format "$f" | cmp -s "$f" -; then
			bad+=("$f")
		fi
	done
	if ((${#bad[@]})); then
		echo "need formatting (${#bad[@]}):"
		printf '  %s\n' "${bad[@]}"
		exit 1
	fi
	echo "all files already formatted"
	exit 0
fi

# In-place format.
clang-format -i "${files[@]}"
echo "formatted ${#files[@]} file(s)"
