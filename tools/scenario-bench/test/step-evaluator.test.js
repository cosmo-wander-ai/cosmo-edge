import assert from 'node:assert/strict';
import test from 'node:test';

import { summarizeStep, runtimeStepDecision } from '../src/step-evaluator.js';

test('step summary records accelerator and accelerator-memory peaks', () => {
  const samples = Array.from({ length: 8 }, (_, index) => ({
    stepIndex: 0,
    ts: index * 3000,
    channels: [],
    hardware: {
      npuUtilization: { usedPercent: 20 + index },
      specialMemoryUtilization: { usedPercent: 30 + index },
      eMMCUtilization: { usedPercent: 40 + index },
    },
  }));

  const summary = summarizeStep(
    { index: 0, channels: 0, holdSec: 24 },
    samples,
    { pass: { maxDiskUsedPercent: 50 } },
  );

  assert.equal(summary.maxNpu, 27);
  assert.equal(summary.maxAcceleratorMem, 37);
  assert.equal(summary.maxDiskUsedPercent, 47);
  assert.equal(summary.perThreshold.find((item) => item.name === 'maxDiskUsedPercent')?.result, 'PASS');
});

test('step summary fails when device disk usage exceeds the configured gate', () => {
  const samples = Array.from({ length: 4 }, (_, index) => ({
    stepIndex: 0,
    ts: index * 3000,
    channels: [],
    hardware: { eMMCUtilization: { usedPercent: 88 + index } },
  }));

  const summary = summarizeStep(
    { index: 0, channels: 0, holdSec: 12 },
    samples,
    { pass: { maxDiskUsedPercent: 90 } },
  );

  assert.equal(summary.maxDiskUsedPercent, 91);
  assert.equal(summary.pass, false);
  assert.match(summary.reasons.join('; '), /磁盘使用率 91%/);
});

test('runtime decision stops CV tasks when steady throughput falls below half baseline', () => {
  const decision = runtimeStepDecision({
    avgDiscard: 0,
    taskStats: [{
      taskKey: 'cv',
      taskDisplayName: 'cv',
      taskType: 'cv',
      minThroughputFps: 4.9,
      avgDiscardRate: 0,
    }],
  }, {
    baselineByTask: { cv: 10 },
    fpsHalveRatio: 0.5,
  });

  assert.equal(decision.stop, true);
  assert.match(decision.reason, /cv fps 4\.9 < baseline 10\.0\*0\.5/);
});

test('first-route VLM runtime decision applies the configured FPS gate instead of its baseline', () => {
  const decision = runtimeStepDecision({
    avgDiscard: 0,
    taskStats: [{
      taskKey: 'vlm',
      taskDisplayName: 'vlm',
      taskType: 'vlm',
      minThroughputFps: 0.6,
      minFpsRatio: 0.7,
      avgDiscardRate: 0,
    }],
  }, {
    baselineByTask: { vlm: 2 },
    thresholds: {
      taskTypes: {
        vlm: { minFpsRatio: 0.8 },
      },
    },
  });

  assert.equal(decision.stop, true);
  assert.match(decision.reason, /vlm fpsRatio 0\.700 < 0\.8/);
  assert.doesNotMatch(decision.reason, /baseline/);
});

test('runtime decision stops on VLM task-local telemetry missing rate', () => {
  const decision = runtimeStepDecision({
    avgDiscard: 0,
    taskStats: [{
      taskKey: 'vlm',
      taskDisplayName: 'vlm',
      taskType: 'vlm',
      minThroughputFps: 0.1,
      minFpsRatio: 1,
      maxMissingRate: 0.05,
      avgDiscardRate: 0,
    }],
  }, {
    thresholds: {
      taskTypes: {
        vlm: { minFpsRatio: 0.8, maxMissingRate: 0 },
      },
    },
  });

  assert.equal(decision.stop, true);
  assert.match(decision.reason, /vlm missingRate 0\.050 > 0/);
  assert.deepEqual(
    decision.gates.map((gate) => gate.name),
    ['maxMissingRate'],
  );
});

test('VLM step summary uses the complete hold window for counter throughput', () => {
  const samples = [0, 1, 2, 3, 4, 5].map((index) => ({
    stepIndex: 0,
    ts: index * 10_000,
    channels: [{
      taskKey: 'vlm',
      taskDisplayName: 'vlm',
      taskType: 'vlm',
      algorithmId: '55009',
      channelId: 'ch1',
      targetFps: 0.1,
      measuredFps: 0,
      fpsRatio: 0,
      primaryProcessTotal: 100 + Math.max(0, index - 2),
      discardRate: 0,
      telemetryMissing: false,
      nodeDurationInfos: [
        { name: 'Qwen3VLWorker', durationAvgUs: 1_200_000 },
      ],
    }],
    hardware: {},
  }));

  const summary = summarizeStep(
    { index: 0, channels: 1, holdSec: 60 },
    samples,
    { taskTypes: { vlm: { minFpsRatio: 0.8, maxMissingRate: 0 } } },
    'local',
  );

  assert.equal(summary.pass, false);
  assert.equal(summary.channelStats[0].windowThroughputFps, 0.06);
  assert.equal(summary.taskStats[0].minFpsRatio, 0.6);
  assert.equal(summary.channelStats[0].sampleCount, 6);
});

