#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${script_dir%/utils}"

build_dir="${BUILD_DIR:-build}"

cmake -S "${repo_root}" -B "${repo_root}/${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${repo_root}/${build_dir}"
