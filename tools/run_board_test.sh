#!/usr/bin/env bash
set -euo pipefail

# Execute one explicitly supplied case on the board, save its log, and copy the
# result files back without overwriting earlier runs.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
# shellcheck source=tools/board_config.sh
source "${script_dir}/board_config.sh"
board_host="${BOARD_HOST}"
board_root="${BOARD_ROOT}"
timestamp="$(date +%Y%m%d-%H%M%S)"
log_file="${LOG_FILE:-${project_root}/results/arm/logs/${CANDIDATE_ID:-candidate}-${timestamp}.log}"
binary_name="${BINARY_NAME:-bin/wrj_arm_port}"
handoff_name="${HANDOFF_NAME:-data/module12_to_module3_handoff.csv}"
candidate_id="${CANDIDATE_ID:-}"
iq_name="${IQ_NAME:-}"
out_name="${OUT_NAME:-results/${candidate_id:-candidate}-${timestamp}}"

if [[ -z "${handoff_name}" || -z "${candidate_id}" || -z "${iq_name}" ]]; then
    echo "Set HANDOFF_NAME, CANDIDATE_ID, and IQ_NAME before running." >&2
    echo "This script intentionally requires an explicit test input." >&2
    exit 2
fi

if [[ ! "${candidate_id}" =~ ^[A-Za-z0-9_.-]+$ ||
      ! "${binary_name}" =~ ^[A-Za-z0-9_./-]+$ ||
      ! "${handoff_name}" =~ ^[A-Za-z0-9_./-]+$ ||
      ! "${iq_name}" =~ ^[A-Za-z0-9_./-]+$ ||
      ! "${out_name}" =~ ^results/[A-Za-z0-9_.-]+$ ||
      "${binary_name}${handoff_name}${iq_name}${out_name}" == *..* ]]; then
    echo "Test paths contain unsupported characters or traversal components." >&2
    exit 2
fi

mkdir -p "$(dirname "${log_file}")" "${project_root}/results/arm"
ssh "${board_host}" "test ! -e '${board_root}/${out_name}'" || {
    echo "Board output already exists: ${board_root}/${out_name}" >&2
    exit 2
}
remote_command="cd '${board_root}' && chmod +x './${binary_name}' && mkdir -p './${out_name}' && './${binary_name}' --handoff './${handoff_name}' --candidate '${candidate_id}' --iq './${iq_name}' --out './${out_name}'"
echo "Running board test on ${board_host}; log: ${log_file}"
ssh "${board_host}" "${remote_command}" 2>&1 | tee "${log_file}"
scp -r "${board_host}:${board_root}/${out_name}" "${project_root}/results/arm/"
echo "Board test log saved: ${log_file}"
echo "Board result copied to ${project_root}/results/arm/${out_name##*/}"
