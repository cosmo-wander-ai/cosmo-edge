import assert from 'node:assert/strict';
import test from 'node:test';

import { ShutdownSignalError } from '../src/shutdown-signal.js';
import { TaskRunner } from '../src/task-runner.js';

test('task runner preserves the VLM completion counter contract in expected bindings', () => {
  const runner = new TaskRunner({}, {
    tasks: [{
      id: 'vlm',
      type: 'vlm',
      algorithmId: '89336',
      algorithmCode: '89336',
      scheduleId: 'always',
      vlmCompletionActionId: 'PDA_00003',
    }],
  });
  runner.setChannels(['channel-1']);

  assert.equal(
    runner.expectedTaskEntries(['channel-1'])[0].vlmCompletionActionId,
    'PDA_00003',
  );
});

test('task runner aborts when a hold sample cannot be captured', async () => {
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch() {
      return { failedList: [] };
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
  });
  runner.setChannels(['channel-1']);

  await assert.rejects(
    runner.runStaircase(
      [{ channels: 1, holdSec: 0.001 }],
      { onSample: async () => { throw new Error('preview keepalive failed'); } },
      0.001,
    ),
    /sample tick failed: preview keepalive failed/,
  );
});

test('task runner stops and disables tasks when a hold fuse trips', async () => {
  const switches = [];
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch(tasks) {
      switches.push(tasks);
      return { failedList: [] };
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
  });
  runner.setChannels(['channel-1']);

  const result = await runner.runStaircase(
    [{ channels: 1, holdSec: 0.002 }],
    { onSample: async () => ({ stop: true, reason: 'disk 90% >= 90%' }) },
    0.001,
  );

  assert.equal(result.bottleneckPhase, 'hold');
  assert.equal(result.bottleneckSource, 'quick-fuse');
  assert.equal(result.bottleneckReason, 'disk 90% >= 90%');
  assert.equal(switches.length, 1);
  assert.equal(switches[0][0].enable, 0);
});

test('task runner preserves runtime-threshold gate identity at a completed hold', async () => {
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch() {
      return { failedList: [] };
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
  });
  runner.setChannels(['channel-1']);
  const gates = [{ taskKey: 'vlm', name: 'minFpsRatio', actual: 0.7, threshold: 0.8 }];

  const result = await runner.runStaircase(
    [{ channels: 1, holdSec: 0.001 }],
    {
      onSample: async () => ({ stop: false }),
      onStepEnd: async () => ({
        stop: true,
        source: 'runtime-threshold',
        reason: 'vlm fpsRatio 0.700 < 0.8',
        gates,
      }),
    },
    0.001,
  );

  assert.equal(result.bottleneckSource, 'runtime-threshold');
  assert.deepEqual(result.bottleneckGates, gates);
});

test('task runner interrupts a hold and disables active tasks on SIGTERM', async () => {
  const controller = new AbortController();
  const switches = [];
  let cleanupStarted = false;
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch(tasks) {
      assert.equal(cleanupStarted, true);
      switches.push(tasks);
      return { failedList: [] };
    },
    beginCleanup() {
      cleanupStarted = true;
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
    signal: controller.signal,
  });
  runner.setChannels(['channel-1']);

  const run = runner.runStaircase([{ channels: 1, holdSec: 60 }], {}, 60);
  await new Promise((resolve) => setImmediate(resolve));
  controller.abort(new ShutdownSignalError('SIGTERM'));

  await assert.rejects(run, (error) => error.signalName === 'SIGTERM' && error.exitCode === 143);
  assert.equal(cleanupStarted, true);
  assert.equal(switches.length, 1);
  assert.equal(switches[0][0].enable, 0);
});
