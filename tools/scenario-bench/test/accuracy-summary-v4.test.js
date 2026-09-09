import assert from 'node:assert/strict';
import test from 'node:test';

import { summarizeAccuracyRun } from '../src/accuracy/evaluator.js';
import { accuracyExitCode, hasMeasuredFailures } from '../src/accuracy/outcome.js';
import {
  assertAccuracySummary,
  validateAccuracySummary,
} from '../src/accuracy/summary.js';
import {
  makeAccuracyFixture,
  makeMeasuredCase,
} from './helpers/accuracy-v4-fixture.js';

function measuredSummary({ cases = measuredCases(), executionBlockReasons = [] } = {}) {
  const fixture = makeAccuracyFixture({ caseCount: cases.length });
  return summarizeAccuracyRun({
    suite: fixture.suite,
    cases,
    targetChip: 'bm1688',
    execution: fixture.execution,
    admission: fixture.admission,
    executionBlockReasons,
  });
}

function measuredCases() {
  return [
    makeMeasuredCase({ id: 'positive-pass', expectation: { minEvents: 1 }, status: 'PASS' }),
    makeMeasuredCase({ id: 'positive-fail', expectation: { minEvents: 1 }, status: 'FAIL' }),
    makeMeasuredCase({ id: 'negative-pass', expectation: { maxEvents: 0 }, status: 'PASS' }),
    makeMeasuredCase({ id: 'negative-fail', expectation: { maxEvents: 0 }, status: 'FAIL' }),
  ];
}

test('complete measurement keeps PASS and FAIL as data and exits zero', () => {
  const summary = measuredSummary();

  assert.equal(summary.schemaVersion, 4);
  assert.equal(summary.protocolVersion, 4);
  assert.equal(summary.executionOutcome, 'COMPLETED');
  assert.equal(accuracyExitCode(summary), 0);
  assert.equal(hasMeasuredFailures(summary), true);
  assert.equal(summary.metrics.micro.positiveHitRate, 0.5);
  assert.equal(summary.metrics.micro.negativeCleanRate, 0.5);
  assert.equal(summary.metrics.micro.negativeFalsePositiveRate, 0.5);
  assert.equal(summary.tasks[0].positive.total, 2);
  assert.equal(summary.tasks[0].negative.total, 2);
  for (const removed of [
    'status', 'purpose', 'regression', 'gateVerdict', 'eligibleForBaseline',
    'eligibilityReasons', 'baseline',
  ]) {
    assert.equal(Object.hasOwn(summary, removed), false);
  }
});

test('infrastructure ERROR means the measurement did not complete', () => {
  const cases = measuredCases();
  cases[0] = makeMeasuredCase({
    id: 'positive-pass', expectation: { minEvents: 1 }, status: 'ERROR', totalMs: 1_000,
  });
  const summary = measuredSummary({ cases });

  assert.equal(summary.executionOutcome, 'ERROR');
  assert.equal(accuracyExitCode(summary), 2);
  assert.ok(summary.executionReasons.includes('case:positive-pass:ERROR'));
});

test('strict per-trial cleanup failures block the execution result', () => {
  const cases = measuredCases();
  cases[0].cleanupBlocked = true;
  const summary = measuredSummary({ cases });

  assert.equal(summary.executionOutcome, 'BLOCKED');
  assert.equal(accuracyExitCode(summary), 2);
  assert.ok(summary.executionReasons.includes('case:positive-pass:cleanup-blocked'));
});

test('explicit execution block reasons are preserved without gate interpretation', () => {
  const summary = measuredSummary({
    executionBlockReasons: ['device cleanup could not be verified'],
  });
  assert.equal(summary.executionOutcome, 'BLOCKED');
  assert.deepEqual(summary.executionReasons, ['device cleanup could not be verified']);
});

test('summary validator checks consumed structure but permits harmless future fields', () => {
  const summary = measuredSummary();
  assert.doesNotThrow(() => assertAccuracySummary(summary));
  assert.equal(validateAccuracySummary(summary).valid, true);

  for (const mutate of [
    (copy) => { delete copy.tasks[0].displayName; },
    (copy) => { delete copy.cases[0].expectation; },
    (copy) => { copy.cases[0].status = 'FLAKY'; },
    (copy) => { copy.execution.selection.count += 1; },
    (copy) => { copy.admission.checks = [{}]; },
  ]) {
    const copy = structuredClone(summary);
    mutate(copy);
    assert.equal(validateAccuracySummary(copy).valid, false);
    assert.throws(() => assertAccuracySummary(copy), /invalid accuracy summary/i);
  }

  const extended = structuredClone(summary);
  extended.futureField = { note: 'ignored by v4 readers' };
  extended.tasks[0].futureMetric = 1;
  assert.equal(validateAccuracySummary(extended).valid, true);
});
