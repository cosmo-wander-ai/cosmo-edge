#!/bin/bash
set -e
# CPU-backend build script — native x86_64 compilation with ONNX Runtime.
# Uses a separate build directory (build_cpu/) to avoid conflicts with aarch64 builds.
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
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource_x86"
elif [ "${RESOURCE_DIR#/}" = "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/${RESOURCE_DIR}"
fi

if [ ! -d "${RESOURCE_DIR}" ]; then
    echo "ERROR: Resource directory not found: ${RESOURCE_DIR}" >&2
    exit 1
fi

BUILD_DIR=${PROJECT_ROOT_PATH}/build_cpu
INSTALL_DIR=${BUILD_DIR}/install

clean_external_project() {
    local name="$1"
    rm -rf "${BUILD_DIR}/${name}-prefix"
    rm -rf "${BUILD_DIR}/CMakeFiles/${name}.dir"
}

clean_external_project openssl_external





if [ -d ${INSTALL_DIR} ]
then
    rm -rf ${INSTALL_DIR}
fi

mkdir -p ${BUILD_DIR}

OPENSSL_SSL_LIB="${BUILD_DIR}/thirdparty_install/openssl/lib/libssl.so"
if [ -f "${OPENSSL_SSL_LIB}" ] && ! file "${OPENSSL_SSL_LIB}" | grep -q "x86-64"; then
    echo "Removing non-x86_64 OpenSSL from CPU build cache: ${OPENSSL_SSL_LIB}"
    rm -rf "${BUILD_DIR}/thirdparty_install/openssl"
    clean_external_project openssl_external
    clean_external_project curl_external
    clean_external_project event_external
fi

if [ -d "${BUILD_DIR}/tokenizers_source" ] &&
   [ ! -f "${BUILD_DIR}/thirdparty_install/tokenizers/lib/libtokenizers_c.a" ]; then
    echo "Removing stale tokenizers build cache from CPU build directory"
    clean_external_project tokenizers_external
fi

cd ${BUILD_DIR}

echo "Dev mode: ${DEV_MODE}"
echo "Backend: CPU (ONNX Runtime)"
echo "Resource dir: ${RESOURCE_DIR}"
echo "Requires: pkg-config and openh264 development package (for x86 realtime OSD H264 encoding)"
echo "Configuring..."
cmake   -DCMAKE_BUILD_TYPE=Release \
        -U CMAKE_TOOLCHAIN_FILE \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
        -DCOSMO_TARGET_ARCH=x86_64 \
        -DCOSMO_NN_USE_SOPHON_BACKEND=OFF \
        -DCOSMO_NN_USE_CPU_BACKEND=ON \
        -DBUILD_TESTS=OFF \
        -DCOSMO_DEV_MODE=${DEV_MODE} \
        -DRESOURCE_DIR="${RESOURCE_DIR}" \
        ..



# Symlink compile_commands.json to project root for IDE and static analysis tools
ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true

echo "Building Cosmo (CPU backend) ..."
cmake --build . --target install -j$(nproc)

echo "Packaging..."
cmake --build . --target package_all
