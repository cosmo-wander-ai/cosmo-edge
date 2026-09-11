#!/bin/bash
set -euo pipefail

COSMO_MODEL_GUARD_BUILD_PROFILE="${COSMO_MODEL_GUARD_BUILD_PROFILE:-public-runtime}"
case "${COSMO_MODEL_GUARD_BUILD_PROFILE}" in
    public-runtime|production-release) ;;
    *)
        echo "ERROR: COSMO_MODEL_GUARD_BUILD_PROFILE must be public-runtime or production-release" >&2
        exit 1
        ;;
esac

chip="bm1688"
if [ "$#" -gt 0 ]; then
    if [ "$#" -ne 2 ] || [ "$1" != "--chip" ]; then
        echo "Usage: $0 [--chip <bm1688|cv186x>]" >&2
        exit 1
    fi
    chip="$2"
fi
chip="${chip,,}"
case "${chip}" in
    bm1688|cv186x) ;;
    *)
        echo "ERROR: unsupported Sophon chip '${chip}'; expected bm1688 or cv186x" >&2
        exit 1
        ;;
esac

if [ "${COSMO_MODEL_GUARD_BUILD_PROFILE}" = "public-runtime" ]; then
    package_variant="Open"
else
    package_variant="Protected"
fi

echo "Starting ${package_variant} cross-compilation for ${chip}..."
if [ -z "${PROJECT_ROOT_PATH:-}" ]; then
    PROJECT_ROOT_PATH=$(cd "$(dirname "$0")/.." && pwd -P)
fi
"${PROJECT_ROOT_PATH}/scripts/build.sh" -T -c "${chip}"

output_dir="${COSMO_BUILD_OUTPUT_ROOT:-/build_output}/${COSMO_MODEL_GUARD_BUILD_PROFILE}/${chip}"

shopt -s nullglob
package_artifacts=("${PROJECT_ROOT_PATH}"/build/packages/*.tar.gz)
shopt -u nullglob
if [ "${#package_artifacts[@]}" -ne 1 ] || [ ! -f "${package_artifacts[0]:-}" ] || [ -L "${package_artifacts[0]:-}" ]; then
    echo "ERROR: expected exactly one package artifact" >&2
    exit 1
fi

python3 "${PROJECT_ROOT_PATH}/scripts/verify_package_contents.py" \
    --archive "${package_artifacts[0]}" \
    --build-profile "${COSMO_MODEL_GUARD_BUILD_PROFILE}" \
    --target-chip "${chip}"

rm -rf -- "${output_dir}"
mkdir -p "${output_dir}"
package_name="${package_artifacts[0]##*/}"
cp -f -- "${package_artifacts[0]}" "${output_dir}/${package_name}"
printf '%s\n' "${chip}" > "${output_dir}/TARGET_CHIP"
(cd "${output_dir}" && sha256sum -- "${package_name}" > SHA256SUMS)
ls -lh "${output_dir}"
echo "Build finished."
