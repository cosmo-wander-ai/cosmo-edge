#!/bin/bash
set -e


if [ -z "${INSTALLPATH}" ]
then
	INSTALLPATH="$(cd "$(dirname "$0")/../" && pwd)"
	echo "INSTALLPATH=${INSTALLPATH}"
fi


if [ ! -d "/data/cwaiuserdata" ]; then
	mkdir "/data/cwaiuserdata"
fi

if [ ! -d "/data/cwaiuserdata/log/logs" ]; then
	mkdir -p "/data/cwaiuserdata/log/logs"
fi
mkdir -p "/data/cwaiuserdata/tmp/nginx_body"
mkdir -p "/data/cwaiuserdata/tmp/nginx_proxy"
mkdir -p "/data/cwaiuserdata/tmp/nginx_fastcgi"
mkdir -p "/data/cwaiuserdata/tmp/nginx_uwsgi"
mkdir -p "/data/cwaiuserdata/tmp/nginx_scgi"
mkdir -p "/data/cwaiuserdata/upgrade"
if [ ! -d "/appfs/cosmo_wander/tools" ]; then
	mkdir -p "/appfs/cosmo_wander/tools"
fi
if [ ! -d "/data/cwaiuserdata/tools" ]; then
	mkdir -p "/data/cwaiuserdata/tools"
fi

mv -f /data/cwaiuserdata/log/logs/nginx_access.log /data/cwaiuserdata/log/logs/nginx_access_last.log 2>/dev/null || true
mv -f /data/cwaiuserdata/log/logs/nginx_error.log /data/cwaiuserdata/log/logs/nginx_error_last.log 2>/dev/null || true

# Log rotation: find current log file and rotate
nowLogFileDefault="/data/cwaiuserdata/log/logs/INTE_RUN_now.1"
nowLogFile="$nowLogFileDefault"
getNowFile() {
    local files_list
    files_list="$(ls /data/cwaiuserdata/log/logs/INTE_RUN_now.* 2>/dev/null)"
    for file in $files_list; do
        nowLogFile="$file"
        return 0
    done
    return 1
}

getNowFile || true
nowFileIndex="${nowLogFile##*.}"
nextFileIndex=$((nowFileIndex + 1))

if [ $nextFileIndex -gt 10 ]
then
	nextFileIndex=1
fi

if [ -f "${nowLogFile}" ]
then
	mv -f "$nowLogFile" "/data/cwaiuserdata/log/logs/INTE_RUN.$nowFileIndex"
	nowLogFile="/data/cwaiuserdata/log/logs/INTE_RUN_now.$nextFileIndex"
fi

echo "write log to $nowLogFile"


logTag="[INTE_RUN]"
logFile="$nowLogFile"

action="$1"

echo "${logTag} In start.sh, start at $(date '+%Y-%m-%d %H:%M:%S'), action is ${action}" >> "$logFile"


INSTALL_HW_SUCCESS_SIGN="/data/cwaiuserdata/mqttHWUpgradeApp"
INSTALL_SUCCESS_SIGN="/data/cwaiuserdata/mqttUpgradeApp"
# 每次启动删除升级标记
rm -f "$INSTALL_SUCCESS_SIGN"
# 固件升级标记存在
if [ -f "${INSTALL_HW_SUCCESS_SIGN}" ]; then
	# 固件升级标记转为升级成功标记 便于MQTT上报
	mv -f "$INSTALL_HW_SUCCESS_SIGN" "$INSTALL_SUCCESS_SIGN"
	# 这次是固件升级启动的
	echo "${logTag} Stop at $(date '+%Y-%m-%d %H:%M:%S'). Just HW Upgrade." >> "$logFile"
fi

#stop
if [[ "$action" == "stop" ]]; then
	"${INSTALLPATH}/scripts/stop.sh"
	echo "${logTag} Stop at $(date '+%Y-%m-%d %H:%M:%S'). Stop Action" >> "$logFile"
	exit 0
fi

#get the packet
TARGZ_SUFFIX="tar.gz"
INSTALL_TYPE=""
EXIST_IF="unexists"
DIRECTORY_STATIC="/data/cwaiuserdata/upgrade"
DIRECTORY_SHELL="${INSTALLPATH}/scripts/"
START_SHELL_PATH="${INSTALLPATH}/scripts/run_start.sh"

#regex pattern of full package name
#Example: cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.tar.gz
TARGZ_PATTERN='^cosmo-[Vv][0-9]{1,}\.[0-9]{1,}\.[0-9]{1,}-[0-9a-fA-F]{32}\.tar\.gz$'

