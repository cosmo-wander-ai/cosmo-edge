#!/bin/bash
# Common configuration and utility functions for Cosmo startup scripts
# Sourced by: inte_run_start.sh, start.sh, run_start.sh, install.sh

# ── Path constants ──
COSMO_DATA_DIR="/data/cwaiuserdata"
COSMO_LOG_DIR="${COSMO_DATA_DIR}/log/logs"
COSMO_INSTALL_DIR="/appfs/cosmo_wander/cwai_data"
COSMO_UPGRADE_DIR="${COSMO_DATA_DIR}/upgrade"
COSMO_NGINX_TMP_DIR="${COSMO_DATA_DIR}/tmp"

# Upgrade signal files
COSMO_HW_UPGRADE_SIGN="${COSMO_DATA_DIR}/mqttHWUpgradeApp"
COSMO_UPGRADE_SIGN="${COSMO_DATA_DIR}/mqttUpgradeApp"

# ── Log helpers ──
# Usage: cosmo_log <TAG> <message> [logFile]
cosmo_log() {
    local tag="$1" msg="$2" file="${3:-}"
    local line="[${tag}] $(date '+%Y-%m-%d %H:%M:%S') ${msg}"
    echo "$line"
    if [ -n "$file" ]; then
        echo "$line" >> "$file"
    fi
}

# ── Directory setup ──
# Create all runtime directories needed before services start
ensure_runtime_dirs() {
    mkdir -p "${COSMO_DATA_DIR}"
    mkdir -p "${COSMO_LOG_DIR}"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_body"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_proxy"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_fastcgi"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_uwsgi"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_scgi"
    mkdir -p "${COSMO_UPGRADE_DIR}"

    mkdir -p "${COSMO_INSTALL_DIR}/bin/nginx_conf/logs"
}

# ── Process helpers ──
# Wait until a TCP port is no longer in LISTEN state (max timeout seconds)
# Usage: wait_for_port_free <port> [timeout_seconds]
wait_for_port_free() {
    local port="$1" timeout="${2:-10}" elapsed=0
    while ss -tlnp 2>/dev/null | grep -q ":${port} " && [ "$elapsed" -lt "$timeout" ]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done
}

# ── Iptables helpers ──
# Add iptables rule only if it does not already exist
# Usage: iptables_ensure <rule arguments...>
iptables_ensure() {
    if ! iptables -C "$@" 2>/dev/null; then
        iptables -A "$@"
    fi
}
