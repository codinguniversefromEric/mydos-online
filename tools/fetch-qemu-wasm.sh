#!/bin/bash
# 下載預先編好的 QEMU Wasm（x86_64）到 web/qemu/
# 來源：https://github.com/ktock/qemu-wasm-demo-images（固定在某個 commit，避免哪天改版就壞掉）
set -euo pipefail
COMMIT=b7c549b5e6f4c376f76483a03e983214421434ad
DEST=web/qemu
FILES="out.js qemu-system-x86_64.wasm qemu-system-x86_64.worker.js load-rom.js load-rom.data"

if [ -f "$DEST/qemu-system-x86_64.wasm" ]; then exit 0; fi
tmp=$(mktemp -d)
git clone -q --filter=blob:none --no-checkout https://github.com/ktock/qemu-wasm-demo-images.git "$tmp"
(cd "$tmp" && git checkout -q "$COMMIT" -- $(for f in $FILES; do echo "alpine-x86_64/$f"; done))
mkdir -p "$DEST"
for f in $FILES; do cp "$tmp/alpine-x86_64/$f" "$DEST/"; done
rm -rf "$tmp"
echo "QEMU Wasm downloaded to $DEST"
