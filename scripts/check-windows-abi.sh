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
# so pointer/size types match Windows, using MinGW CRT/Windows headers and
# *Clang's* resource-dir for compiler intrinsics (never GCC's xmmintrin —
# those conflict with clang builtins and explode the parse).
#
# Prerequisites (Debian/Ubuntu):
#   sudo apt-get install -y mingw-w64 clang clang-tidy
# Fedora/RHEL:
#   sudo dnf install -y mingw64-gcc mingw64-headers clang-tools-extra clang
#
# Usage:
#   ./scripts/check-windows-abi.sh
#   ./scripts/check-windows-abi.sh core/stacktrace.c
#   JOBS=8 ./scripts/check-windows-abi.sh
#
# Env:
#   CLANG_TIDY              clang-tidy binary (default: clang-tidy)
#   CLANG                   clang binary for -print-resource-dir (default: clang)
#   JOBS                    parallel workers (default: nproc or 4)
#   SK_WINDOWS_ABI_TARGET   clang triple (default: x86_64-w64-mingw32)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
CLANG="${CLANG:-clang}"
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
		sed -n '2,32p' "$0"
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
	echo "  sudo apt-get install -y mingw-w64 clang clang-tidy" >&2
	echo "Install on Fedora/RHEL:" >&2
	echo "  sudo dnf install -y mingw64-gcc mingw64-headers clang-tools-extra clang" >&2
	exit 1
fi

# ---- sysroot + clang resource dir -----------------------------------------

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

# Clang's own headers for stddef / xmmintrin / builtins. Do NOT add GCC's
# lib/gcc/.../include (print-file-name=include) — clang + those intrinsics
# headers produce thousands of clang-diagnostic-error noise and fail every
# TU that pulls <windows.h>.
#
# resource-dir major version MUST match clang-tidy. A common agent setup has
# PATH clang-tidy from a pip wheel (LLVM N) while system clang is M — mismatched
# mmintrin.h then yields "__m64 ... integer type 'int' of different size" parse
# failures on every windows.h TU. Prefer same-major clang / llvm layout first.
TIDY_MAJOR="$("${CLANG_TIDY}" --version 2>/dev/null | sed -n 's/.*[Vv]ersion \([0-9][0-9]*\).*/\1/p' | head -n1 || true)"

RESOURCE_DIR=""
resolve_resource_dir() {
	local cand clang_bin
	# 1) Explicit CLANG if set and available.
	if command -v "${CLANG}" >/dev/null 2>&1; then
		cand="$("${CLANG}" -print-resource-dir 2>/dev/null || true)"
		if [[ -n "${cand}" && -d "${cand}/include" ]]; then
			# Accept only when major matches tidy (or tidy version unknown).
			if [[ -z "${TIDY_MAJOR}" || "${cand}" == *"/clang/${TIDY_MAJOR}"* || "${cand}" == *"/clang/${TIDY_MAJOR}."* ]]; then
				RESOURCE_DIR="${cand}"
				return 0
			fi
		fi
	fi
	# 2) clang binary with the same major as clang-tidy.
	if [[ -n "${TIDY_MAJOR}" ]]; then
		for clang_bin in "clang-${TIDY_MAJOR}" "clang++-${TIDY_MAJOR}" "/usr/lib/llvm-${TIDY_MAJOR}/bin/clang"; do
			if command -v "${clang_bin}" >/dev/null 2>&1 || [[ -x "${clang_bin}" ]]; then
				cand="$("${clang_bin}" -print-resource-dir 2>/dev/null || true)"
				if [[ -n "${cand}" && -d "${cand}/include" ]]; then
					RESOURCE_DIR="${cand}"
					return 0
				fi
			fi
		done
		# 3) Distro / pip clang_tidy data layouts for that major.
		for cand in \
			"/usr/lib/llvm-${TIDY_MAJOR}/lib/clang/${TIDY_MAJOR}" \
			"/usr/lib/clang/${TIDY_MAJOR}" \
			"/usr/lib/llvm-${TIDY_MAJOR}/lib/clang/"* \
			"/usr/lib/clang/"*; do
			if [[ -d "${cand}/include" ]]; then
				case "${cand}" in
					*/clang/"${TIDY_MAJOR}" | */clang/"${TIDY_MAJOR}".* )
						RESOURCE_DIR="${cand}"
						return 0
						;;
				esac
			fi
		done
		# pip clang-tidy wheel ships headers next to the entrypoint.
		if command -v python3 >/dev/null 2>&1; then
			cand="$(
				python3 - "${TIDY_MAJOR}" <<'PY' 2>/dev/null || true
