"""Paired source-group bootstrap of frozen first-attempt platform results."""
import argparse
import json
import random
from collections import defaultdict
from pathlib import Path

from .report import read_jsonl, sha256
from .scoring import score_results, percentile

METRICS = ('accuracy', 'macro_f1', 'joint_valid_rate', 'anomaly_recall', 'normal_false_positive_rate')


def compare(truths, results_a, results_b, iterations=2000, seed=20260917):
    if type(iterations) is not int or iterations < 1:
        raise ValueError('iterations must be positive')
    # Validate original manifests and all attempts before selecting primary IDs.
    score_results(truths, results_a)
    score_results(truths, results_b)
    if any(type(row.get('primary_heldout_eligible')) is not bool for row in truths):
        raise ValueError('every truth needs an explicit primary independent Boolean')
    primary = [row for row in truths if row['primary_heldout_eligible']]
    if not primary:
        raise ValueError('primary independent subset is empty')
    groups = defaultdict(list)
    for truth in primary:
        group = truth.get('group_id')
        if not isinstance(group, str) or not group:
            raise ValueError('every primary sample needs a nonempty source group_id')
        groups[group].append(truth)
    ids = {row['id'] for row in primary}
    first_a = {row['id']: row for row in results_a if row.get('attempt', 1) == 1 and row['id'] in ids}
    first_b = {row['id']: row for row in results_b if row.get('attempt', 1) == 1 and row['id'] in ids}
    # Missing records are paired as missing, never removed via an inner join.
    report_a = score_results(primary, list(first_a.values()), _grouped=True)
    report_b = score_results(primary, list(first_b.values()), _grouped=True)
    draws = {key: [] for key in METRICS}
    absent_class_draws = {level: 0 for level in ('I','II','III','IV')}
    names = sorted(groups)
    rng = random.Random(seed)
    for _ in range(iterations):
        sampled, a, b = [], [], []
        for occurrence, group in enumerate(rng.choices(names, k=len(names))):
            for truth in groups[group]:
                key = str(occurrence) + ':' + truth['id']
                sampled.append(dict(truth, id=key))
                if truth['id'] in first_a:
                    a.append(dict(first_a[truth['id']], id=key))
                if truth['id'] in first_b:
                    b.append(dict(first_b[truth['id']], id=key))
        supported = {row['safety_level'] for row in sampled}
        for level in absent_class_draws:
            absent_class_draws[level] += level not in supported
        sa = score_results(sampled, a, _grouped=True)
        sb = score_results(sampled, b, _grouped=True)
        for key in METRICS:
            if sa[key] is not None and sb[key] is not None:
                draws[key].append(sb[key] - sa[key])
    metrics = {}
    for key in METRICS:
        valid = draws[key]
        # One source group cannot establish between-group uncertainty.
        identifiable = len(names) > 1 and bool(valid)
        metrics[key] = dict(platform_a=report_a[key], platform_b=report_b[key],
                            difference_b_minus_a=report_b[key]-report_a[key] if report_a[key] is not None and report_b[key] is not None else None,
                            percentile_95_interval=[percentile(valid,.025), percentile(valid,.975)] if identifiable else None,
                            defined_bootstrap_replicates=len(valid), undefined_bootstrap_replicates=iterations-len(valid))
    return dict(version='paired-source-group-bootstrap-v1', primary_sample_count=len(primary), source_group_count=len(names),
                first_attempt_records={'a':len(first_a),'b':len(first_b)}, missing_first_attempts={'a':len(ids)-len(first_a),'b':len(ids)-len(first_b)},
                iterations=iterations, seed=seed, metrics=metrics,
                absent_bootstrap_replicates_by_class=absent_class_draws,
                resampling='Sample source group_id clusters with replacement; retain all rows per cluster; identical draws and frozen IDs across platforms.',
                interpretation='Descriptive percentile intervals, not a business acceptance threshold or proof of chip causality. Unsupported metrics are N/A. Sparse classes may disappear in draws; macro-F1 follows the scorer support rule and undefined replicates are disclosed. Repeated frames are not independent observations.',
                excluded='Retries, calibration and same-source excluded samples; no inner-join deletion of failed or missing primary rows.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('truth','results-a','results-b','output'):
        parser.add_argument('--'+field, required=True)
    parser.add_argument('--seed', type=int, default=20260917)
    parser.add_argument('--iterations', type=int, default=2000)
    args = parser.parse_args()
    result = compare(read_jsonl(args.truth), read_jsonl(args.results_a), read_jsonl(args.results_b), args.iterations, args.seed)
    result['provenance'] = {field+'_sha256':sha256(getattr(args,field)) for field in ('truth','results_a','results_b')}
    result['provenance']['implementation_sha256'] = sha256(__file__)
    with Path(args.output).open('x') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
