#!/usr/bin/env bash
# Regenerate checked-in UI golden images deliberately (APX-134).
#
# Usage (from repo root, after configuring + building integration tests):
#   ./scripts/regen-ui-goldens.sh
#   BUILD_DIR=build ./scripts/regen-ui-goldens.sh
#
# Sets SK_UI_REGEN_GOLDENS=1 and runs only the offscreen UI golden test.
# Golden PNG is written under plugins/ui/testdata/ (binary; see .gitattributes).
# Stdout is ASCII-only summaries; binary never goes through text decode.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
BIN="${BUILD_DIR}/bin/sk-integration-tests"

if [[ ! -x "${BIN}" ]]; then
  echo "error: ${BIN} not found; build sk-integration-tests first" >&2
  exit 1
fi

export SK_UI_REGEN_GOLDENS=1
# Run from bin/ so plugins/ and DXC runtime resolve relative to cwd.
cd "${BUILD_DIR}/bin"
# Filter to the golden test when the host supports -n; otherwise full suite.
if "${BIN}" -h 2>&1 | grep -q -- '-n' || true; then
  # Unity/SK_TEST hosts vary; try name filter then fall back to full run.
  if ! SK_UI_REGEN_GOLDENS=1 "${BIN}" -n ui_offscreen_draw_list_golden 2>/dev/null; then
    SK_UI_REGEN_GOLDENS=1 "${BIN}"
  fi
else
  SK_UI_REGEN_GOLDENS=1 "${BIN}"
fi

echo "regen-ui-goldens: done (check plugins/ui/testdata/ui_fixture_golden.png)"
