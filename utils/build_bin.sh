#!/usr/bin/env bash
set -euo pipefail

# Build the db_server binary per profile (§4.1, executables get build_bin.sh).
# Links libdb_app statically — preferring the installed archive, falling back
# to the sibling DB_app build tree inside the superproject.
#
# Usage: build_bin.sh [profile ...]      (default: release)

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}"; }
trap cleanup EXIT

die() { printf 'build_bin: %s\n' "$*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd -- "${ROOT_DIR}"

PROFILE_FILE="${ROOT_DIR}/utils/gcc_build_profiles.sh"
[[ -f "${PROFILE_FILE}" ]] || die "gcc profile file not found: ${PROFILE_FILE}"
source "${PROFILE_FILE}"

BUILD_DIR="${ROOT_DIR}/build"

DEPS_FILE="${ROOT_DIR}/utils/deps.sh"
[[ -f "${DEPS_FILE}" ]] || die "deps file not found: ${DEPS_FILE}"
source "${DEPS_FILE}"

mapfile -t SOURCES < <(find app/src -type f -name '*.c' | sort)
((${#SOURCES[@]} > 0)) || die "no sources under app/src"

if (($# > 0)); then
    PROFILE_NAMES=("$@")
else
    PROFILE_NAMES=(release)
fi

for profile in "${PROFILE_NAMES[@]}"; do
    upper="${profile^^}"
    cppflags_var="CPPFLAGS_${upper}[@]"
    cflags_var="CFLAGS_${upper}[@]"
    ldflags_var="LDFLAGS_${upper}[@]"

    profile_dir="${BUILD_DIR}/${profile}"
    obj_dir="${profile_dir}/obj/bin"
    bin_path="${profile_dir}/${BIN_NAME}"
    mkdir -p "${obj_dir}"

    printf '\n[%s]\n' "${profile}"
    objects=()
    for src in "${SOURCES[@]}"; do
        obj="${obj_dir}/${src#app/src/}"
        obj="${obj%.c}.o"
        mkdir -p "$(dirname "${obj}")"
        printf '  compiling: %s\n' "${src}"
        gcc "${!cppflags_var}" "${CPPFLAGS_EXTRA[@]}" "${!cflags_var}" -c "${src}" -o "${obj}"
        objects+=("${obj}")
    done

    printf '  linking:   %s\n' "${bin_path}"
    gcc "${!ldflags_var}" "${objects[@]}" "${LDLIBS[@]}" "${RUNTIME_LDFLAGS[@]}" -o "${bin_path}"

    printf '  artifact:  %s (%s bytes)\n' "${bin_path}" "$(wc -c < "${bin_path}")"

    # Gate hardened profiles: the executable must actually be PIE/full-RELRO/
    # noexecstack. Release/native only — debug/sanitize legitimately differ.
    case "${profile}" in
        release|native)
            "${ROOT_DIR}/utils/check_hardening.sh" "${bin_path}"
            ;;
        *) ;;
    esac
done
