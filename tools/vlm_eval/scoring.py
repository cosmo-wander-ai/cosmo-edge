"""Failure-preserving metrics over a frozen truth manifest."""
import math
from collections import Counter

from .parser import JOINT_PROTOCOL, LEVEL_PROTOCOL, PROTOCOLS, LEVELS, parse_output


def ratio(numerator, denominator):
    return numerator / denominator if denominator else None


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    position = (len(values) - 1) * fraction
    low = int(position)
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (position - low)


def score_results(truths, results, description_scores=None, window_seconds=None, _grouped=False, *, protocol=JOINT_PROTOCOL):
    """Use attempt 1 only. Later attempts never replace missing/failed first results.

    description_scores maps id to {status: 'ok', cosine: number} or a scoring
    failure status. The report CLI additionally requires frozen scorer identity.
    No embedding backend is installed or silently substituted by this function.
    """
    if protocol not in PROTOCOLS:
        raise ValueError("unsupported output protocol")
    level_only = protocol == LEVEL_PROTOCOL
    truths = list(truths)
    results = list(results)
    truth_by_id = {}
    for truth in truths:
        key = truth["id"]
        if key in truth_by_id or truth["safety_level"] not in LEVELS:
            raise ValueError("duplicate truth id or invalid truth level")
        truth_by_id[key] = truth
    first, retries = {}, []
    seen = set()
    for row in results:
        if "output_protocol" in row and row["output_protocol"] != protocol:
            raise ValueError("result output protocol does not match scoring protocol")
        key, attempt = row["id"], row.get("attempt", 1)
        if key not in truth_by_id:
            raise ValueError("result id outside frozen manifest")
        if type(attempt) is not int or attempt < 1 or (key, attempt) in seen:
            raise ValueError("invalid or duplicate attempt")
        seen.add((key, attempt))
        if attempt == 1:
            first[key] = row
        else:
            retries.append(row)
    matrix = {level: dict.fromkeys((*LEVELS, "INVALID", "MISSING"), 0) for level in LEVELS}
    failures, scene_counts, group_counts = Counter(), {}, Counter()
    records, latencies, cosine_values = [], [], []
    description_valid = joint_valid = scoring_failures = 0
    strict_valid = strict_correct = markdown_recovered = 0
    for truth in truths:
        key, target = truth["id"], truth["safety_level"]
        row = first.get(key)
        parsed = parse_output(row.get("raw_output") if row else None, protocol=protocol)
        status = row.get("status", "missing_status") if row else "missing"
        accepted = status == "ok"
        pred = parsed["safety_level"] if accepted and parsed["safety_level_valid"] else ("MISSING" if row is None else "INVALID")
        matrix[target][pred] += 1
        failure = None if accepted and parsed["joint_valid"] else (parsed["error"] if accepted else status)
        if failure:
            failures[failure] += 1
        valid_description = not level_only and accepted and parsed["description_valid"]
        valid_joint = accepted and parsed["joint_valid"]
        description_valid += valid_description
        joint_valid += valid_joint
        valid_strict = accepted and parsed["strict_valid"]
        recovered = accepted and parsed["markdown_recovered"]
        strict_valid += valid_strict
        strict_correct += valid_strict and pred == target
        markdown_recovered += recovered
        elapsed = row.get("elapsed_seconds") if row else None
        if valid_joint and isinstance(elapsed, (int, float)) and not isinstance(elapsed, bool) and math.isfinite(elapsed) and elapsed >= 0:
            latencies.append(elapsed)
        cosine = None
        if valid_description:
            score = (description_scores or {}).get(key, {})
            candidate = score.get("cosine")
            if score.get("status") == "ok" and isinstance(candidate, (int, float)) and not isinstance(candidate, bool) and math.isfinite(candidate) and -1 <= candidate <= 1:
                cosine = candidate
                cosine_values.append(cosine)
            else:
                scoring_failures += 1
        scene = truth.get("scene", "UNSPECIFIED")
        scene_counts.setdefault(scene, dict.fromkeys(LEVELS, 0))[target] += 1
        group_counts[truth.get("group_id", "UNSPECIFIED")] += 1
        records.append(dict(id=key, truth=target, prediction=pred, failure=failure,
                            description_valid=valid_description, joint_valid=valid_joint,
                            description_cosine=cosine, protocol_valid=accepted and parsed["protocol_valid"],
                            strict_valid=valid_strict, markdown_fence=accepted and parsed["markdown_fence"],
                            markdown_recovered=recovered))
    per_level = {}
    for level in LEVELS:
        support = sum(matrix[level].values())
        tp = matrix[level][level]
        fp = sum(matrix[other][level] for other in LEVELS if other != level)
        fn = support - tp
        per_level[level] = dict(support=support, true_positive=tp, false_positive=fp,
                                false_negative=fn, recall=ratio(tp, support),
                                f1=ratio(2 * tp, 2 * tp + fp + fn) if support else None)
    total = len(truths)
    anomalous = sum(per_level[level]["support"] for level in LEVELS[:3])
    normal = per_level["IV"]["support"]
    f1s = [entry["f1"] for entry in per_level.values() if entry["support"]]
    if window_seconds is not None and (not math.isfinite(window_seconds) or window_seconds <= 0):
        raise ValueError("window_seconds must be finite and positive")
    report = dict(version="inspecsafe-scoring-level-v2" if level_only else "inspecsafe-scoring-v1",
                protocol=protocol, total=total, first_attempt_count=len(first),
                retry_count=len(retries), retries=retries, confusion_matrix=matrix,
                accuracy=ratio(sum(matrix[x][x] for x in LEVELS), total),
                macro_f1=ratio(sum(f1s), len(f1s)), per_level=per_level,
                anomaly_recall=ratio(sum(matrix[t][p] for t in LEVELS[:3] for p in LEVELS[:3]), anomalous),
                normal_false_positive_rate=ratio(sum(matrix["IV"][p] for p in LEVELS[:3]), normal),
                normal_correct_rate=ratio(matrix["IV"]["IV"], normal),
                normal_incomplete_rate=ratio(sum(record["truth"] == "IV" and not record["joint_valid"] for record in records), normal),
                risk_underestimate_count=sum(matrix[t][p] for i, t in enumerate(LEVELS) for p in LEVELS[i + 1:]),
                level_i_to_iv_count=matrix["I"]["IV"], joint_valid_count=joint_valid,
                joint_valid_rate=ratio(joint_valid, total),
                protocol_valid_count=joint_valid, protocol_valid_rate=ratio(joint_valid, total),
                strict_valid_count=strict_valid, strict_valid_rate=ratio(strict_valid, total),
                strict_correct_count=strict_correct, strict_accuracy=ratio(strict_correct, total),
                markdown_recovered_count=markdown_recovered, failures=dict(failures),
                failure_rates={key: ratio(value, total) for key, value in failures.items()},
                description=dict(valid_count=description_valid, coverage=ratio(description_valid, total),
                                 scored_count=len(cosine_values), scoring_failure_count=scoring_failures,
                                 status="scoring_pending_or_failed" if scoring_failures else "complete",
                                 valid_mean=None if scoring_failures else ratio(sum(cosine_values), description_valid),
                                 all_sample_score=None if scoring_failures else ratio(sum(cosine_values), total)),
                latency_seconds=dict(p50=percentile(latencies, .5), p95=percentile(latencies, .95),
                                     measured_valid_count=len(latencies), missing_valid_timing_count=joint_valid-len(latencies),
                                     failed_count=total-joint_valid),
                effective_throughput=ratio(joint_valid, window_seconds) if window_seconds else None,
                scene_level_counts=scene_counts, group_counts=dict(group_counts), records=records)

    if level_only:
        report["description"].update(status="not_applicable", coverage=None,
                                     valid_mean=None, all_sample_score=None)

    if not _grouped:
        report["by_scene"] = {}
        for scene in scene_counts:
            subset = [truth for truth in truths if truth.get("scene", "UNSPECIFIED") == scene]
            ids = {truth["id"] for truth in subset}
            report["by_scene"][scene] = score_results(
                subset, [row for row in results if row["id"] in ids],
                description_scores, _grouped=True, protocol=protocol)
    return report
