#!/bin/sh
set -eu

PROJECT_ROOT_PATH=$(cd "$(dirname "$0")/.." && pwd)
BASE_IMAGE="${SOPHON_BASE_IMAGE:-stream_dev:0.2}"
IMAGE_TAR="${SOPHON_STREAM_DEV_TAR:-}"
DFSS_URL="${SOPHON_STREAM_DEV_DFSS_URL:-open@sophgo.com:/sophon-stream/docker/stream_dev_22.04.tar}"
CACHE_DIR="${SOPHON_DOCKER_CACHE_DIR:-${HOME}/.cache/cosmo/sophon}"
PIP_INDEX_URL="${SOPHON_PIP_INDEX_URL:-https://pypi.tuna.tsinghua.edu.cn/simple}"

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker is required but was not found in PATH." >&2
    exit 1
fi

ensure_dfss() {
    echo "Installing or upgrading dfss with pip3 --user..."
    if [ -n "${PIP_INDEX_URL}" ]; then
        python3 -m pip install --user --upgrade -i "${PIP_INDEX_URL}" dfss
    else
        python3 -m pip install --user --upgrade dfss
    fi
}

load_sophon_base_image() {
    if docker image inspect "${BASE_IMAGE}" >/dev/null 2>&1; then
        echo "Sophon base image already exists: ${BASE_IMAGE}"
        return
    fi

    echo "Sophon base image not found locally: ${BASE_IMAGE}"

    if [ -z "${IMAGE_TAR}" ]; then
        mkdir -p "${CACHE_DIR}"
        IMAGE_TAR="${CACHE_DIR}/$(basename "${DFSS_URL}")"
        if [ ! -f "${IMAGE_TAR}" ]; then
            ensure_dfss
            echo "Downloading Sophon stream dev image:"
            echo "  ${DFSS_URL}"
            if ! (
                cd "${CACHE_DIR}"
                python3 -m dfss --url="${DFSS_URL}"
            ); then
                echo "ERROR: Failed to download Sophon stream dev image with dfss." >&2
                echo "The Sophon dfss servers may be unreachable from this network." >&2
                echo "You can either:" >&2
                echo "  1. Download stream_dev_22.04.tar manually, then run:" >&2
                echo "     SOPHON_STREAM_DEV_TAR=/path/to/stream_dev_22.04.tar sh scripts/build_sophon_package.sh" >&2
                echo "  2. Load the image yourself with:" >&2
                echo "     docker load -i /path/to/stream_dev_22.04.tar" >&2
                echo "     sh scripts/build_sophon_package.sh" >&2
                exit 1
            fi
        fi
    fi

    if [ ! -f "${IMAGE_TAR}" ]; then
        echo "ERROR: Sophon image tar was not found: ${IMAGE_TAR}" >&2
        exit 1
    fi

    echo "Loading Sophon base image from ${IMAGE_TAR}..."
    docker load -i "${IMAGE_TAR}"

    if ! docker image inspect "${BASE_IMAGE}" >/dev/null 2>&1; then
        echo "ERROR: docker load completed, but ${BASE_IMAGE} is still unavailable." >&2
        echo "Set SOPHON_BASE_IMAGE to the image tag shown by 'docker images' and rerun this script." >&2
        exit 1
    fi
}

load_sophon_base_image

cd "${PROJECT_ROOT_PATH}"
docker compose -f docker-compose.sophon.yml up --build