import sys
from pathlib import Path
major = sys.argv[1]
try:
    import clang_tidy
except Exception:
    sys.exit(1)
root = Path(clang_tidy.__file__).resolve().parent / "data" / "lib" / "clang"
for child in sorted(root.glob(major + "*")) if root.is_dir() else []:
    if (child / "include").is_dir():
        print(child)
        sys.exit(0)
sys.exit(1)
PY
			)"
			if [[ -n "${cand}" && -d "${cand}/include" ]]; then
				RESOURCE_DIR="${cand}"
				return 0
			fi
		fi
	fi
	# 4) Last resort: any clang on PATH, then glob (may mismatch — best-effort).
	if command -v clang >/dev/null 2>&1; then
		cand="$(clang -print-resource-dir 2>/dev/null || true)"
		if [[ -n "${cand}" && -d "${cand}/include" ]]; then
			RESOURCE_DIR="${cand}"
			return 0
		fi
	fi
	for cand in /usr/lib/llvm-*/lib/clang/* /usr/lib/clang/*; do
		if [[ -d "${cand}/include" ]]; then
			RESOURCE_DIR="${cand}"
			return 0
		fi
	done
	return 1
}
if ! resolve_resource_dir; then
	echo "error: could not locate clang resource-dir (need clang headers matching clang-tidy${TIDY_MAJOR:+ major ${TIDY_MAJOR}})" >&2
	echo "  Debian/Ubuntu: sudo apt-get install -y clang" >&2
	echo "  Or set CLANG to a binary whose -print-resource-dir matches clang-tidy." >&2
	exit 1
fi
# Soft-warn when we could not prove a major match (still try; parse may fail).
if [[ -n "${TIDY_MAJOR}" && "${RESOURCE_DIR}" != *"/clang/${TIDY_MAJOR}"* && "${RESOURCE_DIR}" != *"/clang/${TIDY_MAJOR}."* ]]; then
	echo "warning: clang resource-dir major may not match clang-tidy ${TIDY_MAJOR}:" >&2
	echo "  resource-dir=${RESOURCE_DIR}" >&2
	echo "  (mmintrin / windows.h TUs often fail on mismatched LLVM majors)" >&2
fi

# Optional MinGW libstdc++ for first-party .cpp (e.g. thin VMA TU).
CXX_INC_ROOT=""
if command -v "${TARGET}-g++" >/dev/null 2>&1; then
	_gxx_ver="$("${TARGET}-g++" -dumpversion 2>/dev/null || true)"
	for cand in \
		"/usr/lib/gcc/${TARGET}/${_gxx_ver}/include/c++" \
		"/usr/lib/gcc/${TARGET}/${_gxx_ver}-win32/include/c++" \
		"/usr/lib/gcc/${TARGET}/${_gxx_ver}-posix/include/c++"; do
		if [[ -d "${cand}" ]]; then
			CXX_INC_ROOT="${cand}"
			break
		fi
	done
	if [[ -z "${CXX_INC_ROOT}" ]]; then
		# Glob last-resort (version folder naming varies by distro).
		for cand in /usr/lib/gcc/"${TARGET}"/*/include/c++; do
			if [[ -d "${cand}" ]]; then
				CXX_INC_ROOT="${cand}"
				break
			fi
		done
	fi
	unset _gxx_ver
fi

# ---- helpers --------------------------------------------------------------

# Unix-only TUs are never compiled on Windows; skip under a Windows triple.
is_unix_only() {
	case "$(basename "$1")" in
		*_unix.c | *_unix.cpp | *_posix.c | *_linux.c | *_apple.c | *_cocoa.c | *_cocoa.m | *_macos.c | *_macos.m )
			return 0
			;;
	esac
	return 1
}

