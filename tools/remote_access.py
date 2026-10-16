#!/usr/bin/env python3
"""Task-scoped interactive remote access without credential persistence."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

import agent_workflow as core


PROJECT_ROOT = Path(__file__).resolve().parents[1]
HOST_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,252}$")
USER_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _validate_connection_input(host: str, user: str, port: int) -> tuple[str, str, int]:
    host = host.strip()
    user = user.strip()
    if not HOST_PATTERN.fullmatch(host):
        raise core.WorkflowError("remote host contains unsupported characters")
    if not USER_PATTERN.fullmatch(user):
        raise core.WorkflowError("remote user contains unsupported characters")
    if not 1 <= port <= 65535:
        raise core.WorkflowError("remote port must be between 1 and 65535")
    return host, user, port


def _connection_options(run_dir: Path) -> list[str]:
    session_dir = run_dir / "remote-session"
    session_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    session_dir.chmod(0o700)
    known_hosts = session_dir / "known_hosts"
    known_hosts.touch(mode=0o600, exist_ok=True)
    known_hosts.chmod(0o600)
    return [
        "-o",
        f"UserKnownHostsFile={known_hosts}",
        "-o",
        "HashKnownHosts=yes",
        "-o",
        "StrictHostKeyChecking=ask",
        "-o",
        "NumberOfPasswordPrompts=1",
        "-o",
        "ConnectTimeout=15",
        "-o",
        "ServerAliveInterval=30",
        "-o",
        "ServerAliveCountMax=3",
    ]


def _record_event(run_dir: Path, event: dict[str, Any]) -> None:
    path = run_dir / "remote-access-events.json"
    events: list[dict[str, Any]] = []
    if path.is_file():
        existing = core.load_json(path)
        if not isinstance(existing, list):
            raise core.WorkflowError("remote-access-events.json must be an array")
        events = existing
    events.append(core.redact_data(event))
    core.atomic_write_json(path, events)


def connect_remote(
    contract_arg: str | os.PathLike[str],
    *,
    host: str,
    user: str,
    port: int = 22,
    remote_command: list[str] | None = None,
    project_root: Path = PROJECT_ROOT,
    runner: Callable[..., subprocess.CompletedProcess[Any]] | None = None,
) -> int:
    """Open SSH with inherited terminal I/O; OpenSSH, not this helper, reads the password."""
    contract_path, run_dir, contract = core.resolve_contract_context(
        contract_arg, project_root
    )
    assessment = core.read_route_assessment(
        contract_path, run_dir, contract, require_ready=False
    )
    if assessment.get("routeVerdict") == "UNSUPPORTED":
        raise core.WorkflowError("the assessed route is unsupported; remote access will not start")
    if "remote-execution" not in core._authority_grants(contract):
        raise core.WorkflowError(
            "remote execution is not authorized; obtain or record the user's explicit request first"
        )
    development_environment = contract.get("parameters", {}).get("developmentEnvironment")
    if not isinstance(development_environment, dict) or (
        str(development_environment.get("os", "")).lower() != "linux"
    ):
        raise core.WorkflowError(
            "remote_access.py is limited to the Linux development environment declared by this task"
        )
    host, user, port = _validate_connection_input(host, user, port)
    ssh = shutil.which("ssh")
    if not ssh:
        raise core.WorkflowError("OpenSSH client is unavailable on the agent host")

    command = [
        ssh,
        *_connection_options(run_dir),
        "-p",
        str(port),
        "-l",
        user,
        host,
    ]
    if remote_command:
        if any("\x00" in value or "\n" in value or "\r" in value for value in remote_command):
            raise core.WorkflowError("remote command arguments cannot contain control characters")
        command.extend(remote_command)

    started_at = utc_now()
    execute = runner or subprocess.run
    try:
        process = execute(command, check=False)
    except OSError as error:
        raise core.WorkflowError(f"OpenSSH could not start: {error}") from error
    exit_code = int(process.returncode)
    _record_event(
        run_dir,
        {
            "schemaVersion": "1.0",
            "mode": "interactive-ssh",
            "startedAt": started_at,
            "completedAt": utc_now(),
            "status": "PASS" if exit_code == 0 else "FAIL",
            "exitCode": exit_code,
            "targetReference": "user-provided isolated development environment",
            "routeVerdictAtConnection": assessment.get("routeVerdict"),
            "credentialMaterialStored": False,
            "rawConnectionParametersStored": False,
        },
    )
    return exit_code


def connect_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 tools/remote_access.py",
        description=(
            "Open a task-authorized SSH session. Password input is handled by the "
            "interactive OpenSSH prompt and is never accepted as a script argument."
        ),
    )
    parser.add_argument("--contract", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--user", required=True)
    parser.add_argument("--port", type=int, default=22)
    parser.add_argument("--command", nargs=argparse.REMAINDER)
    options = parser.parse_args(arguments)
    print(
        "Opening task-scoped SSH. Entered credentials are handled by OpenSSH and are not written to run records.",
        file=sys.stderr,
    )
    if not sys.stdin.isatty():
        print(
            "warning: password authentication needs an interactive terminal/PTY",
            file=sys.stderr,
        )
    return connect_remote(
        options.contract,
        host=options.host,
        user=options.user,
        port=options.port,
        remote_command=options.command,
    )


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    try:
        return connect_main(args)
    except core.WorkflowError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    os.umask(0o077)
    raise SystemExit(main())
