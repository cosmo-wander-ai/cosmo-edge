import assert from 'node:assert/strict';
import test from 'node:test';

import {
  makeAccuracyFixture,
  makeMeasuredCase,
} from './helpers/accuracy-v4-fixture.js';

test('fixture models one measurement purpose without eligibility metadata', () => {
  const { execution, suite } = makeAccuracyFixture({ concurrency: 2 });

  assert.equal(execution.concurrency, 2);
  assert.equal(Object.hasOwn(execution, 'purpose'), false);
  assert.equal(Object.hasOwn(execution, 'eligibilityReasons'), false);
  assert.equal(Object.hasOwn(suite, 'eligibility'), false);
  assert.equal(Object.hasOwn(suite, 'trials'), false);
  assert.equal(Object.hasOwn(suite, 'gates'), false);
});

test('measured fixture creates one trial for each PASS or FAIL result', () => {
  const expectation = { minEvents: 1 };
  const passed = makeMeasuredCase({ id: 'passed', expectation, status: 'PASS', totalMs: 3_000 });
  const failed = makeMeasuredCase({ id: 'failed', expectation, status: 'FAIL', totalMs: 3_000 });

  assert.deepEqual(passed.trials.map((trial) => trial.status), ['PASS']);
  assert.deepEqual(failed.trials.map((trial) => trial.status), ['FAIL']);
  assert.equal(failed.trials[0].timingMs.total, 3_000);
});
