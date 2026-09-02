import assert from 'node:assert/strict';
import test from 'node:test';

import { AccuracyRunner, resolveAccuracyExecution } from '../src/accuracy/runner.js';
import { selectAccuracyCases } from '../src/accuracy/selection.js';

function suite() {
  return {
    id: 'suite',
    schemaVersion: 3,
    protocolVersion: 4,
    displayName: 'Suite',
    sourceMode: 'local',
    targetPlatforms: ['bm1688'],
    identity: {
      suiteSha256: 'suite-hash',
      caseManifestSha256: 'case-hash',
      caseSetSha256: 'set-hash',
      taskConfigSha256: { cv: 'cv-hash', vlm: 'vlm-hash' },
    },
    defaults: {
      observeSec: 45,
      eventFlushTimeoutSec: 15,
      infrastructureRetriesPerTrial: 1,
    },
    tasks: [
      {
        id: 'cv', kind: 'cv', algorithmId: '1', algorithmCode: '1',
        configSource: 'frozen', taskConfigSha256: 'cv-hash',
      },
      {
        id: 'vlm', kind: 'vlm', algorithmId: '2', algorithmCode: '2',
        configSource: 'frozen', taskConfigSha256: 'vlm-hash',
      },
    ],
    cases: [
      { id: 'vlm-case', task: 'vlm', expectation: { maxEvents: 0 }, tags: [] },
      { id: 'cv-fail', task: 'cv', expectation: { minEvents: 1 }, tags: ['quick'] },
      {
        id: 'cv-legacy-critical', task: 'cv', critical: true,
        expectation: { maxEvents: 0 }, tags: [],
      },
    ],
  };
}

function preparedExecution(local, {
  profile = 'full', concurrency = 1, caseIds = null, taskIds = null, tags = null,
} = {}) {
  const selectedCases = selectAccuracyCases(local, { profile, caseIds, taskIds, tags });
  return {
    selectedCases,
    execution: resolveAccuracyExecution(local, { profile, concurrency, selectedCases }),
  };
}

test('runner measures every CV case once before measuring VLM cases serially', async () => {
  const local = suite();
  const order = [];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local),
    executeTrial: async ({ case: item, validTrialNumber }) => {
      order.push(`${item.id}:${validTrialNumber}`);
      return { status: item.id === 'cv-fail' ? 'FAIL' : 'PASS', eventCount: 0 };
    },
  });

  const result = await runner.run();
  assert.deepEqual(order, [
    'cv-fail:1', 'cv-legacy-critical:1', 'vlm-case:1',
  ]);
  assert.deepEqual(result.cases.map((item) => item.status), ['FAIL', 'PASS', 'PASS']);
  assert.ok(result.cases.every((item) => item.trials.length === 1));
  assert.equal(Object.hasOwn(result, 'summary'), false);
});

test('quick profile narrows the selection and still measures each case once', async () => {
  const local = suite();
  const order = [];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local, { profile: 'quick' }),
    executeTrial: async ({ case: item }) => {
      order.push(item.id);
      return { status: 'PASS', eventCount: 0 };
    },
  });
  const result = await runner.run();
  assert.deepEqual(order, ['cv-fail', 'cv-legacy-critical']);
  assert.ok(result.cases.every((item) => item.trials.length === 1));
  assert.equal(result.execution.profile, 'quick');
  assert.equal(Object.hasOwn(result.execution, 'purpose'), false);
  assert.equal(Object.hasOwn(result.execution, 'trialPolicy'), false);
});

test('FAIL is a measured result and does not trigger confirmation trials', async () => {
  const local = suite();
  local.cases = [local.cases[1], local.cases[2]];
  local.tasks = [local.tasks[0]];
  const order = [];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local),
    executeTrial: async ({ case: item }) => {
      order.push(item.id);
      return { status: 'FAIL', eventCount: 0 };
    },
  });

  const result = await runner.run();
  assert.deepEqual(order, ['cv-fail', 'cv-legacy-critical']);
  assert.deepEqual(result.cases.map((item) => item.status), ['FAIL', 'FAIL']);
});

test('concurrency applies to CV while VLM remains last and serial', async () => {
  const local = suite();
  local.cases = [
    { id: 'cv-one', task: 'cv', expectation: { maxEvents: 0 }, tags: [] },
    { id: 'cv-two', task: 'cv', expectation: { maxEvents: 0 }, tags: [] },
    { id: 'vlm-one', task: 'vlm', expectation: { maxEvents: 0 }, tags: [] },
    { id: 'vlm-two', task: 'vlm', expectation: { maxEvents: 0 }, tags: [] },
  ];
  let active = 0;
  let maxActive = 0;
  const starts = [];
  const releases = [];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local, { concurrency: 2 }),
    executeTrial: async ({ case: item }) => {
      starts.push(item.id);
      active += 1;
      maxActive = Math.max(maxActive, active);
      if (item.task === 'cv') await new Promise((resolve) => releases.push(resolve));
      active -= 1;
      return { status: 'PASS', eventCount: 0 };
    },
  });
  const pending = runner.run();
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(starts, ['cv-one', 'cv-two']);
  releases.splice(0).forEach((resolve) => resolve());
  const result = await pending;
  assert.equal(maxActive, 2);
  assert.deepEqual(starts, ['cv-one', 'cv-two', 'vlm-one', 'vlm-two']);
  assert.equal(result.execution.concurrency, 2);
});

