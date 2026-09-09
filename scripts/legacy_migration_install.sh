#!/bin/sh
set -eu

IFS="$(printf ' \t\n_')"
IFS="${IFS%_}"
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

fail() {
    echo "[MIGRATION] ERROR: $*" >&2
    exit 1
}

[ "$#" -le 1 ] || fail "legacy entry accepts only the optional log file"
log_file="${1:-/dev/null}"

log() {
    printf '[INSTALL] %s\n' "$*" | tee -a "$log_file"
}

script_path="$(readlink -f "$0")"
[ -n "$script_path" ] || fail "cannot resolve installer path"
payload_root="${script_path%/scripts/install.sh}"
[ "$payload_root" != "$script_path" ] || fail "installer is outside the package scripts directory"

COSMO_PACKAGE_DATA_DIR='/data/cwaiuserdata'
COSMO_PACKAGE_APP_DATA_DIR='/appfs/cosmo_wander/cwai_data'
runtime_paths_file="${payload_root}/share/cosmo/runtime-paths.env"
if [ -f "$runtime_paths_file" ]; then
    package_data_dir_seen=0
    package_app_data_dir_seen=0
    while IFS= read -r runtime_line || [ -n "$runtime_line" ]; do
        case "$runtime_line" in
            COSMO_PACKAGE_DATA_DIR=*)
                [ "$package_data_dir_seen" -eq 0 ] ||
                    fail "package runtime paths contain a duplicate data directory"
                COSMO_PACKAGE_DATA_DIR="${runtime_line#COSMO_PACKAGE_DATA_DIR=}"
                package_data_dir_seen=1
                ;;
            COSMO_PACKAGE_APP_DATA_DIR=*)
                [ "$package_app_data_dir_seen" -eq 0 ] ||
                    fail "package runtime paths contain a duplicate application directory"
                COSMO_PACKAGE_APP_DATA_DIR="${runtime_line#COSMO_PACKAGE_APP_DATA_DIR=}"
                package_app_data_dir_seen=1
                ;;
            '') ;;
            *) fail "package runtime paths contain an unsupported declaration" ;;
        esac
    done <"$runtime_paths_file"
    [ "$package_data_dir_seen" -eq 1 ] ||
        fail "package runtime paths are missing the data directory"
    [ "$package_app_data_dir_seen" -eq 1 ] ||
        fail "package runtime paths are missing the application directory"
fi
case "$COSMO_PACKAGE_DATA_DIR" in
    /data/cwaiuserdata|/userdata/cwaiuserdata) ;;
    *) fail "package data directory is unsupported" ;;
esac
[ "$COSMO_PACKAGE_APP_DATA_DIR" = /appfs/cosmo_wander/cwai_data ] ||
    fail "package application directory is incompatible"

legacy_package_data_dir='/data/cwaiuserdata'
if [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ]; then
    case "$COSMO_MIGRATION_TEST_ROOT" in /*) ;; *) fail "test root must be absolute" ;; esac
    [ "$COSMO_MIGRATION_TEST_ROOT" != / ] || fail "test root is invalid"
    active_root="${COSMO_MIGRATION_TEST_ROOT}/appfs/cosmo_wander/cwai_data"
    systemd_root="${COSMO_MIGRATION_TEST_ROOT}/etc/systemd/system"
    target_data_root="${COSMO_MIGRATION_TEST_ROOT}${COSMO_PACKAGE_DATA_DIR}"
    legacy_data_root="${COSMO_MIGRATION_TEST_ROOT}${legacy_package_data_dir}"
else
    active_root='/appfs/cosmo_wander/cwai_data'
    systemd_root='/etc/systemd/system'
    target_data_root="$COSMO_PACKAGE_DATA_DIR"
    legacy_data_root="$legacy_package_data_dir"
fi
upgrade_sign="${target_data_root}/mqttUpgradeApp"
active_parent="${active_root%/*}"
staging_root="${active_parent}/.cosmo-migration-staging.$$"
backup_root="${active_parent}/.cosmo-migration-backup"
data_migration_marker="${target_data_root}/.cosmo-data-root-migration-v1"
data_staging_root="${target_data_root%/*}/.cwaiuserdata-migration-staging.$$"
data_migration_action='none'
data_migration_created=0
app_backup_created=0
app_new_activated=0
service_temp=''

skip_legacy_data_entry() {
    case "$1" in
        .cosmo-data-root-migration-v1|cwai|log|mqttHWUpgradeApp|mqttUpgradeApp|runtime|temporary|tmp|upload|upgrade|web)
            return 0
            ;;
        *) return 1 ;;
    esac
}

directory_has_persistent_entries() {
    [ -d "$1" ] || return 1
    for data_entry in "$1"/* "$1"/.[!.]* "$1"/..?*; do
        [ -e "$data_entry" ] || [ -L "$data_entry" ] || continue
        data_name="${data_entry##*/}"
        skip_legacy_data_entry "$data_name" && continue
        return 0
    done
    return 1
}

