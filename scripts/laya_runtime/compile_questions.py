#!/usr/bin/env python3
"""Compile one configuration batch in an isolated, short-lived process.

Input is bounded JSON on stdin. Output contains immutable question references;
the caller activates its configuration only after this whole operation succeeds.
The tokenizer is released on exit and is never loaded by the inference worker.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

from visual_protocol import MAX_JSON, parse_json


def validate_batch(request):
    from question_compiler import normalize_question
    if not isinstance(request, dict) or set(request) != {"questions"}:
        raise ValueError("invalid_compile_request")
    rows = request["questions"]
    if not isinstance(rows, list) or not 1 <= len(rows) <= 32:
        raise ValueError("compile_batch_requires_1_to_32_questions")
    identities = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) - {"question", "text_state"} or "question" not in row:
            raise ValueError("invalid_question_record")
        normalized, _, _ = normalize_question(row["question"])
        key = (normalized["id"], normalized["version"])
        if key in identities:
            raise ValueError("duplicate_question_revision")
        identities.add(key)
    return rows


def reference(pack):
    return {**{k: pack[k] for k in ("question_id", "question_version", "compiled_sha256",
                                    "qtype", "temperature", "bindings")},
            "ordered_options": pack["option_labels"]}


def compile_batch(request, compiler, cache):
    rows = validate_batch(request)
    packs = [compiler.compile(row["question"], row.get("text_state", "")) for row in rows]
    # No partial batch is published to the caller. Leftover cache files after an
    # I/O failure are harmless immutable orphans, not activated configurations.
    for pack in packs:
        cache.put(pack)
    return {"status": "prepared", "business_qualified": False,
            "questions": [reference(pack) for pack in packs]}


def atomic_json(path, value):
    payload = json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
    if len(payload) > MAX_JSON:
        raise ValueError("compile_response_too_large")
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temporary = tempfile.mkstemp(prefix=".prepared-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def prepare_cached(data, binding, cache, factory):
    """Warm re-open validates every pack without allocating the tokenizer."""
    from question_compiler import canonical_bytes, normalize_question
    request = parse_json(data)
    rows = validate_batch(request)
    input_sha = hashlib.sha256(data).hexdigest()
    key = hashlib.sha256(canonical_bytes({"input_sha256": input_sha, "binding": binding})).hexdigest()
    path = cache.directory / "prepared" / (key + ".json")
    if path.exists():
        with path.open("rb") as stream:
            encoded = stream.read(MAX_JSON + 1)
        if len(encoded) > MAX_JSON:
            raise ValueError("prepared_cache_corrupted")
        cached = parse_json(encoded)
        if cached.get("input_sha256") != input_sha or cached.get("binding") != binding or (
                cached.get("status") != "prepared" or cached.get("business_qualified") is not False or
                len(cached.get("questions", [])) != len(rows)):
            raise ValueError("prepared_cache_corrupted")
        for row, ref in zip(rows, cached["questions"]):
            pack = cache.get(ref["compiled_sha256"])
            question, labels, _ = normalize_question(row["question"])
            if (reference(pack) != ref or pack["question"] != question or pack["option_labels"] != labels or
                    pack["text_state"] != row.get("text_state", "") or pack["bindings"] != binding["assets"]):
                raise ValueError("prepared_cache_corrupted")
        return {**cached, "cache_hit": True}
    result = compile_batch(request, factory(), cache)
    if any(ref["bindings"] != binding["assets"] for ref in result["questions"]):
        raise ValueError("compiler_asset_mismatch")
    result.update(input_sha256=input_sha, binding=binding, cache_hit=False)
    atomic_json(path, result)
    return result


def manifest_assets(path, expected):
    from image_frontend import sha256
    if sha256(path) != expected:
        raise ValueError("compiler_manifest_identity_mismatch")
    manifest = parse_json(path.read_bytes())
    if manifest.get("schema") != 2 or manifest.get("qualification") != "business-acceptance-pending":
        raise ValueError("unsupported_compiler_manifest")
    for name in ("compile_questions.py", "question_compiler.py", "image_frontend.py", "visual_protocol.py",
                 "laya_shadow_worker.py"):
        spec = manifest["implementation"][name]
        source = (path.parent / spec["path"]).resolve()
        if source != Path(__file__).with_name(name).resolve() or sha256(source) != spec["sha256"]:
            raise ValueError("compiler_implementation_identity_mismatch")
    spec = manifest["tokenizer"]
    return ((path.parent / spec["path"]).resolve(), spec["sha256"],
            (path.parent / manifest["cpu_directory"] / "rl_agent_config.json").resolve(),
            manifest["cpu_files"]["rl_agent_config.json"])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("tokenizer", "config", "manifest"):
        p.add_argument("--" + name, type=Path)
    p.add_argument("--cache", required=True, type=Path)
    p.add_argument("--tokenizer-sha256")
    p.add_argument("--config-sha256")
    p.add_argument("--manifest-sha256")
    a = p.parse_args()
    os.umask(0o077)
    data = sys.stdin.buffer.read(MAX_JSON + 1)
    if len(data) > MAX_JSON:
        raise ValueError("compile_request_too_large")
    a.cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    # A second config editor must retry; it must not allocate another tokenizer.
    with (a.cache / ".compile.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        from question_compiler import FrozenTokenizer, QuestionCompiler, QuestionCache
        from image_frontend import sha256
        if a.manifest:
            a.tokenizer, a.tokenizer_sha256, a.config, a.config_sha256 = manifest_assets(
                a.manifest, a.manifest_sha256)
        if not all((a.tokenizer, a.config, a.tokenizer_sha256, a.config_sha256)):
            raise ValueError("compiler_assets_required")
        if sha256(a.config) != a.config_sha256 or sha256(a.tokenizer) != a.tokenizer_sha256:
            raise ValueError("configuration_asset_mismatch")
        binding = {"manifest_sha256": a.manifest_sha256 or "",
                   "assets": {"tokenizer_sha256": a.tokenizer_sha256,
                              "model_config_sha256": a.config_sha256},
                   "implementation": {name: sha256(Path(__file__).with_name(name)) for name in
                                      ("compile_questions.py", "question_compiler.py", "image_frontend.py",
                                       "visual_protocol.py", "laya_shadow_worker.py")}}
        def factory():
            return QuestionCompiler(FrozenTokenizer(a.tokenizer, a.tokenizer_sha256),
                                    parse_json(a.config.read_bytes()), a.config_sha256)
        result = prepare_cached(data, binding, QuestionCache(a.cache), factory)
    print(json.dumps(result, ensure_ascii=False, allow_nan=False))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print(json.dumps({"status": "rejected", "reason": str(error)}))
        raise SystemExit(1)
