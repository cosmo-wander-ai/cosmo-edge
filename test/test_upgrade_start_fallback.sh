#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
root="$(mktemp -d)"
trap 'rm -rf -- "$root"' EXIT

active="$root/active"
data="$root/data"
package="$data/upgrade/cosmo-V1.1.0-package"
mkdir -p "$active/scripts" "$data/upgrade" \
    "$package/bin" "$package/files" "$package/font" "$package/lib" \
    "$package/scripts" "$package/web"

cp "$repo/scripts/start.sh" "$active/scripts/start.sh"
cp "$repo/scripts/common.sh" "$active/scripts/common.sh"
chmod +x "$active/scripts/start.sh"

cat >"$active/scripts/stop.sh" <<'EOF'
#!/bin/sh
touch "$COSMO_TEST_STOP_CALLED"
EOF
cat >"$active/scripts/run_start.sh" <<'EOF'
#!/bin/sh
touch "$COSMO_TEST_ACTIVE_STARTED"
EOF
cat >"$package/scripts/install.sh" <<'EOF'
#!/bin/sh
touch "$COSMO_TEST_INSTALL_CALLED"
exit 42
EOF
chmod +x "$active/scripts/stop.sh" "$active/scripts/run_start.sh" \
    "$package/scripts/install.sh"

archive_body="$root/archive-body"
printf 'candidate\n' >"$archive_body"
archive_md5="$(md5sum "$archive_body" | awk '{ print $1 }')"
archive="$data/upgrade/cosmo-V1.1.0-${archive_md5}.tar.gz"
cp "$archive_body" "$archive"

COSMO_INSTALL_DIR="$active" \
COSMO_DATA_DIR="$data" \
COSMO_RUNTIME_PATHS_FILE="$root/no-runtime-paths.env" \
COSMO_TEST_STOP_CALLED="$root/stop.called" \
COSMO_TEST_INSTALL_CALLED="$root/install.called" \
COSMO_TEST_ACTIVE_STARTED="$root/active.started" \
    bash "$active/scripts/start.sh" start

test ! -e "$root/stop.called"
test -f "$root/install.called"
test -f "$root/active.started"
test -z "$(find "$data/upgrade" -mindepth 1 -print -quit)"
grep -Fq 'Package installation failed with status 42' \
    "$data/log/logs/INTE_RUN_now.1"

echo "upgrade start fallback tests passed"