installed_runtime_data_dir() {
    installed_runtime_file="${active_root}/share/cosmo/runtime-paths.env"
    [ -f "$installed_runtime_file" ] || return 0
    while IFS= read -r installed_runtime_line || [ -n "$installed_runtime_line" ]; do
        case "$installed_runtime_line" in
            COSMO_PACKAGE_DATA_DIR=*)
                printf '%s\n' "${installed_runtime_line#COSMO_PACKAGE_DATA_DIR=}"
                return 0
                ;;
        esac
    done <"$installed_runtime_file"
}

validate_target_storage() {
    [ ! -L "$target_data_root" ] || fail "target data root must not be a symbolic link"
    [ ! -e "$target_data_root" ] || [ -d "$target_data_root" ] ||
        fail "target data root must be a directory"

    if [ -z "${COSMO_MIGRATION_TEST_ROOT:-}" ] &&
       [ "$COSMO_PACKAGE_DATA_DIR" = /userdata/cwaiuserdata ]; then
        [ -d /userdata ] || fail "/userdata is unavailable for this Rockchip package"
        [ -r /proc/mounts ] || fail "cannot verify the /userdata mount"
        awk '
            $2 == "/userdata" {
                count = split($4, options, ",")
                for (option_index = 1; option_index <= count; option_index++) {
                    if (options[option_index] == "rw") writable_mount = 1
                }
            }
            END { exit(writable_mount ? 0 : 1) }
        ' /proc/mounts ||
            fail "/userdata is not a writable mounted filesystem; refusing fallback to the root disk"
        [ -w /userdata ] || fail "/userdata is not writable"
    fi
}

