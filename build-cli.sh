#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$SCRIPT_DIR/engine"
BUILD_DIR="$ENGINE_DIR/build-cli"

echo "==> Configuring native CLI build..."
cmake -S "$ENGINE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building..."
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo "==> Done. Binary:"
ls -lh "$BUILD_DIR/yatsi_cli"
