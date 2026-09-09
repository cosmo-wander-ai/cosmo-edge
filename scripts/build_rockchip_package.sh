#!/usr/bin/env bash
set -euo pipefail

chip="rk3576"
package_models="include"
build_profile="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"

while (($#)); do
    case "$1" in
        --chip)
            if (($# < 2)); then
                echo "ERROR: --chip requires rk3576 or rv1126b" >&2
                exit 2
            fi
            chip="$2"
            shift 2
            ;;
        --models)
            if (($# < 2)); then
                echo "ERROR: --models requires include or preserve" >&2
                exit 2
            fi
            package_models="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--chip <rk3576|rv1126b>] [--models <include|preserve>]"
            exit 0
            ;;
        *)
            echo "ERROR: unsupported argument: $1" >&2
            exit 2
            ;;
    esac
done

case "${chip}" in
    rk3576|rv1126b) ;;
    *)
        echo "ERROR: unsupported Rockchip target '${chip}'; expected rk3576 or rv1126b" >&2
        exit 2
        ;;
esac
case "${build_profile}" in
    public-runtime|production-release) ;;
    *)
        echo "ERROR: unsupported Model Guard profile '${build_profile}'" >&2
        exit 2
        ;;
esac
case "${package_models}" in
    include|preserve) ;;
    *)
        echo "ERROR: unsupported model policy '${package_models}'; expected include or preserve" >&2
        exit 2
        ;;
esac

if [ -z "${PROJECT_ROOT_PATH:-}" ]; then
    PROJECT_ROOT_PATH=$(cd "$(dirname "$0")/.." && pwd -P)
fi
builder_lock="${PROJECT_ROOT_PATH}/config/rockchip-build/builder-lock.json"
image_lock="${COSMO_ROCKCHIP_BUILDER_LOCK:-/opt/cosmo/rockchip-builder-lock.json}"
if [ ! -f "${builder_lock}" ]; then
    echo "ERROR: Rockchip builder lock is missing: ${builder_lock}" >&2
    exit 1
fi
if [ ! -f "${image_lock}" ]; then
    echo "ERROR: this container is not a locked CosmoEdge Rockchip builder" >&2
    exit 1
fi
if ! cmp -s "${builder_lock}" "${image_lock}"; then
    echo "ERROR: builder image lock does not match this source checkout" >&2
    exit 1
fi

IFS=$'\t' read -r rknn_root rkllm_root media_root media_runtime rkllm_required < <(
    python3 - "${builder_lock}" "${chip}" <<'PY'
import json
import pathlib
import sys

lock = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
chip = sys.argv[2]
common = lock["common"]
target = lock["targets"][chip]
values = (
    common["rknn_root"],
    common["rkllm_root"],
    target["media_root"],
    target["media_runtime_profile"],
    "ON" if target["rkllm_required"] else "OFF",
)
if any("\t" in str(value) or "\n" in str(value) for value in values):
    raise SystemExit("builder lock values must be single-line fields")
print("\t".join(str(value) for value in values))
PY
)

test -f "${rknn_root}/include/rknn_api.h"
test -f "${rknn_root}/lib/librknnrt.so"
test -f "${media_root}/include/rockchip/rk_mpi.h"
test -f "${media_root}/include/rga/im2d.h"
test -f "${media_root}/lib/librockchip_mpp.so"
test -f "${media_root}/lib/librga.so"
if [ "${rkllm_required}" = "ON" ]; then
    test -f "${rkllm_root}/include/rkllm.h"
    test -f "${rkllm_root}/lib/librkllmrt.so"
    test -f "${rkllm_root}/LICENSE"
else
    rkllm_root="${rknn_root}"
fi

rm -rf "${PROJECT_ROOT_PATH}/build_rknn"
COSMO_PACKAGE_MODELS="${package_models}" \
COSMO_MODEL_GUARD_BUILD_PROFILE="${build_profile}" \
COSMO_RKLLM_REQUIRED="${rkllm_required}" \
RKNN_ROOT="${rknn_root}" \
RKLLM_ROOT="${rkllm_root}" \
ROCKCHIP_MEDIA_ROOT="${media_root}" \
    "${PROJECT_ROOT_PATH}/scripts/build_rknn.sh" -c "${chip}" -T

shopt -s nullglob
packages=("${PROJECT_ROOT_PATH}"/build_rknn/packages/*.tar.gz)
if ((${#packages[@]} != 1)) || [ ! -f "${packages[0]:-}" ] || [ -L "${packages[0]:-}" ]; then
    echo "ERROR: expected exactly one regular ${chip} package artifact" >&2
    exit 1
fi

package="${packages[0]}"
python3 "${PROJECT_ROOT_PATH}/scripts/verify_package_contents.py" \
    --archive "$(cd "$(dirname "${package}")" && pwd -P)/$(basename "${package}")" \
    --build-profile "${build_profile}" \
    --target-chip "${chip}" \
    --target-policy-lock "${builder_lock}"

output_root="${COSMO_BUILD_OUTPUT_ROOT:-/build_output}"
if [ "${build_profile}" = "production-release" ]; then
    output_dir="${output_root}/${build_profile}/${chip}"
else
    output_dir="${output_root}/${chip}"
fi
rm -rf "${output_dir}"
mkdir -p "${output_dir}"
cp -f -- "${package}" "${output_dir}/"
printf '%s\n' "${chip}" > "${output_dir}/TARGET_CHIP"
printf '%s\n' "${media_runtime}" > "${output_dir}/MEDIA_RUNTIME_PROFILE"
(
    cd "${output_dir}"
    sha256sum -- "$(basename "${package}")" > SHA256SUMS
    sha256sum -c SHA256SUMS
)
ls -lh "${output_dir}"