# Default set: first-party *C* under LLP64 risk. Skip:
# - tests/ (host-only; need SK_TESTS / full link setup)
# - *.cpp  (only thirdparty glue here, e.g. VMA; MinGW libstdc++ + clang-tidy
#           include paths are fragile and not the cast-bug target)
default_file_ok() {
	local f="$1"
	case "${f}" in
		tests/* | */tests/* ) return 1 ;;
		*.cpp | *.cc | *.cxx ) return 1 ;;
		core/* | app/* | player/* | editor/* | plugins/* ) return 0 ;;
		* ) return 1 ;;
	esac
}

build_extra_args() {
	# Order matters: clang resource-dir first so intrinsics resolve to clang,
	# then MinGW sysroot for CRT / windows.h.
	EXTRA_ARGS=(
		"--extra-arg=--target=${TARGET}"
		"--extra-arg=--sysroot=${SYSROOT}"
		"--extra-arg=-resource-dir=${RESOURCE_DIR}"
		"--extra-arg=-isystem${RESOURCE_DIR}/include"
		"--extra-arg=-isystem${SYSROOT}/include"
		"--extra-arg=-fms-extensions"
		"--extra-arg=-D_CRT_SECURE_NO_WARNINGS"
		"--extra-arg=-DWIN32_LEAN_AND_MEAN"
		"--extra-arg=-DSK_ENGINE_VERSION=\"0.0.1\""
		"--extra-arg=-DSK_VERSION=\"0.0.1-windows-abi\""
		"--extra-arg=-I${ROOT}/core"
		"--extra-arg=-I${ROOT}/app"
		"--extra-arg=-I${ROOT}/player"
		"--extra-arg=-I${ROOT}/editor"
		"--extra-arg=-I${ROOT}/thirdparty/mimalloc/include"
		"--extra-arg=-I${ROOT}/thirdparty/unity/src"
		"--extra-arg=-I${ROOT}/thirdparty/glfw/include"
		"--extra-arg=-I${ROOT}/thirdparty/vulkan/include"
		"--extra-arg=-I${ROOT}/thirdparty/volk/src"
		"--extra-arg=-I${ROOT}/thirdparty/vma/include"
		"--extra-arg=-I${ROOT}/thirdparty/nativefiledialog/src/include"
		"--extra-arg=-I${ROOT}/thirdparty/dxc/include"
		"--extra-arg=-I${ROOT}/thirdparty/yyjson/src"
		"--extra-arg=-Wno-unknown-warning-option"
		"--extra-arg=-std=c11"
	)
	if [[ -n "${CXX_INC_ROOT}" ]]; then
		EXTRA_ARGS+=(
			"--extra-arg=-isystem${CXX_INC_ROOT}"
			"--extra-arg=-isystem${CXX_INC_ROOT}/${TARGET}"
			"--extra-arg=-isystem${CXX_INC_ROOT}/backward"
		)
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
		"--system-headers=0" \
		"--header-filter=${ROOT}/(core|app|player|editor|plugins)/.*" \
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
		tests
	)
	local -a prune_args=()
	local d
	for d in "${prune_dirs[@]}"; do
		if ((${#prune_args[@]})); then
			prune_args+=(-o)
		fi
		prune_args+=(-path "./${d}")
	done
	# C only in the default scan (see default_file_ok). Explicit CLI paths may
	# still pass .cpp for ad-hoc runs.
	find . \
		\( "${prune_args[@]}" \) -prune -o \
		-type f -name '*.c' -print |
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
	# Explicit paths (CLI) always run; default scan stays on library dirs.
	if (($# == 0)) && ! default_file_ok "${f}"; then
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

echo "clang-tidy:   $(${CLANG_TIDY} --version 2>/dev/null | head -n1)"
echo "target:       ${TARGET}"
echo "sysroot:      ${SYSROOT}"
echo "resource-dir: ${RESOURCE_DIR}"
echo "cxx-inc:      ${CXX_INC_ROOT:-"(none — C++ TUs may fail to parse)"}"
echo "files:        ${#files[@]}"
echo "jobs:         ${JOBS}"
echo "config:       ${ROOT}/.clang-tidy"
echo

log_dir="$(mktemp -d "${TMPDIR:-/tmp}/sk-windows-abi.XXXXXX")"
cleanup() { rm -rf "${log_dir}"; }
trap cleanup EXIT

self="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
failed=0

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
