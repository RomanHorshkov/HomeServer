# utils/deps.sh — shared sibling/installed dependency detection for build_bin.sh
# and build_tests.sh. Sourced, not executed: expects ROOT_DIR, BUILD_DIR and
# `die()` already defined by the caller.
#
# Produces: CPPFLAGS_EXTRA, LDLIBS, RUNTIME_LDFLAGS.

BIN_NAME="db_server"

# Sibling superproject builds WIN over the installed packages when present:
# a slice that evolves several repos together (S11 touched DB_lmdb, DB_http,
# DB_app AND this server) must compile against the fresh siblings, and the
# installer force-reinstalls freshly built debs anyway, so production boxes
# (no sibling checkouts) still resolve to /usr/local.
DB_APP_ARCHIVE=""
for candidate in "${ROOT_DIR}/../DB_app/build/release/libdb_app.a" /usr/local/lib/libdb_app.a; do
    if [[ -f "${candidate}" ]]; then
        DB_APP_ARCHIVE="${candidate}"
        break
    fi
done
[[ -n "${DB_APP_ARCHIVE}" ]] || die "libdb_app.a not found (install libdb-app-dev or build ../DB_app first)"

DB_APP_INCLUDE_DIR=""
for candidate in "${ROOT_DIR}/../DB_app/app/include/db_app.h" /usr/local/include/db_app.h; do
    if [[ -f "${candidate}" ]]; then
        DB_APP_INCLUDE_DIR="$(dirname "${candidate}")"
        break
    fi
done
[[ -n "${DB_APP_INCLUDE_DIR}" ]] || die "db_app.h not found"

# DB_http and DB_lmdb: same sibling-first rule (their S11 APIs — stream gate,
# blob-store staging — travel with this server change).
DB_HTTP_CPPFLAGS=()
DB_HTTP_LDFLAGS=()
if [[ -f "${ROOT_DIR}/../DB_http/build/release/libDB_http.a" ]]; then
    # DB_http's repo headers are flat (app/inc/*.h) while sources include <DB_http/...>; a build-local symlink shim namespaces them.
    mkdir -p "${BUILD_DIR}/dep_inc"
    ln -sfn "$(cd "${ROOT_DIR}/../DB_http/app/inc" && pwd)" "${BUILD_DIR}/dep_inc/DB_http"
    DB_HTTP_CPPFLAGS=(-I"${BUILD_DIR}/dep_inc")
    DB_HTTP_LDFLAGS=(-L"${ROOT_DIR}/../DB_http/build/release" -Wl,-rpath,"${ROOT_DIR}/../DB_http/build/release")
fi
DB_LMDB_CPPFLAGS=()
DB_LMDB_LDFLAGS=()
if [[ -f "${ROOT_DIR}/../DB_lmdb/build/release/libdb_lmdb.a" ]]; then
    DB_LMDB_CPPFLAGS=(-I"${ROOT_DIR}/../DB_lmdb/app/include")
    DB_LMDB_LDFLAGS=(-L"${ROOT_DIR}/../DB_lmdb/build/release" -Wl,-rpath,"${ROOT_DIR}/../DB_lmdb/build/release")
fi
EMLOG_CPPFLAGS=()
EMLOG_LDFLAGS=()
if [[ -f "${ROOT_DIR}/../EMlog/build/release/libemlog.so" ]]; then
    EMLOG_CPPFLAGS=(-I"${ROOT_DIR}/../EMlog/app")
    EMLOG_LDFLAGS=(-L"${ROOT_DIR}/../EMlog/build/release" -Wl,-rpath,"${ROOT_DIR}/../EMlog/build/release")
fi

# Strict namespacing: ONE project include root. Every project include is
# written as <db_server/...>; anything else comes from installed deps.
CPPFLAGS_EXTRA=(
    -Iapp/include
    -I"${DB_APP_INCLUDE_DIR}"
    "${DB_HTTP_CPPFLAGS[@]}"
    "${DB_LMDB_CPPFLAGS[@]}"
    "${EMLOG_CPPFLAGS[@]}"
    -I/usr/local/include
)

# db_jose: installed package when current, sibling deb stage otherwise
# (same detection as DB_app's catalog — dies when DB_jose gets namespaced).
DB_JOSE_LDFLAGS=()
if [[ ! -d /usr/include/db_jose/login ]]; then
    _jose_stage="$(ls -d "${ROOT_DIR}"/../DB_jose/build/deb/libdb-jose-dev_*_amd64 2>/dev/null | sort -V | tail -1)"
    [[ -n "${_jose_stage}" ]] || die "no usable DB_jose (install libdb-jose-dev 0.2.0+ or build ../DB_jose)"
    DB_JOSE_LDFLAGS=(-L"${_jose_stage}/usr/lib/x86_64-linux-gnu" -Wl,-rpath,"${_jose_stage}/usr/lib/x86_64-linux-gnu")
fi

LDLIBS=(
    "${DB_APP_ARCHIVE}"
    "${DB_HTTP_LDFLAGS[@]}"
    "${DB_LMDB_LDFLAGS[@]}"
    "${EMLOG_LDFLAGS[@]}"
    "${DB_JOSE_LDFLAGS[@]}"
    -L/usr/local/lib
    -ldb_jose
    -lDB_http
    -lspscring
    -lmpscring
    -lemlog
    -ldb_lmdb
    -luuid7
    -lsodium
    -lz
    -lpthread
)

RUNTIME_LDFLAGS=(-Wl,-rpath,/usr/local/lib)
