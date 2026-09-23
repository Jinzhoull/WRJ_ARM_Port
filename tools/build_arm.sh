#!/usr/bin/env bash
set -euo pipefail

# Configure and build only. This script does not deploy or execute anything.
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-${project_root}/build-arm}"
build_type="${BUILD_TYPE:-Release}"
toolchain_file="${TOOLCHAIN_FILE:-${project_root}/cmake/armv7-linux-gnueabihf.cmake}"

if ! command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
    echo "arm-linux-gnueabihf-gcc is not installed or not in PATH." >&2
    exit 2
fi

if [[ ! -f "${toolchain_file}" ]]; then
    echo "ARMv7 CMake toolchain file not found: ${toolchain_file}" >&2
    exit 2
fi

echo "Configuring ${build_dir} with ${toolchain_file}"
link_flags="${CMAKE_EXE_LINKER_FLAGS:-}"
static_link="${STATIC_LINK:-1}"
if [[ "${static_link}" == "1" ]]; then
    link_flags="${link_flags} -static"
fi
cmake -S "${project_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
    -DCMAKE_TOOLCHAIN_FILE="${toolchain_file}" \
    -DCMAKE_EXE_LINKER_FLAGS="${link_flags}" \
    -DWRJ_ENABLE_NEON="${WRJ_ENABLE_NEON:-OFF}"

cmake --build "${build_dir}" --parallel "${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
echo "ARM build complete: ${build_dir}/wrj_arm_port"
