import {
  evaluateTaskStat,
  latencyMetricsForNodes,
  resolveTaskThresholds,
  strategyForTaskType,
} from './task-strategies.js';

export function summarizeStep(step, samples, thresholds = {}, videoMode = 'local') {
  const allTicks = selectStepSamples(
    samples.filter((s) => s.stepIndex === (step.sampleStepIndex ?? step.index)),
    step,
  );
  const hasVlm = allTicks.some((tick) =>
    (tick.channels ?? []).some((channel) => strategyForTaskType(channel.taskType).id === 'vlm'));
  // VLM steps already have an explicit model-loading phase. Keep the complete
  // hold window so low-frequency completions are divided by the real observed
  // time instead of a shortened first-event-to-end interval.
  const ticks = hasVlm ? allTicks : allTicks.slice(Math.floor(allTicks.length / 2));
  if (!allTicks.length) {
    return {
      step,
      channels: step.channels,
      holdSec: step.holdSec,
      targetFps: null,
      minFpsAcross: null,
      maxDiscard: null,
      avgDiscard: null,
      detectorLatencyMs: null,
      criticalPathLatencyMs: null,
      channelStats: [],
      taskStats: [],
      perThreshold: [],
      pass: null,
      reasons: ['未执行，瓶颈提前停止'],
      skipped: true,
      qualified: step.qualified !== false,
    };
  }

  const byBinding = new Map();
  for (const tick of ticks) {
    for (const ch of tick.channels ?? []) {
      const taskKey = ch.taskKey ?? 'default';
      const key = `${taskKey}::${ch.channelId}`;
      if (!byBinding.has(key)) {
        byBinding.set(key, {
          taskKey,
          taskDisplayName: ch.taskDisplayName ?? taskKey,
          taskType: ch.taskType ?? 'cv',
          algorithmId: ch.algorithmId ?? null,
          channelId: ch.channelId,
          targetFps: ch.targetFps ?? null,
          fps: [],
          primaryTotals: [],
          pipelineMinFps: [],
          fpsRatio: [],
          discardRate: [],
          primaryLat: [],
          criticalLat: [],
          sampleCount: 0,
          missingSamples: 0,
        });
      }
      const stat = byBinding.get(key);
      stat.sampleCount++;
      if (ch.missing) {
        stat.missingSamples++;
        continue;
      }
      if (typeof ch.measuredFps === 'number') stat.fps.push(ch.measuredFps);
      if (typeof ch.primaryProcessTotal === 'number') {
        stat.primaryTotals.push({ ts: tick.ts, value: ch.primaryProcessTotal });
      }
      if (typeof ch.pipelineMinFps === 'number') stat.pipelineMinFps.push(ch.pipelineMinFps);
      if (typeof ch.fpsRatio === 'number') stat.fpsRatio.push(ch.fpsRatio);
      if (typeof ch.discardRate === 'number') stat.discardRate.push(ch.discardRate);

      const { primaryLatencyMs, criticalPathLatencyMs } = latencyMetricsForNodes(
        ch.nodeDurationInfos ?? [],
        stat.taskType,
      );
      if (ch.telemetryMissing
          || (strategyForTaskType(stat.taskType).id === 'vlm' && primaryLatencyMs == null)) {
        stat.missingSamples++;
      }
      if (primaryLatencyMs != null) stat.primaryLat.push(primaryLatencyMs);
      if (criticalPathLatencyMs != null) stat.criticalLat.push(criticalPathLatencyMs);
    }
  }

  const channelStats = [...byBinding.values()].map(summarizeBinding);

  const allThroughputFps = channelStats
    .flatMap((stat) => [stat.avgThroughputFps, stat.minThroughputFps])
    .filter((v) => v != null);
  const allDiscard = channelStats.map((stat) => stat.avgDiscardRate).filter((v) => v != null);
  const allPrimaryLat = channelStats.map((stat) => stat.avgPrimaryLatencyMs).filter((v) => v != null);
  const allCriticalLat = channelStats.map((stat) => stat.avgCriticalPathLatencyMs).filter((v) => v != null);

  const targetFpsValues = [...new Set(channelStats.map((stat) => stat.targetFps).filter((v) => v != null))];
  const targetFps = targetFpsValues.length <= 1 ? (targetFpsValues[0] ?? null) : targetFpsValues.join(' / ');
  const minFpsAcross = allThroughputFps.length ? Math.min(...allThroughputFps) : null;
  const currentVlmStats = selectCurrentVlmStats(step, ticks, channelStats);
  const currentRouteChannelId = currentVlmStats.at(-1)?.channelId ?? null;
  const currentRouteTaskKey = currentVlmStats.at(-1)?.taskKey ?? null;
  const currentRouteValues = currentVlmStats
    .map((stat) => stat.minThroughputFps)
    .filter((value) => value != null);
  const currentRouteFps = currentRouteValues.length ? Math.min(...currentRouteValues) : null;
  const maxDiscard = allDiscard.length ? Math.max(...allDiscard) : null;
  const avgDiscard = allDiscard.length ? round(mean(allDiscard), 4) : null;
  const maxPrimaryLat = allPrimaryLat.length ? Math.max(...allPrimaryLat) : null;
  const maxCriticalLat = allCriticalLat.length ? Math.max(...allCriticalLat) : null;

  const pktDiscard = ticks
    .map((tick) => tick.hardware?.packetDiscardUtilization?.usedPercent ?? null)
    .filter((v) => v != null);
  const maxPktDiscard = pktDiscard.length ? Math.max(...pktDiscard) / 100 : null;

  const steady = ticks.slice(Math.floor(ticks.length / 2));
  const peak = (selector) => {
    const values = steady.map(selector).filter((v) => typeof v === 'number');
    return values.length ? Math.max(...values) : null;
  };
  const maxNpu = peak((tick) => tick.hardware?.npuUtilization?.usedPercent);
  const maxAcceleratorMem = peak((tick) => {
    const memoryKeys = [
      'specialMemoryUtilization',
      'modelMemoryUtilization',
      'pictureMemoryUtilization',
      'TPPMemoryUtilization',
    ];
    const values = memoryKeys
      .map((key) => tick.hardware?.[key]?.usedPercent)
      .filter((value) => typeof value === 'number');
    return values.length ? Math.max(...values) : null;
  });
  const maxCpu = peak((tick) => tick.hardware?.cpuUtilization?.usedPercent);
  const maxMem = peak((tick) => tick.hardware?.generalMemoryUtilization?.usedPercent);
  const maxDiskUsedPercent = peak((tick) => tick.hardware?.eMMCUtilization?.usedPercent);
  const maxPoolAllocatedBytes = peak((tick) => tick.hardware?.memoryPool?.totalAllocatedBytes);
  const maxPoolInUseBytes = peak((tick) => tick.hardware?.memoryPool?.totalInUseBytes);
  const maxPoolUtilizationPercent = peak((tick) => tick.hardware?.memoryPool?.utilizationPercent);
  const taskStats = summarizeTasks(channelStats);
  const mediaStages = summarizeMediaStages(ticks, maxPrimaryLat);

  const perThreshold = [];
  const overall = { pass: true, reasons: [] };
  for (const stat of taskStats) {
    const verdict = evaluateTaskStat(stat, thresholds);
    perThreshold.push(...verdict.checks);
    if (!verdict.pass) {
      overall.pass = false;
      overall.reasons.push(...verdict.reasons);
    }
  }
  const diskLimit = thresholds.pass?.maxDiskUsedPercent;
  if (diskLimit != null) {
    const ok = maxDiskUsedPercent == null || maxDiskUsedPercent <= diskLimit;
    perThreshold.push({
      taskKey: '*',
      taskDisplayName: 'device',
      taskType: 'system',
      strategy: 'system',
      name: 'maxDiskUsedPercent',
      threshold: diskLimit,
      actual: maxDiskUsedPercent,
      result: maxDiskUsedPercent == null ? 'N/A' : (ok ? 'PASS' : 'FAIL'),
    });
    if (!ok) {
      overall.pass = false;
      overall.reasons.push(`设备磁盘使用率 ${maxDiskUsedPercent}%，阈值 ${diskLimit}%`);
    }
  }
  if (videoMode !== 'local') {
    const pass = thresholds.pass ?? {};
    const limit = pass.maxPacketDiscardRate;
    const ok = maxPktDiscard == null || limit == null || maxPktDiscard <= limit;
    perThreshold.push({
      taskKey: '*',
      taskDisplayName: 'network',
      taskType: 'input',
      strategy: 'input',
      name: 'maxPacketDiscardRate',
      threshold: limit,
      actual: maxPktDiscard,
      result: maxPktDiscard == null || limit == null ? 'N/A' : (ok ? 'PASS' : 'FAIL'),
    });
    if (!ok) {
      overall.pass = false;
      overall.reasons.push(`网络丢包率 ${formatPercent(maxPktDiscard)}，阈值 ${formatPercent(limit)}`);
    }
  }

  return {
    step,
    channels: step.channels,
    holdSec: step.holdSec,
    targetFps,
    minFpsAcross,
    currentRouteChannelId,
    currentRouteTaskKey,
    currentRouteFps,
    maxDiscard,
    avgDiscard,
    detectorLatencyMs: maxPrimaryLat,
    primaryLatencyMs: maxPrimaryLat,
    criticalPathLatencyMs: maxCriticalLat,
    maxNpu,
    maxAcceleratorMem,
    maxCpu,
    maxMem,
    maxDiskUsedPercent,
    maxPacketDiscardRate: maxPktDiscard,
    maxPoolAllocatedBytes,
    maxPoolInUseBytes,
    maxPoolUtilizationPercent,
    mediaStages,
    channelStats,
    taskStats,
    perThreshold,
    pass: overall.pass,
    reasons: overall.reasons,
    qualified: step.qualified !== false,
  };
}

