#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
root="$(mktemp -d)"
trap 'rm -rf -- "$root"' EXIT

payload="$root/payload"
active="$root/appfs/cosmo_wander/cwai_data"
mkdir -p "$payload/scripts" "$payload/bin" "$payload/files/Interface" \
    "$payload/web" "$active/resource/models" "$active/bin"
cp "$repo/scripts/legacy_migration_install.sh" "$payload/scripts/install.sh"
printf 'new\n' >"$payload/bin/cosmo-engine"
printf '#!/bin/sh\ntouch "$COSMO_MIGRATION_TEST_ROOT/stop.called"\n' >"$payload/scripts/stop.sh"
printf '#!/bin/sh\n' >"$payload/scripts/start.sh"
printf '#!/bin/sh\n' >"$payload/scripts/inte_run_start.sh"
chmod +x "$payload/scripts/"*.sh
printf 'http\n' >"$payload/files/Interface/ai-box-interface_v1.0.html"
printf 'mqtt\n' >"$payload/files/Interface/mqtt_v1.0.html"
printf 'old\n' >"$active/bin/cosmo-engine"
printf 'existing-model\n' >"$active/resource/models/model.nn"
printf 'preserved\n' >"$active/resource/device-local.conf"

COSMO_MIGRATION_TEST_ROOT="$root" \
    sh "$payload/scripts/install.sh" "$root/install.log"

grep -Fxq new "$active/bin/cosmo-engine"
grep -Fxq existing-model "$active/resource/models/model.nn"
grep -Fxq preserved "$active/resource/device-local.conf"
test ! -e "$root/appfs/cosmo_wander/.cosmo-migration-backup"
test -f "$root/stop.called"
test ! -e "$active/scripts/install.sh"
test -d "$active/bin/nginx_conf/logs"
test -L "$active/web/staticfile/httpInterface.html"
test -L "$active/web/staticfile/mqttInterface.html"
test -f "$root/data/cwaiuserdata/mqttUpgradeApp"
service="$root/etc/systemd/system/cosmo.service"
test -f "$service"
grep -Fxq 'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' "$service"
grep -Fxq 'EnvironmentFile=-/appfs/cosmo_wander/cwai_data/share/cosmo/runtime-paths.env' "$service"
grep -Fxq 'Restart=on-failure' "$service"
grep -Fxq 'RestartSec=10' "$service"
test -L "$root/etc/systemd/system/multi-user.target.wants/cosmo.service"

# The same permanent MD5 lifecycle must remain valid after the first bridge
# from main; a later package uses the same installer contract.
printf 'newer\n' >"$payload/bin/cosmo-engine"
sed -i.bak 's#/appfs/cosmo_wander/cwai_data#/appfs/minivision/mv_data#' "$service"
rm -f -- "${service}.bak"
COSMO_MIGRATION_TEST_ROOT="$root" \
    sh "$payload/scripts/install.sh" "$root/install-again.log"
grep -Fxq newer "$active/bin/cosmo-engine"
grep -Fxq existing-model "$active/resource/models/model.nn"
grep -Fxq 'ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh' "$service"

rm -rf -- "$payload" "$active"
mkdir -p "$payload/scripts" "$payload/bin" "$payload/resource/models" "$active/resource/models"
cp "$repo/scripts/legacy_migration_install.sh" "$payload/scripts/install.sh"
printf 'new\n' >"$payload/bin/cosmo-engine"
printf '#!/bin/sh\n' >"$payload/scripts/stop.sh"
printf '#!/bin/sh\n' >"$payload/scripts/start.sh"
printf '#!/bin/sh\n' >"$payload/scripts/inte_run_start.sh"
chmod +x "$payload/scripts/"*.sh
printf 'packaged-model\n' >"$payload/resource/models/model.nn"
printf 'existing-model\n' >"$active/resource/models/model.nn"
printf 'preserved\n' >"$active/resource/device-local.conf"

COSMO_MIGRATION_TEST_ROOT="$root" \
    sh "$payload/scripts/install.sh" "$root/install.log"

grep -Fxq packaged-model "$active/resource/models/model.nn"
grep -Fxq preserved "$active/resource/device-local.conf"

printf 'stale\n' >"$active/resource/stale.conf"
COSMO_MIGRATION_TEST_ROOT="$root" CLEAN_RESOURCE=1 \
    sh "$payload/scripts/install.sh" "$root/install-clean.log"
test ! -e "$active/resource/stale.conf"
grep -Fxq packaged-model "$active/resource/models/model.nn"

