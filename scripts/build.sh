#!/bin/bash
set -euo pipefail

export LC_ALL=C.UTF-8

COSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"
case "${COSMO_MODEL_GUARD_BUILD_PROFILE}" in
    public-runtime)
        PACKAGE_VARIANT="Open"
        ;;
    production-release)
        PACKAGE_VARIANT="Protected"
        ;;
    *)
        echo "ERROR: COSMO_MODEL_GUARD_BUILD_PROFILE must be public-runtime or production-release" >&2
        exit 1
        ;;
esac

# ── Parse options ──
# -c = Sophon chip model; -m = explicit resource directory (internal/compatibility use);
# -t = dev mode (disable watchdog); -T = also build cosmo-tests in this pass.
CHIP_MODEL=""
RESOURCE_DIR=""
DEV_MODE=OFF
BUILD_TESTS_FLAG=OFF
while getopts "c:m:tT" opt; do
    case $opt in
        c) CHIP_MODEL="${OPTARG,,}" ;;
        m) RESOURCE_DIR="$OPTARG" ;;
        t) DEV_MODE=ON ;;
        T) BUILD_TESTS_FLAG=ON ;;
        *) echo "Usage: $0 [-c <bm1688|cv186x> | -m <resource_repo_path>] [-t] [-T]"; exit 1 ;;
    esac
done

if [ -z "${PROJECT_ROOT_PATH:-}" ]; then
    PROJECT_ROOT_PATH="$(cd "$(dirname "$0")/.." && pwd -P)"
fi

if [ -n "${CHIP_MODEL}" ] && [ -n "${RESOURCE_DIR}" ]; then
    echo "ERROR: -c and -m cannot be used together" >&2
    exit 1
fi

if [ -z "${RESOURCE_DIR}" ]; then
    CHIP_MODEL="${CHIP_MODEL:-bm1688}"
    case "${CHIP_MODEL}" in
        bm1688|cv186x)
            RESOURCE_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource_${CHIP_MODEL}"
            ;;
        *)
            echo "ERROR: unsupported Sophon chip '${CHIP_MODEL}'; expected bm1688 or cv186x" >&2
            exit 1
            ;;
    esac
elif [ "${RESOURCE_DIR#/}" = "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/${RESOURCE_DIR}"
fi

if [ ! -d "${RESOURCE_DIR}" ]; then
    echo "ERROR: Resource directory not found: ${RESOURCE_DIR}" >&2
    exit 1
fi

BUILD_DIR="${PROJECT_ROOT_PATH}/build"
INSTALL_DIR="${BUILD_DIR}/install"
PACKAGE_DIR="${BUILD_DIR}/packages"
DEFAULT_COSMO_GUARD_SDK_DIR="${PROJECT_ROOT_PATH}/prebuild/model-guard-v2"
if [ "${COSMO_MODEL_GUARD_BUILD_PROFILE}" = "production-release" ] &&
   [ -d /build_output/model-guard-sdk-production ]; then
    DEFAULT_COSMO_GUARD_SDK_DIR=/build_output/model-guard-sdk-production
fi
COSMO_GUARD_SDK_DIR="${COSMO_MODEL_GUARD_SDK_ROOT:-${DEFAULT_COSMO_GUARD_SDK_DIR}}"
MODEL_GUARD_PROFILE_ARGS=(
    -DCOSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE}"
    -DCOSMO_PACKAGE_MODELS="${COSMO_PACKAGE_MODELS:-include}"
)
if [ -d "${INSTALL_DIR}" ]; then
    rm -rf -- "${INSTALL_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "Dev mode: ${DEV_MODE}"
if [ -n "${CHIP_MODEL}" ]; then
    echo "Sophon chip: ${CHIP_MODEL}"
fi
echo "Resource dir: ${RESOURCE_DIR}"
echo "Package variant: ${PACKAGE_VARIANT}"
echo "Internal Model Guard build profile: ${COSMO_MODEL_GUARD_BUILD_PROFILE}"
echo "Configuring protected build..."
cmake   -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DBUILD_TESTS="${BUILD_TESTS_FLAG}" \
        -DCOSMO_DEV_MODE="${DEV_MODE}" \
        -DCOSMO_TARGET_CHIP="${CHIP_MODEL:-unspecified}" \
        -DCOSMO_MODEL_GUARD_SDK_ROOT="${COSMO_GUARD_SDK_DIR}" \
        -DRESOURCE_DIR="${RESOURCE_DIR}" \
        "${MODEL_GUARD_PROFILE_ARGS[@]}" \
        ..

# Symlink compile_commands.json to project root for IDE and static analysis tools
ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true

echo "Building Cosmo ..."
build_targets=(--target install)
if [ "${BUILD_TESTS_FLAG}" = "ON" ]; then
    echo "Also building cosmo-tests in this pass..."
    build_targets+=(--target cosmo-tests)
fi
cmake --build . "${build_targets[@]}" -j"$(nproc)"

echo "Auditing installed AArch64 ELF paths..."
unsafe_elf_path=0
while IFS= read -r -d '' installed_file; do
    if aarch64-linux-gnu-readelf -hW "${installed_file}" >/dev/null 2>&1; then
        dynamic_metadata=$(aarch64-linux-gnu-readelf -dW "${installed_file}")
        if grep -Eq '/workspace|thirdparty_install|3rd/libsophon' \
                <<<"${dynamic_metadata}"
        then
            echo "ERROR: installed ELF dynamic metadata leaks a build-only path: ${installed_file}" >&2
            unsafe_elf_path=1
        fi
    fi
done < <(find "${INSTALL_DIR}" -type f -print0)
if [ "${unsafe_elf_path}" -ne 0 ]; then
    exit 1
fi

if [ "${BUILD_TESTS_FLAG}" = "ON" ]; then
    echo "Running package regression suites..."
    /usr/bin/python3 -I -B "${PROJECT_ROOT_PATH}/test/test_package_profile.py"
    /usr/bin/python3 -I -B "${PROJECT_ROOT_PATH}/test/test_verify_model_guard_v2_sdk.py"
fi

installed_python_cache="$(
    find "${INSTALL_DIR}" \
        \( -name __pycache__ -o -name '*.pyc' -o -name '*.pyo' \) \
        -print -quit
)"
if [ -n "${installed_python_cache}" ]; then
    printf 'ERROR: installed payload contains Python bytecode cache: %q\n' \
        "${installed_python_cache}" >&2
    exit 1
fi

echo "Packaging..."
cmake --build . --target package_all

shopt -s nullglob
package_artifacts=("${PACKAGE_DIR}"/*.tar.gz)
shopt -u nullglob
if [ "${#package_artifacts[@]}" -ne 1 ] ||
   [ ! -f "${package_artifacts[0]:-}" ] ||
   [ -L "${package_artifacts[0]:-}" ]; then
    echo "ERROR: packaging must produce exactly one regular archive" >&2
    exit 1
fi

/usr/bin/python3 -I -B \
    "${PROJECT_ROOT_PATH}/scripts/verify_package_contents.py" \
    --archive "${package_artifacts[0]}" \
    --build-profile "${COSMO_MODEL_GUARD_BUILD_PROFILE}" \
    --target-chip "${CHIP_MODEL:-unspecified}"

package_sha256="$(sha256sum -- "${package_artifacts[0]}")"
package_sha256="${package_sha256%% *}"
echo "Verified ${PACKAGE_VARIANT} package: ${package_artifacts[0]}"
echo "Package SHA-256: ${package_sha256}"
