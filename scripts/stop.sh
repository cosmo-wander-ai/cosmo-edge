#!/bin/bash

# Stop managed processes gracefully (SIGTERM first, then SIGKILL)
PROC_LIST="cosmo-engine srs nginx"

for proc in $PROC_LIST; do
    pids=$(pidof "$proc" 2>/dev/null) || true
    if [ -n "$pids" ]; then
        echo "Stopping $proc (PID: $pids)..."
        kill -15 $pids 2>/dev/null || true
    fi
done

# Wait briefly for graceful shutdown
sleep 2

# Force kill any remaining processes
for proc in $PROC_LIST; do
    pids=$(pidof "$proc" 2>/dev/null) || true
    if [ -n "$pids" ]; then
        echo "Force killing $proc (PID: $pids)..."
        kill -9 $pids 2>/dev/null || true
    fi
done
