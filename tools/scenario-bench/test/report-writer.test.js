import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { ReportWriter } from '../src/report-writer.js';

test('partial reports are checkpointed at a bounded interval', async () => {
  const output = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-report-writer-'));
  let now = 1_000;
  try {
    const writer = new ReportWriter(output, { partialWriteIntervalMs: 30_000, now: () => now });
    const first = await writer.writePartial({ samples: [1] });
    assert.equal(first.skipped, false);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1]);

    now += 10_000;
    const skipped = await writer.writePartial({ samples: [1, 2] });
    assert.equal(skipped.skipped, true);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1]);

    now += 20_000;
    const checkpoint = await writer.writePartial({ samples: [1, 2, 3] });
    assert.equal(checkpoint.skipped, false);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1, 2, 3]);

    now += 1;
    const forced = await writer.writePartial({ samples: [1, 2, 3, 4] }, { force: true });
    assert.equal(forced.skipped, false);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1, 2, 3, 4]);
  } finally {
    fs.rmSync(output, { recursive: true, force: true });
  }
});

test('capacity uses only the passing prefix that starts at one channel', () => {
  const writer = new ReportWriter('.');
  const steps = [
    { step: { index: 0 }, channels: 1, pass: false, reasons: ['warmup window below target'] },
    { step: { index: 1 }, channels: 2, pass: true, reasons: [] },
    { step: { index: 2 }, channels: 3, pass: true, reasons: [] },
    { step: { index: 3 }, channels: 4, pass: true, reasons: [] },
    { step: { index: 4 }, channels: 5, pass: true, reasons: [] },
    { step: { index: 5 }, channels: 6, pass: true, reasons: [] },
    { step: { index: 6 }, channels: 7, pass: false, reasons: ['fpsRatio 0.661 < 0.8'] },
  ].map(withCompleteHold);

  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    bottleneck: {
      stepIndex: 6,
      stepNumber: 7,
      channels: 7,
      reason: 'fpsRatio 0.661 < 0.8',
    },
  }, steps);

  assert.equal(summary.maxVerifiedPassedChannels, null);
  assert.equal(summary.maxStableChannels, null);
  assert.equal(summary.maxStableChannelsExact, false);
  assert.equal(summary.capacityMeasured, false);
  assert.equal(summary.capacityExclusionReason, 'execution-blocked');
  assert.match(summary.conclusion, /执行受阻/);
  assert.doesNotMatch(summary.conclusion, /容量上限：6 路/);
});

test('capacity is exact only when the next channel after the passing prefix fails', () => {
  const writer = new ReportWriter('.');
  const steps = [
    { step: { index: 0 }, channels: 1, pass: true, reasons: [] },
    { step: { index: 1 }, channels: 2, pass: true, reasons: [] },
    { step: { index: 2 }, channels: 3, pass: false, reasons: ['fpsRatio 0.7 < 0.8'] },
  ].map(withCompleteHold);

  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    bottleneck: {
      stepIndex: 2,
      stepNumber: 3,
      channels: 3,
      phase: 'hold',
      reason: 'fpsRatio 0.7 < 0.8',
    },
  }, steps);

  assert.equal(summary.maxVerifiedPassedChannels, 2);
  assert.equal(summary.maxStableChannels, 2);
  assert.equal(summary.maxStableChannelsExact, true);
  assert.equal(summary.capacityMeasured, true);
  assert.match(summary.conclusion, /容量上限：2 路/);
  assert.match(summary.conclusion, /3 路.*未通过容量门禁/);
});

test('a report-only adjacent failure without a raw runtime bottleneck is execution-blocked', () => {
  const writer = new ReportWriter('.');
  const steps = [
    { step: { index: 0 }, channels: 1, pass: true, reasons: [] },
    { step: { index: 1 }, channels: 2, pass: true, reasons: [] },
    {
      step: { index: 2 }, channels: 3, pass: false,
      reasons: ['fpsRatio 0.7 < 0.8'],
      perThreshold: [{ name: 'minFpsRatio', result: 'FAIL' }],
    },
  ].map(withCompleteHold);

  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    bottleneck: null,
  }, steps);

  assert.equal(summary.maxStableChannels, null);
  assert.equal(summary.maxStableChannelsExact, false);
  assert.equal(summary.capacityMeasured, false);
  assert.equal(summary.capacityExecutionBlocked, true);
  assert.equal(summary.capacityExclusionReason, 'execution-blocked');
  assert.equal(summary.bottleneck, null);
  assert.ok(summary.capacityExecutionIssues.includes(
    'report-failure-without-runtime-bottleneck',
  ));
  assert.doesNotMatch(summary.conclusion, /容量上限：2 路/);
});

