#!/usr/bin/env python3
"""Contract tests for the network audio-pillar simulator."""

from __future__ import annotations

import importlib.util
import json
import sys
import threading
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


sys.dont_write_bytecode = True
REPOSITORY = Path(__file__).resolve().parents[1]
SERVER_SOURCE = REPOSITORY / "tools" / "mock_audio_pillar_server.py"


def load_server_module():
    spec = importlib.util.spec_from_file_location("mock_audio_pillar_server", SERVER_SOURCE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {SERVER_SOURCE}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def request_json(url: str, *, method: str = "GET", body=None):
    data = None if body is None else json.dumps(body).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=2) as response:
        return response.status, json.loads(response.read().decode("utf-8"))


class AudioFileHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        body = b"synthetic-mp3-data"
        self.send_response(206 if self.headers.get("Range") else 200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args) -> None:
        return


class AudioPillarContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server_module = load_server_module()
        cls.server = cls.server_module.create_server("127.0.0.1", 0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def setUp(self) -> None:
        request_json(f"{self.base_url}/__mock__/events", method="DELETE")

    def test_alive_matches_platform_contract(self) -> None:
        status, response = request_json(
            f"{self.base_url}/v1/check_alive", method="GET"
        )
        self.assertEqual(status, 200)
        self.assertEqual(response["code"], 200)
        _, mock_status = request_json(f"{self.base_url}/__mock__/status")
        self.assertEqual(mock_status["aliveChecks"], 1)
        self.assertIsNotNone(mock_status["lastAliveAt"])

    def test_text_speech_is_validated_and_recorded(self) -> None:
        payload = {
            "text": "发现人员闯入",
            "vcn": "xiaoyan",
            "speed": 45,
            "volume": 70,
            "rdn": "0",
            "rcn": "0",
            "reg": 0,
            "sync": False,
            "queue": False,
            "prompt": False,
            "loop": {"duration": 60, "times": 2, "gap": 3},
        }
        _, response = request_json(
            f"{self.base_url}/v1/speech", method="POST", body=payload
        )
        self.assertEqual(response["code"], 200)

        _, events = request_json(f"{self.base_url}/__mock__/events")
        self.assertEqual(events["count"], 1)
        self.assertEqual(events["events"][0]["kind"], "text")
        self.assertEqual(events["events"][0]["payload"], payload)

    def test_audio_url_speech_is_validated_and_recorded(self) -> None:
        payload = {
            "url": "http://192.168.0.72/audio/alarm.mp3",
            "sync": False,
            "queue": False,
            "volume": 50,
            "prompt": False,
            "loop": {"duration": 30, "times": 1, "gap": 1},
        }
        request_json(f"{self.base_url}/v1/speech", method="POST", body=payload)

        _, events = request_json(f"{self.base_url}/__mock__/events")
        self.assertEqual(events["events"][0]["kind"], "audio_url")
        self.assertEqual(events["events"][0]["payload"]["url"], payload["url"])

    def test_audio_url_can_be_fetched_from_the_platform(self) -> None:
        audio_server = ThreadingHTTPServer(("127.0.0.1", 0), AudioFileHandler)
        audio_thread = threading.Thread(target=audio_server.serve_forever, daemon=True)
        audio_thread.start()
        pillar_server = self.server_module.create_server(
            "127.0.0.1", 0, verify_audio_urls=True
        )
        pillar_thread = threading.Thread(target=pillar_server.serve_forever, daemon=True)
        pillar_thread.start()
        try:
            payload = {
                "url": f"http://127.0.0.1:{audio_server.server_port}/alarm.mp3",
                "volume": 50,
                "loop": {"duration": 30, "times": 1, "gap": 1},
            }
            _, response = request_json(
                f"http://127.0.0.1:{pillar_server.server_port}/v1/speech",
                method="POST",
                body=payload,
            )
            self.assertEqual(response["code"], 200)
            _, events = request_json(
                f"http://127.0.0.1:{pillar_server.server_port}/__mock__/events"
            )
            self.assertTrue(events["events"][0]["audioFetch"]["ok"])
            self.assertEqual(
                events["events"][0]["audioFetch"]["contentType"], "audio/mpeg"
            )
        finally:
            pillar_server.shutdown()
            pillar_server.server_close()
            pillar_thread.join(timeout=2)
            audio_server.shutdown()
            audio_server.server_close()
            audio_thread.join(timeout=2)

    def test_invalid_speech_is_rejected_without_recording(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as raised:
            request_json(
                f"{self.base_url}/v1/speech",
                method="POST",
                body={"volume": 101, "loop": {}},
            )
        self.assertEqual(raised.exception.code, 400)

        _, events = request_json(f"{self.base_url}/__mock__/events")
        self.assertEqual(events["count"], 0)

    def test_next_command_can_simulate_device_failure(self) -> None:
        request_json(f"{self.base_url}/__mock__/fail-next", method="POST", body={})
        status, response = request_json(
            f"{self.base_url}/v1/speech",
            method="POST",
            body={"text": "test", "volume": 50, "speed": 50, "loop": {}},
        )
        self.assertEqual(status, 200)
        self.assertEqual(response["code"], 500)


if __name__ == "__main__":
    unittest.main()
