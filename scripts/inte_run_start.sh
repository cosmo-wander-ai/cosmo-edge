#!/bin/bash
set -e

# System boot entry script - called by systemd cosmo.service to start services
# Relies on systemd After=network-online.target for network readiness.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

cosmo_log "BOOT" "Preparing startup environment..."
ensure_runtime_dirs

cosmo_log "BOOT" "Starting Cosmo services..."

cd "$SCRIPT_DIR" || exit 1
"$SCRIPT_DIR/start.sh" start
