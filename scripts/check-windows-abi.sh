#!/usr/bin/env bash
# Catch Windows LLP64 type issues on a Linux host (no MSVC required).
#
# Why: Linux/macOS use LP64 (uintptr_t is often unsigned long). Windows x64
# uses LLP64 (uintptr_t is unsigned long long). Casts / printf helpers that
# look fine on Linux can trip readability-redundant-casting (and similar)
# only under a Windows data model — the common agent↔CI loop.
#
# This runs clang-tidy on first-party sources with
#   --target=x86_64-w64-mingw32
# so pointer/size types match Windows, using MinGW headers for <windows.h>.
#
# Prerequisites (Debian/Ubuntu):
#   sudo apt-get install -y mingw-w64 clang-tidy
# Fedora/RHEL:
#   sudo dnf install -y mingw64-gcc mingw64-headers clang-tools-extra
#
# Usage:
#   ./scripts/check-windows-abi.sh
#   ./scripts/check-windows-abi.sh core/stacktrace.c
#   JOBS=8 ./scripts/check-windows-abi.sh
#
# Env:
#   CLANG_TIDY              clang-tidy binary (default: clang-tidy)
#   JOBS                    parallel workers (default: nproc or 4)
#   SK_WINDOWS_ABI_TARGET   clang triple (default: x86_64-w64-mingw32)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
TARGET="${SK_WINDOWS_ABI_TARGET:-x86_64-w64-mingw32}"
JOBS="${JOBS:-}"

if [[ -z "${JOBS}" ]]; then
	if command -v nproc >/dev/null 2>&1; then
		JOBS="$(nproc)"
	else
		JOBS=4
	fi
fi

case "${1:-}" in
	-h | --help )
		sed -n '2,28p' "$0"
		exit 0
		;;
esac

# ---- tools ----------------------------------------------------------------

missing=0
if ! command -v "${CLANG_TIDY}" >/dev/null 2>&1; then
	echo "error: clang-tidy not found (looked for: ${CLANG_TIDY})" >&2
	missing=1
fi
if ! command -v "${TARGET}-gcc" >/dev/null 2>&1; then
	echo "error: MinGW cross-compiler not found (${TARGET}-gcc)" >&2
	missing=1
fi
if ((missing)); then
	echo >&2
	echo "Install on Debian/Ubuntu:" >&2
	echo "  sudo apt-get install -y mingw-w64 clang-tidy" >&2
	echo "Install on Fedora/RHEL:" >&2
	echo "  sudo dnf install -y mingw64-gcc mingw64-headers clang-tools-extra" >&2
	exit 1
fi

# ---- sysroot --------------------------------------------------------------

SYSROOT="$("${TARGET}-gcc" -print-sysroot 2>/dev/null || true)"
if [[ -z "${SYSROOT}" || ! -d "${SYSROOT}/include" ]]; then
	for cand in "/usr/${TARGET}" "/usr/x86_64-w64-mingw32"; do
		if [[ -d "${cand}/include" ]]; then
			SYSROOT="${cand}"
			break
		fi
	done
fi
if [[ -z "${SYSROOT}" || ! -d "${SYSROOT}/include" ]]; then
	echo "error: could not locate MinGW sysroot (need ${TARGET} headers under include/)" >&2
	exit 1
fi

GCC_INCLUDE="$("${TARGET}-gcc" -print-file-name=include 2>/dev/null || true)"
if [[ ! -d "${GCC_INCLUDE}" ]]; then
	GCC_INCLUDE=""
fi

# ---- helpers --------------------------------------------------------------

# Unix-only TUs are never compiled on Windows; skip them under a Windows triple.
is_unix_only() {
	case "$(basename "$1")" in
		*_unix.c | *_unix.cpp | *_posix.c | *_linux.c | *_apple.c | *_cocoa.c | *_cocoa.m | *_macos.c | *_macos.m )
			return 0
			;;
	esac
	return 1
}

build_extra_args() {
	EXTRA_ARGS=(
		"--extra-arg=--target=${TARGET}"
		"--extra-arg=--sysroot=${SYSROOT}"
		"--extra-arg=-fms-extensions"
		"--extra-arg=-D_CRT_SECURE_NO_WARNINGS"
		"--extra-arg=-DSK_ENGINE_VERSION=\"0.0.1\""
		"--extra-arg=-DSK_VERSION=\"0.0.1-windows-abi\""
		"--extra-arg=-I${ROOT}/core"
		"--extra-arg=-I${ROOT}/app"
		"--extra-arg=-I${ROOT}/player"
		"--extra-arg=-I${ROOT}/editor"
		"--extra-arg=-I${ROOT}/tests"
		"--extra-arg=-I${ROOT}/thirdparty/mimalloc/include"
		"--extra-arg=-I${ROOT}/thirdparty/unity/src"
		"--extra-arg=-I${ROOT}/thirdparty/glfw/include"
		"--extra-arg=-I${ROOT}/thirdparty/vulkan/include"
		"--extra-arg=-I${ROOT}/thirdparty/volk/src"
		"--extra-arg=-I${ROOT}/thirdparty/vma/include"
		"--extra-arg=-I${ROOT}/thirdparty/nativefiledialog/src/include"
		"--extra-arg=-I${ROOT}/thirdparty/dxc/include"
		"--extra-arg=-Wno-unknown-warning-option"
		"--extra-arg=-isystem${SYSROOT}/include"
		"--extra-arg=-std=c11"
	)
	if [[ -n "${GCC_INCLUDE}" ]]; then
		EXTRA_ARGS+=("--extra-arg=-isystem${GCC_INCLUDE}")
	fi
	local pdir
	while IFS= read -r -d '' pdir; do
		EXTRA_ARGS+=("--extra-arg=-I${pdir}")
	done < <(find "${ROOT}/plugins" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null || true)
}

