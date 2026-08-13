#!/usr/bin/env bash
# Run widget-vision, flexbox, and interaction UI suites with one command (APX-263).
#
# Usage (from repo root, after configure):
#   ./scripts/run-ui-integration-tests.sh
#   BUILD_DIR=build ./scripts/run-ui-integration-tests.sh
#   ./scripts/run-ui-integration-tests.sh --no-build
#   SUITES=widget,flexbox ./scripts/run-ui-integration-tests.sh
#
# Suites (comma-separated via SUITES, default: all):
#   widget       — per-widget vision (tests/integration/ui_widget_vision.c)
#   flexbox      — flexbox layout vision (ui_flexbox_vision.c)
#   interaction  — behavioural engine suite (plugin) + interaction vision (integration)
#   vision-helper — ui_vision_assert unit/mock tests
#   dock         — docking public-API e2e (tests/integration/ui_dock.c)
#   text-screenshot — APX-268 deterministic MSDF text PNG suite
#
# Vision credentials (optional for structural/interaction; required for live grades):
#   XAI_API_KEY or SK_UI_VISION_API_KEY
# Without keys, vision grades SKIP with a clear message (Unity IGNORE), never a
# silent PASS. Set SK_UI_VISION_REQUIRED=1 to fail instead of ignore when keys
# are missing (vision-required CI job).
#
# Artifacts: PNGs under $SK_TEST_ARTIFACT_DIR (default: $BUILD_DIR/test-artifacts),
# including {scene}_vision_fail.png on vision FAIL and fail frames from authoring
# asserts. CI uploads this directory.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
SUITES="${SUITES:-widget,flexbox,interaction,vision-helper,dock}"
DO_BUILD=1
EXTRA_ARGS=()

usage() {
  sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage ;;
    --no-build) DO_BUILD=0; shift ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --suites) SUITES="$2"; shift 2 ;;
    *) EXTRA_ARGS+=("$1"); shift ;;
  esac
done

# Resolve BUILD_DIR to an absolute path so artifact writes work when cwd is bin/.
if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT}/${BUILD_DIR}"
fi

