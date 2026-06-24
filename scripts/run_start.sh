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

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

logTag="RUN_START"
logFile="$2"

cosmo_log "$logTag" "Start, action=$1" "$logFile"

# Set INSTALLPATH if not already set
if [ -z "${INSTALLPATH}" ]; then
    INSTALLPATH="$(cd "${SCRIPT_DIR}/../" && pwd)"
    cosmo_log "$logTag" "INSTALLPATH=${INSTALLPATH}" "$logFile"
fi

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

# Set multicast options
sysctl -w net.ipv4.igmp_max_memberships=20 2>/dev/null || true

# Run dependency libs
IED_LIB="${INSTALLPATH}/lib"
export LD_LIBRARY_PATH="${IED_LIB}:${LD_LIBRARY_PATH:-}:/usr/lib"

# Main process binary file path
BINPATH="${INSTALLPATH}/bin"
NGINX_PREFIX="${BINPATH}/nginx_conf"
NGINX_CONF="${NGINX_PREFIX}/conf/nginx.conf"

# Stop all running processes before starting (including nginx)
cosmo_log "$logTag" "Stopping all running processes before start..." "$logFile"
"${INSTALLPATH}/scripts/stop.sh"

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

# SRS streaming environment
export COSMO_STREAM_PLAY_MODE=srs
export COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
export COSMO_STREAM_RTC_API_PORT=1985
export COSMO_STREAM_HTTP_PORT=18088

# Start nginx
cosmo_log "$logTag" "Starting nginx..." "$logFile"
nginx -p "${NGINX_PREFIX}" -c "${NGINX_CONF}"

# Start SRS media server (for srs/webrtc/srs-flv/httpflv-srs play modes)
PLAY_MODE="${COSMO_STREAM_PLAY_MODE}"
if [ "$PLAY_MODE" = "srs" ] || [ "$PLAY_MODE" = "webrtc" ] || [ "$PLAY_MODE" = "srs-flv" ] || [ "$PLAY_MODE" = "httpflv-srs" ]; then
    cosmo_log "$logTag" "Starting SRS media server (mode: ${PLAY_MODE})..." "$logFile"
    ./srs -c srs_conf/srs.conf &
fi

# Start cosmo-engine (foreground, managed by systemd)
cosmo_log "$logTag" "Starting cosmo-engine (foreground)..." "$logFile"
./cosmo-engine

cosmo_log "$logTag" "Script ended." "$logFile"