function selectCurrentVlmStats(step, ticks, channelStats) {
  const vlmStats = channelStats.filter(
    (stat) => strategyForTaskType(stat.taskType).id === 'vlm',
  );
  if (!vlmStats.length) return [];

  const byBinding = new Map(vlmStats.map((stat) => [bindingKey(stat), stat]));
  const requested = Array.isArray(step.currentVlmBindings) ? step.currentVlmBindings : [];
  const selected = requested
    .map((binding) => byBinding.get(bindingKey(binding)))
    .filter(Boolean);
  if (selected.length) return [...new Map(selected.map((stat) => [bindingKey(stat), stat])).values()];

  const latestVlmChannel = [...(ticks.at(-1)?.channels ?? [])].reverse().find(
    (channel) => strategyForTaskType(channel.taskType).id === 'vlm',
  );
  const latest = latestVlmChannel ? byBinding.get(bindingKey(latestVlmChannel)) : null;
  return latest ? [latest] : [vlmStats.at(-1)];
}

function bindingKey(binding) {
  return `${binding?.taskKey ?? 'default'}::${binding?.channelId ?? ''}`;
}

function summarizeMediaStages(ticks, inferMs) {
  const first = ticks.find((tick) => tick.hardware?.accelerator)?.hardware?.accelerator;
  const last = [...ticks].reverse().find((tick) => tick.hardware?.accelerator)?.hardware?.accelerator;
  const counterAverage = (totalKey, countKey) => {
    if (!first || !last) return null;
    const count = Number(last[countKey]) - Number(first[countKey]);
    const total = Number(last[totalKey]) - Number(first[totalKey]);
    return count > 0 && total >= 0 ? round(total / count, 3) : null;
  };
  const counterDelta = (key) => {
    if (!first || !last) return null;
    const delta = Number(last[key]) - Number(first[key]);
    return Number.isFinite(delta) && delta >= 0 ? delta : null;
  };
  const maxValue = (selector) => {
    const values = ticks.map(selector).filter((value) => Number.isFinite(value));
    return values.length ? Math.max(...values) : null;
  };
  const maxNonNegative = (selector) => {
    const value = maxValue(selector);
    return value != null && value >= 0 ? value : null;
  };
  const nodeStages = summarizeNodeStages(ticks);

  return {
    preprocessAvgMs: nodeStages.preprocess,
    inferAvgMs: inferMs,
    postprocessAvgMs: nodeStages.postprocess,
    colorConvertAvgMs: counterAverage('colorConvertMs', 'colorConvertFrames'),
    blobConvertAvgMs: counterAverage('blobConvertMs', 'blobConvertFrames'),
    graphForwardAvgMs: counterAverage('graphForwardMs', 'graphForwardFrames'),
    resultParseAvgMs: counterAverage('resultParseMs', 'resultParseFrames'),
    graphForwardFailures: counterDelta('graphForwardFailures'),
    resultParseFailures: counterDelta('resultParseFailures'),
    rknnPrepareAvgMs: counterAverage('rknnPrepareMs', 'rknnPrepareCalls'),
    rknnInputsSetAvgMs: counterAverage('rknnInputsSetMs', 'rknnInputsSetCalls'),
    rknnRunAvgMs: counterAverage('rknnRunMs', 'rknnRunCalls'),
    rknnOutputsGetAvgMs: counterAverage('rknnOutputsGetMs', 'rknnOutputsGetCalls'),
    rknnOutputsReleaseAvgMs: counterAverage(
      'rknnOutputsReleaseMs',
      'rknnOutputsReleaseCalls',
    ),
    rknnOutputTransformAvgMs: counterAverage(
      'rknnOutputTransformMs',
      'rknnOutputTransformCalls',
    ),
    rknnForwardAvgMs: counterAverage('rknnForwardMs', 'rknnForwards'),
    rknnForwardFailures: counterDelta('rknnForwardFailures'),
    rknnMutexWaitAvgMs: counterAverage('rknnMutexWaitMs', 'rknnMutexWaitCalls'),
    rknnDetectorPrepareAvgMs: counterAverage(
      'rknnDetectorPrepareMs',
      'rknnDetectorPrepareCalls',
    ),
    rknnDetectorInputsSetAvgMs: counterAverage(
      'rknnDetectorInputsSetMs',
      'rknnDetectorInputsSetCalls',
    ),
    rknnDetectorRunAvgMs: counterAverage('rknnDetectorRunMs', 'rknnDetectorRunCalls'),
    rknnDetectorOutputsGetAvgMs: counterAverage(
      'rknnDetectorOutputsGetMs',
      'rknnDetectorOutputsGetCalls',
    ),
    rknnDetectorOutputsReleaseAvgMs: counterAverage(
      'rknnDetectorOutputsReleaseMs',
      'rknnDetectorOutputsReleaseCalls',
    ),
    rknnDetectorOutputTransformAvgMs: counterAverage(
      'rknnDetectorOutputTransformMs',
      'rknnDetectorOutputTransformCalls',
    ),
    rknnDetectorForwardAvgMs: counterAverage(
      'rknnDetectorForwardMs',
      'rknnDetectorForwards',
    ),
    rknnDetectorForwardFailures: counterDelta('rknnDetectorForwardFailures'),
    rknnDetectorMutexWaitAvgMs: counterAverage(
      'rknnDetectorMutexWaitMs',
      'rknnDetectorMutexWaitCalls',
    ),
    rknnPreprocessFastHits: counterDelta('rknnPreprocessFastHits'),
    rknnRgaFillAvgMs: counterAverage('rknnRgaFillMs', 'rknnRgaFillCalls'),
    rknnRgaResizeColorAvgMs: counterAverage(
      'rknnRgaResizeColorMs',
      'rknnRgaResizeColorCalls',
    ),
    rknnRgaCropResizeAvgMs: counterAverage(
      'rknnRgaCropResizeMs',
      'rknnRgaCropResizeCalls',
    ),
    rknnRgaCropResizeCalls: counterDelta('rknnRgaCropResizeCalls'),
    rknnRgaCropResizeFailures: counterDelta('rknnRgaCropResizeFailures'),
    rknnRgaCropDmaBufFrames: counterDelta('rknnRgaCropDmaBufFrames'),
    rknnRgaCropHostFallbacks: counterDelta('rknnRgaCropHostFallbacks'),
    rknnRgaFailures: counterDelta('rknnRgaFailures'),
    rknnCpuResizeFallbackAvgMs: counterAverage(
      'rknnCpuResizeFallbackMs',
      'rknnCpuResizeFallbackCalls',
    ),
    rknnCpuResizeFallbacks: counterDelta('rknnCpuResizeFallbackCalls'),
    rknnCpuCropResizeFallbackAvgMs: counterAverage(
      'rknnCpuCropResizeFallbackMs',
      'rknnCpuCropResizeFallbackCalls',
    ),
    rknnCpuCropResizeFallbacks: counterDelta('rknnCpuCropResizeFallbackCalls'),
    rknnCpuNormalizeFallbackAvgMs: counterAverage(
      'rknnCpuNormalizeFallbackMs',
      'rknnCpuNormalizeFallbackCalls',
    ),
    rknnCpuNormalizeFallbacks: counterDelta('rknnCpuNormalizeFallbackCalls'),
    rknnNativeInputMapAvgMs: counterAverage(
      'rknnNativeInputMapMs',
      'rknnNativeInputMapCalls',
    ),
    rknnNativeInt8Inputs: counterDelta('rknnNativeInt8Inputs'),
    rknnFloatInputs: counterDelta('rknnFloatInputs'),
    rknnInputCompatibilityFallbacks: counterDelta('rknnInputCompatibilityFallbacks'),
    rknnBoundInputBindAttempts: counterDelta('rknnBoundInputBindAttempts'),
    rknnBoundInputBindFailures: counterDelta('rknnBoundInputBindFailures'),
    rknnBoundInputCopyAvgMs: counterAverage(
      'rknnBoundInputCopyMs',
      'rknnBoundInputCopyCalls',
    ),
    rknnBoundInputCopyAvgBytes: counterAverage(
      'rknnBoundInputCopyBytes',
      'rknnBoundInputCopyCalls',
    ),
    rknnBoundInputCopyFailures: counterDelta('rknnBoundInputCopyFailures'),
    rknnBoundInputSyncAvgMs: counterAverage(
      'rknnBoundInputSyncMs',
      'rknnBoundInputSyncCalls',
    ),
    rknnBoundInputSyncFailures: counterDelta('rknnBoundInputSyncFailures'),
    rknnBoundInputFrames: counterDelta('rknnBoundInputFrames'),
    rknnRgaBoundInputBindAttempts: counterDelta('rknnRgaBoundInputBindAttempts'),
    rknnRgaBoundInputBindFailures: counterDelta('rknnRgaBoundInputBindFailures'),
    rknnRgaBoundInputImportCalls: counterDelta('rknnRgaBoundInputImportCalls'),
    rknnRgaBoundInputImportAvgMs: counterAverage(
      'rknnRgaBoundInputImportMs',
      'rknnRgaBoundInputImportCalls',
    ),
    rknnRgaBoundInputImportFailures: counterDelta('rknnRgaBoundInputImportFailures'),
    rknnRgaBoundInputFrames: counterDelta('rknnRgaBoundInputFrames'),
    rknnRgaBoundUint8Frames: counterDelta('rknnRgaBoundUint8Frames'),
    rknnRgaBoundNativeInt8Frames: counterDelta('rknnRgaBoundNativeInt8Frames'),
    rknnRgaBoundRequantizeCalls: counterDelta('rknnRgaBoundRequantizeCalls'),
    rknnRgaBoundRequantizeAvgMs: counterAverage(
      'rknnRgaBoundRequantizeMs',
      'rknnRgaBoundRequantizeCalls',
    ),
    rknnRgaBoundRequantizeFailures: counterDelta('rknnRgaBoundRequantizeFailures'),
    rknnRgaBoundInputNormalizeBypasses: counterDelta(
      'rknnRgaBoundInputNormalizeBypasses',
    ),
    rknnMppDmaBufImportCalls: counterDelta('rknnMppDmaBufImportCalls'),
    rknnMppDmaBufImportAvgMs: counterAverage(
      'rknnMppDmaBufImportMs',
      'rknnMppDmaBufImportCalls',
    ),
    rknnMppDmaBufImportFailures: counterDelta('rknnMppDmaBufImportFailures'),
    rknnMppDmaBufFrames: counterDelta('rknnMppDmaBufFrames'),
    rknnMppDmaBufFallbacks: counterDelta('rknnMppDmaBufFallbacks'),
    rknnMppDmaBufSourceAvgBytes: counterAverage(
      'rknnMppDmaBufSourceBytes',
      'rknnMppDmaBufFrames',
    ),
    rknnNativeInt8Outputs: counterDelta('rknnNativeInt8Outputs'),
    rknnFloatOutputs: counterDelta('rknnFloatOutputs'),
    rknnOutputCompatibilityFallbacks: counterDelta('rknnOutputCompatibilityFallbacks'),
    rknnNativeOutputAvgBytes: counterAverage(
      'rknnNativeOutputBytes',
      'rknnNativeInt8Outputs',
    ),
    rknnFloatOutputAvgBytes: counterAverage('rknnFloatOutputBytes', 'rknnFloatOutputs'),
    rknnYolov8DflAvgMs: counterAverage('rknnYolov8DflMs', 'rknnYolov8DflCalls'),
    rknnYolov8ClassAvgMs: counterAverage('rknnYolov8ClassMs', 'rknnYolov8ClassCalls'),
    rknnYolov8DirectCandidateCalls: counterDelta('rknnYolov8DirectCandidateCalls'),
    rknnYolov8DirectCandidateFailures: counterDelta('rknnYolov8DirectCandidateFailures'),
    rknnYolov8DirectAvgPointsScanned: counterAverage(
      'rknnYolov8DirectPointsScanned',
      'rknnYolov8DirectCandidateCalls',
    ),
    rknnYolov8DirectAvgPointsDecoded: counterAverage(
      'rknnYolov8DirectPointsDecoded',
      'rknnYolov8DirectCandidateCalls',
    ),
    rknnYolov8ScoreSumAvgPointsRejected: counterAverage(
      'rknnYolov8ScoreSumPointsRejected',
      'rknnYolov8DirectCandidateCalls',
    ),
    rknnYolov8LogicalFloatBytesAvoided: counterDelta(
      'rknnYolov8LogicalFloatBytesAvoided',
    ),
    yolov8PostprocessAvgMs: counterAverage('yolov8PostprocessMs', 'yolov8PostprocessCalls'),
    yolov8NmsAvgMs: counterAverage('yolov8NmsMs', 'yolov8NmsCalls'),
    rgaAvgMs: counterAverage('rgaMs', 'rgaFrames'),
    rgaFailures: counterDelta('rgaFailures'),
    mppEncodeAvgMs: counterAverage('mppEncodeMs', 'mppEncodedFrames'),
    mppEncodeFailures: counterDelta('mppEncodeFailures'),
    mppDecodeAvgMs: counterAverage('mppDecodeMs', 'mppDecodedFrames'),
    mppDecodedFrames: counterDelta('mppDecodedFrames'),
    mppDecodeFailures: counterDelta('mppDecodeFailures'),
    mppDecodeFallbacks: counterDelta('mppDecodeFallbacks'),
    mppCopyOutAvgMs: counterAverage('mppCopyOutMs', 'mppCopyOutFrames'),
    mppCopyOutFrames: counterDelta('mppCopyOutFrames'),
    mppCopyOutFailures: counterDelta('mppCopyOutFailures'),
    mppRgaCopyOutFrames: counterDelta('mppRgaCopyOutFrames'),
    mppRgaCopyOutFailures: counterDelta('mppRgaCopyOutFailures'),
    mppCpuCopyOutFallbacks: counterDelta('mppCpuCopyOutFallbacks'),
    mppRgaCopyInFrames: counterDelta('mppRgaCopyInFrames'),
    mppRgaCopyInFailures: counterDelta('mppRgaCopyInFailures'),
    mppCpuCopyInFallbacks: counterDelta('mppCpuCopyInFallbacks'),
    mppEarlyDroppedFrames: counterDelta('mppEarlyDroppedFrames'),
    osdAvgMs: counterAverage('osdMs', 'osdFrames'),
    publishAvgMs: counterAverage('publishMs', 'publishedFrames'),
    firstFrameAvgMs: counterAverage('firstFrameMs', 'firstFrames'),
    firstFrameMaxMs: maxNonNegative(
      (tick) => Number(tick.hardware?.accelerator?.firstFrameMaxMs),
    ),
    osdFrames: counterDelta('osdFrames'),
    publishedFrames: counterDelta('publishedFrames'),
    firstFrames: counterDelta('firstFrames'),
    activePreviewStreamsPeak: maxValue(
      (tick) => Number(tick.hardware?.accelerator?.activePreviewStreams),
    ),
    activePreviewPublishersPeak: maxValue(
      (tick) => Number(tick.hardware?.accelerator?.activePreviewPublishers),
    ),
    activeRawPreviewStreamsPeak: maxValue(
      (tick) => Number(tick.hardware?.accelerator?.activeRawPreviewStreams),
    ),
    activeAlgorithmPreviewStreamsPeak: maxValue(
      (tick) => Number(tick.hardware?.accelerator?.activeAlgorithmPreviewStreams),
    ),
    srsStreamsPeak: maxValue((tick) => Number(tick.preview?.srsStreams)),
    srsClientsPeak: maxValue((tick) => Number(tick.preview?.srsClients)),
    previewStartsDelta: counterDelta('previewStreamStarts'),
    previewStopsDelta: counterDelta('previewStreamStops'),
    previewFailuresDelta: counterDelta('previewStreamFailures'),
  };
}

