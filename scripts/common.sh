#!/bin/bash
# Common configuration and utility functions for Cosmo startup scripts
# Sourced by: inte_run_start.sh, start.sh, run_start.sh, install.sh

# ── Path constants ──
COSMO_INSTALL_DIR="${COSMO_INSTALL_DIR:-/appfs/cosmo_wander/cwai_data}"
COSMO_RUNTIME_PATHS_FILE="${COSMO_RUNTIME_PATHS_FILE:-${COSMO_INSTALL_DIR}/share/cosmo/runtime-paths.env}"
if [ -f "${COSMO_RUNTIME_PATHS_FILE}" ]; then
    # Generated from the package target chip. It provides defaults only;
    # deployment-specific COSMO_* overrides remain authoritative.
    # shellcheck disable=SC1090
    . "${COSMO_RUNTIME_PATHS_FILE}"
fi
COSMO_DATA_DIR="${COSMO_DATA_DIR:-${COSMO_PACKAGE_DATA_DIR:-/data/cwaiuserdata}}"
COSMO_APP_DATA_DIR="${COSMO_APP_DATA_DIR:-${COSMO_PACKAGE_APP_DATA_DIR:-${COSMO_INSTALL_DIR}}}"
COSMO_LOG_DIR="${COSMO_DATA_DIR}/log/logs"
COSMO_UPGRADE_DIR="${COSMO_DATA_DIR}/upgrade"
COSMO_NGINX_TMP_DIR="${COSMO_DATA_DIR}/tmp"
COSMO_GB28181_ENABLED="${COSMO_GB28181_ENABLED:-off}"
COSMO_GB28181_CANDIDATE="${COSMO_GB28181_CANDIDATE:-*}"
COSMO_GB28181_SIP_PORT="${COSMO_GB28181_SIP_PORT:-5060}"
COSMO_GB28181_MEDIA_PORT="${COSMO_GB28181_MEDIA_PORT:-9001}"


# Upgrade signal files
COSMO_UPGRADE_SIGN="${COSMO_DATA_DIR}/mqttUpgradeApp"