test('a quick-fuse partial hold cannot become an exact capacity boundary', () => {
  const writer = new ReportWriter('.');
  const runResult = {
    status: 'completed',
    profileMode: 'capacity',
    videoMode: 'local',
    sampleIntervalSec: 3,
    tasks: [{ id: 'cv', type: 'cv', targetFps: 5 }],
    thresholds: { pass: { minThroughputFps: 5 } },
    steps: [
      { index: 0, channels: 1, holdSec: 12 },
      { index: 1, channels: 2, holdSec: 12 },
      { index: 2, channels: 3, holdSec: 12 },
    ],
    bottleneck: {
      stepIndex: 2,
      stepNumber: 3,
      channels: 3,
      phase: 'hold',
      source: 'quick-fuse',
      reason: 'CPU >= 98% for 3 consecutive samples',
    },
    samples: [
      ...holdSamples({ stepIndex: 0, channels: 1, count: 4, startTs: 3_000, fps: 5 }),
      ...holdSamples({ stepIndex: 1, channels: 2, count: 4, startTs: 20_000, fps: 5 }),
      ...holdSamples({ stepIndex: 2, channels: 3, count: 2, startTs: 40_000, fps: 1 }),
    ],
  };

  const stepSummaries = writer._summarizeSteps(runResult);
  assert.equal(stepSummaries[2].pass, false);
  assert.equal(stepSummaries[2].holdWindow.complete, false);
  assert.ok(stepSummaries[2].holdWindow.reasons.includes('hold-sample-count-incomplete'));

  const summary = writer._buildSummary(runResult, stepSummaries);
  assert.equal(summary.maxStableChannels, null);
  assert.equal(summary.maxStableChannelsExact, false);
  assert.equal(summary.capacityMeasured, false);
  assert.equal(summary.capacityExecutionBlocked, true);
  assert.equal(summary.capacityExclusionReason, 'execution-blocked');
  assert.ok(summary.capacityExecutionIssues.some((issue) =>
    issue.includes('hold-sample-count-incomplete')));
  assert.ok(summary.capacityExecutionIssues.includes('runtime-stop:quick-fuse'));
  assert.doesNotMatch(summary.conclusion, /容量上限：2 路/);
});

test('passing every configured channel produces a lower bound instead of an exact capacity', () => {
  const writer = new ReportWriter('.');
  const steps = Array.from({ length: 4 }, (_, index) => withCompleteHold({
    step: { index },
    channels: index + 1,
    pass: true,
    reasons: [],
  }));

  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    steps: Array.from({ length: 4 }, (_, index) => ({ index, channels: index + 1 })),
  }, steps);

  assert.equal(summary.maxVerifiedPassedChannels, 4);
  assert.equal(summary.maxStableChannels, 4);
  assert.equal(summary.maxStableChannelsExact, false);
  assert.equal(summary.capacityMeasured, true);
  assert.match(summary.conclusion, /容量下界：≥4 路/);
  assert.doesNotMatch(summary.conclusion, /容量上限/);
});

test('task binding blocks are execution failures, not capacity failures', () => {
  const writer = new ReportWriter('.');
  const steps = [
    { step: { index: 0 }, channels: 1, pass: true, reasons: [] },
    { step: { index: 1 }, channels: 2, pass: true, reasons: [] },
    {
      step: { index: 2 }, channels: 3, qualified: false, pass: null,
      reasons: ['ramp probe only'],
    },
  ].map(withCompleteHold);

  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    steps: Array.from({ length: 3 }, (_, index) => ({ index, channels: index + 1 })),
    bottleneck: {
      stepIndex: 2,
      stepNumber: 3,
      channels: 3,
      phase: 'ramp',
      reason: '任务绑定失败: task-3',
    },
  }, steps);

  assert.equal(summary.maxVerifiedPassedChannels, 2);
  assert.equal(summary.maxStableChannels, null);
  assert.equal(summary.maxStableChannelsExact, false);
  assert.equal(summary.capacityMeasured, false);
  assert.equal(summary.capacityExecutionBlocked, true);
  assert.equal(summary.capacityExclusionReason, 'execution-blocked');
  assert.match(summary.conclusion, /阻断不计为容量失败/);
  assert.doesNotMatch(summary.conclusion, /容量上限：/);
});

