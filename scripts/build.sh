#!/bin/bash
export LC_ALL=C.UTF-8

# ── Parse options ──
RESOURCE_DIR=""
DEV_MODE=OFF
while getopts "m:t" opt; do
    case $opt in
        m) RESOURCE_DIR="$OPTARG" ;;
        t) DEV_MODE=ON ;;
        *) echo "Usage: $0 [-m <resource_repo_path>] [-t (enable dev mode)]"; exit 1 ;;
    esac
done

if [ -z "${PROJECT_ROOT_PATH:-}" ]
then
    PROJECT_ROOT_PATH=$(cd `dirname $0`; pwd)/..
fi

if [ -z "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource"
elif [ "${RESOURCE_DIR#/}" = "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/${RESOURCE_DIR}"
fi

if [ ! -d "${RESOURCE_DIR}" ]; then
    echo "ERROR: Resource directory not found: ${RESOURCE_DIR}" >&2
    exit 1
fi

BUILD_DIR=${PROJECT_ROOT_PATH}/build
INSTALL_DIR=${BUILD_DIR}/install

if [ -d ${INSTALL_DIR} ]
then 
    rm -rf ${INSTALL_DIR}
fi

mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

echo "Dev mode: ${DEV_MODE}"
echo "Resource dir: ${RESOURCE_DIR}"
echo "Configuring..."
cmake   -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
        -DBUILD_TESTS=OFF \
        -DCOSMO_DEV_MODE=${DEV_MODE} \
        -DRESOURCE_DIR="${RESOURCE_DIR}" \
        ..

# Symlink compile_commands.json to project root for IDE and static analysis tools
ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true

echo "Building Cosmo ..."
cmake --build . --target install -j$(nproc)

echo "Packaging..."
cmake --build . --target package_all
