#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_PREFIX="${QT_PREFIX:-/Users/asher/Qt/5.12.12/clang_64}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-x86_64}"
APP_PATH="$BUILD_DIR/MultiMaterialSlicer.app"
DIST_DIR="$ROOT_DIR/dist"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

"$QT_PREFIX/bin/macdeployqt" "$APP_PATH" -verbose=1

mkdir -p "$DIST_DIR"
ditto -c -k --keepParent "$APP_PATH" "$DIST_DIR/MultiMaterialSlicer-mac-x86_64.zip"

echo "Package written to: $DIST_DIR/MultiMaterialSlicer-mac-x86_64.zip"