validate_runtime_root() {
    local root="$1" label="$2"
    case "$root" in
        /*) ;;
        *) echo "${label} must be an absolute path: ${root}" >&2; return 1 ;;
    esac
    if [ "$root" = / ] || [[ "$root" == *$'\n'* ]] || [[ "$root" == *$'\r'* ]]; then
        echo "${label} is not a safe runtime root: ${root}" >&2
        return 1
    fi
}
validate_gb28181_config() {
    case "$COSMO_GB28181_ENABLED" in
        on|off) ;;
        *)
            echo "COSMO_GB28181_ENABLED must be on or off: ${COSMO_GB28181_ENABLED}" >&2
            return 1
            ;;
    esac

    local port label
    for port in "$COSMO_GB28181_SIP_PORT" "$COSMO_GB28181_MEDIA_PORT"; do
        if [ "$port" = "$COSMO_GB28181_SIP_PORT" ]; then
            label=COSMO_GB28181_SIP_PORT
        else
            label=COSMO_GB28181_MEDIA_PORT
        fi
        if [[ ! "$port" =~ ^[0-9]+$ ]] || [ "${#port}" -gt 5 ]; then
            echo "${label} must be a port from 1 to 65535: ${port}" >&2
            return 1
        fi
        if ((10#$port < 1 || 10#$port > 65535)); then
            echo "${label} must be a port from 1 to 65535: ${port}" >&2
            return 1
        fi
    done

    if [ "$COSMO_GB28181_ENABLED" = on ]; then
        local reserved_port
        if [ "$COSMO_GB28181_SIP_PORT" = "$COSMO_GB28181_MEDIA_PORT" ]; then
            echo "COSMO_GB28181_SIP_PORT and COSMO_GB28181_MEDIA_PORT must differ" >&2
            return 1
        fi
        for port in "$COSMO_GB28181_SIP_PORT" "$COSMO_GB28181_MEDIA_PORT"; do
            for reserved_port in 1936 1985 18088 "${COSMO_HTTP_PORT:-8000}" \
                "${COSMO_WEBSOCKET_PORT:-9000}"; do
                if [ "$port" = "$reserved_port" ]; then
                    echo "GB28181 port ${port} conflicts with a Cosmo runtime port" >&2
                    return 1
                fi
            done
        done
    fi

    if [ "$COSMO_GB28181_CANDIDATE" != "*" ]; then
        if [[ ! "$COSMO_GB28181_CANDIDATE" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]]; then
            echo "COSMO_GB28181_CANDIDATE must be * or an IPv4 address" >&2
            return 1
        fi
        local octet
        local -a octets
        IFS=. read -r -a octets <<<"$COSMO_GB28181_CANDIDATE"
        for octet in "${octets[@]}"; do
            if ((10#$octet > 255)); then
                echo "COSMO_GB28181_CANDIDATE must be * or an IPv4 address" >&2
                return 1
            fi
        done
    fi
}


render_runtime_template() {
    local source_file="$1" output_file="$2" output_tmp line
    output_tmp="${output_file}.tmp.$$"
    [ -f "$source_file" ] || {
        echo "Runtime configuration template is missing: ${source_file}" >&2
        return 1
    }
    : >"$output_tmp"
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        line="${line//@COSMO_DATA_DIR@/${COSMO_DATA_DIR}}"
        line="${line//@COSMO_APP_DATA_DIR@/${COSMO_APP_DATA_DIR}}"
        line="${line//@COSMO_GB28181_ENABLED@/${COSMO_GB28181_ENABLED}}"
        line="${line//@COSMO_GB28181_CANDIDATE@/${COSMO_GB28181_CANDIDATE}}"
        line="${line//@COSMO_GB28181_SIP_PORT@/${COSMO_GB28181_SIP_PORT}}"
        line="${line//@COSMO_GB28181_MEDIA_PORT@/${COSMO_GB28181_MEDIA_PORT}}"
        printf '%s\n' "$line" >>"$output_tmp"
    done <"$source_file"
    mv -f -- "$output_tmp" "$output_file"
}

# Render package templates into the mutable data tree. Nginx and SRS do not
# expand arbitrary environment variables in path directives themselves.
render_runtime_configs() {
    validate_runtime_root "$COSMO_DATA_DIR" COSMO_DATA_DIR
    validate_gb28181_config || return 1
    validate_runtime_root "$COSMO_APP_DATA_DIR" COSMO_APP_DATA_DIR

    local runtime_root="${COSMO_DATA_DIR}/runtime"
    local nginx_template_prefix="${COSMO_INSTALL_DIR}/bin/nginx_conf"
    local srs_template="${COSMO_INSTALL_DIR}/bin/srs_conf/srs.conf"

    COSMO_RUNTIME_NGINX_PREFIX="${runtime_root}/nginx_conf"
    COSMO_RUNTIME_NGINX_CONF="${COSMO_RUNTIME_NGINX_PREFIX}/conf/nginx.conf"
    COSMO_RUNTIME_NGINX_UPSTREAM_CONF="${COSMO_RUNTIME_NGINX_PREFIX}/conf/conf.d/default.conf"
    COSMO_RUNTIME_SRS_CONF="${runtime_root}/srs.conf"

    [ -d "$nginx_template_prefix" ] || {
        echo "Nginx configuration template directory is missing: ${nginx_template_prefix}" >&2
        return 1
    }

    mkdir -p "$runtime_root"
    rm -rf -- "$COSMO_RUNTIME_NGINX_PREFIX"
    mkdir -p "$COSMO_RUNTIME_NGINX_PREFIX"
    cp -a -- "${nginx_template_prefix}/." "${COSMO_RUNTIME_NGINX_PREFIX}/"
    render_runtime_template \
        "${nginx_template_prefix}/conf/nginx.conf" \
        "$COSMO_RUNTIME_NGINX_CONF"
    render_runtime_template \
        "${nginx_template_prefix}/conf/conf.d/default.conf" \
        "$COSMO_RUNTIME_NGINX_UPSTREAM_CONF"
    render_runtime_template "$srs_template" "$COSMO_RUNTIME_SRS_CONF"
}

# ── Log helpers ──
# Rotate launcher/script logs independently from the application's internal
# rotating logger. This protects redirected stdout/stderr and early-start logs.
# Usage: rotate_external_log <logFile>
rotate_external_log() {
    local file="$1"
    local max_bytes="${COSMO_SCRIPT_LOG_MAX_BYTES:-20971520}"
    local keep_files="${COSMO_SCRIPT_LOG_KEEP_FILES:-5}"
    local current_bytes index

    [ -n "$file" ] || return 0
    [ -f "$file" ] || return 0

    case "$max_bytes" in
        ''|*[!0-9]*) max_bytes=20971520 ;;
    esac
    case "$keep_files" in
        ''|*[!0-9]*) keep_files=5 ;;
    esac
    if [ "$max_bytes" -lt 1 ]; then
        max_bytes=20971520
    fi
    if [ "$keep_files" -lt 1 ]; then
        keep_files=5
    fi

    current_bytes="$(wc -c < "$file" 2>/dev/null | tr -d '[:space:]' || echo 0)"
    case "$current_bytes" in
        ''|*[!0-9]*) current_bytes=0 ;;
    esac
    if [ "$current_bytes" -lt "$max_bytes" ]; then
        return 0
    fi

    index=$((keep_files - 1))
    while [ "$index" -ge 1 ]; do
        if [ -e "${file}.${index}" ]; then
            mv -f "${file}.${index}" "${file}.$((index + 1))"
        fi
        index=$((index - 1))
    done
    mv -f "$file" "${file}.1"
}

# Usage: cosmo_log <TAG> <message> [logFile]
cosmo_log() {
    local tag="$1" msg="$2" file="${3:-}"
    local line="[${tag}] $(date '+%Y-%m-%d %H:%M:%S') ${msg}"
    echo "$line"
    if [ -n "$file" ]; then
        rotate_external_log "$file"
        echo "$line" >> "$file"
    fi
}

# ── Network contract helpers ──
is_valid_tcp_port() {
    local port="$1"
    case "$port" in
        ''|*[!0-9]*) return 1 ;;
    esac
    [ "$port" -ge 1 ] && [ "$port" -le 65535 ]
}

nginx_upstream_port() {
    local config_file="$1" upstream_name="$2"
    awk -v target="$upstream_name" '
        $1 == "upstream" && $2 == target { in_target = 1; next }
        in_target && $1 == "server" {
            endpoint = $2
            sub(/;$/, "", endpoint)
            sub(/^.*:/, "", endpoint)
            print endpoint
            exit
        }
        in_target && /}/ { in_target = 0 }
    ' "$config_file"
}

# Refuse startup before stopping the current service when the engine ports and
# packaged Nginx upstreams drift apart.
# Usage: verify_nginx_engine_port_contract <default.conf> <httpPort> <websocketPort>
verify_nginx_engine_port_contract() {
    local config_file="$1" http_port="$2" websocket_port="$3"
    local nginx_http_port nginx_websocket_port

    if [ ! -f "$config_file" ]; then
        echo "Nginx upstream config is missing: ${config_file}" >&2
        return 1
    fi
    if ! is_valid_tcp_port "$http_port"; then
        echo "Invalid COSMO_HTTP_PORT: ${http_port}" >&2
        return 1
    fi
    if ! is_valid_tcp_port "$websocket_port"; then
        echo "Invalid COSMO_WEBSOCKET_PORT: ${websocket_port}" >&2
        return 1
    fi

    nginx_http_port="$(nginx_upstream_port "$config_file" mvit)"
    nginx_websocket_port="$(nginx_upstream_port "$config_file" mvws)"
    if ! is_valid_tcp_port "$nginx_http_port"; then
        echo "Nginx upstream mvit has no valid TCP port in ${config_file}" >&2
        return 1
    fi
    if ! is_valid_tcp_port "$nginx_websocket_port"; then
        echo "Nginx upstream mvws has no valid TCP port in ${config_file}" >&2
        return 1
    fi
    if [ "$nginx_http_port" != "$http_port" ]; then
        echo "HTTP port mismatch: engine=${http_port}, nginx mvit=${nginx_http_port}" >&2
        return 1
    fi
    if [ "$nginx_websocket_port" != "$websocket_port" ]; then
        echo "WebSocket port mismatch: engine=${websocket_port}, nginx mvws=${nginx_websocket_port}" >&2
        return 1
    fi

    echo "HTTP=${http_port}, WebSocket=${websocket_port}"
}

# ── Directory setup ──
# Create all runtime directories needed before services start
ensure_runtime_dirs() {
    mkdir -p "${COSMO_DATA_DIR}"
    mkdir -p "${COSMO_LOG_DIR}"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_body"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_proxy"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_fastcgi"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_uwsgi"
    mkdir -p "${COSMO_NGINX_TMP_DIR}/nginx_scgi"
    mkdir -p "${COSMO_UPGRADE_DIR}"

    if [ -d "${COSMO_INSTALL_DIR}/bin/nginx_conf" ]; then
        mkdir -p "${COSMO_INSTALL_DIR}/bin/nginx_conf/logs"
    fi
}

# ── Process helpers ──
# Wait until a TCP port is no longer in LISTEN state (max timeout seconds)
# Usage: wait_for_port_free <port> [timeout_seconds]
wait_for_port_free() {
    local port="$1" timeout="${2:-10}" elapsed=0
    while ss -tlnp 2>/dev/null | grep -q ":${port} " && [ "$elapsed" -lt "$timeout" ]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done
}

# ── Iptables helpers ──
# Add iptables rule only if it does not already exist
# Usage: iptables_ensure <rule arguments...>
iptables_ensure() {
    if ! iptables -C "$@" 2>/dev/null; then
        iptables -A "$@"
    fi
}