function summarizeNodeStages(ticks) {
  const stages = { preprocess: [], postprocess: [] };
  for (const tick of ticks) {
    for (const channel of tick.channels ?? []) {
      for (const node of channel.nodeDurationInfos ?? []) {
        const name = String(node.name ?? '').toLowerCase();
        const value = Number(node.durationAvgUs) / 1000;
        if (!Number.isFinite(value)) continue;
        if (/preprocess|resize|normaliz|color|padding|letterbox|crop/.test(name)) {
          stages.preprocess.push(value);
        } else if (/postprocess|tracker|tracking|nms|filter|judge|classifier/.test(name)) {
          stages.postprocess.push(value);
        }
      }
    }
  }
  return {
    preprocess: stages.preprocess.length ? round(mean(stages.preprocess), 3) : null,
    postprocess: stages.postprocess.length ? round(mean(stages.postprocess), 3) : null,
  };
}

export function runtimeStepDecision(summary, {
  thresholds = {},
  baselineByTask = {},
  fpsHalveRatio = 0.5,
  discardBottleneck = 0.05,
} = {}) {
  const reasons = [];
  const gates = [];
  const seenGates = new Set();
  const addGate = (gate, reason) => {
    const key = [gate.scope, gate.taskKey, gate.name, gate.actual, gate.threshold].join('::');
    if (seenGates.has(key)) return;
    seenGates.add(key);
    gates.push(gate);
    reasons.push(reason);
  };

  for (const stat of summary.taskStats ?? []) {
    const strategy = strategyForTaskType(stat.taskType);
    const taskThresholds = resolveTaskThresholds(thresholds, {
      taskKey: stat.taskKey,
      taskType: stat.taskType,
    });
    const minFps = stat.minThroughputFps;
    if (strategy.useBaselineFpsFuse) {
      const taskBaseline = baselineByTask[stat.taskKey];
      if (taskBaseline != null && minFps != null && minFps < taskBaseline * fpsHalveRatio) {
        addGate({
          scope: 'task',
          taskKey: stat.taskKey,
          taskType: stat.taskType,
          name: 'baselineFpsFuse',
          actual: minFps,
          threshold: taskBaseline * fpsHalveRatio,
        }, `${stat.taskKey} fps ${minFps.toFixed(1)} < baseline ${taskBaseline.toFixed(1)}*${fpsHalveRatio} (${(taskBaseline * fpsHalveRatio).toFixed(1)})`);
      }
    }

    // Reuse the report evaluator so every task-local threshold that can be
    // decided from the completed hold window is also an execution gate. This
    // includes VLM missing telemetry and configured latency limits, not just
    // the FPS ratio.
    const verdict = evaluateTaskStat(stat, thresholds);
    for (const check of verdict.checks.filter((item) => item.result === 'FAIL')) {
      addGate({
        scope: 'task',
        taskKey: stat.taskKey,
        taskType: stat.taskType,
        name: check.name,
        actual: check.actual,
        threshold: check.threshold,
      }, runtimeThresholdReason(stat.taskKey, check));
    }

    // Keep the historical 5% online discard fuse when a scenario did not
    // configure a report discard gate. Configured gates are already covered by
    // evaluateTaskStat above.
    const configuredDiscardLimit = taskThresholds.avgDiscardRate ?? taskThresholds.maxDiscardRate;
    if (configuredDiscardLimit == null
        && stat.avgDiscardRate != null
        && stat.avgDiscardRate > discardBottleneck) {
      addGate({
        scope: 'task',
        taskKey: stat.taskKey,
        taskType: stat.taskType,
        name: 'runtimeDiscardFuse',
        actual: stat.avgDiscardRate,
        threshold: discardBottleneck,
      }, `${stat.taskKey} meanDiscard ${stat.avgDiscardRate.toFixed(3)} > ${discardBottleneck}`);
    }
  }

  // Device- and input-scoped checks (for example disk and packet discard)
  // are already materialized by summarizeStep in perThreshold.
  for (const check of summary.perThreshold ?? []) {
    if (check.result !== 'FAIL' || check.taskKey !== '*') continue;
    addGate({
      scope: check.strategy ?? 'system',
      taskKey: check.taskKey,
      taskType: check.taskType,
      name: check.name,
      actual: check.actual,
      threshold: check.threshold,
    }, runtimeThresholdReason(check.taskKey, check));
  }

  if (summary.avgDiscard != null && summary.avgDiscard > discardBottleneck) {
    addGate({
      scope: 'aggregate',
      taskKey: '*',
      taskType: 'aggregate',
      name: 'runtimeDiscardFuse',
      actual: summary.avgDiscard,
      threshold: discardBottleneck,
    }, `meanDiscard ${summary.avgDiscard.toFixed(3)} > ${discardBottleneck}`);
  }
  return reasons.length
    ? { stop: true, source: 'runtime-threshold', reason: reasons.join('; '), reasons, gates }
    : { stop: false, source: 'runtime-threshold', reasons: [], gates: [] };
}

