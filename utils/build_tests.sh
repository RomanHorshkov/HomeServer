#!/usr/bin/env bash
set -euo pipefail

# Build every DB_server test binary per profile.
#
# Discovers tests/**/*_tests.c, links each against every app/src/*.c object
# EXCEPT main.c (each test file supplies its own cmocka main()) plus the same
# sibling/installed dependency set build_bin.sh uses, plus cmocka.
#
# Usage: build_tests.sh [profile ...]     (default: debug)

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}"; }
trap cleanup EXIT

die() { printf 'build_tests: %s\n' "$*" >&2; exit 1; }

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

mapfile -t LIB_SOURCES < <(find app/src -type f -name '*.c' ! -name 'main.c' | sort)
((${#LIB_SOURCES[@]} > 0)) || die "no sources under app/src"

mapfile -t TEST_SOURCES < <(find tests -maxdepth 2 -type f -name '*_tests.c' 2>/dev/null | sort)
((${#TEST_SOURCES[@]} > 0)) || die "no tests/**/*_tests.c found"

if (($# > 0)); then
    PROFILE_NAMES=("$@")
else
    PROFILE_NAMES=(debug)
fi

for profile in "${PROFILE_NAMES[@]}"; do
    upper="${profile^^}"
    cppflags_var="CPPFLAGS_${upper}[@]"
    cflags_var="CFLAGS_${upper}[@]"
    ldflags_var="LDFLAGS_${upper}[@]"

    profile_dir="${BUILD_DIR}/${profile}"
    obj_dir="${profile_dir}/obj/lib"
    out_dir="${BUILD_DIR}/tests/${profile}"
    mkdir -p "${obj_dir}" "${out_dir}"

    printf '\n[%s]\n' "${profile}"
    lib_objects=()
    for src in "${LIB_SOURCES[@]}"; do
        obj="${obj_dir}/${src#app/src/}"
        obj="${obj%.c}.o"
        mkdir -p "$(dirname "${obj}")"
        printf '  compiling: %s\n' "${src}"
        gcc "${!cppflags_var}" "${CPPFLAGS_EXTRA[@]}" "${!cflags_var}" -c "${src}" -o "${obj}"
        lib_objects+=("${obj}")
    done

    for src in "${TEST_SOURCES[@]}"; do
        name="$(basename "${src}" .c)"
        bin="${out_dir}/${name}"
        printf '  building test: %s\n' "${bin}"
        gcc "${!cppflags_var}" "${CPPFLAGS_EXTRA[@]}" "${!cflags_var}" \
            "${!ldflags_var}" \
            "${src}" "${lib_objects[@]}" "${LDLIBS[@]}" -lcmocka "${RUNTIME_LDFLAGS[@]}" \
            -o "${bin}"
    done
done

printf '\nartifacts under: %s\n' "${BUILD_DIR}/tests/"
