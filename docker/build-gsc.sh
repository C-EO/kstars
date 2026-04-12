#!/usr/bin/env bash
set -euo pipefail

mkdir -p "$SRC_DIR" "$BUILD_DIR"

cd "$SRC_DIR"
git clone --depth 1 https://git.launchpad.net/gsc

cd "$BUILD_DIR"
cmake -DCMAKE_INSTALL_PREFIX=/usr $SRC_DIR/gsc; \
    make -j${THREADS} install

rm -rf "$SRC_DIR" "$BUILD_DIR"
    
