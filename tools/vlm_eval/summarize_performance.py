"""Summarize all three frozen serial performance replays without inventing data."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path

from .parser import JOINT_PROTOCOL, PROTOCOLS, parse_output
from .report import read_jsonl, sha256
from .scoring import percentile


def recovery_timing_verified(meta):
    restarts = meta.get('restarts')
    starts = meta.get('cold_start_seconds')
    if type(restarts) is not int or restarts < 0 or not isinstance(starts, list):
        return False
    if len(starts) != restarts + 1:
        return False
    if restarts == 0:
        return True
    timings = meta.get('setup_timings')
    if not isinstance(timings, list) or len(timings) != restarts + 1:
        return False
    for index, timing in enumerate(timings):
        initial = index == 0
        if not isinstance(timing, dict) or timing.get('initial') is not initial or timing.get('excluded_from_measurement') is not initial:
            return False
        seconds = timing.get('seconds')
        if type(seconds) not in (int, float) or not math.isfinite(seconds) or seconds < 0:
            return False
    for field, expected in (('excluded_startup_and_warmup_seconds', timings[0]['seconds']),
                            ('included_recovery_setup_seconds', sum(t['seconds'] for t in timings[1:]))):
        actual = meta.get(field)
        if type(actual) not in (int, float) or not math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-12):
            return False
    return True


def summarize(manifest, rounds, manifest_hash):
    if len(rounds) != 3:
        raise ValueError('exactly three performance rounds are required')
    ids = [row['id'] for row in manifest]
    if len(ids) != len(set(ids)):
        raise ValueError('duplicate frozen performance id')
    reports = []
    reference = None
    for index, (results, meta) in enumerate(rounds, 1):
        if meta.get('manifest_sha256') != manifest_hash or meta.get('requests') != len(ids):
            raise ValueError('round manifest identity/count mismatch')
        identity = {key: meta.get(key) for key in ('prompt_sha256', 'config_sha256', 'execution_identity',
                                                  'cache_policy', 'concurrency', 'warmup_count_per_start', 'mode')}
        protocol = meta.get('output_protocol', JOINT_PROTOCOL)
        if protocol not in PROTOCOLS:
            raise ValueError('unsupported output protocol')
        identity['output_protocol'] = protocol
        if not identity['prompt_sha256'] or not identity['config_sha256'] or not identity['execution_identity']:
            raise ValueError('round execution identity is absent')
        if reference is None:
            reference = identity
        elif reference != identity:
            raise ValueError('round execution conditions differ')
        first, seen, retries = {}, set(), 0
        for row in results:
            if row.get('output_protocol', protocol) != protocol:
                raise ValueError('result output protocol differs from metadata')
            key, attempt = row['id'], row.get('attempt', 1)
            if key not in set(ids) or type(attempt) is not int or attempt < 1 or (key, attempt) in seen:
                raise ValueError('invalid result id or duplicate/invalid attempt')
            seen.add((key, attempt))
            if attempt == 1:
                first[key] = row
            else:
                retries += 1
        failures, durations, all_durations, valid, attempted = Counter(), [], [], 0, 0
        for key in ids:
            row = first.get(key)
            parsed = parse_output(row.get('raw_output'), protocol) if row else None
            if row and (row.get('request_attempted') is True or row.get('phase') == 'prepare'):
                attempted += 1
                elapsed = row.get('elapsed_seconds')
                if type(elapsed) in (int, float) and math.isfinite(elapsed) and elapsed >= 0:
                    all_durations.append(elapsed)
            if row is None:
                failures['missing'] += 1
            elif row.get('status') != 'ok':
                failures[row.get('status', 'missing_status')] += 1
            elif not parsed['joint_valid']:
                failures[parsed['error']] += 1
            else:
                valid += 1
                elapsed = row.get('elapsed_seconds')
                if type(elapsed) in (int, float) and math.isfinite(elapsed) and elapsed >= 0:
                    durations.append(elapsed)
        if valid != meta.get('joint_valid_count'):
            raise ValueError('metadata valid count differs from raw results')
        window = meta.get('measurement_window_seconds')
        complete = (len(first) == len(ids) and attempted == len(ids) and
                    meta.get('exit_code') == 0 and not meta.get('terminal_reason') and
                    not meta.get('unconfirmed_processes'))
        window_verified = recovery_timing_verified(meta)
        if complete and not window_verified:
            raise ValueError('recovery setup timing is unverified; cannot publish a complete-round rate')
        if type(window) not in (int, float) or not math.isfinite(window) or window < 0 or (complete and window == 0):
            raise ValueError('measurement window must be finite and positive for a complete round')
        observed_throughput = valid / window if window > 0 else None
        reported = meta.get('effective_throughput')
        if ((observed_throughput is None and reported is not None) or
                (observed_throughput is not None and
                 (type(reported) not in (int, float) or
                  not math.isclose(reported, observed_throughput, rel_tol=1e-9, abs_tol=1e-12)))):
            raise ValueError('metadata throughput does not match valid count/window')
        reports.append(dict(round=index, status='complete' if complete else 'incomplete',
                            unexecuted=len(ids)-attempted, terminal_reason=meta.get('terminal_reason'),
                            frozen_requests=len(ids), first_attempts=len(first), retries=retries,
                            joint_valid=valid, failed=len(ids)-valid, failures=dict(failures),
                            window_seconds=window, window_verified=window_verified,
                            effective_throughput=observed_throughput if complete else None,
                            valid_latency_seconds=dict(p50=percentile(durations,.5), p95=percentile(durations,.95),
                                                       measured=len(durations), missing=valid-len(durations)),
                            all_attempt_latency_seconds=dict(p50=percentile(all_durations,.5),
                                                             p95=percentile(all_durations,.95),
                                                             attempted=attempted, measured=len(all_durations),
                                                             missing_timing=attempted-len(all_durations)),
                            cold_start_seconds=meta.get('cold_start_seconds'), restarts=meta.get('restarts'),
                            excluded_startup_and_warmup_seconds=meta.get('excluded_startup_and_warmup_seconds'),
                            memory=meta.get('memory', {'status':'unavailable'})))
    throughputs = [row['effective_throughput'] for row in reports if row['status'] == 'complete']
    memory = [row['memory'].get('peak_rss_kib') for row in reports if row['memory'].get('status') == 'sampled']
    memory = [value for value in memory if type(value) in (int,float) and math.isfinite(value)]
    return dict(version='serial-performance-three-rounds-v2', conditions=reference,
                frozen_unique_request_count=len(ids), rounds=reports,
                throughput_across_rounds=dict(complete_rounds=len(throughputs),
                                              mean=sum(throughputs)/3 if len(throughputs)==3 else None,
                                              minimum=min(throughputs) if len(throughputs)==3 else None,
                                              maximum=max(throughputs) if len(throughputs)==3 else None,
                                              spread=max(throughputs)-min(throughputs) if len(throughputs)==3 else None),
                sampled_worker_peak_rss_kib=dict(minimum=min(memory) if memory else None,
                                                 maximum=max(memory) if memory else None,
                                                 spread=max(memory)-min(memory) if memory else None,
                                                 sampled_rounds=len(memory)),
                limitation='Repeated requests do not add independent samples; memory scope is copied per round; no device or accelerator memory inference.')


def summarize_memory(rows):
    samples = [row for row in rows if row.get('event') == 'sample']
    errors = [row for row in rows if row.get('event') == 'error']
    starts = [row for row in rows if row.get('event') == 'start']
    stops = [row for row in rows if row.get('event') == 'stop']
    if len(starts) != 1 or len(stops) > 1:
        raise ValueError('memory trace requires one start and at most one stop')
    for sample in samples:
        if sample.get('unit') != 'runtime-reported MB':
            raise ValueError('unrecognized memory unit')
        for field in ('total', 'used', 'available', 'monotonic_seconds', 'unix_seconds'):
            value = sample.get(field)
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise ValueError('invalid memory sample value')
    heaps = {}
    for sample in samples:
        for heap in sample.get('heaps', []):
            heaps.setdefault(str(heap['index']), []).append(heap)
    return dict(status='complete' if stops and samples and not errors else 'incomplete_or_errors',
                samples=len(samples), errors=len(errors), start=starts[0], stop=stops[0] if stops else None,
                unit='runtime-reported MB',
                scope='whole-device heaps across sampler lifetime; not process-exclusive and not additive with RSS',
                peak_used=max((row['used'] for row in samples), default=None),
                minimum_available=min((row['available'] for row in samples), default=None),
                heaps={key: dict(peak_used=max(row['used'] for row in values),
                                 minimum_available=min(row['available'] for row in values),
                                 minimum_total=min(row['total'] for row in values),
                                 maximum_total=max(row['total'] for row in values)) for key, values in heaps.items()},
                first_sample_monotonic=samples[0]['monotonic_seconds'] if samples else None,
                last_sample_monotonic=samples[-1]['monotonic_seconds'] if samples else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--round', action='append', required=True, dest='rounds', help='Results JSONL; adjacent .metadata.json required')
    parser.add_argument('--output', required=True)
    parser.add_argument('--memory', action='append', help='Optional three accelerator-memory JSONL files in round order')
    args = parser.parse_args()
    pairs, provenance = [], []
    for path in args.rounds:
        metadata = Path(path).with_suffix('.metadata.json')
        pairs.append((read_jsonl(path), json.loads(metadata.read_text())))
        provenance.append(dict(results_sha256=sha256(path), metadata_sha256=sha256(metadata)))
    report = summarize(read_jsonl(args.manifest), pairs, sha256(args.manifest))
    if args.memory:
        if len(args.memory) != 3:
            parser.error('provide exactly three memory traces in round order')
        report['accelerator_memory_rounds'] = [dict(round=index, sha256=sha256(path), **summarize_memory(read_jsonl(path))) for index, path in enumerate(args.memory, 1)]
    report['provenance'] = dict(manifest_sha256=sha256(args.manifest), rounds=provenance,
                                implementation_sha256=sha256(__file__))
    with Path(args.output).open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
