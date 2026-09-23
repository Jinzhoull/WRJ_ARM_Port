#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${PC_BUILD_DIR:-${project_root}/build-pc/linux}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"

cmake -S "${project_root}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel "${jobs}"

if [[ "${RUN_TESTS:-0}" == "1" ]]; then
    ctest --test-dir "${build_dir}" --output-on-failure
fi

echo "PC Release build complete: ${build_dir}"
