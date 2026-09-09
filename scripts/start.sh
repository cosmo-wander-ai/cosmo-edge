#!/bin/bash
set -euo pipefail

IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

# Permanent MD5-package upgrade orchestrator. Open and Protected editions use
# this same lifecycle on every upgrade; model authorization is independent.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

ensure_runtime_dirs
action="${1:-}"
log_file="${COSMO_LOG_DIR}/INTE_RUN_now.1"
log_tag='INTE_RUN'

if [ "$action" = stop ]; then
    "${SCRIPT_DIR}/stop.sh"
    exit 0
fi
if [ "$action" != start ]; then
    cosmo_log "$log_tag" "Unsupported action: ${action}" "$log_file"
    exit 2
fi

run_active() {
    rm -rf -- "${COSMO_UPGRADE_DIR:?}/"*
    cd "${COSMO_INSTALL_DIR}/scripts"
    INSTALLPATH="$COSMO_INSTALL_DIR" exec "${COSMO_INSTALL_DIR}/scripts/run_start.sh" start "$log_file"
}

is_compatible_archive_name() {
    printf '%s\n' "$1" |
        grep -Eq '^cosmo-[Vv][0-9]+\.[0-9]+\.[0-9]+-[0-9a-fA-F]{32}\.tar\.gz$'
}

archive=''
archive_count=0
for candidate in "${COSMO_UPGRADE_DIR}"/*.tar.gz; do
    [ -f "$candidate" ] || continue
    if is_compatible_archive_name "$(basename "$candidate")"; then
        archive="$candidate"
        archive_count=$((archive_count + 1))
    fi
done

if [ "$archive_count" -eq 0 ]; then
    run_active
fi
if [ "$archive_count" -ne 1 ]; then
    cosmo_log "$log_tag" "Upgrade directory must contain exactly one compatible archive" "$log_file"
    run_active
fi

expected_md5="${archive%.tar.gz}"
expected_md5="${expected_md5##*-}"
expected_md5="$(printf '%s' "$expected_md5" | tr 'A-F' 'a-f')"
actual_md5="$(md5sum -- "$archive")"
actual_md5="${actual_md5%% *}"
if [ "$actual_md5" != "$expected_md5" ]; then
    cosmo_log "$log_tag" "Upgrade archive MD5 mismatch" "$log_file"
    run_active
fi

package_root=''
for candidate in "${COSMO_UPGRADE_DIR}"/*; do
    [ -d "$candidate" ] || continue
    valid=yes
    for directory in bin files font lib scripts web; do
        if [ ! -d "${candidate}/${directory}" ]; then
            valid=no
            break
        fi
    done
    if [ "$valid" = yes ]; then
        if [ -n "$package_root" ]; then
            cosmo_log "$log_tag" "Upgrade directory contains multiple package roots" "$log_file"
            run_active
        fi
        package_root="$candidate"
    fi
done

if [ -z "$package_root" ] || [ ! -x "${package_root}/scripts/install.sh" ]; then
    cosmo_log "$log_tag" "Compatible package root or installer is missing" "$log_file"
    run_active
fi

cosmo_log "$log_tag" "Installing compatible package $(basename "$archive")" "$log_file"
if "${package_root}/scripts/install.sh" "$log_file"; then
    cosmo_log "$log_tag" "Compatible package installation completed" "$log_file"
else
    install_status=$?
    cosmo_log "$log_tag" \
        "Package installation failed with status ${install_status}; restoring the active application" \
        "$log_file"
    run_active
fi
mkdir -p -- "$(dirname "$COSMO_UPGRADE_SIGN")"
: >"$COSMO_UPGRADE_SIGN"
sync
run_active
