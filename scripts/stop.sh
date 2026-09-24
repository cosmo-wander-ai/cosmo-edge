#!/bin/bash
set -eu
IFS=$' \t\n'
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export IFS PATH

# Give cosmo-engine enough time to leave its HTTP loop, disable the hardware
# watchdog, and drain managed workers before falling back to SIGKILL.
graceful_timeout="${COSMO_STOP_TIMEOUT_SECONDS:-15}"
case "$graceful_timeout" in
    ''|*[!0-9]*) graceful_timeout=15 ;;
esac
if [ "$graceful_timeout" -lt 1 ] || [ "$graceful_timeout" -gt 60 ]; then
    graceful_timeout=15
fi

process_ids() {
    if [ "$1" = cosmo-runtime ]; then
        # runtime_supervisor.py sets this Linux comm name before spawning any
        # children. Do not match/kill unrelated Python processes.
        pgrep -x cosmo-runtime
    else
        pidof "$1"
    fi
}

wait_for_exit() {
    local timeout="$1" elapsed=0 proc any_running
    shift
    while true; do
        any_running=0
        for proc in "$@"; do
            if process_ids "$proc" >/dev/null 2>&1; then
                any_running=1
                break
            fi
        done
        [ "$any_running" -eq 0 ] && return 0
        [ "$elapsed" -ge "$timeout" ] && return 1
        sleep 1
        elapsed=$((elapsed + 1))
    done
}

stop_group() {
    local proc pids timeout="$graceful_timeout"
    # The owner drains the engine and then media sequentially. Wait for the
    # owner as well, so no restart can race with a successful migration barrier.
    if [ "$1" = cosmo-runtime ]; then
        timeout=$((graceful_timeout + 15))
    fi
    for proc in "$@"; do
        pids=$(process_ids "$proc" 2>/dev/null) || true
        if [ -n "$pids" ]; then
            echo "Stopping $proc (PID: $pids)..."
            kill -15 $pids 2>/dev/null || true
        fi
    done
    if wait_for_exit "$timeout" "$@"; then
        return 0
    fi
    for proc in "$@"; do
        pids=$(process_ids "$proc" 2>/dev/null) || true
        if [ -n "$pids" ]; then
            echo "Graceful shutdown timeout; force killing $proc (PID: $pids)..."
            kill -9 $pids 2>/dev/null || true
        fi
    done
    wait_for_exit 5 "$@"
}

# Keep RTMP/SIP media and HTTP dependencies alive until all engine workers
# have drained. Merely sending TERM to the engine first does not establish
# this barrier: it may still be probing or reading a stream when SRS exits.
if ! stop_group cosmo-runtime cosmo-engine; then
    echo "Engine remains after forced shutdown; keeping dependencies and refusing release migration" >&2
    exit 1
fi
if ! stop_group srs nginx; then
    echo "Managed dependencies remain after forced shutdown; refusing release migration" >&2
    exit 1
fi

# A successful stop result is a migration security boundary.  Do not let the
# release transaction move any facade while a managed process can still be
# executing through the legacy tree.
if wait_for_exit 0 cosmo-runtime cosmo-engine srs nginx; then
    exit 0
fi

echo "Managed processes remain after forced shutdown; refusing release migration" >&2
exit 1
