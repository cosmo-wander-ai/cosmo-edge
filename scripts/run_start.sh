#!/bin/bash
set -e

# Service starter - configures environment and launches nginx, SRS, cosmo-engine.
# Called by start.sh after upgrade check is complete.

# Check if enough arguments are provided
if [ $# -lt 2 ]; then
    echo "Usage: $0 start <logfile>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Resolve the active package before loading common defaults so side-by-side
# validation installs read their own target-specific runtime-path declaration.
if [ -z "${INSTALLPATH:-}" ]; then
    INSTALLPATH="$(cd "${SCRIPT_DIR}/../" && pwd)"
fi
COSMO_INSTALL_DIR="${INSTALLPATH}"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

logTag="RUN_START"
logFile="$2"

cosmo_log "$logTag" "Start, action=$1" "$logFile"
cosmo_log "$logTag" "Install path=${INSTALLPATH}" "$logFile"

# Keep mutable runtime data separate from packaged, read-only resources.  The
# application root must follow the actual installation path, including
# side-by-side validation installs, instead of silently falling back to the
# production default when INSTALLPATH is overridden.
export COSMO_DATA_DIR
export COSMO_APP_DATA_DIR="${COSMO_APP_DATA_DIR:-${INSTALLPATH}}"

if [ "$1" != "start" ]; then
    cosmo_log "$logTag" "Unsupported action: [$1], use 'start'" "$logFile"
    exit 1
fi

PLTFORM_TYPE="${COSMO_PLATFORM_TYPE:-$(uname -m)}"
case "${PLTFORM_TYPE}" in
    x86_64|amd64)
        PLTFORM_TYPE="x86_64-cpu"
        ;;
    aarch64|arm64)
        PLTFORM_TYPE="sophon"
        ;;
esac
cosmo_log "$logTag" "Install path=${INSTALLPATH}, platform=${PLTFORM_TYPE}" "$logFile"

# On Rockchip kernels debugfs itself is root-only even though the NPU load file
# is read-only. Prepare a narrow bind mount when startup has sufficient rights;
# metric failure must not prevent the inference service from starting.
if [ -x "${SCRIPT_DIR}/prepare_rknpu_metrics.sh" ]; then
    if rknpu_metrics_status="$("${SCRIPT_DIR}/prepare_rknpu_metrics.sh" 2>&1)"; then
        if [ -n "${rknpu_metrics_status}" ]; then
            cosmo_log "$logTag" "${rknpu_metrics_status}" "$logFile"
        fi
    else
        cosmo_log "$logTag" "WARNING: ${rknpu_metrics_status}" "$logFile"
    fi
fi

# Set multicast options
sysctl -w net.ipv4.igmp_max_memberships=20 2>/dev/null || true

# Run dependency libs
IED_LIB="${INSTALLPATH}/lib"
export LD_LIBRARY_PATH="${IED_LIB}:${LD_LIBRARY_PATH:-}:/usr/lib"

# Main process binary file path
BINPATH="${INSTALLPATH}/bin"

export COSMO_HTTP_PORT="${COSMO_HTTP_PORT:-8000}"
export COSMO_WEBSOCKET_PORT="${COSMO_WEBSOCKET_PORT:-9000}"

if ! render_runtime_configs; then
    cosmo_log "$logTag" "Runtime config render failed" "$logFile"
    exit 1
fi
NGINX_PREFIX="${COSMO_RUNTIME_NGINX_PREFIX}"
NGINX_CONF="${COSMO_RUNTIME_NGINX_CONF}"
NGINX_UPSTREAM_CONF="${COSMO_RUNTIME_NGINX_UPSTREAM_CONF}"
SRS_CONF="${COSMO_RUNTIME_SRS_CONF}"

# Validate the reverse-proxy/backend contract before stopping the currently
# running service. A bad override or stale Nginx config must fail closed.
if ! port_contract="$(verify_nginx_engine_port_contract "$NGINX_UPSTREAM_CONF" \
    "$COSMO_HTTP_PORT" "$COSMO_WEBSOCKET_PORT" 2>&1)"; then
    cosmo_log "$logTag" "Port contract check failed: ${port_contract}" "$logFile"
    exit 1
