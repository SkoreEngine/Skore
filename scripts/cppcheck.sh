#!/usr/bin/env bash
# Run cppcheck on first-party sources via compile_commands.json.
# Skips thirdparty/ (same policy as clang-tidy / format.sh).
#
# Prerequisites:
#   - cppcheck on PATH
#   - configure first so compile_commands.json exists:
#       cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#
# Usage:
#   ./scripts/cppcheck.sh
#   BUILD_DIR=cmake-build-debug ./scripts/cppcheck.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
SUPPRESSIONS="${ROOT}/cppcheck-suppressions.txt"

if ! command -v cppcheck >/dev/null 2>&1; then
	echo "error: cppcheck not found in PATH" >&2
	exit 1
fi

if [[ ! -f "${COMPILE_DB}" ]]; then
	echo "error: ${COMPILE_DB} not found; configure the project first:" >&2
	echo "  cmake -S . -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Debug" >&2
	exit 1
fi

echo "cppcheck $(cppcheck --version 2>&1 | head -n1)"
echo "project: ${COMPILE_DB}"
echo

# Gate on warning/performance/portability only. Style (const churn, unused
# members, scope nits) is suppressed in cppcheck-suppressions.txt and is a poor
# fit for this C engine; clang-tidy already covers smart static analysis.
args=(
	--project="${COMPILE_DB}"
	--enable=warning,performance,portability
	--std=c11
	--inline-suppr
	--error-exitcode=1
	--quiet
	# Vendored code is out of scope (mirrors clang-tidy thirdparty exemption).
	-i thirdparty
	-i "${BUILD_DIR}"
)

if [[ -f "${SUPPRESSIONS}" ]]; then
	args+=(--suppressions-list="${SUPPRESSIONS}")
fi

exec cppcheck "${args[@]}"