tidy_one() {
	local f="$1"
	local lang=()
	case "${f}" in
		*.cpp | *.cc | *.cxx ) lang=("--extra-arg=-std=c++20") ;;
	esac
	"${CLANG_TIDY}" \
		"--config-file=${ROOT}/.clang-tidy" \
		"-warnings-as-errors=*" \
		"--quiet" \
		"--header-filter=${ROOT}/(core|app|player|editor|plugins|tests)/.*" \
		"${EXTRA_ARGS[@]}" \
		"${lang[@]}" \
		"${f}" --
}

collect_default_files() {
	local -a prune_dirs=(
		thirdparty
		build
		build-mingw-abi
		cmake-build-debug
		cmake-build-release
		cmake-build-relwithdebinfo
		cmake-build-minsizerel
		.git
	)
	local -a prune_args=()
	local d
	for d in "${prune_dirs[@]}"; do
		if ((${#prune_args[@]})); then
			prune_args+=(-o)
		fi
		prune_args+=(-path "./${d}")
	done
	find . \
		\( "${prune_args[@]}" \) -prune -o \
		-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print |
		sed 's|^\./||' |
		sort
}

# ---- single-file worker (parallel xargs re-enters the script) -------------

if [[ "${SK_WINDOWS_ABI_WORKER:-0}" == "1" ]]; then
	if (($# != 1)); then
		echo "error: worker mode expects exactly one file" >&2
		exit 2
	fi
	build_extra_args
	tidy_one "$1"
	exit $?
fi

# ---- file list ------------------------------------------------------------

if (($# > 0)); then
	files=("$@")
else
	mapfile -t files < <(collect_default_files)
fi

filtered=()
for f in "${files[@]}"; do
	if [[ ! -f "${f}" ]]; then
		echo "error: not a file: ${f}" >&2
		exit 2
	fi
	case "${f}" in
		thirdparty/* | */thirdparty/* ) continue ;;
	esac
	if is_unix_only "${f}"; then
		continue
	fi
	filtered+=("${f}")
done
files=("${filtered[@]}")

if ((${#files[@]} == 0)); then
	echo "no first-party sources to check"
	exit 0
fi

build_extra_args

echo "clang-tidy: $(${CLANG_TIDY} --version 2>/dev/null | head -n1)"
echo "target:     ${TARGET}"
echo "sysroot:    ${SYSROOT}"
echo "files:      ${#files[@]}"
echo "jobs:       ${JOBS}"
echo "config:     ${ROOT}/.clang-tidy"
echo

log_dir="$(mktemp -d "${TMPDIR:-/tmp}/sk-windows-abi.XXXXXX")"
cleanup() { rm -rf "${log_dir}"; }
trap cleanup EXIT

self="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
failed=0

# Always collect per-file logs; run workers in parallel via xargs.
printf '%s\0' "${files[@]}" |
	xargs -0 -P "${JOBS}" -I{} bash -c '
		f="$1"
		self="$2"
		log_dir="$3"
		clang_tidy="$4"
		target="$5"
		log="${log_dir}/$(echo "${f}" | tr "/\\" "__").log"
		if env SK_WINDOWS_ABI_WORKER=1 \
			CLANG_TIDY="${clang_tidy}" \
			SK_WINDOWS_ABI_TARGET="${target}" \
			bash "${self}" "${f}" >"${log}" 2>&1; then
			: > "${log_dir}/$(echo "${f}" | tr "/\\" "__").ok"
		else
			: > "${log_dir}/$(echo "${f}" | tr "/\\" "__").fail"
		fi
	' _ {} "${self}" "${log_dir}" "${CLANG_TIDY}" "${TARGET}"

for f in "${files[@]}"; do
	stem="$(echo "${f}" | tr '/\\' '__')"
	if [[ -f "${log_dir}/${stem}.fail" ]]; then
		failed=$((failed + 1))
		echo "==> FAIL ${f}"
		cat "${log_dir}/${stem}.log" || true
		echo
	fi
done

echo "checked: ${#files[@]}  failed: ${failed}"
if ((failed > 0)); then
	echo "Windows ABI check failed (LLP64 / MinGW clang-tidy)." >&2
	exit 1
fi
echo "Windows ABI check passed"
exit 0
