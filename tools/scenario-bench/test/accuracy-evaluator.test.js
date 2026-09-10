import assert from 'node:assert/strict';
import test from 'node:test';

import { evaluateExpectation, summarizeAccuracyRun } from '../src/accuracy/evaluator.js';

const SHA_A = 'a'.repeat(64);
const SHA_B = 'b'.repeat(64);
const SHA_C = 'c'.repeat(64);

function execution(caseCount, profile = 'full') {
  return {
    profile,
    concurrency: 1,
    selection: { count: caseCount, sha256: SHA_C },
    measurementConfigSha256: SHA_A,
  };
}

function suite() {
  return {
    id: 'suite',
    protocolVersion: 4,
    displayName: 'Suite',
    identity: {
      suiteSha256: SHA_A,
      caseManifestSha256: SHA_B,
      caseSetSha256: SHA_C,
      taskConfigSha256: { helmet: SHA_A },
    },
    sourceMode: 'local',
    targetPlatforms: ['bm1688'],
    tasks: [{
      id: 'helmet', displayName: 'Helmet', algorithmId: '15', algorithmCode: '15',
      kind: 'cv', configSource: 'frozen', taskConfigSha256: SHA_A,
    }],
  };
}

test('evaluates minimum and maximum event expectations', () => {
  assert.equal(evaluateExpectation({ minEvents: 1 }, 1).status, 'PASS');
  assert.equal(evaluateExpectation({ minEvents: 1 }, 0).status, 'FAIL');
  assert.equal(evaluateExpectation({ maxEvents: 0 }, 0).status, 'PASS');
  assert.equal(evaluateExpectation({ maxEvents: 0 }, 2).status, 'FAIL');
  assert.throws(() => evaluateExpectation({}, 0), /minEvents or maxEvents/);
});

test('measurement keeps FAIL as quality data and ERROR as incomplete execution', () => {
  const cases = [
    { id: 'p1', task: 'helmet', expectation: { minEvents: 1 }, status: 'PASS', critical: false },
    { id: 'p2', task: 'helmet', expectation: { minEvents: 1 }, status: 'ERROR', critical: false },
    { id: 'n1', task: 'helmet', expectation: { maxEvents: 0 }, status: 'FAIL', critical: false },
  ];
  assert.throws(
    () => summarizeAccuracyRun({ suite: suite(), cases, targetChip: 'bm1688' }),
    /execution is required/i,
  );
  const summary = summarizeAccuracyRun({
    suite: suite(), cases, targetChip: 'bm1688', execution: execution(cases.length),
  });
  assert.equal(summary.executionOutcome, 'ERROR');
  assert.equal(summary.tasks[0].errors, 1);
  assert.equal(summary.tasks[0].positive.total, 1);
  assert.equal(summary.tasks[0].negative.total, 1);
});

test('task measurement exposes algorithm and applied configuration identity', () => {
  const summary = summarizeAccuracyRun({
    suite: suite(),
    targetChip: 'bm1688',
    execution: execution(1, 'quick'),
    cases: [{
      id: 'positive', task: 'helmet', expectation: { minEvents: 1 },
      status: 'PASS', critical: false,
      trials: [{ status: 'PASS', taskConfigSha256: SHA_A }],
    }],
  });
  assert.equal(summary.executionOutcome, 'COMPLETED');
  assert.equal(summary.execution.profile, 'quick');
  assert.equal(summary.tasks[0].algorithmId, '15');
  assert.equal(summary.tasks[0].algorithmCode, '15');
  assert.deepEqual(summary.tasks[0].taskConfigHashes, [SHA_A]);
});
