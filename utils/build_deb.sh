#!/usr/bin/env bash
# =============================================================================
# build_deb.sh — package the db_server executable as a Debian package
#
# author  Roman Horshkov <github.com/RomanHorshkov>
#
# Produces  db-server_<ver>_<arch>.deb  installing /usr/local/bin/db_server.
# The binary links the platform shared libraries (libdb_jose, libDB_http,
# libdb_lmdb, libspscring, libemlog) plus libsodium/liblmdb — those come from
# their own debs (install/packages/install-all.sh installs the whole set in
# dependency order); this package ships only the executable.
#
# In production the service actually runs from /srv/home_server/bin/server
# (install/provision/install-binaries.sh copies it there); this deb is the
# distributable artifact, produced by the pipeline.
#
# Usage:  ./utils/build_deb.sh
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd -- "${ROOT_DIR}"

command -v dpkg-deb >/dev/null 2>&1 || { printf 'dpkg-deb not found in PATH\n' >&2; exit 1; }

PKG_NAME="db-server"
VER="$(tr -d '[:space:]' < "${ROOT_DIR}/VERSION")"
ARCH="$(dpkg --print-architecture)"
STRIP="${STRIP:-strip}"

# Build the release binary (build_bin.sh prefers installed libs; the pipeline
# builds/installs them first).
"${SCRIPT_DIR}/build_bin.sh" release
BIN="${ROOT_DIR}/build/release/db_server"
[[ -f "${BIN}" ]] || { printf 'build produced no %s\n' "${BIN}" >&2; exit 1; }

STAGE="${ROOT_DIR}/build/pkgroot"
rm -rf -- "${STAGE}"
mkdir -p -- "${STAGE}/DEBIAN" "${STAGE}/usr/local/bin"

install -m 0755 "${BIN}" "${STAGE}/usr/local/bin/db_server"
"${STRIP}" --strip-unneeded "${STAGE}/usr/local/bin/db_server" 2>/dev/null || true

cat > "${STAGE}/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VER}
Section: net
Priority: optional
Architecture: ${ARCH}
Maintainer: Roman Horshkov <https://github.com/RomanHorshkov>
Depends: libc6, libsodium23, liblmdb0
Description: DB platform network daemon (db_server) under /usr/local/bin
 The loopback-only epoll HTTP front for the DB platform: it parses requests,
 routes /api/app/* into libdb_app, and streams responses. Links the platform
 shared libraries (libdb_jose, libDB_http, libdb_lmdb, libspscring, libemlog),
 which are provided by their own packages installed alongside this one.
EOF

DEB="${PKG_NAME}_${VER}_${ARCH}.deb"
fakeroot dpkg-deb --build "${STAGE}" "${DEB}"

OUT_DIR="${OUT_DIR:-${ROOT_DIR}/build/debs}"
mkdir -p -- "${OUT_DIR}"
mv -f "${DEB}" "${OUT_DIR}/"

printf '\nBuilt %s\n' "${OUT_DIR}/${DEB}"
printf 'inspect:  dpkg-deb -I %s   /   dpkg-deb -c %s\n' "${OUT_DIR}/${DEB}" "${OUT_DIR}/${DEB}"
printf 'install:  sudo apt-get install %s/%s\n' "${OUT_DIR}" "${DEB}"
