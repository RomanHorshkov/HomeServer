#!/usr/bin/env bash
set -euo pipefail

MANIFEST="contract/manifest.json"
OUT="app/include/contract_version.h"

ver=$(jq -r '.version' "$MANIFEST")
sha=$(sha256sum "$MANIFEST" | awk '{print $1}')

mkdir -p "$(dirname "$OUT")"
cat > "$OUT" <<EOF
#ifndef CONTRACT_VERSION_H
#define CONTRACT_VERSION_H
#define CONTRACT_VERSION "${ver}"
#define CONTRACT_MANIFEST_SHA256 "${sha}"
#endif
EOF

echo "Generated $OUT (version=$ver, sha256=$sha)"

