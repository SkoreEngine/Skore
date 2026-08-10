#!/usr/bin/env bash
# Configure the project with the Ninja generator (required by AGENTS.md / Apex).
#
# Recovers from a stale build dir that was configured with a different generator
# (e.g. Unix Makefiles). Without this, `cmake -G Ninja` fails with:
#   CMake Error: Error: generator : Ninja
#   Does not match the generator used previously: ...
#
# Usage:
#   ./scripts/cmake-configure.sh
#   BUILD_DIR=build BUILD_TYPE=Release ./scripts/cmake-configure.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
	gen="$(
		grep -E '^CMAKE_GENERATOR:INTERNAL=' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null \
			| head -n1 \
			| cut -d= -f2- \
			|| true
	)"
	if [[ -n "${gen}" && "${gen}" != "Ninja" ]]; then
		echo "warning: ${BUILD_DIR} uses generator '${gen}'; removing for Ninja reconfigure" >&2
		rm -rf "${BUILD_DIR}"
	fi
fi

if ! command -v ninja >/dev/null 2>&1 && ! command -v ninja-build >/dev/null 2>&1; then
	echo "error: Ninja not found on PATH (install ninja-build)" >&2
	exit 1
fi

exec cmake -S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
