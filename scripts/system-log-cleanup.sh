#!/bin/bash
# Boot recovery only. No daemon, periodic cleanup, or application data removal.
# Trigger: root free space < 1 GiB AND managed logs > 512 MiB.
# Defaults are deliberately fixed; this file may be sourced by isolated tests.
LOG_DIR=/var/log
ROOT_DIR=/
BOOT_MARKER=/run/cosmo-log-cleanup.done
MIN_FREE_KIB=1048576
MIN_LOG_KIB=524288
MIN_ACTIVE_KIB=51200
JOURNAL_TARGET=128M

cleanup_log() {
    printf '[LOG-CLEANUP] %s\n' "$*"
}

available_kib() {
    local value
    value=$(LC_ALL=C df -Pk -- "$ROOT_DIR" 2>/dev/null | awk 'NR == 2 {print $4}')
    [[ "$value" =~ ^[0-9]+$ ]] || return 1
    printf '%s\n' "$value"
}

space_recovered() {
    local value
    value=$(available_kib) || {
        cleanup_log 'Cannot remeasure free space; stopping cleanup.'
        return 0
    }
    (( value >= MIN_FREE_KIB ))
}

# OverlayFS files and directories can have different st_dev values even on the
# same mount. Compare mount IDs; this also rejects bind mounts on the same disk.
mount_id() {
    findmnt -rn -o ID -T "$1" 2>/dev/null
}

on_root_mount() {
    [[ $(mount_id "$1") == "$root_mount_id" ]]
}

# Never follow links, touch hard-linked files, or cross into another mount.
managed_file() {
    [[ -f "$1" && ! -L "$1" ]] || return 1
    on_root_mount "$1" && [[ $(stat -c '%h' -- "$1" 2>/dev/null) == 1 ]]
}

managed_journal() {
    local mounts target unsafe_entry
    [[ -d "$LOG_DIR/journal" && ! -L "$LOG_DIR/journal" ]] || return 1
    on_root_mount "$LOG_DIR/journal" || return 1
    mounts=$(findmnt -rn -o TARGET 2>/dev/null) || return 1
    while IFS= read -r target; do
        [[ "$target" == "$LOG_DIR/journal/"* ]] && return 1
    done <<< "$mounts"
    unsafe_entry=$(find "$LOG_DIR/journal" \( -type l -o -type f -links +1 \) -print -quit) || return 1
    [[ -z "$unsafe_entry" ]]
}

