#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
root="$(mktemp -d)"
trap 'rm -rf -- "$root"' EXIT
unset COSMO_DATA_DIR COSMO_APP_DATA_DIR COSMO_PACKAGE_DATA_DIR COSMO_PACKAGE_APP_DATA_DIR

make_runtime_defaults() {
    local install_root="$1" data_dir="$2"
    mkdir -p "${install_root}/share/cosmo"
    cat >"${install_root}/share/cosmo/runtime-paths.env" <<EOF
COSMO_PACKAGE_DATA_DIR=${data_dir}
COSMO_PACKAGE_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data
EOF
}

rk_install="${root}/rk-install"
sophon_install="${root}/sophon-install"
make_runtime_defaults "$rk_install" /userdata/cwaiuserdata
make_runtime_defaults "$sophon_install" /data/cwaiuserdata

resolve_paths() {
    local install_root="$1"
    (
        COSMO_INSTALL_DIR="$install_root"
        # shellcheck source=../scripts/common.sh
        . "${repo}/scripts/common.sh"
        printf '%s\n%s\n' "$COSMO_DATA_DIR" "$COSMO_APP_DATA_DIR"
    )
}

mapfile -t rk_paths < <(resolve_paths "$rk_install")
test "${rk_paths[0]}" = /userdata/cwaiuserdata
test "${rk_paths[1]}" = /appfs/cosmo_wander/cwai_data

mapfile -t sophon_paths < <(resolve_paths "$sophon_install")
test "${sophon_paths[0]}" = /data/cwaiuserdata
test "${sophon_paths[1]}" = /appfs/cosmo_wander/cwai_data

mapfile -t override_paths < <(
    COSMO_DATA_DIR=/mnt/cosmo-data \
        COSMO_APP_DATA_DIR=/opt/cosmo-app \
        resolve_paths "$rk_install"
)
test "${override_paths[0]}" = /mnt/cosmo-data
test "${override_paths[1]}" = /opt/cosmo-app

assert_generated_defaults() {
    local chip="$1" expected_data_dir="$2" output_dir
    output_dir="${root}/generated-${chip}"
    cmake \
        -DTEST_TARGET_CHIP="$chip" \
        -DTEST_OUTPUT_DIR="$output_dir" \
        -P "${repo}/test/test_runtime_paths.cmake"
    grep -Fxq "COSMO_PACKAGE_DATA_DIR=${expected_data_dir}" "${output_dir}/runtime-paths.env"
    grep -Fxq "COSMO_PACKAGE_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data" \
        "${output_dir}/runtime-paths.env"
    grep -Fq "\"${expected_data_dir}\"" "${output_dir}/RuntimePathsConfig.h"
    grep -Fq '"/appfs/cosmo_wander/cwai_data"' "${output_dir}/RuntimePathsConfig.h"
}

assert_generated_defaults bm1688 /data/cwaiuserdata
assert_generated_defaults cv186x /data/cwaiuserdata
assert_generated_defaults x86 /data/cwaiuserdata
assert_generated_defaults rk3576 /userdata/cwaiuserdata
assert_generated_defaults rv1126b /userdata/cwaiuserdata

render_install="${root}/render-install"
mkdir -p "${render_install}/bin/nginx_conf" "${render_install}/bin/srs_conf"
cp -R "${repo}/nginx/conf" "${render_install}/bin/nginx_conf/"
cp "${repo}/cmake/srs.conf.in" "${render_install}/bin/srs_conf/srs.conf"

render_data_dir="${root}/userdata/cwaiuserdata"
(
    unset COSMO_PACKAGE_DATA_DIR COSMO_PACKAGE_APP_DATA_DIR
    COSMO_INSTALL_DIR="$render_install"
    COSMO_DATA_DIR="$render_data_dir"
    # shellcheck source=../scripts/common.sh
    . "${repo}/scripts/common.sh"
    render_runtime_configs

    test "$COSMO_RUNTIME_NGINX_PREFIX" = "${render_data_dir}/runtime/nginx_conf"
    test "$COSMO_RUNTIME_SRS_CONF" = "${render_data_dir}/runtime/srs.conf"
    grep -Fq "error_log  ${render_data_dir}/log/logs/nginx_error.log warn;" \
        "$COSMO_RUNTIME_NGINX_CONF"
    grep -Fq "alias ${render_data_dir}/event;" "$COSMO_RUNTIME_NGINX_UPSTREAM_CONF"
    grep -Fq "pid                 ${render_data_dir}/log/logs/srs.pid;" \
        "$COSMO_RUNTIME_SRS_CONF"
    ! grep -R -Fq '@COSMO_DATA_DIR@' "$COSMO_RUNTIME_NGINX_PREFIX" "$COSMO_RUNTIME_SRS_CONF"
    ! grep -R -Fq '/data/cwaiuserdata' "$COSMO_RUNTIME_NGINX_PREFIX" "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'enabled off;' "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'listen 9000;' "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'listen 5060;' "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'candidate *;' "$COSMO_RUNTIME_SRS_CONF"
    ! grep -Fq '@COSMO_GB28181_' "$COSMO_RUNTIME_SRS_CONF"
)

gb_data_dir="${root}/gb28181/cwaiuserdata"
(
    unset COSMO_PACKAGE_DATA_DIR COSMO_PACKAGE_APP_DATA_DIR
    COSMO_INSTALL_DIR="$render_install"
    COSMO_DATA_DIR="$gb_data_dir"
    COSMO_GB28181_ENABLED=on
    COSMO_GB28181_CANDIDATE=192.0.2.20
    COSMO_GB28181_SIP_PORT=15060
    COSMO_GB28181_MEDIA_PORT=19000
    # shellcheck source=../scripts/common.sh
    . "${repo}/scripts/common.sh"
    render_runtime_configs

    grep -Fq 'enabled on;' "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'listen 19000;' "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'listen 15060;' "$COSMO_RUNTIME_SRS_CONF"
    grep -Fq 'candidate 192.0.2.20;' "$COSMO_RUNTIME_SRS_CONF"
)

if (
    unset COSMO_PACKAGE_DATA_DIR COSMO_PACKAGE_APP_DATA_DIR
    COSMO_INSTALL_DIR="$render_install"
    COSMO_DATA_DIR="${root}/invalid-gb28181"
    COSMO_GB28181_ENABLED=on
    COSMO_GB28181_CANDIDATE='192.0.2.20; daemon on'
    # shellcheck source=../scripts/common.sh
    . "${repo}/scripts/common.sh"
    render_runtime_configs >/dev/null 2>&1
); then
    echo 'invalid GB28181 candidate was accepted' >&2
    exit 1
fi

echo "runtime path resolution tests passed"
