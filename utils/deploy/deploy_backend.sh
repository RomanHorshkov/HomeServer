#!/usr/bin/env bash
set -euo pipefail

#proj="$HOME/HomeServer"
proj="."
bin="server"
dest="/srv/home_server/bin"

pushd "$proj" >/dev/null

make clean
make all

sudo install -D -m 0755 "build/bin/$bin" "$dest/$bin"
popd >/dev/null

exec "$dest/$bin" 3491