BIN_DIR="${BUILD_DIR}/bin"
INTEGRATION_BIN="${BIN_DIR}/sk-integration-tests"
UNIT_BIN="${BIN_DIR}/sk-tests"
if [[ -n "${SK_TEST_ARTIFACT_DIR:-}" ]]; then
  ARTIFACT_DIR="${SK_TEST_ARTIFACT_DIR}"
  if [[ "${ARTIFACT_DIR}" != /* ]]; then
    ARTIFACT_DIR="${ROOT}/${ARTIFACT_DIR}"
  fi
else
  ARTIFACT_DIR="${BUILD_DIR}/test-artifacts"
fi

# Headless Vulkan when no GPU / ICD is configured (Linux Mesa lavapipe).
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

export SK_TEST_ARTIFACT_DIR="${ARTIFACT_DIR}"
mkdir -p "${ARTIFACT_DIR}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "error: build dir '${BUILD_DIR}' missing; configure first:" >&2
  echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON" >&2
  exit 1
fi

if [[ "${DO_BUILD}" -eq 1 ]]; then
  echo "==> Building sk-integration-tests + sk-tests (${BUILD_DIR})"
  cmake --build "${BUILD_DIR}" --target sk-integration-tests sk-tests --parallel
fi

if [[ ! -x "${INTEGRATION_BIN}" ]]; then
  echo "error: ${INTEGRATION_BIN} not found; build sk-integration-tests first" >&2
  exit 1
fi

# Build SK_TEST_FILTER tokens from suite list (prefix wildcards; exact for small sets).
integration_tokens=()
plugin_tokens=()
IFS=',' read -r -a suite_list <<< "${SUITES}"
for raw in "${suite_list[@]}"; do
  s="$(echo "$raw" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
  case "$s" in
    "" ) ;;
    widget|widgets|widget-vision|widget_vision)
      integration_tokens+=("ui_widget_vision_*")
      ;;
    flexbox|flex|flexbox-vision|flexbox_vision)
      integration_tokens+=("ui_flexbox_vision_*")
      ;;
    interaction|ix|engine)
      # Integration vision + behavioural bridge; plugin suite via sk-tests.
      integration_tokens+=("ui_ix_*")
      plugin_tokens+=("ui_author_ix_*")
      ;;
    vision-helper|vision_helper|helper|assert)
      integration_tokens+=("ui_vision_assert_*" "ui_vision_rubrics_*")
      ;;
    dock|docking|ui-dock|ui_dock)
      integration_tokens+=("ui_dock_*")
      ;;
    authoring|author)
      integration_tokens+=("ui_author_*")
      plugin_tokens+=("ui_author_*")
      ;;
    text-screenshot|textshot|screenshot)
      # APX-268: deterministic text rendering screenshot suite (MSDF pipeline).
      integration_tokens+=("ui_text_screenshot_*")
      ;;
    all)
      integration_tokens+=(
        "ui_widget_vision_*"
        "ui_flexbox_vision_*"
        "ui_ix_*"
        "ui_vision_assert_*"
        "ui_vision_rubrics_*"
        "ui_author_*"
        "ui_dock_*"
        "ui_text_screenshot_*"
      )
      plugin_tokens+=("ui_author_ix_*")
      ;;
    *)
      # Treat as a raw filter token (exact or prefix*).
      integration_tokens+=("$s")
      ;;
  esac
done

join_csv() {
  local out="" t
  for t in "$@"; do
    [[ -z "$t" ]] && continue
    if [[ -z "$out" ]]; then
      out="$t"
    else
      out="${out},${t}"
    fi
  done
  printf '%s' "$out"
}

# Dedup tokens while preserving order.
dedup_join() {
  local seen="|" out="" t
  for t in "$@"; do
    [[ -z "$t" ]] && continue
    case "$seen" in
      *"|$t|"*) continue ;;
    esac
    seen="${seen}${t}|"
    if [[ -z "$out" ]]; then
      out="$t"
    else
      out="${out},${t}"
    fi
  done
  printf '%s' "$out"
}

INTEGRATION_FILTER="$(dedup_join "${integration_tokens[@]}")"
PLUGIN_FILTER="$(dedup_join "${plugin_tokens[@]}")"

if [[ -z "${INTEGRATION_FILTER}" && -z "${PLUGIN_FILTER}" ]]; then
  echo "error: no suites selected (SUITES=${SUITES})" >&2
  exit 1
fi

has_vision_key=0
if [[ -n "${XAI_API_KEY:-}" || -n "${SK_UI_VISION_API_KEY:-}" ]]; then
  has_vision_key=1
elif [[ -f "${HOME}/.grok/auth.json" ]]; then
  has_vision_key=1
fi

# Live AI grading is explicit opt-in in the test binaries (plain ctest skips
# without env keys); this dedicated integration flow opts in whenever
# credentials are present, including a discoverable ~/.grok/auth.json.
if [[ "${has_vision_key}" -eq 1 ]]; then
  export SK_UI_VISION_LIVE=1
fi

echo "==> UI integration suites (APX-263)"
echo "    BUILD_DIR=${BUILD_DIR}"
echo "    SUITES=${SUITES}"
echo "    SK_TEST_ARTIFACT_DIR=${SK_TEST_ARTIFACT_DIR}"
if [[ -n "${VK_ICD_FILENAMES:-}" ]]; then
  echo "    VK_ICD_FILENAMES=${VK_ICD_FILENAMES}"
else
  echo "    VK_ICD_FILENAMES=(unset — GPU capture tests may IGNORE without an ICD)"
fi
if [[ "${has_vision_key}" -eq 1 ]]; then
  echo "    vision credentials: present (live grades enabled)"
else
  echo "    vision credentials: ABSENT - vision grades will SKIP with clear messages"
  echo "    (set XAI_API_KEY or SK_UI_VISION_API_KEY; structural/interaction still run)"
  if [[ "${SK_UI_VISION_REQUIRED:-}" == "1" ]]; then
    echo "    SK_UI_VISION_REQUIRED=1 — missing credentials will FAIL (not ignore)"
  fi
fi

status=0

if [[ -n "${INTEGRATION_FILTER}" ]]; then
  echo ""
  echo "==> sk-integration-tests  SK_TEST_FILTER=${INTEGRATION_FILTER}"
  set +e
  (
    cd "${BIN_DIR}"
    export SK_TEST_FILTER="${INTEGRATION_FILTER}"
    # shellcheck disable=SC2086
    ./sk-integration-tests ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
  )
  rc=$?
  set -e
  if [[ $rc -ne 0 ]]; then
    echo "sk-integration-tests failed (exit ${rc})" >&2
    status=$rc
  fi
fi

if [[ -n "${PLUGIN_FILTER}" ]]; then
  if [[ ! -x "${UNIT_BIN}" ]]; then
    echo "error: ${UNIT_BIN} not found; build sk-tests for the interaction suite" >&2
    exit 1
  fi
  echo ""
  echo "==> sk-tests (plugin interaction)  SK_TEST_FILTER=${PLUGIN_FILTER}"
  set +e
  (
    cd "${BIN_DIR}"
    export SK_TEST_FILTER="${PLUGIN_FILTER}"
    ./sk-tests
  )
  rc=$?
  set -e
  if [[ $rc -ne 0 ]]; then
    echo "sk-tests (interaction) failed (exit ${rc})" >&2
    status=$rc
  fi
fi

echo ""
echo "==> Artifacts under ${ARTIFACT_DIR}"
if command -v find >/dev/null 2>&1; then
  find "${ARTIFACT_DIR}" -type f \( -name '*.png' -o -name '*_vision_fail.png' \) 2>/dev/null \
    | sort | head -n 40 || true
  fail_frames="$(find "${ARTIFACT_DIR}" -type f -name '*_vision_fail.png' 2>/dev/null | wc -l | tr -d ' ')"
  if [[ "${fail_frames}" != "0" ]]; then
    echo "    vision fail frames: ${fail_frames} (download from CI artifacts on failure)"
  fi
fi

if [[ $status -ne 0 ]]; then
  echo "UI integration suites FAILED (exit ${status})" >&2
  exit "$status"
fi
echo "UI integration suites OK"
exit 0
