#!/usr/bin/env python3
"""Real JPEG/socket/cache/multi-question acceptance for a private dynamic worker.

This controls only its own child worker. The caller owns admission, service
maintenance and restoration. No scene accuracy or engine-integration claim.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "questions", "image", "compiler_deps", "output"):
        p.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    p.add_argument("--manifest-sha256", required=True)
    a = p.parse_args()
    a.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    base = a.manifest.parent
    sys.path.insert(0, str(base))
    from PIL import Image
    from visual_protocol import PROTOCOL, PROFILE
    from laya_shadow_worker import read_exact
    from bmrt_bridge import sha256
    if sha256(a.manifest) != a.manifest_sha256:
        raise ValueError("manifest mismatch")
    manifest = json.loads(a.manifest.read_text())
    report = {"status": "RUNNING", "scope": "private real worker only; not Cosmo entrypoints or accuracy",
              "manifest_sha256": a.manifest_sha256, "verifier_sha256": sha256(__file__), "checks": {}}
    path = a.output / "report.json"
    worker = None
    log = (a.output / "worker.log").open("w")

    def save():
        path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")

    def call(request, image):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as c:
            c.settimeout(16)
            c.connect(str(a.output / "run/worker.sock"))
            data = json.dumps(request, ensure_ascii=False).encode()
            c.sendall(struct.pack("!I", len(data)) + data + image)
            size = struct.unpack("!I", read_exact(c, 4))[0]
            return json.loads(read_exact(c, size))

    def start():
        nonlocal worker
        worker = subprocess.Popen([sys.executable, str(base / "dynamic_worker.py"),
            "--manifest", str(a.manifest), "--manifest-sha256", a.manifest_sha256,
            "--cache", str(a.output / "cache"), "--runtime", str(a.output / "run"),
            "--logs", str(a.output / "logs")], stdout=log, stderr=log)
        until = time.monotonic() + 90
        while time.monotonic() < until:
            if worker.poll() is not None:
                raise RuntimeError("worker exited before readiness")
            try:
                state = json.loads((a.output / "run/status.json").read_text())
                if (state["state"] == "ready" and state["pid"] == worker.pid and
                        state["manifest_sha256"] == a.manifest_sha256):
                    return state
            except (OSError, json.JSONDecodeError):
                pass
            time.sleep(.1)
        raise TimeoutError("worker readiness")

    def stop():
        if worker is not None and worker.poll() is None:
            worker.terminate()
            try:
                worker.wait(10)
            except subprocess.TimeoutExpired:
                worker.kill()
                worker.wait(5)
                raise RuntimeError("worker required SIGKILL")

    try:
        selected = {}
        for file in sorted(a.questions.glob("*.json")):
            pack = json.loads(file.read_text())
            if pack["question_id"] in ("helmet-choice-en", "helmet-zh", "flame-en") and not pack["text_state"]:
                selected[pack["question_id"]] = pack
        if len(selected) != 3:
            raise ValueError("three reference question types required")
        compiler = [sys.executable, str(base / "compile_questions.py"),
            "--tokenizer", str(base / manifest["tokenizer"]["path"]),
            "--tokenizer-sha256", manifest["tokenizer"]["sha256"],
            "--config", str(base / manifest["cpu_directory"] / "rl_agent_config.json"),
            "--config-sha256", manifest["cpu_files"]["rl_agent_config.json"],
            "--cache", str(a.output / "cache")]
        env = dict(os.environ)
        env["PYTHONPATH"] = str(a.compiler_deps) + os.pathsep + env.get("PYTHONPATH", "")
        began = time.monotonic()
        completed = subprocess.run(compiler, input=json.dumps({"questions": [
            {"question": x["question"]} for x in selected.values()]}), text=True,
            capture_output=True, env=env, timeout=60)
        (a.output / "compiler.stdout").write_text(completed.stdout)
        (a.output / "compiler.stderr").write_text(completed.stderr)
        if completed.returncode:
            raise RuntimeError("compiler failed")
        compiled = json.loads(completed.stdout)
        refs = compiled["questions"]
        assert compiled["status"] == "prepared"
        assert {x["compiled_sha256"] for x in refs} == {x["compiled_sha256"] for x in selected.values()}
        report["checks"]["compile_exact_references"] = True
        report["compile_seconds"] = time.monotonic() - began
        report["first_runtime"] = start()
        save()
        image = Image.open(a.image).convert("RGB")
        image.thumbnail((1024, 1024))
        encoded = io.BytesIO()
        image.save(encoded, format="JPEG", quality=90)
        jpeg = encoded.getvalue()
        w, h = image.size
        items = [{"item_id": "question-" + str(i), **r} for i, r in enumerate(refs)]

        def request(roi, data, size, epoch="epoch-1"):
            return {"protocol": PROTOCOL, "profile": PROFILE, "manifest_sha256": a.manifest_sha256,
                "request_id": "request-" + roi, "task_id": "parity-task", "run_epoch": epoch,
                "frame_id": "frame-1", "roi_id": roi, "config_revision": "revision-1", "items": items,
                "deadline_monotonic_ms": int(time.monotonic() * 1000) + 15000,
                "image_encoding": "jpeg", "image_size": len(data), "image_width": size[0], "image_height": size[1]}

        first = call(request("A", jpeg, (w, h)), jpeg)
        assert first["status"] == "completed" and len(first["items"]) == 3
        assert [x["item_id"] for x in first["items"]] == [x["item_id"] for x in items]
        assert all(not x["business_qualified"] for x in first["items"])
        report["checks"]["three_typed_questions_one_roi"] = True
        report["first_response"] = first
        print("real JPEG with three questions: PASS", flush=True)
        crop = image.crop((0, 0, max(1, w // 2), h))
        encoded = io.BytesIO()
        crop.save(encoded, format="JPEG", quality=90)
        cropped = encoded.getvalue()
        second = call(request("B", cropped, crop.size, "epoch-2"), cropped)
        assert second["status"] == "completed" and second["roi_id"] == "B" and second["run_epoch"] == "epoch-2"
        assert second["image_sha256"] == hashlib.sha256(cropped).hexdigest() != first["image_sha256"]
        report["checks"]["independent_roi_and_epoch"] = True
        report["second_response"] = second
        bad = request("missing", jpeg, (w, h))
        bad["items"] = [dict(x) for x in items]
        bad["items"][0]["compiled_sha256"] = "0" * 64
        missing = call(bad, jpeg)
        assert missing["status"] == "partial" and missing["items"][0]["status"] == "unknown"
        assert all(x["status"] == "completed" for x in missing["items"][1:])
        report["checks"]["missing_question_isolated"] = True
        expired = request("expired", jpeg, (w, h))
        expired["deadline_monotonic_ms"] = int(time.monotonic() * 1000) - 1
        late = call(expired, jpeg)
        assert late["reason"] == "deadline_exceeded" and all("probabilities" not in x for x in late["items"])
        report["checks"]["expired_request_rejected"] = True
        report["failure_responses"] = [missing, late]
        report["worker_rss_kib"] = int(next(line.split()[1] for line in Path(
            "/proc/%s/status" % worker.pid).read_text().splitlines() if line.startswith("VmRSS:")))
        old_pid = worker.pid
        stop()
        report["second_runtime"] = start()
        replay = call(request("A", jpeg, (w, h), "epoch-after-restart"), jpeg)
        assert replay["status"] == "completed" and worker.pid != old_pid
        import numpy as np
        assert all(np.allclose(x["probabilities"], y["probabilities"], atol=1e-6, rtol=1e-6)
                   for x, y in zip(first["items"], replay["items"]))
        report["checks"]["cache_survives_worker_restart"] = True
        report["status"] = "PASS"
        print("ROI, deadline, missing question and restart: PASS", flush=True)
    except BaseException as error:
        report.update(status="FAIL", error=type(error).__name__ + ": " + str(error))
        raise
    finally:
        try:
            stop()
            report["own_worker_stopped"] = True
        finally:
            log.close()
            save()


if __name__ == "__main__":
    main()
