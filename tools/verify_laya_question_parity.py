#!/usr/bin/env python3
"""Check dynamic inputs against original upstream AST and its HF tokenizer.

Uses an existing admitted reference environment. No downloads, model loading,
device writes or changes to source/tokenizer assets. This proves sequence
preparation only, not visual accuracy or compiled-model numerical parity.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import sys
import threading
from typing import Dict, List, Optional, Union


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cases():
    yield {"id": "helmet-choice-en", "version": 1, "type": "choice",
           "instructions": "Is the person wearing a safety helmet?",
           "criteria": {"wearing": "The person wears a safety helmet",
                        "not_wearing": "The person does not wear a safety helmet",
                        "uncertain": "Cannot determine from this image"}}
    for key, prompt in (("helmet-zh", "图中人员是否未佩戴安全帽？"),
                        ("workwear-zh", "图中人员是否穿着反光衣？"),
                        ("smoke-zh", "区域内是否存在可见烟雾？"),
                        ("flame-en", "Is there visible fire?"),
                        ("blocked-zh", "通道是否被物品堵塞？"),
                        ("phone-zh", "图中人员是否正在手持手机？"),
                        ("negative-zh", "区域内是否没有车辆？"),
                        ("short-en", "Fire?")):
        yield {"id": key, "version": 1, "type": "noul", "instructions": prompt}
    yield {"id": "boolean-labels-zh", "version": 1, "type": "noul",
           "instructions": "图中是否有人？", "labels": {"true": "是", "false": "否"},
           "criteria": {"true": "画面中存在人员", "false": "画面中没有人员"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--common-sha256", required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError("use a fresh evidence directory")
    common = args.source / "laya/common.py"
    if digest(common) != args.common_sha256:
        raise ValueError("upstream source identity mismatch")
    from transformers import PreTrainedTokenizerFast
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/laya_runtime"))
    from question_compiler import FrozenTokenizer, QuestionCompiler, QuestionCache
    tokenizer_path = args.model / "tokenizer/tokenizer.json"
    tokenizer = FrozenTokenizer(tokenizer_path, digest(tokenizer_path))
    reference = PreTrainedTokenizerFast.from_pretrained(str(tokenizer_path.parent), local_files_only=True)
    config_path = args.model / "rl_agent_config.json"
    config = json.loads(config_path.read_text())
    compiler = QuestionCompiler(tokenizer, config, digest(config_path))
    names = {"encode_text", "serialize_state", "render_criterion", "_resolve_noul_labels",
             "render_options", "build_sequence"}
    tree = ast.parse(common.read_text())
    nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    if {node.name for node in nodes} != names:
        raise ValueError("upstream sequence API mismatch")
    scope = dict(globals(), _TOKENIZE_LOCK=threading.RLock(),
                 _DEFAULT_NOUL_LABELS={"false": "false", "true": "true"})
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(common), "exec"), scope)
    args.output.mkdir(parents=True, mode=0o700)
    cache = QuestionCache(args.output / "questions")
    records = []
    for question in cases():
        for text_state in ("", "区域：作业区A"):
            pack = compiler.compile(question, text_state)
            q = {"t": question["type"], "ins": question["instructions"],
                 "crit": question.get("criteria")}
            if "labels" in question:
                q["labels"] = question["labels"]
            matches = []
            for length in (256, config["max_len"]):
                ids, markers, pos = scope["build_sequence"](
                    reference, text_state, q, max_len=length, head_max_len=config["head_max_len"], image_slots=130)
                expected = {"input_ids": ids, "marker_positions": markers,
                            "image_position": pos, "valid_length": len(ids)}
                if expected != pack["sequence"]:
                    raise AssertionError("original sequence mismatch: " + question["id"])
                matches.append(length)
            key = cache.put(pack)
            if cache.get(key) != pack:
                raise AssertionError("cache round trip changed question")
            records.append({"id": question["id"], "has_context": bool(text_state),
                            "compiled_sha256": key, "valid_length": pack["sequence"]["valid_length"],
                            "qtype": pack["qtype"], "exact_at_lengths": matches})
    long_question = {"id": "overflow", "version": 1, "type": "noul",
                     "instructions": "inspect the image carefully " * 180}
    try:
        compiler.compile(long_question)
    except ValueError as error:
        if "budget_exceeded" not in str(error):
            raise
        overflow = "REJECTED_WITHOUT_TRUNCATION"
    else:
        raise AssertionError("long question silently accepted")
    report = {"status": "PASS", "scope": "original tokenizer and sequence parity only",
              "common_sha256": digest(common), "tokenizer_sha256": tokenizer.sha256,
              "model_config_sha256": digest(config_path), "cases": records,
              "overflow": overflow, "device_numerical_parity": "UNVERIFIED",
              "business_accuracy": "UNVERIFIED"}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"status": "PASS", "cases": len(records), "min_valid_length": min(x["valid_length"] for x in records),
                      "max_valid_length": max(x["valid_length"] for x in records), "output": str(args.output)}))


if __name__ == "__main__":
    main()