test('readiness timeouts and device execution aborts never become capacity failures', () => {
  const writer = new ReportWriter('.');
  const steps = [
    { step: { index: 0 }, channels: 1, pass: true, reasons: [] },
    { step: { index: 1 }, channels: 2, pass: true, reasons: [] },
  ].map(withCompleteHold);

  for (const message of [
    'VLM readiness timed out after 180s',
    'sample tick failed: device request timed out',
  ]) {
    const summary = writer._buildSummary({
      status: 'aborted',
      profileMode: 'capacity',
      steps: Array.from({ length: 3 }, (_, index) => ({ index, channels: index + 1 })),
      error: { message, atChannels: 3, atStepIndex: 2 },
    }, steps);

    assert.equal(summary.maxVerifiedPassedChannels, 2);
    assert.equal(summary.maxStableChannels, null);
    assert.equal(summary.maxStableChannelsExact, false);
    assert.equal(summary.capacityMeasured, false);
    assert.equal(summary.capacityExecutionBlocked, true);
    assert.equal(summary.capacityExclusionReason, 'execution-blocked');
    assert.match(summary.conclusion, /执行中断不计为容量失败/);
    assert.doesNotMatch(summary.conclusion, /容量上限：/);
  }
});

test('missing VLM readiness evidence is execution-blocked even after a full hold', () => {
  const writer = new ReportWriter('.');
  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    tasks: [{ id: 'vlm', type: 'vlm', targetFps: 0.1 }],
    thresholds: { taskTypes: { vlm: { minFpsRatio: 0.8, maxMissingRate: 0 } } },
  }, [withCompleteHold({
    step: {
      index: 0,
      currentVlmBindings: [{ taskKey: 'vlm', channelId: 'channel-1' }],
    },
    channels: 1,
    pass: true,
    reasons: [],
  })]);

  assert.equal(summary.overallPass, false);
  assert.equal(summary.capacityMeasured, false);
  assert.equal(summary.capacityExecutionBlocked, true);
  assert.equal(summary.capacityExclusionReason, 'execution-blocked');
  assert.ok(summary.capacityExecutionIssues.includes('1ch:vlm-readiness-incomplete'));
});

test('VLM report with a disabled throughput gate never claims capacity', () => {
  const writer = new ReportWriter('.');
  const steps = Array.from({ length: 6 }, (_, index) => withCompleteHold({
    step: { index, vlmReadiness: readyReadiness(index) },
    channels: index + 1,
    pass: true,
    reasons: [],
    perThreshold: [],
    taskStats: [],
    mediaStages: {},
  }));
  const runResult = {
    status: 'completed',
    profileMode: 'capacity',
    tasks: [{ id: 'vlm', type: 'vlm', targetFps: 0.1 }],
    thresholds: {
      taskTypes: {
        vlm: { minFpsRatio: null, maxMissingRate: 0 },
      },
    },
  };

  const summary = writer._buildSummary(runResult, steps);

  assert.equal(summary.overallPass, true);
  assert.equal(summary.capacityMeasured, false);
  assert.equal(summary.capacityExclusionReason, 'vlm-throughput-gate-disabled');
  assert.equal(summary.maxStableChannels, null);
  assert.equal(summary.maxStableChannelsExact, false);
  assert.equal(summary.maxVerifiedPassedChannels, 6);
  assert.match(summary.conclusion, /不形成容量结论/);
  assert.doesNotMatch(summary.conclusion, /容量上限/);

  const html = writer._renderHtml(runResult, steps, summary);
  assert.match(html, /VLM 吞吐门禁未启用/);
  assert.doesNotMatch(html, /可直接给出容量上限/);
});

test('summary preserves per-step VLM task-local readiness evidence', () => {
  const writer = new ReportWriter('.');
  const readiness = {
    stepIndex: 0,
    stepNumber: 1,
    targetChannels: 1,
    ready: true,
    status: 'ready',
    probes: 2,
    elapsedMs: 3000,
    bindings: [{
      taskId: 'vlm-1',
      channelId: 'channel-1',
      completionActionId: 'BA_00004',
      baselineTotal: 4,
      currentTotal: 5,
      completionAdvanced: true,
      qwenLatencyMs: 900,
      ready: true,
      pendingReasons: [],
    }],
  };
  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    steps: [{ index: 0, channels: 1, holdSec: 60, vlmReadiness: readiness }],
    tasks: [{ id: 'vlm', type: 'vlm', targetFps: 0.1 }],
    thresholds: { taskTypes: { vlm: { minFpsRatio: 0.8, maxMissingRate: 0 } } },
  }, [{
    step: { index: 0 },
    channels: 1,
    pass: true,
    reasons: [],
    perThreshold: [],
    taskStats: [],
    mediaStages: {},
  }]);

  assert.deepEqual(summary.vlmReadiness, [readiness]);
});

