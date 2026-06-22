#!/usr/bin/env bash
set -euo pipefail

export INSTALLPATH="${INSTALLPATH:-/appfs/cosmo_wander/cwai_data}"
export COSMO_PLATFORM_TYPE="${COSMO_PLATFORM_TYPE:-x86_64}"

mkdir -p /data/cwaiuserdata/log/logs
mkdir -p /data/cwaiuserdata/tmp/nginx_body
mkdir -p /data/cwaiuserdata/tmp/nginx_proxy
mkdir -p /data/cwaiuserdata/tmp/nginx_fastcgi
mkdir -p /data/cwaiuserdata/tmp/nginx_uwsgi
mkdir -p /data/cwaiuserdata/tmp/nginx_scgi
mkdir -p /data/cwaiuserdata/upgrade
mkdir -p /appfs/cosmo_wander/tools
mkdir -p /etc/netplan

# 复制发布包到宿主机挂载目录
if [ -d "/cosmo-packages" ] && [ -d "/build_output" ]; then
    echo "Copying release packages to /build_output..."
    cp -f /cosmo-packages/* /build_output/ 2>/dev/null || true
    ls -lh /build_output/
fi

if [ ! -x "${INSTALLPATH}/scripts/run_start.sh" ]; then
    echo "Missing runtime script: ${INSTALLPATH}/scripts/run_start.sh" >&2
    exit 1
fi

exec "${INSTALLPATH}/scripts/run_start.sh" start /data/cwaiuserdata/log/logs/INTE_RUN_container.log