assess_data_root_migration() {
    [ "$COSMO_PACKAGE_DATA_DIR" != "$legacy_package_data_dir" ] || return 0
    validate_target_storage

    if [ -f "$data_migration_marker" ]; then
        if ! grep -Fxq 'schema=1' "$data_migration_marker" ||
           ! grep -Fxq "source=${legacy_package_data_dir}" "$data_migration_marker" ||
           ! grep -Fxq "target=${COSMO_PACKAGE_DATA_DIR}" "$data_migration_marker"; then
            fail "target data root contains an invalid migration marker"
        fi
        log "Data root migration already completed; keeping ${COSMO_PACKAGE_DATA_DIR}"
        return 0
    fi

    [ ! -L "$legacy_data_root" ] || fail "legacy data root must not be a symbolic link"
    [ ! -e "$legacy_data_root" ] || [ -d "$legacy_data_root" ] ||
        fail "legacy data root must be a directory"

    target_has_persistent_state=0
    legacy_has_persistent_state=0
    directory_has_persistent_entries "$target_data_root" && target_has_persistent_state=1
    directory_has_persistent_entries "$legacy_data_root" && legacy_has_persistent_state=1

    installed_data_dir="$(installed_runtime_data_dir)"
    if [ "$target_has_persistent_state" -eq 1 ]; then
        if [ "$installed_data_dir" = "$COSMO_PACKAGE_DATA_DIR" ]; then
            log "Installed runtime already uses ${COSMO_PACKAGE_DATA_DIR}; keeping its persistent state authoritative"
            return 0
        fi
        if [ "$legacy_has_persistent_state" -eq 1 ]; then
            fail "both legacy and target data roots contain persistent state; refusing an ambiguous merge"
        fi
        log "Only ${COSMO_PACKAGE_DATA_DIR} contains persistent state; keeping it authoritative"
        return 0
    fi
    [ "$legacy_has_persistent_state" -eq 1 ] || return 0

    mkdir -p -- "${target_data_root%/*}"
    [ ! -e "$data_staging_root" ] && [ ! -L "$data_staging_root" ] ||
        fail "data migration staging path already exists"

    if [ -z "${COSMO_MIGRATION_TEST_ROOT:-}" ]; then
        selected_data_kib=0
        for legacy_entry in \
            "$legacy_data_root"/* "$legacy_data_root"/.[!.]* "$legacy_data_root"/..?*; do
            [ -e "$legacy_entry" ] || [ -L "$legacy_entry" ] || continue
            legacy_name="${legacy_entry##*/}"
            skip_legacy_data_entry "$legacy_name" && continue
            entry_kib="$(du -sk "$legacy_entry" 2>/dev/null | awk '{ print $1 }')"
            case "$entry_kib" in
                ''|*[!0-9]*) fail "cannot measure legacy data entry" ;;
            esac
            selected_data_kib=$((selected_data_kib + entry_kib))
        done
        available_kib="$(df -Pk "${target_data_root%/*}" | awk 'NR == 2 { print $4 }')"
        case "$available_kib" in
            ''|*[!0-9]*) fail "cannot determine target data-root free space" ;;
        esac
        required_kib=$((selected_data_kib + 65536))
        [ "$available_kib" -ge "$required_kib" ] ||
            fail "target data root needs the persistent data size plus 64 MiB of free space"
    fi

    data_migration_action='copy'
    log "Persistent data will be copied from ${legacy_package_data_dir} to ${COSMO_PACKAGE_DATA_DIR}; the source will be retained"
}

migrate_data_root() {
    [ "$data_migration_action" = copy ] || return 0
    mkdir -m 0700 -- "$data_staging_root"

    for legacy_entry in \
        "$legacy_data_root"/* "$legacy_data_root"/.[!.]* "$legacy_data_root"/..?*; do
        [ -e "$legacy_entry" ] || [ -L "$legacy_entry" ] || continue
        legacy_name="${legacy_entry##*/}"
        skip_legacy_data_entry "$legacy_name" && continue
        cp -a -- "$legacy_entry" "$data_staging_root/"
    done

    if [ -d "$target_data_root" ]; then
        directory_has_persistent_entries "$target_data_root" &&
            fail "target data root gained persistent state while the service was stopping"
        rm -rf -- "$target_data_root"
    fi
    mv -- "$data_staging_root" "$target_data_root"
    data_migration_created=1
    log "Persistent data root migrated; legacy source retained at ${legacy_package_data_dir}"
}

commit_data_root_migration() {
    [ "$data_migration_created" -eq 1 ] || return 0
    marker_temp="${data_migration_marker}.tmp.$$"
    previous_umask="$(umask)"
    umask 077
    {
        printf '%s\n' \
            'schema=1' \
            "source=${legacy_package_data_dir}" \
            "target=${COSMO_PACKAGE_DATA_DIR}" \
            'source_retained=true'
    } >"$marker_temp"
    chmod 0600 "$marker_temp"
    mv -f -- "$marker_temp" "$data_migration_marker"
    umask "$previous_umask"
}

rollback_installation() {
    rollback_status="$1"
    trap - EXIT HUP INT TERM
    set +e
    [ -z "$service_temp" ] || rm -f -- "$service_temp"
    rm -rf -- "$staging_root" "$data_staging_root"
    if [ "$app_new_activated" -eq 1 ]; then
        rm -rf -- "$active_root"
    fi
    if [ "$app_backup_created" -eq 1 ] &&
       { [ -e "$backup_root" ] || [ -L "$backup_root" ]; }; then
        mv -- "$backup_root" "$active_root"
    fi
    if [ "$data_migration_created" -eq 1 ]; then
        rm -rf -- "$target_data_root"
    fi
    printf '[MIGRATION] ERROR: incomplete installation transaction rolled back\n' >&2
    exit "$rollback_status"
}

[ -f "${payload_root}/bin/cosmo-engine" ] || fail "package is missing bin/cosmo-engine"
for required_script in stop.sh start.sh inte_run_start.sh; do
    [ -x "${payload_root}/scripts/${required_script}" ] ||
        fail "package script is missing or not executable: ${required_script}"
