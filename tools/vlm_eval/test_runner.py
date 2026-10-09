"""Synthetic child-process checks; these do not establish hardware readiness."""
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import subprocess

from tools.vlm_eval.runner import TerminationUnconfirmed, Worker, execution_identity, file_sha256, run, validate_readiness
from tools.vlm_eval.parser import JOINT_PROTOCOL, LEVEL_PROTOCOL

WORKER = '''#!/usr/bin/env python3
import json, os, signal, sys, time
print(json.dumps({'type':'ready','effective_config':{}}), flush=True)
for line in sys.stdin:
 r=json.loads(line)
 if r['id'].startswith('warmup-'): time.sleep(.06)
 if r['id']=='timeout':
  def stopped(sig, frame):
   with open(sys.argv[2]+'.seen','w') as f:
    f.write(open(sys.argv[2]).read())
   sys.exit(0)
  signal.signal(signal.SIGTERM, stopped)
  time.sleep(10)
 if r['id']=='crash': os._exit(7)
 print(json.dumps({'type':'result','id':r['id'],'status':'ok','process_restart_required':r['id']=='restart','raw_output':'{"description":"A scene.","safety_level":"IV"}'}),flush=True)
'''


class RunnerTests(unittest.TestCase):
    def execute(self, ids, source=WORKER, replacement=None, protocol=JOINT_PROTOCOL):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        worker = root / 'worker'
        worker.write_text(source)
        worker.chmod(0o700)
        image = root / 'image'; image.write_bytes(b'synthetic')
        prompt = root / 'prompt'; prompt.write_text('Synthetic test')
        manifest = root / 'manifest'
        manifest.write_text(''.join(json.dumps({'id': key, 'image': str(image)})+'\n' for key in ids))
        output = root / 'results.jsonl'
        args = SimpleNamespace(manifest=str(manifest), prompt=str(prompt), config=str(output),
                               output=str(output), worker=str(worker), timeout=.15,
                               startup_timeout=2, warmup=1, warmup_image=str(image), mode='engineering',
                               protocol=protocol)
        if replacement is None:
            exit_code = run(args)
        else:
            with patch('tools.vlm_eval.runner.Worker', replacement):
                exit_code = run(args)
        self.last_exit_code = exit_code
        return root, [json.loads(line) for line in output.read_text().splitlines()], json.loads(output.with_suffix('.metadata.json').read_text())

    def test_level_fences_count_as_completed_without_changing_raw_output(self):
        raw = '```json\n{"safety_level":"IV"}\n```'
        source = WORKER.replace("'raw_output':'{\"description\":\"A scene.\",\"safety_level\":\"IV\"}'",
                                "'raw_output':" + repr(raw))
        _, rows, meta = self.execute(['success'], source, protocol=LEVEL_PROTOCOL)
        self.assertEqual(rows[0]['raw_output'], raw)
        self.assertEqual(rows[0]['output_protocol'], LEVEL_PROTOCOL)
        self.assertTrue(rows[0]['output_parse']['markdown_recovered'])
        self.assertEqual(meta['protocol_valid_count'], 1)
        self.assertEqual(meta['strict_valid_count'], 0)
        self.assertEqual(meta['markdown_recovered_count'], 1)
        self.assertEqual(meta['effective_throughput'], 1 / meta['measurement_window_seconds'])

    def test_failed_execution_with_fenced_answer_stays_failed(self):
        raw = '```json\n{"safety_level":"IV"}\n```'
        source = WORKER.replace("'raw_output':'{\"description\":\"A scene.\",\"safety_level\":\"IV\"}'",
                                "'raw_output':" + repr(raw))
        _, rows, meta = self.execute(['restart'], source, protocol=LEVEL_PROTOCOL)
        self.assertEqual(rows[0]['raw_output'], raw)
        self.assertEqual(rows[0]['status'], 'inference_error')
        self.assertEqual(meta['protocol_valid_count'], 0)
        self.assertEqual(meta['markdown_recovered_count'], 0)

    def test_timeout_persisted_before_kill_and_recovery_warmup(self):
        root, rows, meta = self.execute(['timeout', 'success'])
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]['status'], 'timeout')
        self.assertEqual(rows[1]['status'], 'ok')
        self.assertEqual(json.loads((root / 'results.jsonl.seen').read_text())['status'], 'timeout')
        events = [event['event'] for event in meta['events']]
        self.assertLess(events.index('failure_persisted'), events.index('worker_exit_confirmed'))
        self.assertEqual(events.count('warmup_complete'), 2)
        self.assertEqual(meta['restarts'], 1)
        self.assertGreater(meta['excluded_startup_and_warmup_seconds'], .05)
        self.assertGreater(meta['included_recovery_setup_seconds'], .05)
        self.assertEqual([s['excluded_from_measurement'] for s in meta['setup_timings']], [True, False])
        self.assertEqual(meta['joint_valid_count'], 1)
        self.assertGreater(meta['memory']['peak_rss_kib'], 0)

    def test_last_failure_recovers_without_extra_quality_row(self):
        for request in ('timeout', 'restart'):
            with self.subTest(request=request):
                _, rows, meta = self.execute([request])
                self.assertEqual(len(rows), 1)
                self.assertEqual(rows[0]['id'], request)
                self.assertEqual(meta['requests'], 1)
                self.assertEqual(meta['restarts'], 1)
                self.assertEqual(self.last_exit_code, 0)
                self.assertEqual(sum(e['event'] == 'warmup_complete' for e in meta['events']), 2)
                self.assertTrue(any(e['event'] == 'tail_recovery_complete' for e in meta['events']))
                self.assertGreater(meta['included_recovery_setup_seconds'], .05)

    def test_tail_recovery_failures_remain_terminal_without_extra_row(self):
        sources = {
            'startup_failed': WORKER.replace("print(json.dumps({'type':'ready'",
                "if os.path.exists(sys.argv[2]+'.seen'): sys.exit(9)\nprint(json.dumps({'type':'ready'"),
            'warmup_failed': WORKER.replace("if r['id'].startswith('warmup-'): time.sleep(.06)",
                "if r['id'].startswith('warmup-') and os.path.exists(sys.argv[2]+'.seen'): os._exit(8)")}
        for reason, source in sources.items():
            with self.subTest(reason=reason):
                _, rows, meta = self.execute(['timeout'], source)
                self.assertEqual([r['status'] for r in rows], ['timeout'])
                self.assertEqual(meta['terminal_reason'], reason)
                self.assertEqual(meta['restarts'], 1)
                self.assertEqual(self.last_exit_code, 2)
                self.assertTrue(any(e['event'] == 'tail_recovery_failed' for e in meta['events']))

    def test_tail_recovery_unknown_constructor_exit_is_preserved(self):
        class UnknownExitWorker:
            starts = 0
            def __init__(self, *args):
                UnknownExitWorker.starts += 1
                if UnknownExitWorker.starts == 2:
                    raise TerminationUnconfirmed(9876, 'constructed startup cleanup failure')
                self.process = SimpleNamespace(pid=1234, returncode=None)
                self.effective_config = {}
                self.peak_rss_kib = self.rss_samples = 1
            def infer(self, request, timeout):
                if not request['id'].startswith('warmup-'):
                    raise TimeoutError('constructed timeout')
                return dict(type='result', id=request['id'], status='empty_output', raw_output='')
            def close(self):
                self.process.returncode = -15
        _, rows, meta = self.execute(['timeout'], replacement=UnknownExitWorker)
        self.assertEqual(len(rows), 1)
        self.assertEqual(meta['restarts'], 1)
        self.assertEqual(meta['terminal_reason'], 'termination_unconfirmed')
        self.assertEqual(meta['unconfirmed_processes'][0]['pid'], 9876)
        self.assertEqual(UnknownExitWorker.starts, 2)
        self.assertEqual(self.last_exit_code, 2)

    def test_recovery_load_warmup_and_termination_remain_in_window(self):
        clock = [0.0]
        class TimedWorker:
            count = 0
            def __init__(self, *args):
                TimedWorker.count += 1
                clock[0] += 2
                self.process = SimpleNamespace(pid=TimedWorker.count, returncode=None)
                self.effective_config = {}
                self.peak_rss_kib = self.rss_samples = 1
            def infer(self, request, timeout):
                if request['id'].startswith('warmup-'):
                    clock[0] += 3
                elif request['id'] == 'timeout':
                    clock[0] += 7
                    raise TimeoutError('constructed timeout')
                else:
                    clock[0] += 11
                return dict(type='result', id=request['id'], status='ok',
                            raw_output='{"description":"A scene.","safety_level":"IV"}')
            def close(self):
                clock[0] += 2
                self.process.returncode = -15
        with patch('tools.vlm_eval.runner.time.monotonic', side_effect=lambda: clock[0]):
            _, rows, meta = self.execute(['timeout', 'success'], replacement=TimedWorker)
        self.assertEqual([row['elapsed_seconds'] for row in rows], [7, 11])
        self.assertEqual(meta['excluded_startup_and_warmup_seconds'], 5)
        self.assertEqual(meta['included_recovery_setup_seconds'], 5)
        # Timeout7 + necessary termination2 + reload2 + warmup3 + request11.
        self.assertEqual(meta['measurement_window_seconds'], 25)
        self.assertEqual(meta['effective_throughput'], 1 / 25)

        clock[0] = 0
        TimedWorker.count = 0
        with patch('tools.vlm_eval.runner.time.monotonic', side_effect=lambda: clock[0]):
            _, rows, meta = self.execute(['timeout'], replacement=TimedWorker)
        self.assertEqual(len(rows), 1)
        # Final timeout7 + necessary termination2 + recovery reload2 + warmup3.
        self.assertEqual(meta['measurement_window_seconds'], 14)
        self.assertEqual(meta['included_recovery_setup_seconds'], 5)
        self.assertEqual(meta['restarts'], 1)
        self.assertIsNone(meta['terminal_reason'])

    def test_warmup_model_output_failures_do_not_block_execution(self):
        for status, raw in [('ok', '```json\n{}\n```'), ('empty_output', '')]:
            with self.subTest(status=status):
                injection = (" if r['id'].startswith('warmup-'):\n"
                             "  print(json.dumps({'type':'result','id':r['id'],'status':" + repr(status) +
                             ", 'raw_output':" + repr(raw) + "}),flush=True)\n  continue\n")
                source = WORKER.replace(' print(json.dumps(', injection + ' print(json.dumps(')
                _, rows, meta = self.execute(['timeout', 'success'], source)
                self.assertEqual([row['status'] for row in rows], ['timeout', 'ok'])
                self.assertEqual(meta['restarts'], 1)
                self.assertIsNone(meta['terminal_reason'])
                warmups = [e for e in meta['events'] if e['event'] == 'warmup_response']
                self.assertEqual(len(warmups), 2)
                self.assertTrue(all(not e['joint_valid'] for e in warmups))

    def test_empty_output_continues_without_restart(self):
        source = WORKER.replace(" print(json.dumps(",
                                " if r['id']=='empty':\n"
                                "  print(json.dumps({'type':'result','id':r['id'],'status':'empty_output','raw_output':''}),flush=True)\n"
                                "  continue\n print(json.dumps(")
        _, rows, meta = self.execute(['empty', 'success'], source)
        self.assertEqual([row['status'] for row in rows], ['empty_output', 'ok'])
        self.assertEqual(meta['restarts'], 0)
        self.assertEqual(len(meta['cold_start_seconds']), 1)
        self.assertEqual(meta['joint_valid_count'], 1)
        self.assertTrue(all(row['request_attempted'] for row in rows))

    def test_empty_output_restart_flag_still_requires_recovery(self):
        source = WORKER.replace("'status':'ok','process_restart_required'",
                                "'status':'empty_output' if r['id']=='restart' else 'ok','process_restart_required'")
        _, rows, meta = self.execute(['restart', 'success'], source)
        self.assertEqual([row['status'] for row in rows], ['inference_error', 'ok'])
        self.assertEqual(meta['restarts'], 1)

    def test_crash_keeps_denominator_and_recovers(self):
        _, rows, meta = self.execute(['crash', 'success'])
        self.assertEqual([row['status'] for row in rows], ['inference_error', 'ok'])
        self.assertEqual(meta['requests'], 2)
        self.assertEqual(meta['restarts'], 1)

    def test_startup_failure_stops_remaining_requests(self):
        source = '#!/usr/bin/env python3\nimport sys\nsys.exit(9)\n'
        _, rows, meta = self.execute(['a', 'b', 'c'], source)
        self.assertEqual([row['status'] for row in rows], ['startup_error', 'not_run_recovery_failed', 'not_run_recovery_failed'])
        self.assertTrue(all(not row['request_attempted'] for row in rows))
        self.assertEqual(meta['terminal_reason'], 'startup_failed')
        self.assertEqual(self.last_exit_code, 2)

    def test_recovery_warmup_failure_stops_future_requests(self):
        source = WORKER.replace("if r['id'].startswith('warmup-'): time.sleep(.06)",
                                "if r['id'].startswith('warmup-') and os.path.exists(sys.argv[2]+'.seen'): os._exit(8)")
        _, rows, meta = self.execute(['timeout', 'b', 'c'], source)
        self.assertEqual([row['status'] for row in rows], ['timeout', 'warmup_error', 'not_run_recovery_failed'])
        self.assertEqual([row['request_attempted'] for row in rows], [True, False, False])
        self.assertEqual(meta['terminal_reason'], 'warmup_failed')
        self.assertEqual(self.last_exit_code, 2)

    def test_unconfirmed_termination_preserves_denominator_and_never_restarts(self):
        class FakeWorker:
            created = 0
            def __init__(self, *args):
                FakeWorker.created += 1
                self.process = SimpleNamespace(pid=1234, returncode=None)
                self.effective_config = {}
            def infer(self, request, timeout):
                if request['id'].startswith('warmup-'):
                    return dict(type='result', id=request['id'], status='ok', raw_output='{"description":"A scene","safety_level":"IV"}')
                raise TimeoutError('constructed request timeout')
            def close(self):
                raise subprocess.TimeoutExpired('constructed kill', 5)
        _, rows, meta = self.execute(['a', 'b', 'c'], replacement=FakeWorker)
        self.assertEqual([row['status'] for row in rows], ['timeout', 'termination_unconfirmed', 'termination_unconfirmed'])
        self.assertEqual(FakeWorker.created, 1)
        self.assertEqual(meta['unconfirmed_processes'][0]['pid'], 1234)
        self.assertEqual(meta['terminal_reason'], 'termination_unconfirmed')
        self.assertEqual(self.last_exit_code, 2)

    def test_constructor_cleanup_timeout_preserves_unconfirmed_pid(self):
        # No OS process is launched: exercise the actual constructor's exception path.
        fake_process = SimpleNamespace(pid=5678)
        fake_thread = SimpleNamespace(start=lambda: None)
        with patch('tools.vlm_eval.runner.subprocess.Popen', return_value=fake_process) as launch, \
             patch('tools.vlm_eval.runner.threading.Thread', return_value=fake_thread), \
             patch('tools.vlm_eval.runner.Worker.receive', side_effect=TimeoutError('ready timeout')), \
             patch('tools.vlm_eval.runner.Worker.close', side_effect=subprocess.TimeoutExpired('kill', 5)):
            _, rows, meta = self.execute(['a', 'b', 'c'])
        self.assertEqual(launch.call_count, 1)
        self.assertEqual(len(rows), 3)
        self.assertEqual([row['status'] for row in rows], ['startup_error', 'termination_unconfirmed', 'termination_unconfirmed'])
        self.assertTrue(all(not row['request_attempted'] for row in rows))
        self.assertEqual(meta['terminal_reason'], 'termination_unconfirmed')
        self.assertEqual(meta['unconfirmed_processes'][0]['pid'], 5678)
        self.assertEqual(self.last_exit_code, 2)

    def test_worker_restart_flag_prevents_success_and_forces_recovery(self):
        _, rows, meta = self.execute(['restart', 'success'])
        self.assertEqual([row['status'] for row in rows], ['inference_error', 'ok'])
        self.assertTrue(rows[0]['process_restart_required'])
        self.assertTrue(rows[0]['raw_output'])
        self.assertEqual(meta['restarts'], 1)
        self.assertEqual(meta['joint_valid_count'], 1)
        self.assertEqual(sum(event['event'] == 'warmup_complete' for event in meta['events']), 2)

    def test_warmup_restart_flag_stops_without_consuming_requests(self):
        source = WORKER.replace("r['id']=='restart'", "r['id'].startswith('warmup-')")
        _, rows, meta = self.execute(['a', 'b', 'c'], source)
        self.assertEqual([row['status'] for row in rows],
                         ['warmup_error', 'not_run_recovery_failed', 'not_run_recovery_failed'])
        self.assertTrue(all(not row['request_attempted'] for row in rows))
        self.assertEqual(meta['joint_valid_count'], 0)
        self.assertEqual(meta['terminal_reason'], 'warmup_failed')
        self.assertEqual(self.last_exit_code, 2)

    def test_ready_backend_must_match_frozen_backend(self):
        for expected, actual in (('rkllm', None), ('rkllm', 'sophon'), ('sophon', 'rkllm')):
            with self.subTest(expected=expected, actual=actual), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                worker = root / 'worker'
                worker.write_text('#!/usr/bin/env python3\nimport json\nprint(json.dumps(' +
                                  repr({'type': 'ready', 'effective_config': {} if actual is None else {'backend': actual}}) +
                                  '), flush=True)\n')
                worker.chmod(0o700)
                with (root / 'log').open('w') as log:
                    with self.assertRaisesRegex(RuntimeError, 'ready backend'):
                        Worker([str(worker)], log, 2, expected)

    def test_formal_requires_readiness_before_starting(self):
        with self.assertRaisesRegex(ValueError, 'readiness'):
            run(SimpleNamespace(mode='formal', readiness=None))

    def formal_fixture(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        for name in ('manifest', 'prompt', 'model', 'tokenizer', 'warmup', 'worker'):
            (root / name).write_text(name)
        (root / 'worker').chmod(0o700)
        config = root / 'config.json'
        config.write_text(json.dumps({'model_path': str(root / 'model'),
                                      'tokenizer_path': str(root / 'tokenizer'),
                                      'max_new_tokens': 256, 'context_length': 2048}))
        args = SimpleNamespace(mode='formal', readiness=str(root / 'readiness.json'),
                               manifest=str(root / 'manifest'), prompt=str(root / 'prompt'),
                               config=str(config), worker=str(root / 'worker'),
                               warmup_image=str(root / 'warmup'), warmup=1,
                               timeout=30.0, startup_timeout=60.0)
        record = {gate: True for gate in (
            'input_reference_verified', 'device_input_verified', 'artifact_verified',
            'small_sample_verified', 'label_audit_complete', 'scorer_verified',
            'parameters_frozen', 'timeout_recovery_verified')}
        record.update({field + '_sha256': file_sha256(getattr(args, field))
                       for field in ('manifest', 'prompt', 'config')})
        record['execution_identity'] = execution_identity(args)
        Path(args.readiness).write_text(json.dumps(record))
        return root, args

    def test_formal_binds_supervisor_parameters(self):
        for field, changed in (('timeout', 2.0), ('startup_timeout', 3.0), ('warmup', 2),
                               ('protocol', LEVEL_PROTOCOL)):
            with self.subTest(field=field):
                _, args = self.formal_fixture()
                self.assertIsNotNone(validate_readiness(args))
                setattr(args, field, changed)
                with self.assertRaisesRegex(ValueError, 'execution_identity'):
                    validate_readiness(args)

    def test_formal_detects_replaced_inputs_at_same_path(self):
        for field in ('model', 'tokenizer', 'worker', 'warmup'):
            with self.subTest(field=field):
                root, args = self.formal_fixture()
                (root / field).write_text('replacement content')
                with self.assertRaisesRegex(ValueError, 'execution_identity'):
                    validate_readiness(args)

    def rk_formal_fixture(self, directory_path=False):
        root, args = self.formal_fixture()
        (root / 'model.rkllm').write_bytes(b'language weights and embedded tokenizer')
        (root / 'vision.rknn').write_bytes(b'vision weights')
        config = {'backend': 'rkllm', 'model_path': str(root if directory_path else root / 'model.rkllm'),
                  'max_new_tokens': 256, 'context_length': 2048,
                  'expected_input_tokens': 682, 'vision_normalization': 'embedded_mean127.5_std127.5'}
        Path(args.config).write_text(json.dumps(config))
        record = json.loads(Path(args.readiness).read_text())
        record['config_sha256'] = file_sha256(args.config)
        record['execution_identity'] = execution_identity(args)
        Path(args.readiness).write_text(json.dumps(record))
        return root, args

    def test_rk_formal_binds_both_artifacts_without_external_tokenizer(self):
        for directory_path in (False, True):
            for filename in ('model.rkllm', 'vision.rknn'):
                with self.subTest(directory=directory_path, artifact=filename):
                    root, args = self.rk_formal_fixture(directory_path)
                    identity = validate_readiness(args)
                    self.assertEqual(set(identity['artifact_sha256']), {'model_path', 'vision_path'})
                    (root / filename).write_bytes(b'changed artifact')
                    with self.assertRaisesRegex(ValueError, 'execution_identity'):
                        validate_readiness(args)

    def test_rk_formal_missing_vision_rejected(self):
        root, args = self.rk_formal_fixture()
        (root / 'vision.rknn').unlink()
        with self.assertRaisesRegex(ValueError, 'vision_path'):
            validate_readiness(args)

    def test_rk_formal_still_requires_every_admission_gate(self):
        gates = ('input_reference_verified', 'device_input_verified', 'artifact_verified',
                 'small_sample_verified', 'label_audit_complete', 'scorer_verified',
                 'parameters_frozen', 'timeout_recovery_verified')
        for gate in gates:
            with self.subTest(gate=gate):
                _, args = self.rk_formal_fixture()
                record = json.loads(Path(args.readiness).read_text())
                record.pop(gate)
                Path(args.readiness).write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, gate):
                    validate_readiness(args)

    def test_standalone_config_requires_files_not_model_directories(self):
        root, args = self.formal_fixture()
        config = json.loads(Path(args.config).read_text())
        config['tokenizer_path'] = str(root)
        Path(args.config).write_text(json.dumps(config))
        with self.assertRaisesRegex(ValueError, 'tokenizer_path'):
            execution_identity(args)


if __name__ == '__main__':
    unittest.main()
