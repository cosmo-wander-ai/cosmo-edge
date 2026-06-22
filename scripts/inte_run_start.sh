#!/bin/bash
set -e

# System boot entry script - called by init system to start services

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "准备启动环境..."
mkdir -p /data/cwaiuserdata
mkdir -p /appfs/cosmo_wander/cwai_data/bin/nginx_conf/logs

echo "Ready To Start System..."

# 等待网络等系统进程就绪
sleep 15

echo "Start By System..."

cd "$SCRIPT_DIR" || exit 1
"$SCRIPT_DIR/start.sh" start
