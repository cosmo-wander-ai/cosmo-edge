import assert from 'node:assert/strict';
import test from 'node:test';

import {
  applyThresholdValue,
  assertThresholdDiagnosticDocument,
  summarizeThresholdDiagnostic,
} from '../src/accuracy/threshold.js';

test('threshold override changes only explicitly allowlisted parameters and leaves input immutable', () => {
  const original = {
    params: [
      { key: 'aiParam.detector.confidence', value: '0.5' },
      { key: 'aiParam.classifier.confidence', value: '0.7' },
    ],
    areas: [],
  };
  const changed = applyThresholdValue(original, ['aiParam.detector.confidence'], 0.3);
  assert.equal(changed.params[0].value, '0.3');
  assert.equal(changed.params[1].value, '0.7');
  assert.equal(original.params[0].value, '0.5');
  assert.throws(
    () => applyThresholdValue(original, ['aiParam.*.confidence'], 0.3),
    /wildcard/i,
  );
  assert.throws(
    () => applyThresholdValue(original, ['aiParam.missing.confidence'], 0.3),
    /not found/i,
  );
});

test('threshold diagnostic labels only unanimous three-trial values as stable', () => {
  const summary = summarizeThresholdDiagnostic([
    { threshold: 0.5, trials: [{ status: 'FAIL' }, { status: 'FAIL' }, { status: 'FAIL' }] },
    { threshold: 0.4, trials: [{ status: 'PASS' }, { status: 'FAIL' }, { status: 'PASS' }] },
    { threshold: 0.3, trials: [{ status: 'PASS' }, { status: 'PASS' }, { status: 'PASS' }] },
  ]);
  assert.deepEqual(summary.points.map((item) => item.status), ['STABLE_FAIL', 'FLAKY', 'STABLE_PASS']);
  assert.deepEqual(summary.stablePassValues, [0.3]);
  assert.equal(summary.appliedAutomatically, false);
});

test('threshold diagnostic uses an independent protocol-v4 document', () => {
  const hash = 'a'.repeat(64);
  const document = {
    schemaVersion: 1,
    protocolVersion: 4,
    evidenceKind: 'cosmo-accuracy-threshold-diagnostic',
    evidenceStatus: 'DIAGNOSTIC',
    executionOutcome: 'COMPLETED',
    executionReasons: [],
    appliedAutomatically: false,
    runId: 'run',
    sourceRunSha256: hash,
    caseId: 'case',
    parameterKeys: ['aiParam.detector.confidence'],
    execution: { profile: 'full', concurrency: 1 },
    admission: { status: 'PASS' },
    suite: { id: 'suite' },
    points: [{ threshold: 0.4, status: 'FLAKY', trialStatuses: ['PASS', 'FAIL', 'PASS'] }],
    stablePassValues: [],
    stableFailValues: [],
    flakyValues: [0.4],
    device: { model: 'BM1688' },
    toolIdentitySha256: hash,
    repository: { commit: null, tree: null, dirty: true },
  };
  assert.equal(assertThresholdDiagnosticDocument(document), document);
  assert.throws(
    () => assertThresholdDiagnosticDocument({ ...document, protocolVersion: 3 }),
    /protocolVersion must equal 4/i,
  );
  assert.throws(
    () => assertThresholdDiagnosticDocument({ ...document, unexpectedField: true }),
    /unsupported property unexpectedField/i,
  );
});
