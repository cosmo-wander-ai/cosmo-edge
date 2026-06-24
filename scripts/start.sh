#!/bin/bash
set -e

# Upgrade orchestrator - log rotation, OTA upgrade detection, MD5 verify, install, start.
# Called by inte_run_start.sh at system boot.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=common.sh
. "${SCRIPT_DIR}/common.sh"

if [ -z "${INSTALLPATH}" ]; then
    INSTALLPATH="$(cd "${SCRIPT_DIR}/../" && pwd)"
    echo "INSTALLPATH=${INSTALLPATH}"
fi

# Ensure all runtime directories exist
ensure_runtime_dirs

# Rotate nginx logs
mv -f "${COSMO_LOG_DIR}/nginx_access.log" "${COSMO_LOG_DIR}/nginx_access_last.log" 2>/dev/null || true
mv -f "${COSMO_LOG_DIR}/nginx_error.log" "${COSMO_LOG_DIR}/nginx_error_last.log" 2>/dev/null || true

# ── Log rotation ──
# Maintains up to 10 rotated log files: INTE_RUN.1 .. INTE_RUN.10
# The current active log is always named INTE_RUN_now.<N>
nowLogFileDefault="${COSMO_LOG_DIR}/INTE_RUN_now.1"
nowLogFile="$nowLogFileDefault"

getNowFile() {
    local latest=""
    for f in "${COSMO_LOG_DIR}"/INTE_RUN_now.*; do
        [ -f "$f" ] && latest="$f"
    done
    if [ -n "$latest" ]; then
        nowLogFile="$latest"
        return 0
    fi
    return 1
}

getNowFile || true
nowFileIndex="${nowLogFile##*.}"
nextFileIndex=$((nowFileIndex + 1))

if [ "$nextFileIndex" -gt 10 ]; then
    nextFileIndex=1
fi

if [ -f "${nowLogFile}" ]; then
    mv -f "$nowLogFile" "${COSMO_LOG_DIR}/INTE_RUN.$nowFileIndex"
    nowLogFile="${COSMO_LOG_DIR}/INTE_RUN_now.$nextFileIndex"
fi

echo "Log file: $nowLogFile"

logTag="INTE_RUN"
logFile="$nowLogFile"

action="$1"

cosmo_log "$logTag" "In start.sh, action=${action}" "$logFile"

# Clean previous upgrade sign, convert HW upgrade sign if present
rm -f "$COSMO_UPGRADE_SIGN"
if [ -f "${COSMO_HW_UPGRADE_SIGN}" ]; then
    # Convert HW upgrade marker to upgrade-success marker for MQTT reporting
    mv -f "$COSMO_HW_UPGRADE_SIGN" "$COSMO_UPGRADE_SIGN"
    cosmo_log "$logTag" "HW upgrade detected, marker converted." "$logFile"
fi

# Handle stop action
if [ "$action" = "stop" ]; then
    "${INSTALLPATH}/scripts/stop.sh"
    cosmo_log "$logTag" "Stop action completed." "$logFile"
    exit 0
fi

# ── OTA upgrade detection ──
TARGZ_SUFFIX="tar.gz"
INSTALL_TYPE=""
EXIST_IF="unexists"
DIRECTORY_STATIC="${COSMO_UPGRADE_DIR}"
DIRECTORY_SHELL="${INSTALLPATH}/scripts/"
START_SHELL_PATH="${INSTALLPATH}/scripts/run_start.sh"

# Regex pattern for full package name
# Example: cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.tar.gz
TARGZ_PATTERN='^cosmo-[Vv][0-9]{1,}\.[0-9]{1,}\.[0-9]{1,}-[0-9a-fA-F]{32}\.tar\.gz$'