done

log "Install Start"
log "script=${script_path}, logFile=${log_file}"
assess_data_root_migration
trap 'rollback_installation $?' EXIT
trap 'exit 1' HUP INT TERM
log "Stopping active Cosmo processes"
"${payload_root}/scripts/stop.sh"
migrate_data_root

mkdir -p -- "$active_parent"
[ ! -e "$staging_root" ] && [ ! -L "$staging_root" ] || fail "staging path already exists"
mkdir -- "$staging_root"

# Match the historical installer: preserve the installed resource tree by
# default, then overlay packaged resources. Copying the installed tree first
# avoids keeping a second packaged-resource copy on space-constrained /appfs.
# CLEAN_RESOURCE=1 makes the package resource tree authoritative.
if [ "${CLEAN_RESOURCE:-0}" != 1 ] && [ -d "${active_root}/resource" ]; then
    mkdir -- "${staging_root}/resource"
    cp -a -- "${active_root}/resource/." "${staging_root}/resource/"
fi
cp -a -- "${payload_root}/." "$staging_root/"

[ ! -e "$backup_root" ] && [ ! -L "$backup_root" ] || fail "stale migration backup exists"
if [ -e "$active_root" ] || [ -L "$active_root" ]; then
    mv -- "$active_root" "$backup_root"
    app_backup_created=1
fi
if ! mv -- "$staging_root" "$active_root"; then
    fail "cannot activate migrated application"
fi
app_new_activated=1

if [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ] &&
   [ "${COSMO_MIGRATION_TEST_FAIL_AFTER_ACTIVATION:-0}" = 1 ]; then
    fail "injected post-activation failure"
fi

# Preserve the historical post-install filesystem contract.
mkdir -p -- "${active_root}/web/staticfile" "${active_root}/bin/nginx_conf/logs"
rm -f -- \
    "${active_root}/web/staticfile/httpInterface.html" \
    "${active_root}/web/staticfile/mqttInterface.html"
ln -s -- \
    "${active_root}/files/Interface/ai-box-interface_v1.0.html" \
    "${active_root}/web/staticfile/httpInterface.html"
ln -s -- \
    "${active_root}/files/Interface/mqtt_v1.0.html" \
    "${active_root}/web/staticfile/mqttInterface.html"
rm -f -- "${active_root}/scripts/install.sh"

# Always replace the unit. Existing main installations may still point to the
# former /appfs/minivision/mv_data tree, while a deleted unit must be recreated.
mkdir -p -- "$systemd_root"
service_file="${systemd_root}/cosmo.service"
service_temp="${service_file}.tmp.$$"
umask 022
{
    printf '%s\n' \
        '[Unit]' \
        'Description=Cosmo Edge AI Engine' \
        'Wants=network-online.target' \
        'After=network-online.target docker.service' \
        '' \
        '[Service]' \
        'Type=simple' \
        'User=root' \
        'EnvironmentFile=-/appfs/cosmo_wander/cwai_data/share/cosmo/runtime-paths.env' \
        'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' \
        'Restart=on-failure' \
        'RestartSec=10' \
        '' \
        '[Install]' \
        'WantedBy=multi-user.target'
} >"$service_temp"
chmod 0644 "$service_temp"
mv -f -- "$service_temp" "$service_file"
service_temp=''

if [ -n "${COSMO_MIGRATION_TEST_ROOT:-}" ]; then
    wants_dir="${systemd_root}/multi-user.target.wants"
    mkdir -p -- "$wants_dir"
    ln -sfn ../cosmo.service "${wants_dir}/cosmo.service"
else
    systemctl daemon-reload
    systemctl enable cosmo.service
fi

commit_data_root_migration
mkdir -p -- "${upgrade_sign%/*}"
: >"$upgrade_sign"
sync
trap - EXIT HUP INT TERM
if ! rm -rf -- "$backup_root"; then
    log "WARNING: previous application backup could not be removed: ${backup_root}"
fi
log "Install files Done"
log "systemd service [cosmo] installed and enabled"
log "installed legacy-compatible bridge at ${active_root}"
log "Install End"