function runtimeThresholdReason(taskKey, check) {
  const actual = Number(check.actual);
  const threshold = Number(check.threshold);
  const actualFixed = Number.isFinite(actual) ? actual : check.actual;
  const thresholdFixed = Number.isFinite(threshold) ? threshold : check.threshold;
  switch (check.name) {
    case 'minFpsRatio':
      return `${taskKey} fpsRatio ${actual.toFixed(3)} < ${thresholdFixed}`;
    case 'minThroughputFps':
      return `${taskKey} fps ${actual.toFixed(2)} < ${thresholdFixed}`;
    case 'maxMissingRate':
      return `${taskKey} missingRate ${actual.toFixed(3)} > ${thresholdFixed}`;
    case 'avgDiscardRate':
    case 'maxDiscardRate':
      return `${taskKey} meanDiscard ${actual.toFixed(3)} > ${thresholdFixed}`;
    case 'maxDiskUsedPercent':
      return `disk ${actualFixed}% > ${thresholdFixed}%`;
    case 'maxPacketDiscardRate':
      return `packetDiscard ${actual.toFixed(3)} > ${thresholdFixed}`;
    default:
      return `${taskKey} ${check.name} ${actualFixed} > ${thresholdFixed}`;
  }
}

function summarizeBinding(stat) {
  const isVlm = strategyForTaskType(stat.taskType).id === 'vlm';
  const windowThroughputFps = isVlm ? counterWindowFps(stat.primaryTotals) : null;
  const avgThroughputFps = windowThroughputFps ?? (stat.fps.length ? mean(stat.fps) : null);
  const minThroughputFps = windowThroughputFps ?? (stat.fps.length ? Math.min(...stat.fps) : null);
  const minFpsRatio = windowThroughputFps != null && stat.targetFps != null && stat.targetFps > 0
    ? windowThroughputFps / stat.targetFps
    : (stat.fpsRatio.length ? Math.min(...stat.fpsRatio) : null);

  return {
    taskKey: stat.taskKey,
    taskDisplayName: stat.taskDisplayName,
    taskType: stat.taskType,
    algorithmId: stat.algorithmId,
    channelId: stat.channelId,
    targetFps: stat.targetFps,
    sampleCount: stat.sampleCount,
    missingSamples: stat.missingSamples,
    missingRate: stat.sampleCount ? round(stat.missingSamples / stat.sampleCount, 4) : null,
    avgThroughputFps: avgThroughputFps != null ? round(avgThroughputFps, 2) : null,
    minThroughputFps: minThroughputFps != null ? round(minThroughputFps, 2) : null,
    avgDetectorFps: avgThroughputFps != null ? round(avgThroughputFps, 2) : null,
    minDetectorFps: minThroughputFps != null ? round(minThroughputFps, 2) : null,
    windowThroughputFps: windowThroughputFps != null ? round(windowThroughputFps, 2) : null,
    minFpsRatio: minFpsRatio != null ? round(minFpsRatio, 3) : null,
    minPipelineFps: stat.pipelineMinFps.length ? round(Math.min(...stat.pipelineMinFps), 2) : null,
    avgDiscardRate: stat.discardRate.length ? round(mean(stat.discardRate), 4) : null,
    avgPrimaryLatencyMs: stat.primaryLat.length ? round(mean(stat.primaryLat), 1) : null,
    avgDetectorLatencyMs: stat.primaryLat.length ? round(mean(stat.primaryLat), 1) : null,
    avgCriticalPathLatencyMs: stat.criticalLat.length ? round(mean(stat.criticalLat), 1) : null,
  };
}