# Rockchip packages place mutable state below /userdata while retaining the
# shared /appfs application root.
rk_root="${root}/rk-case"
rk_payload="${rk_root}/payload"
rk_active="${rk_root}/appfs/cosmo_wander/cwai_data"
rk_legacy_data="${rk_root}/data/cwaiuserdata"
rk_target_data="${rk_root}/userdata/cwaiuserdata"
mkdir -p "$rk_payload/scripts" "$rk_payload/bin" "$rk_payload/files/Interface" \
    "$rk_payload/web" "$rk_payload/share/cosmo" \
    "$rk_legacy_data/conf" "$rk_legacy_data/confb" "$rk_legacy_data/db" \
    "$rk_legacy_data/db2" "$rk_legacy_data/camera" \
    "$rk_legacy_data/resource/models" "$rk_legacy_data/weblibPic/personPicture" \
    "$rk_legacy_data/event/2026/08/21" "$rk_legacy_data/upload/sessions/in-flight" \
    "$rk_legacy_data/upgrade" "$rk_legacy_data/tmp" "$rk_legacy_data/log" \
    "$rk_legacy_data/cwai" "$rk_legacy_data/runtime" "$rk_legacy_data/web"
cp "$repo/scripts/legacy_migration_install.sh" "$rk_payload/scripts/install.sh"
printf 'new\n' >"$rk_payload/bin/cosmo-engine"
printf '#!/bin/sh\n' >"$rk_payload/scripts/stop.sh"
printf '#!/bin/sh\n' >"$rk_payload/scripts/start.sh"
printf '#!/bin/sh\n' >"$rk_payload/scripts/inte_run_start.sh"
chmod +x "$rk_payload/scripts/"*.sh
printf 'http\n' >"$rk_payload/files/Interface/ai-box-interface_v1.0.html"
printf 'mqtt\n' >"$rk_payload/files/Interface/mqtt_v1.0.html"
printf '%s\n' \
    'COSMO_PACKAGE_DATA_DIR=/userdata/cwaiuserdata' \
    'COSMO_PACKAGE_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data' \
    >"$rk_payload/share/cosmo/runtime-paths.env"
printf 'configuration\n' >"$rk_legacy_data/conf/device.json"
printf 'backup-configuration\n' >"$rk_legacy_data/confb/device.json"
printf 'database\n' >"$rk_legacy_data/db/cosmo.db"
printf 'database-backup\n' >"$rk_legacy_data/db2/cosmo.db"
printf 'camera\n' >"$rk_legacy_data/camera/camera.json"
printf 'user-model\n' >"$rk_legacy_data/resource/models/model.nn"
printf 'person\n' >"$rk_legacy_data/weblibPic/personPicture/person.jpg"
printf 'event\n' >"$rk_legacy_data/event/2026/08/21/event.json"
printf 'session\n' >"$rk_legacy_data/upload/sessions/in-flight/chunk.bin"
printf 'archive\n' >"$rk_legacy_data/upgrade/cosmo.tar.gz"
printf 'temporary\n' >"$rk_legacy_data/tmp/file"
printf 'log\n' >"$rk_legacy_data/log/file"
printf 'runtime\n' >"$rk_legacy_data/cwai/file"
printf 'runtime\n' >"$rk_legacy_data/runtime/file"
printf 'web\n' >"$rk_legacy_data/web/file"

COSMO_MIGRATION_TEST_ROOT="$rk_root" \
    sh "$rk_payload/scripts/install.sh" "$rk_root/install.log"

test -f "$rk_root/userdata/cwaiuserdata/mqttUpgradeApp"
test ! -e "$rk_root/data/cwaiuserdata/mqttUpgradeApp"
grep -Fxq configuration "$rk_target_data/conf/device.json"
grep -Fxq backup-configuration "$rk_target_data/confb/device.json"
grep -Fxq database "$rk_target_data/db/cosmo.db"
grep -Fxq database-backup "$rk_target_data/db2/cosmo.db"
grep -Fxq camera "$rk_target_data/camera/camera.json"
grep -Fxq user-model "$rk_target_data/resource/models/model.nn"
grep -Fxq person "$rk_target_data/weblibPic/personPicture/person.jpg"
grep -Fxq event "$rk_target_data/event/2026/08/21/event.json"
test ! -e "$rk_target_data/upload"
test ! -e "$rk_target_data/upgrade"
test ! -e "$rk_target_data/tmp"
test ! -e "$rk_target_data/log"
test ! -e "$rk_target_data/cwai"
test ! -e "$rk_target_data/runtime"
test ! -e "$rk_target_data/web"
test -f "$rk_target_data/.cosmo-data-root-migration-v1"
grep -Fxq 'schema=1' "$rk_target_data/.cosmo-data-root-migration-v1"
grep -Fxq 'source=/data/cwaiuserdata' "$rk_target_data/.cosmo-data-root-migration-v1"
grep -Fxq 'target=/userdata/cwaiuserdata' "$rk_target_data/.cosmo-data-root-migration-v1"
grep -Fxq 'source_retained=true' "$rk_target_data/.cosmo-data-root-migration-v1"
marker_mode="$(stat -c '%a' "$rk_target_data/.cosmo-data-root-migration-v1" 2>/dev/null || \
    stat -f '%Lp' "$rk_target_data/.cosmo-data-root-migration-v1")"