# Execute run_start.sh and exit
RUN() {
    cosmo_log "$logTag" "[RUN] Before starting run_start.sh" "$logFile"
    rm -rf "${DIRECTORY_STATIC:?}"/*
    if [ "$action" = "start" ]; then
        cd "$DIRECTORY_SHELL" || exit 1
        cosmo_log "$logTag" "[RUN] Executing $START_SHELL_PATH" "$logFile"
        sh "$START_SHELL_PATH" start "$logFile"
    fi
    cosmo_log "$logTag" "Script ended." "$logFile"
    exit 0
}

# Check the legality of package name
# $1: filename string, $2: regex pattern
checkFileName() {
    cosmo_log "$logTag" "Checking filename legality: $1" "$logFile"
    local regex_ret
    regex_ret=$(echo "$1" | grep -E "$2") || true
    if [ -n "${regex_ret}" ]; then
        cosmo_log "$logTag" "Valid filename." "$logFile"
        return 0
    else
        cosmo_log "$logTag" "$1 file format error!" "$logFile"
        return 1
    fi
}

# Validate that extracted directory has the expected package layout
hasUpgradePackageLayout() {
    local root="$1"
    for dir in bin files font scripts web; do
        if [ ! -d "$root/$dir" ]; then
            cosmo_log "$logTag" "Missing required package directory: $root/$dir" "$logFile"
            return 1
        fi
    done
    return 0
}

if [ ! -d "$DIRECTORY_STATIC" ]; then
    RUN
    # NOTE: RUN() calls exit, code below is unreachable
fi

# Scan for upgrade package
cosmo_log "$logTag" "Checking for upgrade package..." "$logFile"
for FILENAME_WHOLE in "$DIRECTORY_STATIC"/*; do
    FILE_NAME_WITHOUT_PATH=$(basename "${FILENAME_WHOLE}")
    if [ "${FILE_NAME_WITHOUT_PATH}" != "*" ] && echo "$FILE_NAME_WITHOUT_PATH" | grep -q "\.${TARGZ_SUFFIX}$"; then
        if checkFileName "$FILE_NAME_WITHOUT_PATH" "$TARGZ_PATTERN"; then
            INSTALL_TYPE="install"
            EXIST_IF="exists"
            break
        fi
    fi
done

cosmo_log "$logTag" "INSTALL_TYPE: ${INSTALL_TYPE}" "$logFile"

if [ "$EXIST_IF" = "exists" ]; then
    cosmo_log "$logTag" "Upgrade package found: $FILENAME_WHOLE" "$logFile"
else
    cosmo_log "$logTag" "No upgrade package found, starting normally." "$logFile"
    RUN
fi

# ── MD5 verification ──
cosmo_log "$logTag" "Verifying MD5 checksum..." "$logFile"
MD5_VALUE_IN_FILENAME="${FILENAME_WHOLE%.tar.gz}"
MD5_VALUE_IN_FILENAME="${MD5_VALUE_IN_FILENAME##*-}"
MD5_VALUE_IN_FILENAME=$(echo "$MD5_VALUE_IN_FILENAME" | tr 'A-F' 'a-f')
cosmo_log "$logTag" "MD5 from filename: $MD5_VALUE_IN_FILENAME" "$logFile"

REAL_MD5_VALUE=$(/usr/bin/md5sum "${FILENAME_WHOLE}")
REAL_MD5_VALUE="${REAL_MD5_VALUE:0:${#MD5_VALUE_IN_FILENAME}}"
cosmo_log "$logTag" "MD5 computed: $REAL_MD5_VALUE" "$logFile"

if [ "$MD5_VALUE_IN_FILENAME" = "$REAL_MD5_VALUE" ]; then
    cosmo_log "$logTag" "MD5 verified. Proceeding with upgrade..." "$logFile"
else
    cosmo_log "$logTag" "MD5 mismatch! Discarding package." "$logFile"
    RUN
fi

# Stop all processes before upgrade
cosmo_log "$logTag" "Stopping processes for upgrade..." "$logFile"
"${INSTALLPATH}/scripts/stop.sh"

# Extract upgrade package
cosmo_log "$logTag" "Extracting upgrade package..." "$logFile"
tar -zxf "$FILENAME_WHOLE" -C "$DIRECTORY_STATIC"

# Detect package layout (flat or nested directory)
if hasUpgradePackageLayout "$DIRECTORY_STATIC"; then
    PACKAGE_ROOT="$DIRECTORY_STATIC"
else
    # Find the top-level directory extracted by tar
    UNZIP_DIRNAME=""
    for d in "$DIRECTORY_STATIC"/*/; do
        if [ -d "$d" ]; then
            UNZIP_DIRNAME="$(basename "$d")"
            break
        fi
    done
    PACKAGE_ROOT="$DIRECTORY_STATIC/$UNZIP_DIRNAME"
    if ! hasUpgradePackageLayout "$PACKAGE_ROOT"; then
        cosmo_log "$logTag" "Upgrade package layout error, discarding." "$logFile"
        RUN
    fi
fi
cosmo_log "$logTag" "PACKAGE_ROOT: $PACKAGE_ROOT" "$logFile"
cd "$PACKAGE_ROOT" || exit 1

cosmo_log "$logTag" "Extraction complete." "$logFile"

# Run install script from the upgrade package
cosmo_log "$logTag" "Running install.sh from upgrade package..." "$logFile"
cd "$PACKAGE_ROOT/scripts/" || exit 1
sh "$PACKAGE_ROOT/scripts/install.sh" "$logFile"
cosmo_log "$logTag" "install.sh completed." "$logFile"

# Start services
RUN