fi
cosmo_log "$logTag" "Port contract OK: ${port_contract}" "$logFile"

# Check supervisor dependencies before stopping the previous release.
PLAY_MODE="${COSMO_STREAM_PLAY_MODE:-srs}"
SUPERVISE_MEDIA=false
case "$PLAY_MODE" in
    srs|webrtc|srs-flv|httpflv-srs)
        command -v python3 >/dev/null
        command -v pgrep >/dev/null
        test -f "${SCRIPT_DIR}/runtime_supervisor.py"
        SUPERVISE_MEDIA=true
        ;;
esac

# Stop all running processes before starting (including nginx)
cosmo_log "$logTag" "Stopping all running processes before start..." "$logFile"
TRUSTED_STOP_SCRIPT="${COSMO_TRUSTED_STOP_SCRIPT:-${INSTALLPATH}/scripts/stop.sh}"
if [ ! -f "${TRUSTED_STOP_SCRIPT}" ] || [ ! -x "${TRUSTED_STOP_SCRIPT}" ]; then
    cosmo_log "$logTag" "Stop script is unavailable" "$logFile"
    exit 1
fi
"${TRUSTED_STOP_SCRIPT}"

# Add iptables rule (idempotent - skips if already exists)
if hash iptables 2>/dev/null; then
    cosmo_log "$logTag" "Ensuring iptables UDP/46000 rule..." "$logFile"
    iptables_ensure INPUT -p udp --dport 46000 -j ACCEPT
fi

# Create audio symlink
mkdir -p "${COSMO_DATA_DIR}/audioMng"
ln -sf "${INSTALLPATH}/files/Audio/beep.ogg" "${COSMO_DATA_DIR}/audioMng/beep.ogg"

if [ ! -f /etc/netplan/01-failsafe.yaml.bak ] || ! cmp -s "${INSTALLPATH}/scripts/01-failsafe.yaml.bak" /etc/netplan/01-failsafe.yaml.bak; then
    cp -f "${INSTALLPATH}/scripts/01-failsafe.yaml.bak" /etc/netplan/
fi

cd "${BINPATH}" || exit 1

# SRS streaming environment. Deployment-specific values (for example the
# deterministic HTTP-FLV path used by the macOS Preview) take precedence over
# these defaults.
export COSMO_STREAM_PLAY_MODE="${COSMO_STREAM_PLAY_MODE:-srs}"
export COSMO_STREAM_RTMP_BASE="${COSMO_STREAM_RTMP_BASE:-rtmp://127.0.0.1:1936/live}"
export COSMO_STREAM_RTC_API_PORT="${COSMO_STREAM_RTC_API_PORT:-1985}"
export COSMO_STREAM_HTTP_PORT="${COSMO_STREAM_HTTP_PORT:-18088}"

# Start nginx
cosmo_log "$logTag" "Starting nginx..." "$logFile"
nginx -p "${NGINX_PREFIX}" -c "${NGINX_CONF}"

# The supervisor owns foreground SRS and the engine, preserves shutdown order,
# and restarts only the media dependency after exit or sustained health failure.
if [ "$SUPERVISE_MEDIA" = true ]; then
    cosmo_log "$logTag" "Starting supervised media and engine (mode: ${PLAY_MODE})..." "$logFile"
    exec python3 "${SCRIPT_DIR}/runtime_supervisor.py" \
        --engine "${BINPATH}/cosmo-engine" --media "${BINPATH}/srs" \
        --config "${SRS_CONF}" --log-dir "${COSMO_LOG_DIR}"
fi

# Non-SRS modes retain direct engine supervision by systemd/container runtime.
cosmo_log "$logTag" "Starting cosmo-engine (foreground)..." "$logFile"
exec ./cosmo-engine
