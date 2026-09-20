#!/usr/bin/python3
"""Install bounded system logging; also run as rsyslog's synchronous rotator.

Only explicitly listed system logs are managed. The rotation command runs as
rsyslog's unprivileged user. Installation is transactional for configuration;
expired log contents, like any normal log rotation, cannot be rolled back.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile


MIB = 1024 * 1024
LIMITS = {name: (4 * MIB, 2) for name in (
    "auth.log", "mail.log", "mail.info", "mail.warn", "mail.err", "daemon.log",
    "user.log", "lpr.log", "cron.log", "debug", "messages", "prompt-cmd.log",
)}
LIMITS.update({"syslog": (32 * MIB, 3), "kern.log": (32 * MIB, 3)})
HELPER = "/usr/local/lib/cosmo/system-log-retention.py"
JOURNAL_CONFIG = "/etc/systemd/journald.conf.d/90-cosmo-log-retention.conf"
STATE = "/var/lib/cosmo-log-retention/pending"
TAG = "# cosmo-log-retention-v1"
JOURNAL_VALUES = {
    "SystemMaxUse": "128M", "SystemMaxFileSize": "16M", "SystemKeepFree": "1G",
    "RuntimeMaxUse": "32M", "RuntimeMaxFileSize": "8M",
}
JOURNAL_TEXT = "# Managed by Cosmo installation and upgrade.\n[Journal]\n" + "".join(
    f"{key}={value}\n" for key, value in JOURNAL_VALUES.items()
)


class PolicyError(RuntimeError):
    pass


def checked_path(root: Path, absolute: str) -> Path:
    """Never let a configuration or log symlink redirect privileged writes."""
    relative = Path(absolute)
    if not relative.is_absolute() or ".." in relative.parts:
        raise PolicyError(f"invalid managed path: {absolute}")
    target = root / relative.relative_to("/")
    for part in [target, *target.parents]:
        if part == root:
            break
        if part.is_symlink():
            raise PolicyError(f"symbolic link in managed path: {part}")
    if target.exists() and target.is_file() and target.stat().st_nlink != 1:
        raise PolicyError(f"hard-linked managed file: {target}")
    return target


def rotation_action(selector: str, path: str, template: str = "RSYSLOG_TraditionalFileFormat",
                    helper: str = HELPER) -> str:
    limit, _ = LIMITS[Path(path).name]
    channel = "cosmo_log_" + re.sub(r"\W", "_", Path(path).name)
    # Ubuntu 22.04's rsyslog 8.2112 supports synchronous size rotation through
    # $outchannel, but does NOT accept the newer rotation.sizeLimit parameters.
    # Keep this compatible syntax until the minimum supported daemon changes.
    # Older daemons pass everything after the executable as ONE argument.
    # Give the helper just the log path, with no shell and no argument splitting.
    return (f"{TAG}\n$outchannel {channel},{path},{limit},{helper} {path}\n"
            f"{selector} :omfile:${channel};{template}\n")


def rewrite_rsyslog(text: str) -> tuple[str, list[str]]:
    legacy = re.compile(r"^(\s*)([\w*.,;!=]+)\s+-?(/var/log/[\w.-]+)"
                        r"(?:;([\w]+))?\s*(?:#.*)?$")
    channel_definition = re.compile(r'^\$outchannel (cosmo_log_[\w]+),(/var/log/[\w.-]+),'
                                    r'[0-9]+,' + re.escape(HELPER) + r' (/var/log/[\w.-]+)$')
    channel_action = re.compile(r'^([\w*.,;!=]+)\s+:omfile:\$(cosmo_log_[\w]+);([\w]+)$')
    output, paths = [], []
    lines = iter(text.splitlines(keepends=True))
    for line in lines:
        if line.strip() == TAG:
            definition = channel_definition.fullmatch(next(lines, "").strip())
            action = channel_action.fullmatch(next(lines, "").strip())
            if (not definition or not action or definition[1] != action[2] or
                    definition[2] != definition[3] or Path(definition[2]).name not in LIMITS):
                raise PolicyError("modified managed rsyslog block; refusing to overwrite it")
            output.append(rotation_action(action[1], definition[2], action[3]))
            paths.append(definition[2])
            continue
        match = legacy.match(line.rstrip("\r\n"))
        if match and Path(match[3]).name in LIMITS:
            output.append(match[1] + rotation_action(match[2], match[3], match[4] or
                                                    "RSYSLOG_TraditionalFileFormat"))
            paths.append(match[3])
        else:
            if not line.lstrip().startswith("#") and any(
                re.search(re.escape("/var/log/" + name) + r'(?![\w./-])', line)
                for name in LIMITS
            ):
                raise PolicyError("unsupported rsyslog file action; refusing a partial policy")
            output.append(line)
    return "".join(output), paths


def remove_logrotate_paths(text: str, managed: set[str]) -> str:
    """Remove only exact managed paths, preserving other blocks and scripts."""
    active_text = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
    patterns = re.findall(r'''(?:^|\s)["']?(/var/log/[^\s"'{]+)''', active_text)
    if not any(fnmatch.fnmatchcase(path, pattern) for path in managed for pattern in patterns):
        return text
    lines = text.splitlines(keepends=True)
    output, index = [], 0
    while index < len(lines):
        line = lines[index]
        if not line.lstrip().startswith(("/", '\"/')):
            output.append(line)
            index += 1
            continue
        start = index
        header = []
        while index < len(lines) and "{" not in lines[index]:
            header.append(lines[index])
            index += 1
        if index == len(lines):
            raise PolicyError("unsupported logrotate block header")
        prefix, _, suffix = lines[index].partition("{")
        if suffix.strip():
            raise PolicyError("unsupported inline logrotate block")
        header.append(prefix)
        paths = shlex.split(" ".join(header), comments=True)
        for pattern in paths:
            if pattern not in managed and any(fnmatch.fnmatchcase(p, pattern) for p in managed):
                raise PolicyError(f"logrotate wildcard overlaps managed logs: {pattern}")
        body_start = index + 1
        in_script = False
        index += 1
        while index < len(lines):
            token = lines[index].strip()
            if token in {"prerotate", "postrotate", "firstaction", "lastaction", "preremove"}:
                in_script = True
            elif token == "endscript":
                in_script = False
            elif token == "}" and not in_script:
                break
            index += 1
        if index == len(lines):
            raise PolicyError("unterminated logrotate block")
        remaining = [p for p in paths if p not in managed]
        if remaining == paths:
            output.extend(lines[start:index + 1])
        elif remaining:
            output.append("\n".join(shlex.quote(p) for p in remaining) + "\n{\n")
            output.extend(lines[body_start:index + 1])
        index += 1
    return "".join(output)


def trim_recent(path: Path, limit: int) -> None:
    """Bound an old oversized file without allocating a second file on disk."""
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
        raise PolicyError(f"unsafe log file: {path}")
    if before.st_size <= limit:
        return
    fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise PolicyError(f"unsafe log file: {path}")
        if info.st_size <= limit:
            return
        os.lseek(fd, -limit, os.SEEK_END)
        chunks, left = [], limit
        while left:
            block = os.read(fd, left)
            if not block:
                break
            chunks.append(block)
            left -= len(block)
        tail = b"".join(chunks)
        newline = tail.find(b"\n")
        if newline >= 0:
            tail = tail[newline + 1:]
        os.ftruncate(fd, 0)
        os.lseek(fd, 0, os.SEEK_SET)
        view = memoryview(tail)
        while view:
            written = os.write(fd, view)
            if written == 0:
                raise PolicyError(f"cannot retain recent log contents: {path}")
            view = view[written:]
    finally:
        os.close(fd)


def maintain_log(root: Path, name: str, rotate: bool) -> None:
    import fcntl

    if name not in LIMITS:
        raise PolicyError("log is outside the managed system-log list")
    log_dir = checked_path(root, "/var/log")
    if not log_dir.is_dir():
        return
    directory_fd = os.open(log_dir, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        # All helper invocations serialize on the existing directory inode;
        # no root-owned lock file can obstruct rsyslog's unprivileged user.
        fcntl.flock(directory_fd, fcntl.LOCK_EX)
        active = checked_path(root, "/var/log/" + name)
        if active.exists() and not active.is_file():
            raise PolicyError(f"not a regular log: {active}")
        limit, keep = LIMITS[name]
        archives = []
        for candidate in log_dir.glob(name + ".*"):
            match = re.fullmatch(re.escape(name) + r"\.([0-9]+)(\.gz)?", candidate.name)
            if match:
                checked_path(root, "/var/log/" + candidate.name)
                if not candidate.is_file():
                    raise PolicyError(f"not a regular log archive: {candidate}")
                archives.append((candidate, int(match[1]), bool(match[2])))
        # Validate every destination before making changes. Old compressed
        # archives are expired at adoption; never decompress unbounded files.
        for candidate, number, compressed in archives:
            if compressed or not 1 <= number <= keep or candidate.name != f"{name}.{number}":
                candidate.unlink()
            else:
                trim_recent(candidate, limit)
        if not active.exists():
            return
        should_rotate = rotate and active.stat().st_size >= limit
        trim_recent(active, limit)
        if should_rotate:
            for number in range(keep, 0, -1):
                source = active if number == 1 else log_dir / f"{name}.{number - 1}"
                destination = log_dir / f"{name}.{number}"
                if source.exists():
                    os.replace(source, destination)
            # rsyslog reopens the missing active file with its configured owner
            # and mode. Do not create it here as the installer may run as root.
    finally:
        os.close(directory_fd)


def atomic_write(path: Path, content: bytes, mode: int, uid: int = -1, gid: int = -1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".cosmo-log-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            if uid != -1:
                os.fchown(stream.fileno(), uid, gid)
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


class Installer:
    def __init__(self, root: Path):
        self.root = root.absolute()
        if self.root != self.root.resolve() or not self.root.is_dir():
            raise PolicyError("installation root must be an existing canonical directory")
        self.live = self.root == Path("/")
        self.state = checked_path(self.root, STATE)
        if self.live and os.geteuid() != 0:
            raise PolicyError("system log policy installation requires root")

    def command(self, *args: str) -> subprocess.CompletedProcess:
        result = subprocess.run(args, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise PolicyError(f"{' '.join(args)} failed: {result.stderr.strip()}")
        return result

    def plan(self) -> tuple[dict[str, tuple[bytes, int]], set[str]]:
        changes = {HELPER: (Path(__file__).read_bytes(), 0o755),
                   JOURNAL_CONFIG: (JOURNAL_TEXT.encode(), 0o644)}
        managed: set[str] = set()
        config = checked_path(self.root, "/etc/rsyslog.conf")
        candidates = [config, *sorted((self.root / "etc/rsyslog.d").glob("*.conf"))]
        for path in candidates:
            if not path.exists():
                continue
            absolute = "/" + str(path.relative_to(self.root))
            checked_path(self.root, absolute)
            original = path.read_text()
            rendered, paths = rewrite_rsyslog(original)
            for item in paths:
                if item in managed:
                    raise PolicyError(f"multiple rsyslog actions write {item}")
                managed.add(item)
            if rendered != original:
                changes[absolute] = (rendered.encode(), stat.S_IMODE(path.stat().st_mode))
        if config.exists() and not {"/var/log/syslog", "/var/log/kern.log"} <= managed:
            raise PolicyError("rsyslog configuration does not expose both supported system-log actions")
        candidates = [self.root / "etc/logrotate.conf", *sorted(
            (self.root / "etc/logrotate.d").glob("*"))]
        for path in candidates:
            if not path.exists() or path.is_dir():
                continue
            if path.name.startswith(".") or path.name.endswith((
                "~", ".bak", ".disabled", ".dpkg-old", ".dpkg-dist", ".dpkg-new", ".dpkg-bak",
                ".rpmnew", ".rpmsave", ".rpmorig", ".ucf-old", ".ucf-new", ".ucf-dist",
            )):
                continue
            absolute = "/" + str(path.relative_to(self.root))
            checked_path(self.root, absolute)
            original = path.read_text()
            rendered = remove_logrotate_paths(original, managed)
            if rendered != original:
                changes[absolute] = (rendered.encode(), stat.S_IMODE(path.stat().st_mode))
        for absolute in changes:
            checked_path(self.root, absolute)
        return changes, managed

    def check(self) -> None:
        if self.state.exists():
            raise PolicyError("unfinished log-policy transaction; roll it back before retrying")
        self.plan()
        if self.live:
            for tool in ("systemctl", "journalctl", "systemd-analyze", "findmnt"):
                if shutil.which(tool) is None:
                    raise PolicyError(f"required system logging tool is missing: {tool}")
            if (self.root / "etc/rsyslog.conf").exists() and shutil.which("rsyslogd") is None:
                raise PolicyError("rsyslog configuration exists but rsyslogd is missing")
            if (self.root / "etc/logrotate.conf").exists() and shutil.which("logrotate") is None:
                raise PolicyError("logrotate configuration exists but logrotate is missing")

    def validate(self, managed: set[str]) -> None:
        if not self.live:
            return
        if managed:
            self.command("rsyslogd", "-N1")
        if (self.root / "etc/logrotate.conf").exists():
            # Debug mode parses and reports decisions without rotating logs or
            # updating the state file; postrotate scripts are not executed.
            self.command("logrotate", "--debug", "/etc/logrotate.conf")
        effective = self.command("systemd-analyze", "cat-config", "systemd/journald.conf").stdout
        values = {}
        for line in effective.splitlines():
            if line.strip() and not line.lstrip().startswith(("#", ";", "[")) and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
        if any(values.get(k) != v for k, v in JOURNAL_VALUES.items()):
            raise PolicyError("another journald drop-in overrides the requested retention limits")

    def apply(self) -> None:
        self.check()
        changes, managed = self.plan()
        self.state.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.state.parent.chmod(0o700)
        self.state.mkdir(mode=0o700)
        manifest = {"files": [], "managed": sorted(managed), "services": []}
        if self.live:
            for service in ("rsyslog", "systemd-journald"):
                if subprocess.run(["systemctl", "is-active", "--quiet", service]).returncode == 0:
                    manifest["services"].append(service)
        try:
            for number, (absolute, (content, mode)) in enumerate(changes.items()):
                target = checked_path(self.root, absolute)
                old = target.stat() if target.exists() else None
                record = {"path": absolute, "existed": old is not None,
                          "mode": stat.S_IMODE(old.st_mode) if old else mode,
                          "uid": old.st_uid if old else -1, "gid": old.st_gid if old else -1,
                          "backup": str(number)}
                if old:
                    atomic_write(self.state / str(number), target.read_bytes(), 0o600)
                manifest["files"].append(record)
            atomic_write(self.state / "manifest.json", json.dumps(manifest).encode(), 0o600)
            for absolute, (content, mode) in changes.items():
                target = checked_path(self.root, absolute)
                old = target.stat() if target.exists() else None
                atomic_write(target, content, mode, old.st_uid if old and self.live else -1,
                             old.st_gid if old and self.live else -1)
            self.validate(managed)
            # Stop only the text-log writer while adopting already oversized
            # logs. Journald continues collecting; the application is untouched.
            if self.live and "rsyslog" in manifest["services"]:
                self.command("systemctl", "stop", "rsyslog")
            for path in sorted(managed):
                maintain_log(self.root, Path(path).name, rotate=False)
            if self.live:
                if "rsyslog" in manifest["services"]:
                    self.command("systemctl", "start", "rsyslog")
                if "systemd-journald" in manifest["services"]:
                    self.command("systemctl", "restart", "systemd-journald")
                    self.command("journalctl", "--rotate")
                    self.command("journalctl", "--vacuum-size=128M")
        except BaseException:
            if (self.state / "manifest.json").exists():
                self.rollback()
            else:
                shutil.rmtree(self.state)
            raise

    def rollback(self) -> None:
        if not self.state.exists():
            return
        manifest = json.loads((self.state / "manifest.json").read_text())
        for record in reversed(manifest["files"]):
            target = checked_path(self.root, record["path"])
            if record["existed"]:
                atomic_write(target, (self.state / record["backup"]).read_bytes(), record["mode"],
                             record["uid"] if self.live else -1, record["gid"] if self.live else -1)
            elif target.exists():
                target.unlink()
        if self.live:
            for service in manifest["services"]:
                self.command("systemctl", "restart", service)
        shutil.rmtree(self.state)

    def commit(self) -> None:
        if self.state.exists():
            shutil.rmtree(self.state)


def main() -> int:
    # $outchannel on rsyslog 8.2112 passes a single path argument. Keep the
    # explicit rotate subcommand for administrators and isolated tests as well.
    argv = sys.argv[1:]
    if argv and argv[0].startswith("/var/log/"):
        argv = ["rotate", *argv]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "apply", "commit", "rollback", "rotate"))
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--root", type=Path, default=Path("/"),
                        help="offline image/test root; never controls live services")
    args = parser.parse_args(argv)
    try:
        if args.action == "rotate":
            if not 1 <= len(args.paths) <= 2 or any(p != args.paths[0] for p in args.paths):
                raise PolicyError("rotate requires one log path (optional identical rsyslog suffix)")
            path = Path(args.paths[0])
            if str(path) != "/var/log/" + path.name:
                raise PolicyError("rotation path is outside /var/log")
            maintain_log(args.root.resolve(), path.name, rotate=True)
        else:
            if args.paths:
                raise PolicyError("unexpected path argument")
            getattr(Installer(args.root), args.action)()
    except (OSError, ValueError, PolicyError, subprocess.TimeoutExpired) as error:
        print(f"[LOG-RETENTION] ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
