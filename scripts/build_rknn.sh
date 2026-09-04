#!/bin/bash
set -euo pipefail
export LC_ALL=C.UTF-8

RESOURCE_DIR=""
TARGET_CHIP="${COSMO_TARGET_CHIP:-rk3576}"
RKNN_ROOT_PATH="${RKNN_ROOT:-}"
ROCKCHIP_MEDIA_ROOT_PATH="${ROCKCHIP_MEDIA_ROOT:-}"
RKLLM_ROOT_PATH="${RKLLM_ROOT:-}"
RKLLM_REQUIRED="${COSMO_RKLLM_REQUIRED:-OFF}"
MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"
case "${MODEL_GUARD_BUILD_PROFILE}" in
    public-runtime|production-release) ;;
    *)
        echo "ERROR: COSMO_MODEL_GUARD_BUILD_PROFILE must be public-runtime or production-release" >&2
        exit 1
        ;;
esac
MODEL_GUARD_SDK_ROOT="${COSMO_MODEL_GUARD_SDK_ROOT:-}"
DEV_MODE=OFF
BUILD_TESTS_FLAG=OFF
while getopts "c:m:r:p:tT" opt; do
    case ${opt} in
        c) TARGET_CHIP="${OPTARG}" ;;
        m) RESOURCE_DIR="${OPTARG}" ;;
        r) RKNN_ROOT_PATH="${OPTARG}" ;;
        p) ROCKCHIP_MEDIA_ROOT_PATH="${OPTARG}" ;;
        t) DEV_MODE=ON ;;
        T) BUILD_TESTS_FLAG=ON ;;
        *) echo "Usage: $0 -r <rknn-runtime-root> [-c rk3576|rv1126b] [-p <rockchip-media-root>] [-m <resource-dir>] [-t] [-T]"; exit 1 ;;
    esac
done

if [ -z "${PROJECT_ROOT_PATH:-}" ]; then
    PROJECT_ROOT_PATH=$(cd "$(dirname "$0")/.." && pwd)
fi
if [ -z "${MODEL_GUARD_SDK_ROOT}" ]; then
    MODEL_GUARD_SDK_ROOT="${PROJECT_ROOT_PATH}/prebuild/model-guard-v2-rknn-abi1"
fi
MODEL_GUARD_CMAKE_ARGS=(
    -DCOSMO_MODEL_GUARD_BUILD_PROFILE="${MODEL_GUARD_BUILD_PROFILE}"
)
if [ "${MODEL_GUARD_BUILD_PROFILE}" = "production-release" ]; then
    MODEL_GUARD_CMAKE_ARGS+=(
        -DCOSMO_MODEL_GUARD_SDK_ROOT="${MODEL_GUARD_SDK_ROOT}"
    )
fi

