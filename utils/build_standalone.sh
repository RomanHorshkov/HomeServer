#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${script_dir%/utils}"

build_dir="${BUILD_DIR:-build_standalone}"

# Configure and build the "standalone" entry-point so the http manager stack is ready.
cmake -S "${repo_root}" -B "${repo_root}/${build_dir}" -DCMAKE_BUILD_TYPE=Debug -DHS_ENABLE_DB_APP=OFF
cmake --build "${repo_root}/${build_dir}" --target server