test "$marker_mode" = 600
test -f "$rk_legacy_data/db/cosmo.db"
test -f "$rk_legacy_data/upload/sessions/in-flight/chunk.bin"
grep -Fq 'Persistent data root migrated; legacy source retained' "$rk_root/install.log"
test -f "$rk_active/share/cosmo/runtime-paths.env"
grep -Fxq 'EnvironmentFile=-/appfs/cosmo_wander/cwai_data/share/cosmo/runtime-paths.env' \
    "$rk_root/etc/systemd/system/cosmo.service"

# A completed migration is idempotent. The retained legacy source must not
# overwrite newer state already written below /userdata.
printf 'authoritative-target\n' >"$rk_target_data/db/cosmo.db"
printf 'stale-legacy\n' >"$rk_legacy_data/db/cosmo.db"
printf 'newer\n' >"$rk_payload/bin/cosmo-engine"
COSMO_MIGRATION_TEST_ROOT="$rk_root" \
    sh "$rk_payload/scripts/install.sh" "$rk_root/install-again.log"
grep -Fxq authoritative-target "$rk_target_data/db/cosmo.db"
grep -Fq 'Data root migration already completed' "$rk_root/install-again.log"

# If both roots contain state and the installed application does not already
# declare /userdata authoritative, fail before stopping or replacing anything.
conflict_root="${root}/rk-conflict"
conflict_payload="${conflict_root}/payload"
mkdir -p "$conflict_root/data/cwaiuserdata/conf" \
    "$conflict_root/userdata/cwaiuserdata/conf" \
    "$conflict_root/appfs/cosmo_wander/cwai_data/bin"
cp -R "$rk_payload" "$conflict_payload"
printf '#!/bin/sh\ntouch "$COSMO_MIGRATION_TEST_ROOT/stop.called"\n' \
    >"$conflict_payload/scripts/stop.sh"
chmod +x "$conflict_payload/scripts/stop.sh"
printf 'legacy\n' >"$conflict_root/data/cwaiuserdata/conf/device.json"
printf 'target\n' >"$conflict_root/userdata/cwaiuserdata/conf/device.json"
printf 'old\n' >"$conflict_root/appfs/cosmo_wander/cwai_data/bin/cosmo-engine"
if COSMO_MIGRATION_TEST_ROOT="$conflict_root" \
    sh "$conflict_payload/scripts/install.sh" "$conflict_root/install.log" \
    2>"$conflict_root/install.stderr"; then
    echo "ambiguous data roots must fail" >&2
    exit 1
fi
test ! -e "$conflict_root/stop.called"
grep -Fxq old "$conflict_root/appfs/cosmo_wander/cwai_data/bin/cosmo-engine"
grep -Fq 'refusing an ambiguous merge' "$conflict_root/install.stderr"

# A pre-release Rockchip installation that already declares /userdata remains
# authoritative even when a stale /data tree is still present.
already_root="${root}/rk-already-active"
already_payload="${already_root}/payload"
already_active="${already_root}/appfs/cosmo_wander/cwai_data"
mkdir -p "$already_root/data/cwaiuserdata/db" \
    "$already_root/userdata/cwaiuserdata/db" "$already_active/share/cosmo" \
    "$already_active/bin"
cp -R "$rk_payload" "$already_payload"
printf '%s\n' \
    'COSMO_PACKAGE_DATA_DIR=/userdata/cwaiuserdata' \
    'COSMO_PACKAGE_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data' \
    >"$already_active/share/cosmo/runtime-paths.env"
printf 'stale-legacy\n' >"$already_root/data/cwaiuserdata/db/cosmo.db"
printf 'current-target\n' >"$already_root/userdata/cwaiuserdata/db/cosmo.db"
printf 'old-engine\n' >"$already_active/bin/cosmo-engine"
COSMO_MIGRATION_TEST_ROOT="$already_root" \
    sh "$already_payload/scripts/install.sh" "$already_root/install.log"
grep -Fxq current-target "$already_root/userdata/cwaiuserdata/db/cosmo.db"
grep -Fxq stale-legacy "$already_root/data/cwaiuserdata/db/cosmo.db"
grep -Fq 'Installed runtime already uses /userdata/cwaiuserdata' \
    "$already_root/install.log"

# An earlier broken bridge may have created only an upgrade marker and logs in
# /userdata. Those transient entries must not hide durable legacy state.
transient_root="${root}/rk-transient-target"
transient_payload="${transient_root}/payload"
transient_active="${transient_root}/appfs/cosmo_wander/cwai_data"
mkdir -p "$transient_root/data/cwaiuserdata/db" \
    "$transient_root/userdata/cwaiuserdata/log" \
    "$transient_active/share/cosmo" "$transient_active/bin"
