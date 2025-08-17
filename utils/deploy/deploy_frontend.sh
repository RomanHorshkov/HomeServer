#!/usr/bin/env bash
set -euo pipefail

# Save current working directory
ORIG_DIR="$(pwd)"

# Go to the HomeServer root
cd ~/HomeServer

# 1) Build & deploy frontend
echo "Building frontend…"
cd frontend
npm install
npm run build

echo "Deploying frontend to /srv/home_server/pub/"
sudo rm -rf /srv/home_server/pub/* 
sudo cp -r dist/* /srv/home_server/pub/

# Restore original directory
cd "$ORIG_DIR"