function selectStepSamples(samples, step) {
  let out = samples;

  if (Number.isInteger(step.sampleChannels)) {
    out = out.filter((s) => Number(s.activeChannels) === step.sampleChannels);
  }

  const hold = out.filter((s) => s.phase !== 'ramp');
  if (hold.length) out = hold;

  return out;
}

function counterWindowFps(samples) {
  if (!Array.isArray(samples) || samples.length < 2) return null;
  const first = samples[0];
  const last = samples[samples.length - 1];
  if (last.ts <= first.ts || last.value < first.value) return null;
  return ((last.value - first.value) * 1000) / (last.ts - first.ts);
}

function summarizeTasks(channelStats) {
  const byTask = new Map();
  for (const stat of channelStats) {
    const key = stat.taskKey ?? 'default';
    if (!byTask.has(key)) {
      byTask.set(key, {
        taskKey: key,
        taskDisplayName: stat.taskDisplayName ?? key,
        taskType: stat.taskType ?? 'cv',
        strategy: strategyForTaskType(stat.taskType).id,
        algorithmId: stat.algorithmId ?? null,
        targetFps: stat.targetFps ?? null,
        fps: [],
        fpsRatio: [],
        missing: [],
        discard: [],
        primaryLat: [],
        criticalLat: [],
        bindingCount: 0,
      });
    }
    const task = byTask.get(key);
    task.bindingCount++;
    if (stat.minThroughputFps != null) task.fps.push(stat.minThroughputFps);
    if (stat.minFpsRatio != null) task.fpsRatio.push(stat.minFpsRatio);
    if (stat.missingRate != null) task.missing.push(stat.missingRate);
    if (stat.avgDiscardRate != null) task.discard.push(stat.avgDiscardRate);
    if (stat.avgPrimaryLatencyMs != null) task.primaryLat.push(stat.avgPrimaryLatencyMs);
    if (stat.avgCriticalPathLatencyMs != null) task.criticalLat.push(stat.avgCriticalPathLatencyMs);
  }

  return [...byTask.values()].map((task) => ({
    taskKey: task.taskKey,
    taskDisplayName: task.taskDisplayName,
    taskType: task.taskType,
    strategy: task.strategy,
    algorithmId: task.algorithmId,
    targetFps: task.targetFps,
    bindingCount: task.bindingCount,
    minThroughputFps: task.fps.length ? round(Math.min(...task.fps), 2) : null,
    minDetectorFps: task.fps.length ? round(Math.min(...task.fps), 2) : null,
    minFpsRatio: task.fpsRatio.length ? round(Math.min(...task.fpsRatio), 3) : null,
    avgMissingRate: task.missing.length ? round(mean(task.missing), 4) : null,
    maxMissingRate: task.missing.length ? round(Math.max(...task.missing), 4) : null,
    avgDiscardRate: task.discard.length ? round(mean(task.discard), 4) : null,
    maxPrimaryLatencyMs: task.primaryLat.length ? round(Math.max(...task.primaryLat), 1) : null,
    maxDetectorLatencyMs: task.primaryLat.length ? round(Math.max(...task.primaryLat), 1) : null,
    maxCriticalPathLatencyMs: task.criticalLat.length ? round(Math.max(...task.criticalLat), 1) : null,
  }));
}

function mean(arr) {
  return arr.reduce((a, b) => a + b, 0) / arr.length;
}

function formatPercent(v) {
  return `${round(Number(v) * 100, 2)}%`;
}

function round(v, digits) {
  const f = 10 ** digits;
  return Math.round(v * f) / f;
}