# Execute start shell and exit
RUN() {
    echo "${logTag} [FunRun] Before starting run_start.sh" >> "$logFile"
    rm -rf "${DIRECTORY_STATIC:?}"/*
    if [[ "$action" == "start" ]]; then
        cd "$DIRECTORY_SHELL" || exit 1
        echo "${logTag} [FunRun] run $START_SHELL_PATH" >> "$logFile"
        sh "$START_SHELL_PATH" start "$logFile"
    fi
    echo "${logTag} Stop at $(date '+%Y-%m-%d %H:%M:%S')." >> "$logFile"
    exit 0
}

# Check the legality of package name
# $1: filename string, $2: regex pattern
checkFileName() {
    echo "${logTag} check FileName legality: $1" >> "$logFile"
    local regex_ret
    regex_ret=$(echo "$1" | grep -E "$2")
    if [ -n "${regex_ret}" ]; then
        echo "${logTag} Right FileName" >> "$logFile"
        return 0
    else
        echo "${logTag} $1 file format error!" >> "$logFile"
        return 1
    fi
}

hasUpgradePackageLayout() {
    local root="$1"
    for dir in bin files font scripts web; do
        if [ ! -d "$root/$dir" ]; then
            echo "${logTag} Missing required package directory: $root/$dir" >> "$logFile"
            return 1
        fi
    done
    return 0
}

if [ ! -d "$DIRECTORY_STATIC" ]; then
    RUN
    # NOTE: RUN() calls exit, code below is unreachable
fi

#currently, only use the first found legal file to do update or install.
echo "${logTag} Checking the Packet..." >> "$logFile"
echo "${logTag} Checking the Packet..."
for FILENAME_WHOLE in $DIRECTORY_STATIC/*
do
	FILE_NAME_WITHOUT_PATH=$(basename "${FILENAME_WHOLE}")
	if [[ "$FILE_NAME_WITHOUT_PATH" == *."${TARGZ_SUFFIX}" ]]; then
		if checkFileName "$FILE_NAME_WITHOUT_PATH" "$TARGZ_PATTERN"; then
			INSTALL_TYPE="install"
			EXIST_IF="exists"
			break
		fi
	fi
done

echo "${logTag} INSTALL_TYPE: ${INSTALL_TYPE}" >> "$logFile"
echo "${logTag} INSTALL_TYPE: ${INSTALL_TYPE}"

if [[ "$EXIST_IF" == "exists" ]]; then
    echo "${logTag} Upgrade Packet Found. FILENAME_WHOLE: $FILENAME_WHOLE" >> "$logFile"
else
    echo "${logTag} Upgrade Packet is not exists, need start" >> "$logFile"
    RUN
fi

# Check the packet MD5
echo "${logTag} Checking MD5 value..." >> "$logFile"
MD5_VALUE_IN_FILENAME="${FILENAME_WHOLE%.tar.gz}"
MD5_VALUE_IN_FILENAME="${MD5_VALUE_IN_FILENAME##*-}"
MD5_VALUE_IN_FILENAME=$(echo "$MD5_VALUE_IN_FILENAME" | tr 'A-F' 'a-f')
echo "${logTag} MD5_VALUE_IN_FILENAME: $MD5_VALUE_IN_FILENAME" >> "$logFile"

REAL_MD5_VALUE=$(/usr/bin/md5sum "${FILENAME_WHOLE}")
REAL_MD5_VALUE="${REAL_MD5_VALUE:0:${#MD5_VALUE_IN_FILENAME}}"
echo "${logTag} REAL_MD5_VALUE: $REAL_MD5_VALUE" >> "$logFile"
echo >> "$logFile"

# If Packet is right one, unzip it
if [[ "$MD5_VALUE_IN_FILENAME" == "$REAL_MD5_VALUE" ]]
then
    echo "${logTag} Right MD5_Packet! Unzip it..." >> "$logFile"
else
    echo "${logTag} Wrong MD5_Packet! Delete all..." >> "$logFile"
    RUN
fi

# Stop all processes before copy
echo "${logTag} Begin to stop processes first!" >> "$logFile"
"${INSTALLPATH}/scripts/stop.sh"
echo >> "$logFile"

echo "${logTag} Start Unzip at $(date '+%Y-%m-%d %H:%M:%S')..." >> "$logFile"
TAR_RESULT=$(tar -zxvf "$FILENAME_WHOLE" -C "$DIRECTORY_STATIC")

if hasUpgradePackageLayout "$DIRECTORY_STATIC"; then
	PACKAGE_ROOT="$DIRECTORY_STATIC"
	UNZIP_DIRNAME="."
else
	UNZIP_DIRNAME="${TAR_RESULT%%/*}"
	PACKAGE_ROOT="$DIRECTORY_STATIC/$UNZIP_DIRNAME"
	if ! hasUpgradePackageLayout "$PACKAGE_ROOT"; then
		echo "${logTag} Upgrade package layout error, delete all..." >> "$logFile"
		RUN
	fi
fi
echo "${logTag} UNZIP_DIRNAME: $UNZIP_DIRNAME" >> "$logFile"
echo "${logTag} PACKAGE_ROOT: $PACKAGE_ROOT" >> "$logFile"
cd "$PACKAGE_ROOT" || exit 1

echo "${logTag} Stop Unzip at $(date '+%Y-%m-%d %H:%M:%S')..." >> "$logFile"

echo "${logTag} Be about to do full install package..." >> "$logFile"
echo "${logTag} cd $PACKAGE_ROOT/scripts/" >> "$logFile"
cd "$PACKAGE_ROOT/scripts/" || exit 1
echo "${logTag} Start Run install.sh $(date '+%Y-%m-%d %H:%M:%S')" >> "$logFile"
sh "$PACKAGE_ROOT/scripts/install.sh" "$logFile"
echo "${logTag} Stop Run install.sh $(date '+%Y-%m-%d %H:%M:%S')" >> "$logFile"

# Start services
RUN
