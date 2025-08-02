#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# === CONFIG ===
REMOTE_HOST="fonta"

# Local source dirs (relative to script or absolute)
PUBLIC_SRC="../../var/www"
PRIVATE_SRC="../../var/lib"

# Remote target dirs
APPNAME="home_server"
REMOTE_PUBLIC="/srv/${APPNAME}/pub"
REMOTE_PRIVATE="/srv/${APPNAME}/prv"

# --- 1) Ensure local sources exist ---
[[ -d "${PUBLIC_SRC}" ]]  || { echo "❌ Public source '${PUBLIC_SRC}' not found.";  exit 1; }
[[ -d "${PRIVATE_SRC}" ]] || { echo "❌ Private source '${PRIVATE_SRC}' not found."; exit 1; }

# --- 2) Make sure remote dirs exist (expand vars locally, escape $USER for remote) ---
ssh -t "${REMOTE_HOST}" \
  "sudo mkdir -p ${REMOTE_PUBLIC} ${REMOTE_PRIVATE} && \
   sudo chown \$USER:\$USER ${REMOTE_PUBLIC} ${REMOTE_PRIVATE}"

# --- 3) Sync public content (readable by HTTP server) ---
scp -r "${PUBLIC_SRC}/." "${REMOTE_HOST}:${REMOTE_PUBLIC}/"

# Tighten perms: dirs 755, files 644
ssh -t "${REMOTE_HOST}" \
  "sudo find ${REMOTE_PUBLIC} -type d -exec chmod 755 {} +; \
   sudo find ${REMOTE_PUBLIC} -type f -exec chmod 644 {} +"

# --- 4) Sync private data (app-only) ---
scp -r "${PRIVATE_SRC}/." "${REMOTE_HOST}:${REMOTE_PRIVATE}/"

# Restrict private data: owner rwx only
ssh -t "${REMOTE_HOST}" \
  "sudo chmod -R 700 ${REMOTE_PRIVATE}"

echo "✅ Public content deployed to ${REMOTE_HOST}:${REMOTE_PUBLIC}"
echo "✅ Private data deployed to ${REMOTE_HOST}:${REMOTE_PRIVATE}"
