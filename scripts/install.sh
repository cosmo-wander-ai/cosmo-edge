#!/bin/bash
set -e

# Enter the directory where the install script is located (before calling stop.sh)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

# Stop running processes first
./stop.sh

# WARNING: Do not modify INSTALLPATH - it is the production deployment root
INSTALLPATH="${COSMO_INSTALL_DIR}"

INSTALL_SUCCESS_SIGN="${COSMO_UPGRADE_SIGN}"

logFile="${1:-/dev/null}"
logTag="[INSTALL]"

echo "${logTag} Install Start" >> "$logFile"
echo "${logTag} script=$0, logFile=$1" >> "$logFile"
echo "${logTag} script dir: $SCRIPT_DIR" >> "$logFile"

echo "Install path is ${INSTALLPATH}"

echo "${logTag} Installing files..." >> "$logFile"
echo "Installing files..."

# Ensure install path exists and is non-empty
if [ -z "${INSTALLPATH}" ]; then
    echo "${logTag} ERROR: INSTALLPATH is empty, aborting!" >> "$logFile"
    exit 1
fi

mkdir -p "${INSTALLPATH}"

# Remove old directories
PACKAGE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
for dir in bin lib scripts web font files; do
    if [ -d "${INSTALLPATH}/${dir}" ]; then
        rm -rf "${INSTALLPATH:?}/${dir}"
    fi
done

# Install new files (skip missing directories)
for dir in bin lib scripts web font files resource; do
    if [ -d "${PACKAGE_DIR}/${dir}" ]; then
        if [ "$dir" = "resource" ]; then
            echo "${logTag} Overwriting ${dir}..." >> "$logFile"
            if [ "${CLEAN_RESOURCE:-0}" = "1" ] && [ -d "${INSTALLPATH}/resource" ]; then
                echo "${logTag} CLEAN_RESOURCE=1, removing ${INSTALLPATH}/resource before install" >> "$logFile"
                rm -rf "${INSTALLPATH:?}/resource"
            fi
            mkdir -p "${INSTALLPATH}/resource"
            cp -rf "${PACKAGE_DIR}/resource/." "${INSTALLPATH}/resource/"
        else
            mv -f "${PACKAGE_DIR}/${dir}" "${INSTALLPATH}/"
        fi
    else
        echo "${logTag} WARNING: ${dir} not found in package, skipping" >> "$logFile"
    fi
done

# Setup static file symlinks
mkdir -p "${INSTALLPATH}/web/staticfile"
rm -f "${INSTALLPATH}/web/staticfile/httpInterface.html"
rm -f "${INSTALLPATH}/web/staticfile/mqttInterface.html"
ln -sf "${INSTALLPATH}/files/Interface/ai-box-interface_v1.0.html" "${INSTALLPATH}/web/staticfile/httpInterface.html"
ln -sf "${INSTALLPATH}/files/Interface/mqtt_v1.0.html" "${INSTALLPATH}/web/staticfile/mqttInterface.html"

mkdir -p "${INSTALLPATH}/bin/nginx_conf/logs"

# Remove install script from deployed location (self-cleanup)
rm -f "${INSTALLPATH}/scripts/install.sh"

echo "Install files Done."
echo "${logTag} Install files Done." >> "$logFile"

# Setup systemd auto-start service
SERVICE_FILE="/etc/systemd/system/cosmo.service"
SERVICE_LINK="/etc/systemd/system/multi-user.target.wants/cosmo.service"

echo "${logTag} Setting up systemd auto-start service..." >> "$logFile"

cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Cosmo Edge AI Engine
After=docker.service network-online.target
Wants=network-online.target

[Service]
User=root
ExecStart=${INSTALLPATH}/scripts/inte_run_start.sh
Type=simple
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Create symlink for auto-start on boot
if [ ! -L "$SERVICE_LINK" ]; then
    ln -sf "$SERVICE_FILE" "$SERVICE_LINK"
fi

# Reload systemd to pick up the new/updated service file
systemctl daemon-reload

echo "${logTag} systemd service [cosmo] installed and enabled." >> "$logFile"
echo "systemd service [cosmo] installed and enabled."

# Upgrade completion marker for MQTT reporting
mkdir -p "$(dirname "$INSTALL_SUCCESS_SIGN")"
touch "$INSTALL_SUCCESS_SIGN"
sync

echo "${logTag} Install End."
echo "${logTag} Install End." >> "$logFile"
