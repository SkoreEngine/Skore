#!/usr/bin/env bash
# Opt-in integration suite (Vulkan / UI capture / text screenshots).
#
# Default `ctest` SKIPs these. This script sets SK_RUN_INTEGRATION=1 and runs
# only the `integration` CTest label.
#
# Usage (from repo root, after configure):
#   ./scripts/run-integration-tests.sh
#   BUILD_DIR=build ./scripts/run-integration-tests.sh
#   ./scripts/run-integration-tests.sh --no-build
#
# For focused UI vision / dock / interaction subsets, use
# scripts/run-ui-integration-tests.sh (invokes the binaries directly).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
DO_BUILD=1

usage() {
  sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage ;;
    --no-build) DO_BUILD=0; shift ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    *) echo "error: unknown argument '$1'" >&2; usage ;;
  esac
done

if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT}/${BUILD_DIR}"
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "error: build dir '${BUILD_DIR}' missing; configure first:" >&2
  echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON" >&2
  exit 1
fi

if [[ -z "${VK_ICD_FILENAMES:-}" ]]; then
  for icd in \
    /usr/share/vulkan/icd.d/lvp_icd.json \
    /usr/share/vulkan/icd.d/lvp_icd.x86_64.json; do
    if [[ -f "$icd" ]]; then
      export VK_ICD_FILENAMES="$icd"
      break
    fi
  done
fi

if [[ "${DO_BUILD}" -eq 1 ]]; then
  echo "==> Building (${BUILD_DIR})"
  cmake --build "${BUILD_DIR}" --parallel
fi

echo "==> Integration suite  SK_RUN_INTEGRATION=1  -L integration"
echo "    BUILD_DIR=${BUILD_DIR}"
if [[ -n "${VK_ICD_FILENAMES:-}" ]]; then
  echo "    VK_ICD_FILENAMES=${VK_ICD_FILENAMES}"
else
  echo "    VK_ICD_FILENAMES=(unset — GPU tests IGNORE / skip without an ICD)"
fi

export SK_RUN_INTEGRATION=1
ctest --test-dir "${BUILD_DIR}" -L integration --output-on-failure