cp -R "$rk_payload" "$transient_payload"
printf '%s\n' \
    'COSMO_PACKAGE_DATA_DIR=/userdata/cwaiuserdata' \
    'COSMO_PACKAGE_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data' \
    >"$transient_active/share/cosmo/runtime-paths.env"
printf 'legacy-durable\n' >"$transient_root/data/cwaiuserdata/db/cosmo.db"
printf 'old-marker\n' >"$transient_root/userdata/cwaiuserdata/mqttUpgradeApp"
printf 'old-log\n' >"$transient_root/userdata/cwaiuserdata/log/start.log"
printf 'old-engine\n' >"$transient_active/bin/cosmo-engine"
COSMO_MIGRATION_TEST_ROOT="$transient_root" \
    sh "$transient_payload/scripts/install.sh" "$transient_root/install.log"
grep -Fxq legacy-durable "$transient_root/userdata/cwaiuserdata/db/cosmo.db"
test ! -e "$transient_root/userdata/cwaiuserdata/log"
test -f "$transient_root/userdata/cwaiuserdata/.cosmo-data-root-migration-v1"
grep -Fq 'Persistent data root migrated' "$transient_root/install.log"

# runtime-paths.env is parsed as data rather than sourced as shell code.
unsafe_root="${root}/unsafe-runtime-paths"
unsafe_payload="${unsafe_root}/payload"
unsafe_marker="${unsafe_root}/runtime-paths-executed"
mkdir -p "$unsafe_root"
cp -R "$rk_payload" "$unsafe_payload"
printf 'COSMO_PACKAGE_DATA_DIR=$(touch %s)\n%s\n' \
    "$unsafe_marker" \
    'COSMO_PACKAGE_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data' \
    >"$unsafe_payload/share/cosmo/runtime-paths.env"
if COSMO_MIGRATION_TEST_ROOT="$unsafe_root" \
    sh "$unsafe_payload/scripts/install.sh" "$unsafe_root/install.log" \
    2>"$unsafe_root/install.stderr"; then
    echo "unsafe runtime paths must fail" >&2
    exit 1
fi
test ! -e "$unsafe_marker"
grep -Fq 'package data directory is unsupported' "$unsafe_root/install.stderr"

# A failure after activation restores both the previous application and the
# pre-migration data-root state. A retry can therefore recopy a consistent DB.
rollback_root="${root}/rk-rollback"
rollback_payload="${rollback_root}/payload"
mkdir -p "$rollback_root/data/cwaiuserdata/db" \
    "$rollback_root/appfs/cosmo_wander/cwai_data/bin"
cp -R "$rk_payload" "$rollback_payload"
printf 'legacy-database\n' >"$rollback_root/data/cwaiuserdata/db/cosmo.db"
printf 'old-engine\n' >"$rollback_root/appfs/cosmo_wander/cwai_data/bin/cosmo-engine"
if COSMO_MIGRATION_TEST_ROOT="$rollback_root" \
    COSMO_MIGRATION_TEST_FAIL_AFTER_ACTIVATION=1 \
    sh "$rollback_payload/scripts/install.sh" "$rollback_root/install.log"; then
    echo "injected post-activation failure must fail" >&2
    exit 1
fi
grep -Fxq old-engine "$rollback_root/appfs/cosmo_wander/cwai_data/bin/cosmo-engine"
grep -Fxq legacy-database "$rollback_root/data/cwaiuserdata/db/cosmo.db"
test ! -e "$rollback_root/userdata/cwaiuserdata"
test ! -e "$rollback_root/appfs/cosmo_wander/.cosmo-migration-backup"
test -z "$(find "$rollback_root/appfs/cosmo_wander" -maxdepth 1 \
    -name '.cosmo-migration-staging.*' -print -quit)"

# Preserved resources must be copied into staging before the package overlays
# them. Keeping the packaged resource tree in staging while copying the active
# tree creates an avoidable second model-sized allocation on /appfs.
installer="$repo/scripts/legacy_migration_install.sh"
preserved_copy_line="$(grep -nF 'cp -a -- "${active_root}/resource/." "${staging_root}/resource/"' "$installer" | cut -d: -f1)"
payload_copy_line="$(grep -nF 'cp -a -- "${payload_root}/." "$staging_root/"' "$installer" | cut -d: -f1)"
test -n "$preserved_copy_line"
test -n "$payload_copy_line"
test "$preserved_copy_line" -lt "$payload_copy_line"
if grep -Fq '.packaged-resource' "$installer"; then
    exit 1
fi
echo "legacy migration installer tests passed"
