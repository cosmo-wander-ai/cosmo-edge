"""Replay preserved raw answers: python3 -m tools.vlm_eval.report --help."""
import argparse
import hashlib
import json
import re
from pathlib import Path

from .parser import JOINT_PROTOCOL, LEVEL_PROTOCOL, PROTOCOLS
from .scoring import score_results


def read_jsonl(path):
    with open(path, encoding="utf-8") as stream:
        return [json.loads(line) for line in stream if line.strip()]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--truth", required=True, help="Frozen truth JSONL")
    parser.add_argument("--results", required=True, help="Raw attempt JSONL")
    parser.add_argument("--output", required=True)
    parser.add_argument("--protocol", choices=PROTOCOLS, default=JOINT_PROTOCOL)
    parser.add_argument("--description-scores", help="JSON containing scorer and scores (id-keyed)")
    parser.add_argument("--window-seconds", type=float)
    parser.add_argument("--conflicts", help="Predeclared audit summary JSON with conflict_ids; adds sensitivity view")
    args = parser.parse_args()
    scores, scorer = None, None
    if args.description_scores and args.protocol == LEVEL_PROTOCOL:
        parser.error("level-only protocol does not accept description scores")
    if args.description_scores:
        bundle = json.loads(Path(args.description_scores).read_text(encoding="utf-8"))
        scorer = bundle["scorer"]
        required = ("model", "weights_sha256", "encoding", "normalization", "truncation", "grade_stripping")
        if not isinstance(scorer, dict) or any(key not in scorer for key in required):
            parser.error("description scorer must record model, weights hash, encoding, normalization, truncation and grade stripping")
        if scorer["model"] != "BAAI/bge-m3":
            parser.error("protocol requires the frozen BAAI/bge-m3 scorer")
        if not re.fullmatch(r"[0-9a-fA-F]{64}", str(scorer["weights_sha256"])):
            parser.error("weights_sha256 must be a SHA-256 digest of the frozen weight manifest")
        if any(not scorer[key] for key in ("encoding", "normalization", "truncation", "grade_stripping")):
            parser.error("scorer configuration cannot be empty")
        if bundle.get("truth_sha256") != sha256(args.truth) or bundle.get("results_sha256") != sha256(args.results):
            parser.error("description scores do not match the supplied truth and raw results hashes")
        scores = bundle["scores"]
    truths, results = read_jsonl(args.truth), read_jsonl(args.results)
    report = score_results(truths, results, scores, args.window_seconds, protocol=args.protocol)
    views = {}
    if any("primary_heldout_eligible" in row for row in truths):
        if any(type(row.get("primary_heldout_eligible")) is not bool for row in truths):
            parser.error("every truth row must have an explicit Boolean independent-subset flag")
        independent = [row for row in truths if row["primary_heldout_eligible"]]
        ids = {row["id"] for row in independent}
        views["primary_independent"] = score_results(independent, [row for row in results if row["id"] in ids], scores, protocol=args.protocol)
    if args.conflicts:
        conflict_ids = json.loads(Path(args.conflicts).read_text())["conflict_ids"]
        if len(set(conflict_ids)) != len(conflict_ids) or set(conflict_ids) - {row["id"] for row in truths}:
            parser.error("conflict ids must be unique members of the frozen truth manifest")
        conflicts = set(conflict_ids)
        subset = [row for row in truths if row["id"] not in conflicts]
        views["sensitivity_excluding_predeclared_conflicts"] = score_results(
            subset, [row for row in results if row["id"] not in conflicts], scores, protocol=args.protocol)
        report["label_conflicts"] = {"ids": conflict_ids, "audit_sha256": sha256(args.conflicts),
                                     "primary_scores_and_denominator_unchanged": True}
    report["views"] = views
    report["provenance"] = dict(protocol=args.protocol, truth_sha256=sha256(args.truth), results_sha256=sha256(args.results),
                                scorer=scorer, description_scores_sha256=sha256(args.description_scores) if args.description_scores else None,
                                parser_sha256=sha256(Path(__file__).with_name("parser.py")),
                                scoring_sha256=sha256(Path(__file__).with_name("scoring.py")))
    Path(args.output).write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