test('HTML rendering formats task percentage fields', () => {
  const writer = new ReportWriter('.');
  const html = writer._renderHtml({
    scenarioName: 'vlm',
    tasks: [{ id: 'vlm', type: 'vlm', algorithmId: '77175', targetFps: 0.1 }],
    videoMode: 'local',
    status: 'completed',
    thresholds: { pass: {} },
    samples: [{ ts: 0 }, { ts: 3000 }],
  }, [{
    step: { index: 0 },
    channels: 1,
    holdSec: 60,
    targetFps: 0.1,
    minFpsAcross: 0.1,
    criticalPathLatencyMs: 1200,
    detectorLatencyMs: 1200,
    avgDiscard: 0,
    maxDiscard: 0,
    maxNpu: 90,
    maxCpu: 3,
    maxMem: 55,
    pass: true,
    reasons: [],
    perThreshold: [],
    taskStats: [{
      taskKey: 'vlm',
      taskDisplayName: 'vlm',
      strategy: 'vlm',
      algorithmId: '77175',
      bindingCount: 1,
      minThroughputFps: 0.1,
      minFpsRatio: 1,
      maxMissingRate: 0,
      avgDiscardRate: 0,
      maxPrimaryLatencyMs: 1200,
      maxCriticalPathLatencyMs: 1200,
    }],
  }], {
    overallPass: true,
    hasBottleneck: false,
    maxStableChannelsExact: true,
    conclusion: 'ok',
  });

  assert.match(html, /100%/);
  assert.match(html, /0%/);
});

test('HTML rendering estimates sampling interval from stable in-step samples', () => {
  const writer = new ReportWriter('.');
  const html = writer._renderHtml({
    scenarioName: 'vlm',
    tasks: [{ id: 'vlm', type: 'vlm', algorithmId: '77175', targetFps: 0.1 }],
    videoMode: 'local',
    status: 'completed',
    thresholds: { pass: {} },
    samples: [
      { stepIndex: 0, ts: 0 },
      { stepIndex: 0, ts: 33000 },
      { stepIndex: 0, ts: 36000 },
      { stepIndex: 0, ts: 39000 },
      { stepIndex: 1, ts: 70000 },
      { stepIndex: 1, ts: 73000 },
    ],
  }, [], {
    overallPass: true,
    hasBottleneck: false,
    maxStableChannelsExact: true,
    conclusion: 'ok',
  });

  assert.match(html, /约每 3s 采样一次/);
});

test('HTML rendering splits ramp-only bottleneck samples by observed channels', () => {
  const writer = new ReportWriter('.');
  const samples = Array.from({ length: 7 }, (_, index) => {
    const activeChannels = index + 1;
    return {
      stepIndex: 0,
      phase: 'ramp',
      targetChannels: 16,
      activeChannels,
      ts: index * 3000,
      channels: Array.from({ length: activeChannels }, (_, channelIndex) => ({
        taskKey: 'helmet-7463',
        taskDisplayName: '安全帽检测 7463',
        taskType: 'cv',
        algorithmId: '7463',
        channelId: `ch-${channelIndex + 1}`,
        measuredFps: 5,
        pipelineMinFps: 5,
        discardRate: 0,
        nodeDurationInfos: [
          { name: 'detector', durationAvgUs: 20_000 },
        ],
      })),
      hardware: {
        npuUtilization: { usedPercent: activeChannels === 7 ? 94 : 50 },
        cpuUtilization: { usedPercent: 20 },
        generalMemoryUtilization: { usedPercent: 40 },
      },
    };
  });
  const runResult = {
    scenarioName: 'helmet',
    profileMode: 'configured',
    videoMode: 'local',
    status: 'completed',
    tasks: [{ id: 'helmet-7463', type: 'cv', algorithmId: '7463', targetFps: 5 }],
    thresholds: { pass: { avgDiscardRate: 0.05 } },
    sampleIntervalSec: 3,
    steps: [{ index: 0, channels: 16, holdSec: 30 }],
    bottleneck: {
      stepIndex: 0,
      stepNumber: 1,
      channels: 16,
      reason: 'NPU >= 90%',
    },
    samples,
  };

  const stepSummaries = writer._summarizeSteps(runResult);
  assert.deepEqual(stepSummaries.map((s) => s.channels), [1, 2, 3, 4, 5, 6, 7]);

  const summary = writer._buildSummary(runResult, stepSummaries);
  assert.equal(summary.bottleneck.channels, 7);
  assert.equal(summary.bottleneck.targetChannels, 16);
  assert.equal(summary.maxStableChannels, null);
  assert.deepEqual(summary.rampProbeChannels, [1, 2, 3, 4, 5, 6, 7]);

  const html = writer._renderHtml(runResult, stepSummaries, summary);
  const routeTable = html.match(/<h2>路数结果<\/h2>[\s\S]*?<h2>媒体与预览分阶段指标<\/h2>/)[0];
  assert.equal((routeTable.match(/<tr>/g) ?? []).length - 1, 7);
  assert.match(routeTable, /<td>7<\/td>[\s\S]*<td class="warn">STOPPED<\/td>/);
  assert.match(routeTable, /<td class="na">PROBE<\/td>/);
  assert.doesNotMatch(routeTable, /<td>16<\/td>/);
});

