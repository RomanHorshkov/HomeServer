#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${script_dir%/utils}"

port="${1:-3490}"
build_dir="${BUILD_DIR:-build}"

"${repo_root}/utils/build.sh"

exec "${repo_root}/${build_dir}/bin/server" "${port}"