test('concurrency uses rolling CV workers and checkpoints completed cases promptly', async () => {
  const local = suite();
  local.cases = ['slow', 'fast', 'next'].map((id) => ({
    id, task: 'cv', expectation: { maxEvents: 0 }, tags: [],
  }));
  let releaseSlow;
  const slow = new Promise((resolve) => { releaseSlow = resolve; });
  const starts = [];
  const checkpoints = [];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local, { concurrency: 2 }),
    evidenceWriter: {
      async writePartial(value) { checkpoints.push(value.cases.map((item) => item.id)); },
    },
    executeTrial: async ({ case: item }) => {
      starts.push(item.id);
      if (item.id === 'slow') await slow;
      return { status: 'PASS', eventCount: 0 };
    },
  });
  const pending = runner.run();
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(starts, ['slow', 'fast', 'next']);
  assert.ok(checkpoints.some((ids) => ids.includes('fast')));
  releaseSlow();
  await pending;
});

test('runner retries infrastructure errors without repeating valid measurements', async () => {
  const local = suite();
  local.cases = [local.cases[2]];
  local.tasks = [local.tasks[0]];
  const attempts = ['ERROR', 'PASS'];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local),
    executeTrial: async () => ({ status: attempts.shift(), eventCount: 0 }),
  });
  const result = await runner.run();
  assert.equal(result.cases[0].status, 'PASS');
  assert.equal(result.cases[0].trials.length, 2);
  assert.equal(result.cases[0].trials[0].status, 'ERROR');
  assert.equal(result.cases[0].trials[1].status, 'PASS');
});

test('runner forces a complete checkpoint after selected cases finish', async () => {
  const local = suite();
  local.cases = [local.cases[0]];
  local.tasks = [local.tasks[1]];
  const checkpoints = [];
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...preparedExecution(local),
    executeTrial: async () => ({ status: 'PASS', eventCount: 0 }),
    evidenceWriter: {
      async writePartial(value, options) { checkpoints.push({ value, options }); },
    },
  });
  await runner.run();
  assert.equal(checkpoints.length, 2);
  assert.equal(checkpoints.at(-1).value.cases[0].status, 'PASS');
  assert.equal(checkpoints.at(-1).options.force, true);
});

test('execution identity tracks the selected sample set and effective measurement settings', () => {
  const local = suite();
  const first = resolveAccuracyExecution(local, { selectedCases: [local.cases[0]] });
  const second = resolveAccuracyExecution(local, { selectedCases: [local.cases[1]] });
  assert.notEqual(first.selection.sha256, second.selection.sha256);
  assert.equal(first.selection.count, 1);

  const changed = structuredClone(local);
  changed.defaults.observeSec = 60;
  const differentSettings = resolveAccuracyExecution(changed, {
    selectedCases: [changed.cases[0]],
  });
  assert.notEqual(first.measurementConfigSha256, differentSettings.measurementConfigSha256);
});

test('execution accepts concurrency one, two, and four without eligibility policy', () => {
  for (const concurrency of [1, 2, 4]) {
    const execution = resolveAccuracyExecution(suite(), { concurrency });
    assert.equal(execution.concurrency, concurrency);
    assert.equal(Object.hasOwn(execution, 'eligibilityReasons'), false);
  }
  assert.throws(() => resolveAccuracyExecution(suite(), { concurrency: 3 }), /1, 2, or 4/);
});

test('filtered runner usage is an ordinary measurement with a frozen selection', async () => {
  const local = suite();
  const prepared = preparedExecution(local, { caseIds: ['cv-fail'] });
  const runner = new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    ...prepared,
    executeTrial: async () => ({ status: 'PASS', eventCount: 0 }),
  });
  const result = await runner.run();
  assert.equal(result.cases.length, 1);
  assert.equal(result.execution.selection.count, 1);
});

test('runner rejects selected cases that do not match its execution identity', () => {
  const local = suite();
  const execution = resolveAccuracyExecution(local, { selectedCases: [local.cases[0]] });
  assert.throws(() => new AccuracyRunner({
    suite: local,
    targetChip: 'bm1688',
    selectedCases: [local.cases[1]],
    execution,
    executeTrial: async () => ({ status: 'PASS', eventCount: 0 }),
  }), /selected cases.*execution identity/i);
});
