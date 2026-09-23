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
run_timestamp="$(date '+%Y-%m-%dT%H:%M:%S%z')"
log_file="${LOG_FILE:-${project_root}/results/arm/logs/${CANDIDATE_ID:-candidate}-${timestamp}.log}"
binary_name="${BINARY_NAME:-bin/wrj_arm_port}"
build_dir="${BUILD_DIR:-${project_root}/build-arm}"
build_binary="${BINARY:-${build_dir}/wrj_arm_port}"
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

# Prefer the effective settings in the ARM CMake cache; use the centralized
# deployment defaults only when the cache is not present on this host.
cache_value() {
    local key="$1"
    local cache_file="${build_dir}/CMakeCache.txt"
    if [[ -f "${cache_file}" ]]; then
        sed -n "s/^${key}:[^=]*=//p" "${cache_file}" | tail -n 1
    fi
}

build_type="$(cache_value CMAKE_BUILD_TYPE)"
build_type="${build_type:-${ARM_BUILD_TYPE}}"
release_flags="$(cache_value CMAKE_C_FLAGS_RELEASE)"
optimization="$(printf '%s\n' "${release_flags}" | grep -oE -- '-O[^[:space:]]+' | tail -n 1 || true)"
optimization="${optimization:-${ARM_OPTIMIZATION}}"
link_flags="$(cache_value CMAKE_EXE_LINKER_FLAGS)"
if [[ -n "${link_flags}" ]]; then
    if [[ " ${link_flags} " == *" -static "* ]]; then
        link_mode="static"
    else
        link_mode="dynamic"
    fi
else
    link_mode="${ARM_LINK_MODE}"
fi

git_commit="$(git -C "${project_root}" rev-parse --short HEAD 2>/dev/null || printf 'unknown')"
git_branch="$(git -C "${project_root}" rev-parse --abbrev-ref HEAD 2>/dev/null || printf 'unknown')"
binary_sha256="unavailable"
if [[ -f "${build_binary}" ]] && command -v sha256sum >/dev/null 2>&1; then
    binary_sha256="$(sha256sum "${build_binary}" | cut -d ' ' -f 1 || printf 'unavailable')"
fi

printf '%s\n' \
    '==================================================' \
    'WRJ ARM Run Metadata' \
    '==================================================' \
    "Git commit      : ${git_commit}" \
    "Git branch      : ${git_branch}" \
    "Build type      : ${build_type}" \
    "Optimization    : ${optimization}" \
    "Target          : ${ARM_TARGET}" \
    "Link mode       : ${link_mode}" \
    "Board           : ${board_host}" \
    "Case            : ${candidate_id}" \
    "Timestamp       : ${run_timestamp}" \
    "Ubuntu binary SHA256: ${binary_sha256}" \
    '==================================================' \
    '' > "${log_file}"

ssh "${board_host}" "test ! -e '${board_root}/${out_name}'" || {
    echo "Board output already exists: ${board_root}/${out_name}" >&2
    exit 2
}
remote_command="cd '${board_root}' && chmod +x './${binary_name}' && mkdir -p './${out_name}' && './${binary_name}' --handoff './${handoff_name}' --candidate '${candidate_id}' --iq './${iq_name}' --out './${out_name}'"
echo "Running board test on ${board_host}; log: ${log_file}"
ssh "${board_host}" "${remote_command}" 2>&1 | tee -a "${log_file}"
scp -r "${board_host}:${board_root}/${out_name}" "${project_root}/results/arm/"
echo "Board test log saved: ${log_file}"
echo "Board result copied to ${project_root}/results/arm/${out_name##*/}"
