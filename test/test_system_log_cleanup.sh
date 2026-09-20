#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd -P)"
root="$(mktemp -d)"
trap 'rm -rf -- "$root"' EXIT
# Source only; all paths and disk measurements below refer to disposable fixtures.
source "$repo/scripts/system-log-cleanup.sh"

prepare() {
    case_root="$root/$1"
    mkdir -p "$case_root/log" "$case_root/run"
    LOG_DIR="$(readlink -f "$case_root/log")"
    ROOT_DIR="$(readlink -f "$case_root")"
    BOOT_MARKER="$case_root/run/done"
    MIN_FREE_KIB=1024
    MIN_LOG_KIB=512
    MIN_ACTIVE_KIB=64
    free_before=0
    usage_before=0
    journal_calls="$case_root/journal.calls"
}

write_kib() {
    dd if=/dev/zero of="$1" bs=1024 count="$2" 2>/dev/null
}

measure() {
    usage_before=$(du -sk "$LOG_DIR" | awk '{print $1}')
}

# Model disk space released by actual fixture deletion/truncation. Nothing
# calls df, journalctl or systemctl on the host during these tests.
available_kib() {
    local current
    current=$(du -sk "$LOG_DIR" | awk '{print $1}')
    echo "$((free_before + usage_before - current))"
}

timeout() {
    shift
    "$@"
}

journalctl() {
    printf '%s\n' "$*" >> "$journal_calls"
    if [[ "$*" == *--vacuum-size=* ]]; then
        rm -f -- "$LOG_DIR/journal/system@old.journal"
    fi
}

prepare healthy
write_kib "$LOG_DIR/syslog" 2048
measure
free_before=2048
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"
test ! -e "$journal_calls"

prepare unrelated_files
write_kib "$LOG_DIR/application-data.bin" 2048
write_kib "$LOG_DIR/syslog" 16
measure
cleanup_main || exit 1
test -s "$LOG_DIR/application-data.bin"
test -s "$LOG_DIR/syslog"

prepare oldest_archive_first
write_kib "$LOG_DIR/syslog.2.gz" 1200
write_kib "$LOG_DIR/syslog.1" 32
write_kib "$LOG_DIR/syslog" 32
touch -t 202001010000 "$LOG_DIR/syslog.2.gz"
measure
cleanup_main || exit 1
test ! -e "$LOG_DIR/syslog.2.gz"
test -s "$LOG_DIR/syslog.1"
test -s "$LOG_DIR/syslog"

prepare active_log
write_kib "$LOG_DIR/syslog" 1200
write_kib "$LOG_DIR/kern.log" 32
before_inode=$(stat -c '%i:%a' "$LOG_DIR/syslog")
measure
cleanup_main || exit 1
test -f "$LOG_DIR/syslog"
test ! -s "$LOG_DIR/syslog"
test "$before_inode" = "$(stat -c '%i:%a' "$LOG_DIR/syslog")"
test -s "$LOG_DIR/kern.log"
# A service restart during this boot must not clear newly written logs.
write_kib "$LOG_DIR/syslog" 1200
measure
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"

prepare larger_kernel_log
write_kib "$LOG_DIR/syslog" 32
write_kib "$LOG_DIR/kern.log" 1200
measure
cleanup_main || exit 1
test ! -s "$LOG_DIR/kern.log"
test -s "$LOG_DIR/syslog"

prepare journal_first
mkdir "$LOG_DIR/journal"
write_kib "$LOG_DIR/journal/system@old.journal" 1200
write_kib "$LOG_DIR/syslog" 32
measure
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"
test ! -e "$LOG_DIR/journal/system@old.journal"
grep -q -- '--vacuum-size=128M' "$journal_calls"
test "$(wc -l < "$journal_calls")" -eq 1

prepare failed_journal
journalctl() { printf '%s\n' "$*" >> "$journal_calls"; return 1; }
mkdir "$LOG_DIR/journal"
write_kib "$LOG_DIR/journal/system@old.journal" 32
write_kib "$LOG_DIR/syslog" 1200
measure
cleanup_main || exit 1
test ! -s "$LOG_DIR/syslog"
test -s "$LOG_DIR/journal/system@old.journal"
test "$(wc -l < "$journal_calls")" -eq 3

prepare failed_journal_small_text
mkdir "$LOG_DIR/journal"
write_kib "$LOG_DIR/journal/system@old.journal" 1200
write_kib "$LOG_DIR/syslog" 32
measure
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"

prepare whitelist_and_links
write_kib "$case_root/keep" 1200
ln -s "$case_root/keep" "$LOG_DIR/syslog"
if [[ ! -L "$LOG_DIR/syslog" ]]; then
    echo 'SKIP: this host emulates symlinks; symlink exclusion needs Linux verification.'
fi
ln "$case_root/keep" "$LOG_DIR/kern.log"
write_kib "$LOG_DIR/syslog.customer.gz" 1200
write_kib "$LOG_DIR/auth.log" 1200
write_kib "$LOG_DIR/syslog.1" 1200
measure
cleanup_main || exit 1
test -s "$case_root/keep"
test -s "$LOG_DIR/syslog.customer.gz"
test -s "$LOG_DIR/auth.log"
test ! -e "$LOG_DIR/syslog.1"

prepare different_filesystem
write_kib "$LOG_DIR/syslog" 1200
# Simulate /var/log on a separate filesystem; root pressure must not erase it.
mount_id() {
    if [[ "$1" == "$LOG_DIR" ]]; then
        echo 999999
    else
        findmnt -rn -o ID -T "$1"
    fi
}
measure
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"
mount_id() { findmnt -rn -o ID -T "$1"; }

# Same mount, different file st_dev: measured on Sophon OverlayFS. The old
# device-number guard skipped both text logs and journal files in this case.
prepare overlay_files
write_kib "$LOG_DIR/syslog" 1200
stat() {
    if [[ "$*" == "-c %d -- $LOG_DIR/syslog" ]]; then
        echo 987654
    else
        command stat "$@"
    fi
}
measure
cleanup_main || exit 1
test ! -s "$LOG_DIR/syslog"
unset -f stat

prepare bind_mounted_log
write_kib "$LOG_DIR/syslog" 1200
mount_id() {
    if [[ "$1" == "$LOG_DIR/syslog" ]]; then
        echo 999999
    else
        findmnt -rn -o ID -T "$1"
    fi
}
measure
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"
mount_id() { findmnt -rn -o ID -T "$1"; }

prepare overlay_journal_usage
mkdir "$LOG_DIR/journal"
write_kib "$LOG_DIR/journal/system@old.journal" 1200
root_mount_id=$(mount_id "$ROOT_DIR")
text_logs=()
du() {
    if [[ "$*" == "-skx -- $LOG_DIR/journal" ]]; then
        printf '4\t%s\n' "$LOG_DIR/journal"
    else
        command du "$@"
    fi
}
test "$(managed_usage_kib)" -ge 1200
unset -f du

prepare linked_journal
mkdir "$LOG_DIR/journal"
write_kib "$case_root/keep" 1200
ln -s "$case_root/keep" "$LOG_DIR/journal/system@old.journal"
root_mount_id=$(mount_id "$ROOT_DIR")
if managed_journal; then
    echo 'journal containing links must not be vacuumed' >&2
    exit 1
fi

prepare failed_measurement
write_kib "$LOG_DIR/syslog" 1200
available_kib() { return 1; }
cleanup_main || exit 1
test -s "$LOG_DIR/syslog"

echo 'system log cleanup tests passed'
