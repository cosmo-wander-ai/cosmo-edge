#!/bin/bash
# Patch SRS auto/depends.sh for cross-compilation environments
# that may lack native host tools (g++, unzip, pkg-config, etc.)
# These tools are only used for native builds; cross-compilation
# uses the toolchain specified via --cc/--cxx/--cross-prefix.
#
# This script is idempotent - safe to run multiple times.

DEPENDS_SH="$1/auto/depends.sh"

if [ ! -f "$DEPENDS_SH" ]; then
    echo "Error: $DEPENDS_SH not found"
    exit 1
fi

patched=0

# Replace native g++ check with cross-compiler g++
if grep -q '^g++ --version' "$DEPENDS_SH" 2>/dev/null; then
    sed -i 's/^g++ --version/aarch64-linux-gnu-g++ --version/' "$DEPENDS_SH"
    patched=1
fi

# Skip unzip check (not needed for cross-compilation)
if grep -q '^unzip -v' "$DEPENDS_SH" 2>/dev/null; then
    sed -i 's/^unzip -v >\/dev\/null 2>\/dev\/null; ret=$?; if/true; ret=$?; if/' "$DEPENDS_SH"
    patched=1
fi

# Skip pkg-config check (not needed for cross-compilation)
if grep -q '^pkg-config --version' "$DEPENDS_SH" 2>/dev/null; then
    sed -i 's/^pkg-config --version >\/dev\/null 2>\/dev\/null; ret=$?; if/true; ret=$?; if/' "$DEPENDS_SH"
    patched=1
fi

if [ $patched -eq 1 ]; then
    echo "Patched $DEPENDS_SH for cross-compilation"
else
    echo "$DEPENDS_SH already patched, skipping"
fi