collect_text_logs() {
    local file name
    text_logs=()
    archives=()
    for file in "$LOG_DIR/syslog" "$LOG_DIR/kern.log" \
        "$LOG_DIR"/syslog.* "$LOG_DIR"/kern.log.*; do
        name=${file##*/}
        [[ "$name" =~ ^(syslog|kern\.log)(\.[0-9]+(\.gz)?)?$ ]] || continue
        managed_file "$file" || continue
        text_logs+=("$file")
        [[ "$name" == syslog || "$name" == kern.log ]] || archives+=("$file")
    done
}

managed_usage_kib() {
    local file blocks total=0 journal_blocks
    for file in "${text_logs[@]}"; do
        blocks=$(stat -c '%b' -- "$file" 2>/dev/null) || return 1
        [[ "$blocks" =~ ^[0-9]+$ ]] || return 1
        total=$((total + blocks * 512))
    done
    if managed_journal; then
        # managed_journal rejects nested mounts/links. Do not use du -x here:
        # it skips regular journal files on some OverlayFS implementations.
        journal_blocks=$(find "$LOG_DIR/journal" -type f \
            \( -name '*.journal' -o -name '*.journal~' \) -printf '%b\n') || return 1
        while IFS= read -r blocks; do
            [[ -n "$blocks" ]] || continue
            [[ "$blocks" =~ ^[0-9]+$ ]] || return 1
            total=$((total + blocks * 512))
        done <<< "$journal_blocks"
    fi
    printf '%s\n' "$((total / 1024))"
}

vacuum_journal() {
    managed_journal || return 0
    command -v journalctl >/dev/null && command -v timeout >/dev/null || return 0
    # Vacuum old archives first: rotation itself may fail on a completely full disk.
    timeout 5 journalctl --directory="$LOG_DIR/journal" --vacuum-size="$JOURNAL_TARGET" ||
        cleanup_log 'Journal archive cleanup failed; continuing.'
    space_recovered && return 0
    timeout 5 journalctl --rotate || cleanup_log 'Journal rotation failed; continuing.'
    timeout 5 journalctl --directory="$LOG_DIR/journal" --vacuum-size="$JOURNAL_TARGET" ||
        cleanup_log 'Journal cleanup after rotation failed; continuing.'
}

cleanup_main() {
    local free_kib usage_kib file first second
    local root_mount_id
    local -a text_logs archives
    # /run is volatile. Mark before working so even a failed manual service restart
    # cannot repeatedly discard logs during the same boot.
    (umask 077; mkdir -- "$BOOT_MARKER") 2>/dev/null || return 0
    [[ -d "$LOG_DIR" && ! -L "$LOG_DIR" ]] || return 0
    [[ $(readlink -f -- "$LOG_DIR") == "$LOG_DIR" ]] || return 0
    root_mount_id=$(mount_id "$ROOT_DIR") || return 0
    [[ "$root_mount_id" =~ ^[0-9]+$ ]] || return 0
    on_root_mount "$LOG_DIR" || return 0
    free_kib=$(available_kib) || { cleanup_log 'Cannot measure root free space; skipped.'; return 0; }
    (( free_kib < MIN_FREE_KIB )) || return 0
    collect_text_logs
    usage_kib=$(managed_usage_kib) || { cleanup_log 'Cannot measure log usage; skipped.'; return 0; }
    if (( usage_kib <= MIN_LOG_KIB )); then
        cleanup_log 'Root space is low but managed logs are small; left other files untouched.'
        return 0
    fi
    cleanup_log "Boot recovery: free=${free_kib}KiB, managed logs=${usage_kib}KiB."

    # Numeric rotation names contain no whitespace. Delete oldest archives first.
    while IFS= read -r file; do
        [[ -n "$file" ]] || continue
        space_recovered && break
        if managed_file "$file"; then
            rm -f -- "$file" || cleanup_log "Cannot remove archive: ${file##*/}"
        fi
    done < <(if (( ${#archives[@]} )); then
        for file in "${archives[@]}"; do
            stat -c '%Y %n' -- "$file" 2>/dev/null
        done | sort -n | cut -d ' ' -f 2-
    fi)

    space_recovered || vacuum_journal
    if ! space_recovered; then
        # Clear the larger active log first, preserving its inode and permissions.
        first="$LOG_DIR/syslog"
        second="$LOG_DIR/kern.log"
        if [[ $(stat -c '%s' -- "$second" 2>/dev/null || echo 0) -gt \
              $(stat -c '%s' -- "$first" 2>/dev/null || echo 0) ]]; then
            first="$LOG_DIR/kern.log"
            second="$LOG_DIR/syslog"
        fi
        for file in "$first" "$second"; do
            space_recovered && break
            managed_file "$file" || continue
            # Do not discard a small current log if journal cleanup failed.
            (( $(stat -c '%s' -- "$file" 2>/dev/null || echo 0) >= MIN_ACTIVE_KIB * 1024 )) || continue
            truncate --no-create -s 0 -- "$file" || cleanup_log "Cannot truncate: ${file##*/}"
        done
    fi
    free_kib=$(available_kib) || free_kib=unknown
    cleanup_log "Boot recovery finished: free=${free_kib}KiB; other files untouched."
    return 0
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    PATH=/usr/sbin:/usr/bin:/sbin:/bin
    export PATH
    cleanup_main
fi