test('VLM step summary exposes the newest route throughput separately', () => {
  const samples = [0, 1, 2, 3].map((index) => ({
    stepIndex: 1,
    ts: index * 10_000,
    channels: [
      {
        taskKey: 'vlm', taskType: 'vlm', channelId: 'ch1', targetFps: 0.1,
        primaryProcessTotal: 10 + index * 2, measuredFps: 0.2, discardRate: 0,
        telemetryMissing: false,
        nodeDurationInfos: [{ name: 'Qwen3VLWorker', durationAvgUs: 1_000_000 }],
      },
      {
        taskKey: 'vlm', taskType: 'vlm', channelId: 'ch2', targetFps: 0.1,
        primaryProcessTotal: 20 + index, measuredFps: 0.1, discardRate: 0,
        telemetryMissing: false,
        nodeDurationInfos: [{ name: 'Qwen3VLWorker', durationAvgUs: 1_000_000 }],
      },
    ],
    hardware: {},
  }));

  const summary = summarizeStep(
    {
      index: 1,
      channels: 2,
      holdSec: 40,
      currentVlmBindings: [{ taskKey: 'vlm', channelId: 'ch2' }],
    },
    samples,
    { taskTypes: { vlm: { minFpsRatio: null, maxMissingRate: 0 } } },
    'local',
  );

  assert.equal(summary.currentRouteChannelId, 'ch2');
  assert.equal(summary.currentRouteFps, 0.1);
});

test('current-route FPS uses only the explicitly newest VLM binding in a mixed workload', () => {
  const samples = [0, 1, 2, 3].map((index) => ({
    stepIndex: 1,
    ts: index * 10_000,
    channels: [
      {
        taskKey: 'vlm-new', taskType: 'vlm', channelId: 'ch2', targetFps: 0.1,
        primaryProcessTotal: 20 + index, measuredFps: 0.1, discardRate: 0,
        telemetryMissing: false,
        nodeDurationInfos: [{ name: 'Qwen3VLWorker', durationAvgUs: 1_000_000 }],
      },
      {
        taskKey: 'cv', taskType: 'cv', channelId: 'ch2', targetFps: 5,
        measuredFps: 0.01, discardRate: 0, telemetryMissing: false,
        nodeDurationInfos: [{ name: 'detector', durationAvgUs: 10_000 }],
      },
      {
        taskKey: 'vlm-old', taskType: 'vlm', channelId: 'ch1', targetFps: 0.1,
        primaryProcessTotal: 100 + index * 3, measuredFps: 0.3, discardRate: 0,
        telemetryMissing: false,
        nodeDurationInfos: [{ name: 'Qwen3VLWorker', durationAvgUs: 1_000_000 }],
      },
    ],
    hardware: {},
  }));

  const summary = summarizeStep(
    {
      index: 1,
      channels: 2,
      holdSec: 40,
      currentVlmBindings: [{ taskKey: 'vlm-new', channelId: 'ch2' }],
    },
    samples,
    { taskTypes: { vlm: { minFpsRatio: null, maxMissingRate: 0 } } },
    'local',
  );

  assert.equal(summary.currentRouteTaskKey, 'vlm-new');
  assert.equal(summary.currentRouteChannelId, 'ch2');
  assert.equal(summary.currentRouteFps, 0.1);
});

test('step summary ignores ramp samples when hold samples are available', () => {
  const ramp = {
    stepIndex: 0,
    phase: 'ramp',
    activeChannels: 1,
    ts: 0,
    channels: [{
      taskKey: 'cv',
      taskDisplayName: 'cv',
      taskType: 'cv',
      channelId: 'ch1',
      measuredFps: 0,
      discardRate: 0,
      nodeDurationInfos: [],
    }],
    hardware: {},
  };
  const hold = [1, 2, 3, 4].map((index) => ({
    stepIndex: 0,
    phase: 'hold',
    activeChannels: 4,
    ts: index * 3000,
    channels: Array.from({ length: 4 }, (_, channelIndex) => ({
      taskKey: 'cv',
      taskDisplayName: 'cv',
      taskType: 'cv',
      channelId: `ch${channelIndex + 1}`,
      measuredFps: 5,
      discardRate: 0,
      nodeDurationInfos: [],
    })),
    hardware: {},
  }));

  const summary = summarizeStep(
    { index: 0, channels: 4, holdSec: 30 },
    [ramp, ...hold],
    {},
    'local',
  );

  assert.equal(summary.minFpsAcross, 5);
  assert.equal(summary.taskStats[0].bindingCount, 4);
});
