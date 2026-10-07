#!/usr/bin/env python3
"""Compile one configuration batch in an isolated, short-lived process.

Input is bounded JSON on stdin. Output contains immutable question references;
the caller activates its configuration only after this whole operation succeeds.
The tokenizer is released on exit and is never loaded by the inference worker.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import sys

from visual_protocol import MAX_JSON, parse_json


def compile_batch(request, compiler, cache):
    if not isinstance(request, dict) or set(request) != {"questions"}:
        raise ValueError("invalid_compile_request")
    rows = request["questions"]
    if not isinstance(rows, list) or not 1 <= len(rows) <= 32:
        raise ValueError("compile_batch_requires_1_to_32_questions")
    packs, identities = [], set()
    for row in rows:
        if not isinstance(row, dict) or set(row) - {"question", "text_state"} or "question" not in row:
            raise ValueError("invalid_question_record")
        pack = compiler.compile(row["question"], row.get("text_state", ""))
        key = (pack["question_id"], pack["question_version"])
        if key in identities:
            raise ValueError("duplicate_question_revision")
        identities.add(key)
        packs.append(pack)
    # No partial batch is published to the caller. Leftover cache files after an
    # I/O failure are harmless immutable orphans, not activated configurations.
    for pack in packs:
        cache.put(pack)
    return {"status": "prepared", "business_qualified": False,
            "questions": [{k: pack[k] for k in ("question_id", "question_version", "compiled_sha256")}
                          for pack in packs]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("tokenizer", "config", "cache"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--tokenizer-sha256", required=True)
    p.add_argument("--config-sha256", required=True)
    a = p.parse_args()
    os.umask(0o077)
    data = sys.stdin.buffer.read(MAX_JSON + 1)
    if len(data) > MAX_JSON:
        raise ValueError("compile_request_too_large")
    request = parse_json(data)
    a.cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    # A second config editor must retry; it must not allocate another tokenizer.
    with (a.cache / ".compile.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        from question_compiler import FrozenTokenizer, QuestionCompiler, QuestionCache
        from image_frontend import sha256
        if sha256(a.config) != a.config_sha256:
            raise ValueError("configuration_asset_mismatch")
        compiler = QuestionCompiler(FrozenTokenizer(a.tokenizer, a.tokenizer_sha256),
                                    parse_json(a.config.read_bytes()), a.config_sha256)
        result = compile_batch(request, compiler, QuestionCache(a.cache))
    print(json.dumps(result, ensure_ascii=False, allow_nan=False))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print(json.dumps({"status": "rejected", "reason": str(error)}))
        raise SystemExit(1)
