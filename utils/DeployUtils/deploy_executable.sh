#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# === CONFIG ===
EXECUTABLE="build/bin/server"
REMOTE_HOST="home-server"
REMOTE_DIR="~/HomeServer/bin"

# 1) Check local executable
if [[ ! -x "${EXECUTABLE}" ]]; then
  echo "❌ '${EXECUTABLE}' not found or not executable."
  exit 1
fi

# 2) Ensure remote directory exists
ssh "${REMOTE_HOST}" "mkdir -p ${REMOTE_DIR}"

# 3) Copy it up
scp "${EXECUTABLE}" "${REMOTE_HOST}:${REMOTE_DIR}/"

# 4) Fix permissions on the remote side
ssh "${REMOTE_HOST}" "chmod +x ${REMOTE_DIR}/server"

echo "✅ '${EXECUTABLE}' deployed to ${REMOTE_HOST}:${REMOTE_DIR}/"

