#!/usr/bin/env python3
"""A dependency-free simulator for the HTTP network audio pillar used by CosmoEdge.

The production platform gets health checks from ``/v1/check_alive`` and posts
text-to-speech and audio URL commands to ``/v1/speech``.  This server implements
that device-side contract and exposes received commands under ``/__mock__/`` so
an operator or an automated test can verify the complete request path.
"""

from __future__ import annotations

import argparse
import json
import logging
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


LOGGER = logging.getLogger("mock-audio-pillar")
MAX_REQUEST_BYTES = 1024 * 1024


@dataclass
class SimulatorState:
    verify_audio_urls: bool = True
    log_file: Path | None = None
    events: list[dict[str, Any]] = field(default_factory=list)
    alive_checks: int = 0
    last_alive_at: str | None = None
    fail_next: bool = False
    lock: threading.Lock = field(default_factory=threading.Lock)

    def clear(self) -> None:
        with self.lock:
            self.events.clear()
            self.fail_next = False
            self.alive_checks = 0
            self.last_alive_at = None

    def record_alive(self) -> None:
        with self.lock:
            self.alive_checks += 1
            self.last_alive_at = timestamp()

    def consume_failure(self) -> bool:
        with self.lock:
            fail = self.fail_next
            self.fail_next = False
            return fail

    def set_failure(self) -> None:
        with self.lock:
            self.fail_next = True

    def append(self, event: dict[str, Any]) -> dict[str, Any]:
        with self.lock:
            event = {"sequence": len(self.events) + 1, **event}
            self.events.append(event)
            if self.log_file is not None:
                self.log_file.parent.mkdir(parents=True, exist_ok=True)
                with self.log_file.open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(event, ensure_ascii=False) + "\n")
            return event

    def snapshot(self) -> list[dict[str, Any]]:
        with self.lock:
            return list(self.events)

    def status(self) -> dict[str, Any]:
        with self.lock:
            return {
                "status": "ok",
                "aliveChecks": self.alive_checks,
                "lastAliveAt": self.last_alive_at,
                "speechCommands": len(self.events),
                "lastSpeech": self.events[-1] if self.events else None,
                "failNext": self.fail_next,
            }


class AudioPillarServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, handler, state: SimulatorState):
        super().__init__(address, handler)
        self.state = state


class AudioPillarHandler(BaseHTTPRequestHandler):
    server: AudioPillarServer
    protocol_version = "HTTP/1.1"

    def log_message(self, message: str, *args: Any) -> None:
        LOGGER.info("%s - %s", self.client_address[0], message % args)

    def _json(self, status: int, body: dict[str, Any]) -> None:
        encoded = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def _read_json(self, *, allow_empty: bool = False) -> dict[str, Any]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise ValueError("invalid Content-Length") from error
        if length > MAX_REQUEST_BYTES:
            raise ValueError("request body is too large")
        raw = self.rfile.read(length)
        if not raw and allow_empty:
            return {}
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError("request body must be a UTF-8 JSON object") from error
        if not isinstance(value, dict):
            raise ValueError("request body must be a JSON object")
        return value

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = urllib.parse.urlsplit(self.path).path
        if path == "/v1/check_alive":
            self.server.state.record_alive()
            self._json(200, {"code": 200, "message": "alive"})
            return
        if path == "/__mock__/events":
            events = self.server.state.snapshot()
            self._json(200, {"count": len(events), "events": events})
            return
        if path == "/__mock__/health":
            self._json(200, {"status": "ok", "service": "mock-audio-pillar"})
            return
        if path == "/__mock__/status":
            self._json(200, self.server.state.status())
            return
        self._json(404, {"code": 404, "message": "not found"})

    def do_DELETE(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if urllib.parse.urlsplit(self.path).path != "/__mock__/events":
            self._json(404, {"code": 404, "message": "not found"})
            return
        self.server.state.clear()
        self._json(200, {"code": 200, "message": "events cleared"})

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = urllib.parse.urlsplit(self.path).path
        try:
            if path == "/__mock__/fail-next":
                self._read_json(allow_empty=True)
                self.server.state.set_failure()
                self._json(200, {"code": 200, "message": "next speech will fail"})
                return
            if path != "/v1/speech":
                self._json(404, {"code": 404, "message": "not found"})
                return

            payload = self._read_json()
            kind = validate_speech(payload)
            event = {
                "receivedAt": timestamp(),
                "client": self.client_address[0],
                "kind": kind,
                "payload": payload,
            }
            if kind == "audio_url" and self.server.state.verify_audio_urls:
                event["audioFetch"] = fetch_audio_url(payload["url"])
                if not event["audioFetch"]["ok"]:
                    self.server.state.append(event)
                    self._json(200, {"code": 502, "message": "audio URL is not reachable"})
                    return

            self.server.state.append(event)
            LOGGER.info("accepted %s command: %s", kind, json.dumps(payload, ensure_ascii=False))
            if self.server.state.consume_failure():
                self._json(200, {"code": 500, "message": "simulated device failure"})
                return
            self._json(200, {"code": 200, "message": "accepted"})
        except ValueError as error:
            self._json(400, {"code": 400, "message": str(error)})


def require_integer(payload: dict[str, Any], name: str, minimum: int, maximum: int) -> None:
    value = payload.get(name)
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"{name} must be an integer from {minimum} to {maximum}")


def timestamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def validate_speech(payload: dict[str, Any]) -> str:
    has_text = "text" in payload
    has_url = "url" in payload
    if has_text == has_url:
        raise ValueError("speech command must contain exactly one of text or url")

    require_integer(payload, "volume", 0, 100)
    loop = payload.get("loop")
    if not isinstance(loop, dict):
        raise ValueError("loop must be a JSON object")
    for name in ("duration", "times", "gap"):
        value = loop.get(name, 0)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"loop.{name} must be a non-negative integer")

    if has_text:
        if not isinstance(payload["text"], str) or not payload["text"].strip():
            raise ValueError("text must be a non-empty string")
        require_integer(payload, "speed", 0, 100)
        return "text"

    if not isinstance(payload["url"], str):
        raise ValueError("url must be a string")
    parsed = urllib.parse.urlsplit(payload["url"])
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        raise ValueError("url must be an absolute HTTP or HTTPS URL")
    return "audio_url"


def fetch_audio_url(url: str) -> dict[str, Any]:
    request = urllib.request.Request(url, headers={"Range": "bytes=0-0"})
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            response.read(1)
            return {
                "ok": 200 <= response.status < 400,
                "status": response.status,
                "contentType": response.headers.get("Content-Type", ""),
            }
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        return {"ok": False, "error": str(error)}


def create_server(
    host: str,
    port: int,
    *,
    verify_audio_urls: bool = False,
    log_file: Path | None = None,
) -> AudioPillarServer:
    state = SimulatorState(verify_audio_urls=verify_audio_urls, log_file=log_file)
    return AudioPillarServer((host, port), AudioPillarHandler, state)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Simulate a CosmoEdge-compatible network audio pillar")
    parser.add_argument("--host", default="0.0.0.0", help="listen address (default: 0.0.0.0)")
    parser.add_argument(
        "--port",
        default=80,
        type=int,
        help="listen port (default: 80; CosmoEdge stores an IPv4 address without a port)",
    )
    parser.add_argument("--log-file", type=Path, help="append accepted commands as JSON Lines")
    parser.add_argument(
        "--verify-audio-url",
        action="store_true",
        help="fetch audio URLs before accepting them, like a real network speaker",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    server = create_server(
        arguments.host,
        arguments.port,
        verify_audio_urls=arguments.verify_audio_url,
        log_file=arguments.log_file,
    )
    LOGGER.info(
        "mock audio pillar listening on http://%s:%d (audio URL verification: %s)",
        arguments.host,
        server.server_port,
        arguments.verify_audio_url,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        LOGGER.info("stopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
