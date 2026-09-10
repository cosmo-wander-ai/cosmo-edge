import assert from 'node:assert/strict';
import test from 'node:test';

import {
  resolveVlmReadyTimeoutSec,
  waitForVlmReady,
} from '../src/vlm-readiness.js';

const entries = [
  { taskKey: 'vlm', taskType: 'vlm', taskId: 'task-1', channelId: 'channel-1' },
  { taskKey: 'vlm', taskType: 'vlm', taskId: 'task-2', channelId: 'channel-2' },
];

function channel(entry, total, { latency = true } = {}) {
  return {
    ...entry,
    completionActionId: entry.vlmCompletionActionId ?? 'BA_00004',
    primaryProcessTotal: total,
    measuredFps: 0,
    fpsRatio: 0,
    nodeDurationInfos: latency
      ? [{ name: 'Qwen3VLWorker', durationAvgUs: 900_000 }]
      : [],
  };
}

test('waits for every newly-added VLM route to advance its own completion counter', async () => {
  const samples = [
    { channels: [channel(entries[0], 10), channel(entries[1], 20)] },
    { channels: [channel(entries[0], 11), channel(entries[1], 20)] },
    { channels: [channel(entries[0], 11), channel(entries[1], 21)] },
  ];
  let now = 0;
  let probeIndex = 0;
  const result = await waitForVlmReady({
    entries,
    probe: async () => samples[probeIndex++],
    timeoutSec: 30,
    pollIntervalSec: 3,
    now: () => now,
    sleep: async (ms) => { now += ms; },
  });

  assert.equal(result.ready, true);
  assert.equal(result.status, 'ready');
  assert.equal(result.probes, 3);
  assert.equal(result.elapsedMs, 6000);
  assert.deepEqual(result.bindings.map((binding) => ({
    taskId: binding.taskId,
    baselineTotal: binding.baselineTotal,
    currentTotal: binding.currentTotal,
    completionAdvanced: binding.completionAdvanced,
    ready: binding.ready,
  })), [
    {
      taskId: 'task-1', baselineTotal: 10, currentTotal: 11,
      completionAdvanced: true, ready: true,
    },
    {
      taskId: 'task-2', baselineTotal: 20, currentTotal: 21,
      completionAdvanced: true, ready: true,
    },
  ]);
});

test('requires direct Qwen latency but never uses FPS as a readiness gate', async () => {
  const oneEntry = [entries[0]];
  const samples = [
    { channels: [channel(entries[0], 4, { latency: false })] },
    { channels: [channel(entries[0], 5, { latency: false })] },
    { channels: [channel(entries[0], 5, { latency: true })] },
  ];
  let now = 0;
  let probeIndex = 0;
  const result = await waitForVlmReady({
    entries: oneEntry,
    probe: async () => samples[probeIndex++],
    timeoutSec: 30,
    pollIntervalSec: 3,
    now: () => now,
    sleep: async (ms) => { now += ms; },
  });

  assert.equal(result.ready, true);
  assert.equal(result.probes, 3);
  assert.equal(result.bindings[0].qwenLatencyMs, 900);
});

test('accepts the task-local PDA counter for a PDA-only VLM binding', async () => {
  const pdaEntry = {
    ...entries[0],
    vlmCompletionActionId: 'PDA_00003',
  };
  const samples = [
    { channels: [channel(pdaEntry, 3)] },
    { channels: [channel(pdaEntry, 4)] },
  ];
  let now = 0;
  let probeIndex = 0;
  const result = await waitForVlmReady({
    entries: [pdaEntry],
    probe: async () => samples[probeIndex++],
    timeoutSec: 30,
    pollIntervalSec: 3,
    now: () => now,
    sleep: async (ms) => { now += ms; },
  });

  assert.equal(result.ready, true);
  assert.equal(result.bindings[0].completionActionId, 'PDA_00003');
  assert.equal(result.bindings[0].currentTotal, 4);
});

test('times out with the incomplete binding reason', async () => {
  let now = 0;
  let error;
  try {
    await waitForVlmReady({
      entries: [entries[0]],
      probe: async () => ({ channels: [channel(entries[0], 7)] }),
      timeoutSec: 6,
      pollIntervalSec: 3,
      now: () => now,
      sleep: async (ms) => { now += ms; },
    });
  } catch (caught) {
    error = caught;
  }
  assert.match(error?.message ?? '', /task-1 \(BA_00004 did not advance from 7\)/);
  assert.equal(error.name, 'VlmReadinessTimeoutError');
  assert.equal(error.readiness.status, 'timed-out');
  assert.equal(error.readiness.ready, false);
  assert.equal(error.readiness.probes, 3);
  assert.equal(error.readiness.bindings[0].completionActionId, 'BA_00004');
  assert.equal(error.readiness.bindings[0].baselineTotal, 7);
  assert.equal(error.readiness.bindings[0].currentTotal, 7);
  assert.equal(error.readiness.bindings[0].ready, false);
  assert.deepEqual(
    error.readiness.bindings[0].pendingReasons,
    ['BA_00004 did not advance from 7'],
  );
});

test('stops readiness polling when the run is aborted', async () => {
  const controller = new AbortController();
  let probes = 0;
  await assert.rejects(
    waitForVlmReady({
      entries: [entries[0]],
      probe: async () => {
        probes++;
        return { channels: [channel(entries[0], 1)] };
      },
      signal: controller.signal,
      sleep: async () => { controller.abort(new Error('stop now')); },
    }),
    /stop now/,
  );
  assert.equal(probes, 1);
});

test('returns immediately when no newly-added binding is VLM', async () => {
  let probes = 0;
  const result = await waitForVlmReady({
    entries: [{ taskType: 'cv', taskId: 'cv-1', channelId: 'channel-1' }],
    probe: async () => { probes++; },
  });
  assert.equal(result.ready, true);
  assert.equal(result.probes, 0);
  assert.equal(probes, 0);
});

test('validates the VLM readiness timeout before a run starts', () => {
  assert.equal(resolveVlmReadyTimeoutSec(undefined), 180);
  assert.equal(resolveVlmReadyTimeoutSec('12.5'), 12.5);
  assert.throws(() => resolveVlmReadyTimeoutSec('invalid'), /positive number/);
  assert.throws(() => resolveVlmReadyTimeoutSec('0'), /positive number/);
});