test('HTML rendering expands single target step from observed channel samples', () => {
  const writer = new ReportWriter('.');
  const samples = [
    ...Array.from({ length: 16 }, (_, index) => sampleForChannels(index + 1, index * 3000, 'ramp')),
    ...Array.from({ length: 10 }, (_, index) => sampleForChannels(16, 48_000 + index * 3000, 'hold')),
  ];
  const runResult = {
    scenarioName: 'helmet',
    profileMode: 'configured',
    videoMode: 'local',
    status: 'completed',
    tasks: [{ id: 'helmet-99898', type: 'cv', algorithmId: '99898', targetFps: 5 }],
    thresholds: { pass: { avgDiscardRate: 0.05 } },
    sampleIntervalSec: 3,
    steps: [{ index: 0, channels: 16, holdSec: 30 }],
    baselineFps: 5.45,
    samples,
  };

  const stepSummaries = writer._summarizeSteps(runResult);
  assert.deepEqual(stepSummaries.map((s) => s.channels), Array.from({ length: 16 }, (_, i) => i + 1));
  assert.equal(stepSummaries[15].taskStats[0].bindingCount, 16);

  const summary = writer._buildSummary(runResult, stepSummaries);
  assert.equal(summary.baselineFps, 5);
  assert.equal(summary.maxStableChannels, null);
  assert.equal(summary.maxVerifiedPassedChannels, 16);
  assert.deepEqual(summary.rampProbeChannels, Array.from({ length: 15 }, (_, i) => i + 1));

  const html = writer._renderHtml(runResult, stepSummaries, summary);
  assert.match(html, /<td>16<\/td>/);
  assert.doesNotMatch(html, /samplePhase/);
});

function sampleForChannels(activeChannels, ts, phase) {
  return {
    stepIndex: 0,
    phase,
    targetChannels: 16,
    activeChannels,
    ts,
    channels: Array.from({ length: activeChannels }, (_, channelIndex) => ({
      taskKey: 'helmet-99898',
      taskDisplayName: 'helmet 99898',
      taskType: 'cv',
      algorithmId: '99898',
      channelId: `ch-${channelIndex + 1}`,
      measuredFps: 5,
      pipelineMinFps: 5,
      discardRate: 0,
      nodeDurationInfos: [
        { name: 'detector', durationAvgUs: 20_000 },
      ],
    })),
    hardware: {
      npuUtilization: { usedPercent: 50 },
      cpuUtilization: { usedPercent: 20 },
      generalMemoryUtilization: { usedPercent: 40 },
    },
  };
}

function holdSamples({ stepIndex, channels, count, startTs, fps }) {
  return Array.from({ length: count }, (_, sampleIndex) => ({
    stepIndex,
    phase: 'hold',
    activeChannels: channels,
    ts: startTs + sampleIndex * 3_000,
    channels: Array.from({ length: channels }, (_, channelIndex) => ({
      taskKey: 'cv',
      taskDisplayName: 'cv',
      taskType: 'cv',
      channelId: `ch-${channelIndex + 1}`,
      targetFps: 5,
      measuredFps: fps,
      discardRate: 0,
      nodeDurationInfos: [{ name: 'detector', durationAvgUs: 20_000 }],
    })),
    hardware: {},
  }));
}

function withCompleteHold(summary) {
  return {
    ...summary,
    holdWindow: {
      complete: true,
      configuredHoldSec: summary.holdSec ?? 60,
      sampleIntervalSec: 3,
      sampleCount: 20,
      expectedSampleCount: 20,
      observedSpanSec: 57,
      reasons: [],
    },
  };
}

function readyReadiness(index) {
  return {
    stepIndex: index,
    ready: true,
    status: 'ready',
    bindings: [{}],
  };
}
