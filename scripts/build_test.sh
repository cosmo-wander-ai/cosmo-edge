#!/bin/bash

if [ -z $PROJECT_ROOT_PATH ]
then
    PROJECT_ROOT_PATH=$(cd `dirname $0`; pwd)/..
fi

BUILD_DIR=${PROJECT_ROOT_PATH}/build

mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

echo "Configuring with tests enabled..."
cmake   -DCMAKE_BUILD_TYPE=release \
        -DBUILD_TESTS=ON \
        ..

# Symlink compile_commands.json to project root for IDE and static analysis tools
ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true

echo "Building cosmo-tests..."
cmake --build . --target cosmo-tests -j$(nproc)

echo ""
echo "Build complete: ${BUILD_DIR}/cosmo-tests"

