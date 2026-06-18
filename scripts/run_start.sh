#!/bin/bash
set -e

# Check if enough arguments are provided
if [ $# -lt 2 ]; then
    echo "Usage: $0 [-h] [start] <logfile>"
    exit 1
fi

logTag="[RUN_START]"
logFile="$2"

echo "${logTag} Start at $(date '+%Y-%m-%d_%H:%M:%S'), action is $1" >> "$logFile"
echo "${logTag} Start at $(date '+%Y-%m-%d_%H:%M:%S'), action is $1"

# Set INSTALLPATH if not already set
if [ -z "${INSTALLPATH}" ]; then
    INSTALLPATH="$(cd "$(dirname "$0")/../" && pwd)"
    echo "INSTALLPATH=${INSTALLPATH}" 
fi

IF_STANDALONE=1
# Help manual

if [ "$1" != "start" ]; then
    echo "Not supported action: [$1], please read help page with -h!"
    echo "${logTag} Stop at $(date '+%Y-%m-%d_%H:%M:%S'). Not supported action: [$1]" >> "$logFile"
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
echo "In run_start.sh, install path is ${INSTALLPATH}, install platform is ${PLTFORM_TYPE}"
echo "${logTag} In run_start.sh, install path is ${INSTALLPATH}, install platform is ${PLTFORM_TYPE}" >> "$logFile"

# Set multicast options
sysctl -w net.ipv4.igmp_max_memberships=20 2>/dev/null || true

# Run dependency libs
IED_LIB="${INSTALLPATH}/lib"
LIBRARY_PATH="$IED_LIB"

export LD_LIBRARY_PATH="$LIBRARY_PATH:$LD_LIBRARY_PATH:/usr/lib"

# Main process binary file path
BINPATH="${INSTALLPATH}/bin"
NGINX_PREFIX="${BINPATH}/nginx_conf"
NGINX_CONF="${NGINX_PREFIX}/conf/nginx.conf"

# Kill running process before starting
echo "Killall running processes before start!"
echo "${logTag} Killall running processes before start!" >> "$logFile"
${INSTALLPATH}/scripts/stop.sh

if hash iptables 2>/dev/null; then
    echo "iptables set."
    echo "${logTag} iptables set." >> "$logFile"
    iptables -A INPUT -p udp --dport 46000 -j ACCEPT
fi

mkdir -p /data/cwaiuserdata/audioMng
ln -sf "${INSTALLPATH}/files/Audio/beep.ogg" /data/cwaiuserdata/audioMng/beep.ogg
mkdir -p /data/cwaiuserdata/tmp/nginx_body
mkdir -p /data/cwaiuserdata/tmp/nginx_proxy
mkdir -p /data/cwaiuserdata/tmp/nginx_fastcgi
mkdir -p /data/cwaiuserdata/tmp/nginx_uwsgi
mkdir -p /data/cwaiuserdata/tmp/nginx_scgi

# Debugging: Print the value of $1
echo "Argument 1: '$1'"

# Fix the condition check
if [ "$1" = "start" ]; then
    if [ ! -f /etc/netplan/01-failsafe.yaml.bak ] || ! cmp -s "${INSTALLPATH}/scripts/01-failsafe.yaml.bak" /etc/netplan/01-failsafe.yaml.bak; then
        cp -f "${INSTALLPATH}/scripts/01-failsafe.yaml.bak" /etc/netplan/
    fi
    # Change to main process binary file path
    cd "${BINPATH}" || exit 1

    # SRS streaming environment
    export COSMO_STREAM_PLAY_MODE=srs
    export COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
    export COSMO_STREAM_RTC_API_PORT=1985
    export COSMO_STREAM_HTTP_PORT=18088

    # Restart nginx: stop first, then start fresh
    killall nginx 2>/dev/null || true
    sleep 1
    echo "${logTag} Starting nginx..." >> "$logFile"
    ./nginx -p "${NGINX_PREFIX}" -c "${NGINX_CONF}"

    # Start SRS media server (for srs/webrtc/srs-flv/httpflv-srs play modes)
    PLAY_MODE="${COSMO_STREAM_PLAY_MODE:-srs}"
    if [ "$PLAY_MODE" = "srs" ] || [ "$PLAY_MODE" = "webrtc" ] || [ "$PLAY_MODE" = "srs-flv" ] || [ "$PLAY_MODE" = "httpflv-srs" ]; then
        killall srs 2>/dev/null || true
        sleep 1
        echo "${logTag} Starting SRS media server (mode: ${PLAY_MODE})..." >> "$logFile"
        ./srs -c srs_conf/srs.conf &
    fi




    echo "${logTag} Process [cosmo-engine] normal start....." >> "$logFile"
    ./cosmo-engine
    sleep 1

    echo "*** Process [cosmo-engine] normal start over!"
    RUNNING_INFO=$(ps -ef | grep -i cosmo-engine | grep -v 'grep')
    echo "Processes running info statics:"
    echo "${RUNNING_INFO}"
    echo "${logTag} *** Process [cosmo-engine] normal start over!" >> "$logFile"
    echo "${logTag} Processes running info statics:" >> "$logFile"
    echo "${logTag} ${RUNNING_INFO}" >> "$logFile"
fi

echo "${logTag} Stop at $(date '+%Y-%m-%d_%H:%M:%S')" >> "$logFile"
