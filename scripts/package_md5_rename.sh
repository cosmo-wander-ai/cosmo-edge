#!/bin/bash
# 打包后计算 MD5 并嵌入文件名
# 用法: package_md5_rename.sh <packages_dir> <package_name>
set -e

PACKAGES_DIR="$1"
PACKAGE_NAME="$2"
ORIG="${PACKAGES_DIR}/${PACKAGE_NAME}.tar.gz"

if [ ! -f "$ORIG" ]; then
    echo "Error: $ORIG not found"
    exit 1
fi

# 清理旧包，只保留当前构建的
find "$PACKAGES_DIR" -name "*.tar.gz" ! -name "$(basename "$ORIG")" -delete 2>/dev/null || true

MD5=$(md5sum "$ORIG" | cut -d' ' -f1)
NEW="${PACKAGES_DIR}/${PACKAGE_NAME}-${MD5}.tar.gz"
mv "$ORIG" "$NEW"
echo "Package: $(basename "$NEW")"
