#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$SCRIPT_DIR/engine"
BUILD_DIR="$ENGINE_DIR/build"
OUTPUT_DIR="$SCRIPT_DIR/src/engine/wasm"

echo "==> Configuring with emcmake..."
emcmake cmake -S "$ENGINE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building with emmake..."
emmake make -C "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo "==> Copying artifacts..."
mkdir -p "$OUTPUT_DIR"
cp "$BUILD_DIR/yatsi.js" "$OUTPUT_DIR/"
cp "$BUILD_DIR/yatsi.wasm" "$OUTPUT_DIR/"

echo "==> Done. Output:"
ls -lh "$OUTPUT_DIR"/yatsi.*
