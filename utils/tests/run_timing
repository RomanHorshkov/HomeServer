#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${repo_root}/build/tests/http_manager"

cmake -S "${repo_root}" -B "${build_dir}" -DHS_BUILD_HTTP_IT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build "${build_dir}" --target http_manager_timing_tests
"${build_dir}/bin/http_manager_timing_tests"
