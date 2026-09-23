#!/usr/bin/env bash
set -euo pipefail

# Upload only; this script never runs the uploaded binary.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
# shellcheck source=tools/board_config.sh
source "${script_dir}/board_config.sh"
board_host="${BOARD_HOST}"
board_root="${BOARD_ROOT}"
build_dir="${BUILD_DIR:-${project_root}/build-arm}"
binary="${BINARY:-${build_dir}/wrj_arm_port}"
handoff="${HANDOFF_FILE:-}"
iq_file="${IQ_FILE:-}"

if [[ ! -f "${binary}" ]]; then
    echo "Binary not found: ${binary}" >&2
    echo "Build first or set BINARY=/path/to/wrj_arm_port." >&2
    exit 2
fi

ssh "${board_host}" "mkdir -p '${board_root}/bin' '${board_root}/data' '${board_root}/results'"
scp "${binary}" "${board_host}:${board_root}/bin/wrj_arm_port"
uploaded=1
if [[ -n "${handoff}" ]]; then
    scp "${handoff}" "${board_host}:${board_root}/data/$(basename "${handoff}")"
    uploaded=$((uploaded + 1))
fi
if [[ -n "${iq_file}" ]]; then
    scp "${iq_file}" "${board_host}:${board_root}/data/$(basename "${iq_file}")"
    uploaded=$((uploaded + 1))
fi
echo "Uploaded ${uploaded} file(s) to ${board_host}:${board_root}"