BUILD_JOBS="${COSMO_BUILD_JOBS:-$(nproc)}"
if ! [[ "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: COSMO_BUILD_JOBS must be a positive integer" >&2
    exit 1
fi

TARGET_CHIP=$(printf '%s' "${TARGET_CHIP}" | tr '[:upper:]' '[:lower:]')
PLATFORM_PROFILE="${PROJECT_ROOT_PATH}/config/rknn/platforms/${TARGET_CHIP}.json"
if [ ! -f "${PLATFORM_PROFILE}" ]; then
    echo "ERROR: unsupported RKNN platform profile: ${TARGET_CHIP}" >&2
    exit 1
fi
IFS=$'\t' read -r PROFILE_CHIP PROFILE_MEDIA_DEFAULT PROFILE_OVERLAY PROFILE_MODELS PROFILE_ARTIFACT_MANIFEST < <(
    python3 - "${PLATFORM_PROFILE}" <<'PY'
import json
import pathlib
import sys

profile = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
values = (
    profile["chip"],
    profile["media"]["default_backend"],
    profile["packaging"]["resource_overlay_directory"],
    profile["packaging"]["legacy_models_directory"],
    profile["packaging"].get("artifact_manifest", "-"),
)
if any("\t" in str(value) or "\n" in str(value) for value in values):
    raise SystemExit("platform profile values must be single-line fields")
print("\t".join(str(value) for value in values))
PY
)
if [ "${PROFILE_CHIP}" != "${TARGET_CHIP}" ]; then
    echo "ERROR: platform profile chip mismatch: ${PROFILE_CHIP} != ${TARGET_CHIP}" >&2
    exit 1
fi

MEDIA_CPU_BACKEND=ON
MEDIA_ROCKCHIP_BACKEND=OFF
if [ -n "${ROCKCHIP_MEDIA_ROOT_PATH}" ]; then
    python3 "${PROJECT_ROOT_PATH}/tools/rknn/media_sysroot_lock.py" verify \
        --platform-profile "${PLATFORM_PROFILE}" \
        --root "${ROCKCHIP_MEDIA_ROOT_PATH}"
    MEDIA_CPU_BACKEND=OFF
    MEDIA_ROCKCHIP_BACKEND=ON
fi
if [ "${PROFILE_MEDIA_DEFAULT}" = "rockchip" ] && [ -z "${ROCKCHIP_MEDIA_ROOT_PATH}" ]; then
    echo "INFO: ${TARGET_CHIP} profile prefers Rockchip media; using the CPU fallback because no media root was supplied"
fi
if [ -z "${RKNN_ROOT_PATH}" ]; then
    echo "ERROR: pass -r <path> or set RKNN_ROOT" >&2
    exit 1
fi
if [ -z "${RKLLM_ROOT_PATH}" ]; then
    RKLLM_ROOT_PATH="${RKNN_ROOT_PATH}"
fi
if [ "${RKLLM_REQUIRED}" = "ON" ]; then
    for required_file in include/rkllm.h lib/librkllmrt.so LICENSE; do
        if [ ! -f "${RKLLM_ROOT_PATH}/${required_file}" ]; then
            echo "ERROR: RKLLM is required, but ${RKLLM_ROOT_PATH}/${required_file} is missing" >&2
            exit 1
        fi
    done
fi
if [ -z "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/data/resource/aiboxresource_x86"
elif [ "${RESOURCE_DIR#/}" = "${RESOURCE_DIR}" ]; then
    RESOURCE_DIR="${PROJECT_ROOT_PATH}/${RESOURCE_DIR}"
fi

RESOURCE_MODELS_DIR="${COSMO_RKNN_MODELS_DIR:-${PROJECT_ROOT_PATH}/${PROFILE_MODELS}}"
RESOURCE_OVERLAY_DIR="${COSMO_RKNN_RESOURCE_OVERLAY_DIR:-${PROJECT_ROOT_PATH}/${PROFILE_OVERLAY}}"
ARTIFACT_MANIFEST="${COSMO_RKNN_ARTIFACT_MANIFEST:-${PROFILE_ARTIFACT_MANIFEST}}"
if [ "${ARTIFACT_MANIFEST}" = "none" ] || [ "${ARTIFACT_MANIFEST}" = "-" ]; then
    ARTIFACT_MANIFEST=""
elif [ "${ARTIFACT_MANIFEST#/}" = "${ARTIFACT_MANIFEST}" ]; then
    ARTIFACT_MANIFEST="${PROJECT_ROOT_PATH}/${ARTIFACT_MANIFEST}"
fi
PACKAGE_MODELS="${COSMO_PACKAGE_MODELS:-include}"
if [ "${PACKAGE_MODELS}" = "include" ]; then
    if [ -n "${ARTIFACT_MANIFEST}" ]; then
        python3 "${PROJECT_ROOT_PATH}/tools/rknn/stage_platform_resources.py" \
            --platform-profile "${PLATFORM_PROFILE}" \
            --artifact-manifest "${ARTIFACT_MANIFEST}" \
            --output-dir "${RESOURCE_OVERLAY_DIR}" \
            --force
    else
        python3 "${PROJECT_ROOT_PATH}/tools/rknn/stage_platform_resources.py" \
            --platform-profile "${PLATFORM_PROFILE}" \
            --output-dir "${RESOURCE_OVERLAY_DIR}" \
            --verify
    fi
    if [ ! -d "${RESOURCE_MODELS_DIR}" ]; then
        echo "ERROR: target-specific model directory is missing: ${RESOURCE_MODELS_DIR}" >&2
        echo "Provide a staged target model bundle, or set COSMO_PACKAGE_MODELS=preserve for a code-only build." >&2
        exit 1
    fi
fi
BUILD_DIR="${PROJECT_ROOT_PATH}/build_rknn"
INSTALL_DIR="${BUILD_DIR}/install"
mkdir -p "${BUILD_DIR}"
rm -rf "${INSTALL_DIR}"

cmake -S "${PROJECT_ROOT_PATH}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCOSMO_TARGET_ARCH=aarch64 \
    -DCOSMO_TARGET_CHIP="${TARGET_CHIP}" \
    -DCOSMO_NN_USE_SOPHON_BACKEND=OFF \
    -DCOSMO_NN_USE_CPU_BACKEND=OFF \
    -DCOSMO_NN_USE_RKNN_BACKEND=ON \
    -DCOSMO_MEDIA_USE_SOPHON_BACKEND=OFF \
    -DCOSMO_MEDIA_USE_CPU_BACKEND="${MEDIA_CPU_BACKEND}" \
    -DCOSMO_MEDIA_USE_ROCKCHIP_BACKEND="${MEDIA_ROCKCHIP_BACKEND}" \
    -DCOSMO_RKNN_ROOT="${RKNN_ROOT_PATH}" \
    -DCOSMO_RKLLM_ROOT="${RKLLM_ROOT_PATH}" \
    -DCOSMO_RKLLM_REQUIRED="${RKLLM_REQUIRED}" \
    -DCOSMO_ROCKCHIP_MEDIA_ROOT="${ROCKCHIP_MEDIA_ROOT_PATH}" \
    -DCOSMO_DEV_MODE="${DEV_MODE}" \
    "${MODEL_GUARD_CMAKE_ARGS[@]}" \
    -DCOSMO_PACKAGE_MODELS="${PACKAGE_MODELS}" \
    -DBUILD_TESTS="${BUILD_TESTS_FLAG}" \
    -DRESOURCE_DIR="${RESOURCE_DIR}" \
    -DRESOURCE_OVERLAY_DIR="${RESOURCE_OVERLAY_DIR}" \
    -DRESOURCE_MODELS_DIR="${RESOURCE_MODELS_DIR}"

ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT_PATH}/compile_commands.json" 2>/dev/null || true
cmake --build "${BUILD_DIR}" --target install -j"${BUILD_JOBS}"
if [ "${BUILD_TESTS_FLAG}" = "ON" ]; then
    cmake --build "${BUILD_DIR}" --target cosmo-tests -j"${BUILD_JOBS}"
fi
cmake --build "${BUILD_DIR}" --target package_all
