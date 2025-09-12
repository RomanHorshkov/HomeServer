#!/usr/bin/env bash
set -euo pipefail

# 1) Build & deploy frontend
echo "Building frontend…"
cd frontend
npm ci
npm run build

# Restore original directory
cd ..
