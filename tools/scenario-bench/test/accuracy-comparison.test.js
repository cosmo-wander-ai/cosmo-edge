import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { summarizeAccuracyRun } from '../src/accuracy/evaluator.js';
import {
  compareAccuracySummaries,
  normalizeAccuracyComparisonSummary,
  writeAccuracyComparison,
} from '../src/accuracy/comparison.js';
import {
  HASH_A,
  HASH_B,
  HASH_C,
  makeAccuracyFixture,
  makeMeasuredCase,
} from './helpers/accuracy-v4-fixture.js';

function summary({
  concurrency = 1,
  wallDurationMs = 10_000,
  positive = 'PASS',
  negative = 'PASS',
  softwareVersion = 'V1',
  algorithmId = '15',
  taskConfigHashes = undefined,
} = {}) {
  const { execution, suite, admission } = makeAccuracyFixture({
    concurrency,
    suiteId: 'measurement-v4',
    suiteDisplayName: 'Measurement',
    algorithmId,
    taskConfigHashes,
  });
  const result = summarizeAccuracyRun({
    suite,
    targetChip: 'bm1688',
    execution,
    admission,
    cases: [
      makeMeasuredCase({
        id: 'positive', expectation: { minEvents: 1 }, status: positive, totalMs: 3_000,
      }),
      makeMeasuredCase({
        id: 'negative', expectation: { maxEvents: 0 }, status: negative, totalMs: 3_000,
      }),
    ],
  });
  return {
    ...result,
    runId: `run-c${concurrency}`,
    wallDurationMs,
    activeDurationMs: wallDurationMs,
    admissionMs: 100,
    device: { model: 'BM1688', softwareVersion, hardwareVersion: 'A1', fingerprint: HASH_A },
    toolIdentitySha256: concurrency === 1 ? HASH_A : HASH_B,
    repository: { commit: HASH_A, tree: HASH_B, dirty: false },
  };
}

test('compare reports speed, metric deltas, and every case status change', () => {
  const reference = summary();
  const c2 = summary({
    concurrency: 2, wallDurationMs: 6_000, positive: 'FAIL', softwareVersion: 'V2',
  });
  const c4 = summary({
    concurrency: 4, wallDurationMs: 4_000, negative: 'ERROR', softwareVersion: 'V3',
  });
  const comparison = compareAccuracySummaries(reference, [c2, c4]);

  assert.equal(comparison.protocolVersion, 4);
  assert.equal(comparison.candidates.length, 2);
  assert.equal(comparison.candidates[0].dimensions.concurrency, 2);
  assert.equal(comparison.candidates[0].timing.speedup, 10_000 / 6_000);
  assert.equal(comparison.candidates[0].timing.savedWallMs, 4_000);
  assert.equal(comparison.candidates[0].metrics.micro.positiveHitRateDeltaPoints, -100);
  assert.equal(comparison.candidates[0].tasks[0].positiveHitRateDeltaPoints, -100);
  assert.deepEqual(comparison.candidates[0].transitions.passToNonPass, ['positive']);
  assert.deepEqual(comparison.candidates[1].transitions.allChanges.map((item) => item.id), ['negative']);
  assert.equal(comparison.candidates[1].health.errorCases, 1);
});

test('compare allows algorithm, config, concurrency, software, source, and chip changes as dimensions', () => {
  const candidate = summary({
    concurrency: 2,
    softwareVersion: 'V2',
    algorithmId: '99',
    taskConfigHashes: [HASH_B],
  });
  candidate.suite.targetChip = 'cv186x';
  candidate.suite.sourceMode = 'rtsp-deterministic';
  candidate.execution.measurementConfigSha256 = HASH_B;
  const comparison = compareAccuracySummaries(summary(), [candidate]);
  const changed = comparison.candidates[0].contextChanges.map((item) => item.field);

  for (const field of [
    'concurrency', 'sourceMode', 'targetChip', 'measurementConfigSha256',
    'softwareVersion', 'algorithms',
  ]) {
    assert.ok(changed.includes(field));
  }
  assert.equal(comparison.candidates[0].tasks[0].referenceAlgorithmId, '15');
  assert.equal(comparison.candidates[0].tasks[0].algorithmId, '99');
});

test('compare requires the same selected video samples', () => {
  const candidate = summary({ concurrency: 2 });
  candidate.execution.selection.sha256 = HASH_B;
  candidate.admission.selection.sha256 = HASH_B;
  assert.throws(
    () => compareAccuracySummaries(summary(), [candidate]),
    /sample mismatch.*selectionSha256/i,
  );

  const differentCase = summary({ concurrency: 2 });
  differentCase.cases[0].id = 'different-positive';
  assert.throws(
    () => compareAccuracySummaries(summary(), [differentCase]),
    /sample mismatch.*caseIds/i,
  );
});

test('comparison accepts only current v4 summaries', () => {
  for (const protocolVersion of [2, 3]) {
    const legacy = { ...summary(), schemaVersion: protocolVersion, protocolVersion };
    assert.throws(() => normalizeAccuracyComparisonSummary(legacy), /only v4/i);
    assert.throws(() => compareAccuracySummaries(summary(), [legacy]), /only v4/i);
  }
});

test('comparison writer creates deterministic private JSON, HTML, and integrity', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-comparison-'));
  try {
    const comparison = compareAccuracySummaries(summary(), [summary({
      concurrency: 2, wallDurationMs: 6_000, positive: 'FAIL',
    })]);
    const sources = [
      { role: 'reference', sha256: HASH_A },
      { role: 'candidate-1', sha256: HASH_B },
    ];
    assert.throws(
      () => writeAccuracyComparison(path.join(root, 'missing-sources'), comparison),
      /source summaries are required/i,
    );
    const first = writeAccuracyComparison(path.join(root, 'first'), comparison, sources);
    const second = writeAccuracyComparison(path.join(root, 'second'), comparison, sources);
    assert.equal(Object.hasOwn(comparison, 'sources'), false);
    for (const name of ['comparison.json', 'report.html', 'integrity.json']) {
      assert.equal(fs.existsSync(path.join(first.outputDir, name)), true);
      assert.equal(
        fs.readFileSync(path.join(first.outputDir, name), 'utf8'),
        fs.readFileSync(path.join(second.outputDir, name), 'utf8'),
      );
    }
    assert.equal(fs.statSync(first.outputDir).mode & 0o777, 0o700);
    assert.equal(fs.statSync(first.jsonPath).mode & 0o777, 0o600);
    const html = fs.readFileSync(first.htmlPath, 'utf8');
    assert.match(html, /1\.67/);
    assert.match(html, /Trial work/);
    assert.match(html, /Saved wall/);
    assert.match(html, /完整 case 差异见 comparison\.json/);
    assert.equal(fs.existsSync(path.join(first.outputDir, 'report.md')), false);
    assert.equal(Object.hasOwn(first, 'markdownPath'), false);
    const integrity = JSON.parse(fs.readFileSync(first.integrityPath));
    assert.deepEqual(integrity.artifacts.map((item) => item.path), [
      'comparison.json', 'report.html',
    ]);
    assert.throws(() => writeAccuracyComparison(first.outputDir, comparison, sources), /new and empty/i);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
