// Emit metrics.json, summary.json, and a self-contained HTML report.

import fs from 'node:fs';
import path from 'node:path';
import { summarizeStep } from './step-evaluator.js';
import {
  resolveTaskThresholds,
  strategyForTask,
  strategyForTaskType,
  thresholdLabel,
} from './task-strategies.js';

export class ReportWriter {
  constructor(outputDir, { partialWriteIntervalMs = 30_000, now = () => Date.now() } = {}) {
    this.outputDir = path.resolve(outputDir);
    this.partialWriteIntervalMs = partialWriteIntervalMs;
    this.now = now;
    this.lastPartialWriteAt = null;
  }

  async write(runResult) {
    fs.mkdirSync(this.outputDir, { recursive: true });

    const jsonPath = path.join(this.outputDir, 'metrics.json');
    fs.writeFileSync(jsonPath, JSON.stringify(runResult, null, 2), 'utf8');

    const stepSummaries = this._summarizeSteps(runResult);
    const summary = this._buildSummary(runResult, stepSummaries);
    const summaryPath = path.join(this.outputDir, 'summary.json');
    fs.writeFileSync(summaryPath, JSON.stringify(summary, null, 2), 'utf8');

    const html = this._renderHtml(runResult, stepSummaries, summary);
    const htmlPath = path.join(this.outputDir, 'report.html');
    fs.writeFileSync(htmlPath, html, 'utf8');

    return { jsonPath, summaryPath, htmlPath };
  }

  async writePartial(runResult, { force = false } = {}) {
    const now = this.now();
    if (!force && this.lastPartialWriteAt != null
        && now - this.lastPartialWriteAt < this.partialWriteIntervalMs) {
      return { jsonPath: null, skipped: true };
    }
    fs.mkdirSync(this.outputDir, { recursive: true });
    const jsonPath = path.join(this.outputDir, 'metrics.partial.json');
    const temporaryPath = `${jsonPath}.tmp`;
    fs.writeFileSync(temporaryPath, JSON.stringify(runResult, null, 2), 'utf8');
    fs.renameSync(temporaryPath, jsonPath);
    this.lastPartialWriteAt = now;
    return { jsonPath, skipped: false };
  }

  _summarizeSteps(r) {
    const samples = r.samples ?? [];
    const thresholds = r.thresholds ?? {};
    const videoMode = r.videoMode ?? 'local';
    const sampleIntervalSec = resolveSampleIntervalSec(r, samples);
    return buildReportSteps(r).map((st) => {
      const summary = this._summarizeStep(st, samples, thresholds, videoMode);
      return {
        ...summary,
        holdWindow: evaluateHoldWindow(st, samples, sampleIntervalSec),
      };
    });
  }

  _summarizeStep(step, samples, thresholds, videoMode) {
    return summarizeStep(step, samples, thresholds, videoMode);
  }

  _buildSummary(r, stepSummaries) {
    const ran = stepSummaries.filter((s) => !s.skipped);
    const qualifiedRan = ran
      .filter((s) => s.qualified !== false)
      .sort((a, b) => a.channels - b.channels);
    const qualifiedWithEvidence = qualifiedRan.map((summary) => ({
      ...summary,
      executionEvidence: assessStepExecutionEvidence(r, summary),
    }));
    const firstFailed = qualifiedWithEvidence.find((s) => s.pass === false) ?? null;
    const runtimeBottleneck = normalizeBottleneck(r.bottleneck, stepSummaries);
    // A report-time FAIL is not evidence that the runner stopped. Keep the raw
    // runtime bottleneck separate instead of synthesizing one from a summary.
    const bottleneck = runtimeBottleneck;
    const hasBottleneck = Boolean(bottleneck);
    const configuredVerifiedPassed = qualifiedWithEvidence.filter((s) =>
      s.pass && s.executionEvidence.complete
        && (!hasBottleneck || s.step.index < bottleneck.stepIndex),
    );
    const maxConfiguredVerifiedPassedChannels = configuredVerifiedPassed.length
      ? Math.max(...configuredVerifiedPassed.map((s) => s.channels))
      : null;
    const passingPrefix = continuousPassingPrefix(qualifiedWithEvidence);
    const maxContiguousPassedChannels = passingPrefix.length
      ? passingPrefix[passingPrefix.length - 1].channels
      : null;
    const continuousProfile = isContinuousChannelProfile(qualifiedWithEvidence);
    const capacityProfile = r.profileMode === 'capacity' || continuousProfile;
    const maxVerifiedPassedChannels = capacityProfile
      ? maxContiguousPassedChannels
      : maxConfiguredVerifiedPassedChannels;
    const vlmThroughputGateDisabled = (r.tasks ?? []).some((task) => {
      if (strategyForTask(task).id !== 'vlm') return false;
      const rules = resolveTaskThresholds(r.thresholds ?? {}, task);
      return rules.minFpsRatio == null && rules.minThroughputFps == null;
    });
    const capacityEligible = !vlmThroughputGateDisabled;
    const allRanStepsPass = qualifiedWithEvidence.length > 0
      && qualifiedWithEvidence.every((s) => s.pass);
    const nextPrefixChannel = (maxContiguousPassedChannels ?? 0) + 1;
    const capacityFailure = r.status !== 'aborted' && capacityProfile
      ? qualifiedWithEvidence.find((s) =>
          s.channels === nextPrefixChannel && s.pass === false) ?? null
      : null;
    const executionIssues = [];
    for (const summary of qualifiedWithEvidence) {
      executionIssues.push(...summary.executionEvidence.issues.map(
        (issue) => `${summary.channels}ch:${issue}`,
      ));
    }
    if (r.status === 'aborted') executionIssues.push('device-execution-aborted');

    const rawRuntimeBottleneck = r.bottleneck ?? null;
    const rawRuntimeIsThreshold = isRuntimeThresholdBottleneck(rawRuntimeBottleneck);
    if (rawRuntimeBottleneck && !rawRuntimeIsThreshold) {
      executionIssues.push(`runtime-stop:${rawRuntimeBottleneck.source ?? 'non-threshold'}`);
    }
    if (firstFailed && !rawRuntimeBottleneck) {
      executionIssues.push('report-failure-without-runtime-bottleneck');
    }
    const runtimeMatchesFirstFailure = firstFailed != null
      && runtimeBottleneckMatchesFailure(rawRuntimeBottleneck, firstFailed);
    if (firstFailed && rawRuntimeBottleneck && !runtimeMatchesFirstFailure) {
      executionIssues.push('runtime-bottleneck-does-not-match-first-failure');
    }
    if (rawRuntimeBottleneck && !firstFailed) {
      executionIssues.push('runtime-bottleneck-without-report-failure');
    }
    if (firstFailed && qualifiedWithEvidence.some((s) => s.channels > firstFailed.channels)) {
      executionIssues.push('steps-executed-after-first-failure');
    }
    const capacityExecutionBlocked = executionIssues.length > 0;
    const overallPass = r.status !== 'aborted'
      && allRanStepsPass
      && !hasBottleneck
      && !capacityExecutionBlocked;
    const configuredUpperChannels = maxConfiguredChannels(r);
    const observedUpperChannels = qualifiedWithEvidence.length
      ? qualifiedWithEvidence[qualifiedWithEvidence.length - 1].channels
      : null;
    const reachedConfiguredUpper = configuredUpperChannels == null
      || observedUpperChannels === configuredUpperChannels;
    const maxStableChannelsExact = capacityEligible
      && capacityProfile
      && !capacityExecutionBlocked
      && maxContiguousPassedChannels != null
      && capacityFailure != null
      && capacityFailure.executionEvidence.complete
      && runtimeBottleneckMatchesFailure(rawRuntimeBottleneck, capacityFailure);
    const capacityLowerBound = capacityEligible
      && capacityProfile
      && !capacityExecutionBlocked
      && overallPass
      && continuousProfile
      && reachedConfiguredUpper
      && maxContiguousPassedChannels != null;
    const capacityMeasured = maxStableChannelsExact || capacityLowerBound;
    const maxStableChannels = capacityMeasured ? maxContiguousPassedChannels : null;
    const capacityExclusionReason = vlmThroughputGateDisabled
      ? 'vlm-throughput-gate-disabled'
      : capacityExecutionBlocked
        ? 'execution-blocked'
        : capacityProfile && maxContiguousPassedChannels == null
          ? 'no-contiguous-passing-prefix'
          : capacityProfile && !capacityMeasured
            ? 'incomplete-contiguous-scan'
            : null;
    const prefixEvidence = maxContiguousPassedChannels == null
      ? '未形成从 1 路开始的连续通过前缀'
      : `已验证从 1 路起连续通过至 ${maxContiguousPassedChannels} 路`;
    const blockedStepNumber = bottleneck?.stepNumber
      ?? (firstFailed ? firstFailed.step.index + 1 : '?');
    const blockedChannels = bottleneck?.channels ?? firstFailed?.channels ?? '?';
    const blockedReason = (bottleneck?.reason
      ?? firstFailed?.reasons?.join('; ')
      ?? executionIssues.join('; ')) || '设备执行未完成';

    let conclusion;
    if (r.status === 'aborted') {
      conclusion = `压测执行中断：运行到 ${r.error?.atChannels ?? '?'} 路时停止，原因：${r.error?.message ?? '未知错误'}；${prefixEvidence}。执行中断不计为容量失败，本次不形成容量结论`;
    } else if (vlmThroughputGateDisabled) {
      const stopText = bottleneck
        ? `；第 ${bottleneck.stepNumber} 阶段（${bottleneck.channels} 路）停止，原因：${bottleneck.reason}`
        : '';
      conclusion = `VLM FPS 门禁未启用；已完成至 ${maxVerifiedPassedChannels ?? 0} 路的非 FPS 短时观测，不形成容量结论${stopText}`;
    } else if (maxStableChannelsExact) {
      conclusion = `容量上限：${maxStableChannels} 路；第 ${capacityFailure.step.index + 1} 阶段（${capacityFailure.channels} 路）未通过容量门禁，原因：${capacityFailure.reasons.join('; ')}`;
    } else if (capacityLowerBound) {
      conclusion = `全部配置路数通过；容量下界：≥${maxStableChannels} 路（已到本次配置上限，未测得失败边界）`;
    } else if (capacityExecutionBlocked) {
      conclusion = `压测执行受阻：第 ${blockedStepNumber} 阶段（${blockedChannels} 路），原因：${blockedReason}；${prefixEvidence}。该阻断不计为容量失败，本次不形成容量上限`;
    } else if (capacityProfile && maxContiguousPassedChannels == null) {
      const failureText = firstFailed
        ? `；首个未通过阶段为 ${firstFailed.channels} 路，原因：${firstFailed.reasons.join('; ')}`
        : '';
      conclusion = `未形成从 1 路开始的连续通过前缀，不输出容量值${failureText}`;
    } else if (capacityProfile && firstFailed) {
      conclusion = `${prefixEvidence}；第 ${firstFailed.step.index + 1} 阶段（${firstFailed.channels} 路）未通过，但缺少相邻的连续容量边界，本次不输出精确容量`;
    } else if (bottleneck) {
      const upper = firstFailed?.channels ?? bottleneck.channels;
      conclusion = `已验证通过阶梯：${maxVerifiedPassedChannels ?? 0} 路；连续最大稳定路数未精确测定，已知 >= ${maxVerifiedPassedChannels ?? 0} 路且 < ${upper} 路；第 ${bottleneck.stepNumber} 阶段 ${bottleneck.channels} 路触发失败/停止，原因：${bottleneck.reason}`;
    } else if (overallPass) {
      conclusion = capacityProfile
        ? `${prefixEvidence}，但未完成连续扫描至本次配置上限，不形成容量结论`
        : `全部配置阶梯通过；已验证通过阶梯：${maxVerifiedPassedChannels ?? '-'} 路；连续最大稳定路数未精确测定`;
    } else {
      conclusion = '没有足够的有效采样点形成结论';
    }

    return {
      scenarioName: r.scenarioName,
      algorithmId: r.algorithmId,
      algorithmName: r.algorithmName,
      tasks: r.tasks ?? [],
      targetFps: r.targetFps,
      videoMode: r.videoMode,
      previewProfile: r.previewProfile ?? { mode: 'none' },
      profileMode: r.profileMode ?? 'configured',
      status: r.status,
      overallPass,
      allRanStepsPass,
      hasBottleneck,
      capacityMeasured,
      capacityExclusionReason,
      capacityExecutionBlocked,
      capacityExecutionIssues: [...new Set(executionIssues)],
      conclusion,
      maxStableChannels,
      maxStableChannelsExact,
      maxVerifiedPassedChannels,
      firstFailedStep: firstFailed ? {
        stepIndex: firstFailed.step.index,
        stepNumber: firstFailed.step.index + 1,
        channels: firstFailed.channels,
        reasons: firstFailed.reasons,
      } : null,
      capacityBound: capacityEligible && !capacityProfile && firstFailed ? {
        lowerInclusive: maxVerifiedPassedChannels,
        upperExclusive: firstFailed.channels,
      } : null,
      bottleneck,
      baselineFps: reportBaselineFps(r, stepSummaries),
      baselineByTask: r.baselineByTask ?? {},
      device: r.device,
      startedAt: r.startedAt,
      endedAt: r.endedAt,
      sampleCount: (r.samples ?? []).length,
      rampProbeChannels: ran.filter((s) => s.qualified === false).map((s) => s.channels),
      holdWindows: qualifiedWithEvidence.map((step) => ({
        channels: step.channels,
        ...step.executionEvidence.holdWindow,
      })),
      vlmReadiness: (r.steps ?? [])
        .filter((step) => step.vlmReadiness)
        .map((step) => step.vlmReadiness),
      mediaStages: stepSummaries.map((step) => ({ channels: step.channels, ...step.mediaStages })),
    };
  }

  _renderHtml(r, stepSummaries, summary) {
    const isVlmReport = (r.tasks ?? []).some((task) => strategyForTask(task).id === 'vlm');
    const throughputHeader = isVlmReport ? '当前新增路处理FPS' : '处理FPS(参考)';
    const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const pass = r.thresholds?.pass ?? {};
    const sampleInterval = estimateSampleIntervalSec(r.samples ?? []);
    const aggregationWindowText = isVlmReport
      ? 'VLM 吞吐判定使用 readiness 完成后的完整保持窗口；设备资源峰值使用保持窗口后半段。'
      : '阶梯汇总使用该阶梯后半段采样点作为稳定窗口。';
    const samplingText = Number.isFinite(sampleInterval)
      ? `约每 ${Math.round(sampleInterval)}s 采样一次；${aggregationWindowText}`
      : aggregationWindowText;
    const profileText = summary.capacityExclusionReason === 'vlm-throughput-gate-disabled'
      ? 'VLM 吞吐门禁未启用；本报告只保留非 FPS 短时观测和实测 FPS，不给出容量上限。'
      : summary.maxStableChannelsExact
        ? '当前按连续路数扫描，可直接给出容量上限。容量上限是最后一个完整执行且通过报告阈值的路数。'
        : summary.capacityMeasured && summary.maxStableChannels != null
          ? `连续路数扫描已到本次配置上限，所有路数均通过；本次只形成容量下界 ≥${summary.maxStableChannels} 路，不代表精确上限。`
          : summary.capacityExecutionBlocked
            ? 'readiness、任务绑定或设备执行阻断不属于容量失败；本次只保留已完成路数的验证证据，不形成容量上限。'
            : summary.capacityExclusionReason === 'no-contiguous-passing-prefix'
              ? '未形成从 1 路开始的连续通过前缀，不输出容量值。'
              : `当前阶梯未形成完整的连续容量边界，只能给出已验证通过阶梯；爬坡瞬时采样只标记为 PROBE，不计入稳定容量。${summary.capacityBound ? `本次已知 >= ${summary.capacityBound.lowerInclusive ?? 0} 路且 < ${summary.capacityBound.upperExclusive} 路。` : ''}`;
    const interpretationRows = [
      ['容量结论', profileText],
      ['路数 PASS/FAIL', `每个任务按 task type 选择判定策略。CV 默认使用关键链路、检测节点和丢弃率；VLM 默认使用分析 FPS 达标率和采样缺失率，并可配置端到端延时。全局平均丢弃率阈值为 ${pass.avgDiscardRate ?? pass.maxDiscardRate ?? '-'}。`],
      ['瓶颈停止', '运行期保护熔断，用于避免设备继续加压。CV 使用稳定窗口 FPS 相对基线折半作为保护；VLM 使用目标 FPS 达标率，避免低帧率任务被短窗口误判。丢弃率、CPU、内存等保护仍然通用。'],
      ['采样窗口', samplingText],
      ['失败原因', '表格中的失败原因来自未通过的阈值项；若同时存在瓶颈停止，顶部横幅展示触发提前停止的运行期原因。'],
    ];
    const baseRows = [
      ['场景', r.scenarioName],
      ['任务', formatTaskList(r.tasks, r.algorithmId, r.algorithmName)],
      ['任务策略', formatTaskStrategies(r.tasks)],
      ['编排设定 FPS（参考）', formatTargetFps(r.tasks, r.targetFps)],
      ['视频模式', r.videoMode],
      ['预览负载', formatPreviewProfile(r.previewProfile)],
      ['设备', `${r.device?.model ?? ''} / ${r.device?.sn ?? ''}`],
      ['软件版本', r.device?.softwareVersion ?? ''],
      ['开始时间', r.startedAt],
      ['结束时间', r.endedAt],
      ['总采样点', (r.samples ?? []).length],
      ['基线 FPS（参考）', summary.baselineFps != null ? `${summary.baselineFps} (step 1, 1ch)` : '-'],
    ];

    const bottleneckBanner = summary.bottleneck
      ? `<div class="banner warn"><strong>${summary.capacityExecutionBlocked ? '执行受阻（非容量失败）' : '检测到瓶颈'}</strong><br>第 ${summary.bottleneck.stepNumber} 阶段（${summary.bottleneck.channels} 路${summary.bottleneck.targetChannels && summary.bottleneck.targetChannels !== summary.bottleneck.channels ? `，目标 ${summary.bottleneck.targetChannels} 路` : ''}）触发停止。原因：${esc(summary.bottleneck.reason)}</div>`
      : '';

    const abortedBanner = r.status === 'aborted'
      ? `<div class="banner error"><strong>压测中断（部分报告）</strong><br>运行到 ${r.error?.atChannels ?? '?'} 路 / 第 ${r.error?.atStepIndex != null ? r.error.atStepIndex + 1 : '?'} 阶段时中断。原因：${esc(r.error?.message ?? '未知错误')}</div>`
      : '';

    const runBadge = summary.hasBottleneck
      ? { className: 'warn', label: 'STOPPED' }
      : (summary.overallPass ? { className: 'pass', label: 'PASS' } : { className: 'fail', label: 'FAIL' });
    const stepStatus = (s) => {
      if (s.skipped) return { className: 'na', label: 'SKIP' };
      if (summary.bottleneck?.stepIndex === s.step.index
          && (summary.bottleneck.channels == null || summary.bottleneck.channels === s.channels)) {
        return { className: 'warn', label: 'STOPPED' };
      }
      if (s.qualified === false) return { className: 'na', label: 'PROBE' };
      return s.pass ? { className: 'pass', label: 'PASS' } : { className: 'fail', label: 'FAIL' };
    };

    const stepRows = stepSummaries.map((s, rowIndex) => {
      const status = stepStatus(s);
      const displayedFps = isVlmReport ? (s.currentRouteFps ?? s.minFpsAcross) : s.minFpsAcross;
      return `
      <tr>
        <td>${rowIndex + 1}</td>
        <td>${s.channels}</td>
        <td>${s.holdSec}s</td>
        <td>${s.targetFps ?? '-'}</td>
        <td>${displayedFps ?? '-'}</td>
        <td>${s.criticalPathLatencyMs ?? '-'}</td>
        <td>${s.detectorLatencyMs ?? '-'}</td>
        <td>${s.avgDiscard != null ? s.avgDiscard : '-'}</td>
        <td>${s.maxDiscard != null ? s.maxDiscard : '-'}</td>
        <td class="${s.maxNpu >= 90 ? 'fail' : ''}">${s.maxNpu != null ? s.maxNpu + '%' : '-'}</td>
        <td class="${s.maxAcceleratorMem >= 90 ? 'fail' : ''}">${s.maxAcceleratorMem != null ? s.maxAcceleratorMem + '%' : '-'}</td>
        <td class="${s.maxCpu >= 90 ? 'fail' : ''}">${s.maxCpu != null ? s.maxCpu + '%' : '-'}</td>
        <td class="${s.maxMem >= 90 ? 'fail' : ''}">${s.maxMem != null ? s.maxMem + '%' : '-'}</td>
        <td class="${s.maxDiskUsedPercent >= 90 ? 'fail' : ''}">${s.maxDiskUsedPercent != null ? s.maxDiskUsedPercent + '%' : '-'}</td>
        <td>${formatMib(s.maxPoolInUseBytes)}/${formatMib(s.maxPoolAllocatedBytes)}/${s.maxPoolUtilizationPercent != null ? s.maxPoolUtilizationPercent + '%' : '-'}</td>
        <td class="${status.className}">${status.label}</td>
        <td>${esc((s.reasons ?? []).join('; '))}</td>
      </tr>`;
    }).join('');

    const verdictRows = stepSummaries.flatMap((s) =>
      s.perThreshold.map((t) => `
        <tr>
          <td>${s.channels}ch</td>
          <td>${esc(t.taskDisplayName ?? t.taskKey ?? '-')}</td>
          <td>${esc(t.strategy ?? '-')}</td>
          <td>${esc(thresholdLabel(t.name, strategyForTask(t)))}</td>
          <td>${t.threshold ?? '-'}</td>
          <td>${t.actual ?? '-'}</td>
          <td class="${t.result === 'PASS' ? 'pass' : (t.result === 'FAIL' ? 'fail' : 'na')}">${t.result}</td>
        </tr>`),
    ).join('');

    const taskRows = stepSummaries.flatMap((s) =>
      (s.taskStats ?? []).map((t) => `
        <tr>
          <td>${s.channels}ch</td>
          <td>${esc(t.taskDisplayName ?? t.taskKey)}</td>
          <td>${esc(t.strategy)}</td>
          <td>${t.algorithmId ?? '-'}</td>
          <td>${t.bindingCount}</td>
          <td>${t.minThroughputFps ?? '-'}</td>
          <td>${t.minFpsRatio != null ? formatPercent(t.minFpsRatio) : '-'}</td>
          <td>${t.maxMissingRate != null ? formatPercent(t.maxMissingRate) : '-'}</td>
          <td>${t.avgDiscardRate ?? '-'}</td>
          <td>${t.maxPrimaryLatencyMs ?? '-'}</td>
          <td>${t.maxCriticalPathLatencyMs ?? '-'}</td>
        </tr>`),
    ).join('');

    const mediaRows = stepSummaries.map((s) => {
      const m = s.mediaStages ?? {};
      return `<tr>
        <td>${s.channels}ch</td>
        <td>${metric(m.preprocessAvgMs)}</td>
        <td>${metric(m.inferAvgMs)}</td>
        <td>${metric(m.postprocessAvgMs)}</td>
        <td>${metric(m.colorConvertAvgMs)}/${metric(m.blobConvertAvgMs)}</td>
        <td>${metric(m.graphForwardAvgMs)}/${metric(m.resultParseAvgMs)}</td>
        <td>${metric(m.rknnPrepareAvgMs)}/${metric(m.rknnInputsSetAvgMs)}</td>
        <td>${metric(m.rknnRunAvgMs)}/${metric(m.rknnOutputsGetAvgMs)}/${metric(m.rknnOutputsReleaseAvgMs)}/${metric(m.rknnOutputTransformAvgMs)}</td>
        <td>${metric(m.rknnForwardAvgMs)}/${m.rknnForwardFailures ?? '-'}</td>
        <td>${metric(m.rknnDetectorForwardAvgMs)}/${metric(m.rknnDetectorMutexWaitAvgMs)}/${m.rknnDetectorForwardFailures ?? '-'}</td>
        <td>${metric(m.rknnRgaFillAvgMs)}/${metric(m.rknnRgaResizeColorAvgMs)}/${metric(m.rknnRgaCropResizeAvgMs)}/${m.rknnRgaCropResizeCalls ?? '-'}/${m.rknnRgaCropDmaBufFrames ?? '-'}/${m.rknnRgaCropHostFallbacks ?? '-'}/${metric(m.rknnNativeInputMapAvgMs)}/${m.rknnPreprocessFastHits ?? '-'}/${m.rknnRgaFailures ?? '-'}/${m.rknnRgaCropResizeFailures ?? '-'}</td>
        <td>${m.rknnCpuResizeFallbacks ?? '-'}/${m.rknnCpuCropResizeFallbacks ?? '-'}/${m.rknnCpuNormalizeFallbacks ?? '-'}/${m.rknnInputCompatibilityFallbacks ?? '-'}</td>
        <td>${m.rknnBoundInputBindAttempts ?? '-'}/${m.rknnBoundInputBindFailures ?? '-'}/${m.rknnBoundInputFrames ?? '-'}/${metric(m.rknnBoundInputCopyAvgMs)}/${metric(m.rknnBoundInputSyncAvgMs)}/${formatMib(m.rknnBoundInputCopyAvgBytes)}/${m.rknnBoundInputCopyFailures ?? '-'}/${m.rknnBoundInputSyncFailures ?? '-'}</td>
        <td>${m.rknnRgaBoundInputBindAttempts ?? '-'}/${m.rknnRgaBoundInputBindFailures ?? '-'}/${m.rknnRgaBoundInputImportCalls ?? '-'}/${metric(m.rknnRgaBoundInputImportAvgMs)}/${m.rknnRgaBoundInputImportFailures ?? '-'}/${m.rknnRgaBoundInputFrames ?? '-'}/${m.rknnRgaBoundUint8Frames ?? '-'}/${m.rknnRgaBoundNativeInt8Frames ?? '-'}/${m.rknnRgaBoundRequantizeCalls ?? '-'}/${metric(m.rknnRgaBoundRequantizeAvgMs)}/${m.rknnRgaBoundRequantizeFailures ?? '-'}/${m.rknnRgaBoundInputNormalizeBypasses ?? '-'}</td>
        <td>${m.rknnMppDmaBufImportCalls ?? '-'}/${metric(m.rknnMppDmaBufImportAvgMs)}/${m.rknnMppDmaBufImportFailures ?? '-'}/${m.rknnMppDmaBufFrames ?? '-'}/${m.rknnMppDmaBufFallbacks ?? '-'}/${formatMib(m.rknnMppDmaBufSourceAvgBytes)}</td>
        <td>${m.rknnNativeInt8Outputs ?? '-'}/${m.rknnFloatOutputs ?? '-'}/${m.rknnOutputCompatibilityFallbacks ?? '-'}/${formatMib(m.rknnNativeOutputAvgBytes)}/${formatMib(m.rknnFloatOutputAvgBytes)}</td>
        <td>${metric(m.rknnYolov8DflAvgMs)}/${metric(m.rknnYolov8ClassAvgMs)}</td>
        <td>${m.rknnYolov8DirectCandidateCalls ?? '-'}/${m.rknnYolov8DirectCandidateFailures ?? '-'}/${metric(m.rknnYolov8DirectAvgPointsScanned)}/${metric(m.rknnYolov8DirectAvgPointsDecoded)}/${metric(m.rknnYolov8ScoreSumAvgPointsRejected)}/${formatMib(m.rknnYolov8LogicalFloatBytesAvoided)}</td>
        <td>${metric(m.yolov8PostprocessAvgMs)}/${metric(m.yolov8NmsAvgMs)}</td>
        <td>${metric(m.rgaAvgMs)}/${m.rgaFailures ?? '-'}</td>
        <td>${metric(m.mppEncodeAvgMs)}/${m.mppEncodeFailures ?? '-'}/${m.mppRgaCopyInFrames ?? '-'}/${m.mppRgaCopyInFailures ?? '-'}/${m.mppCpuCopyInFallbacks ?? '-'}</td>
        <td>${metric(m.mppDecodeAvgMs)}/${m.mppDecodeFailures ?? '-'}/${m.mppDecodeFallbacks ?? '-'}</td>
        <td>${metric(m.mppCopyOutAvgMs)}/${m.mppDecodedFrames ?? '-'}/${m.mppCopyOutFrames ?? '-'}/${m.mppRgaCopyOutFrames ?? '-'}/${m.mppCpuCopyOutFallbacks ?? '-'}/${m.mppEarlyDroppedFrames ?? '-'}/${m.mppCopyOutFailures ?? '-'}</td>
        <td>${metric(m.osdAvgMs)}</td>
        <td>${metric(m.publishAvgMs)}</td>
        <td>${metric(m.firstFrameAvgMs)}/${metric(m.firstFrameMaxMs)}</td>
        <td>${m.activePreviewStreamsPeak ?? '-'}/${m.activePreviewPublishersPeak ?? '-'}</td>
        <td>${m.activeRawPreviewStreamsPeak ?? '-'}/${m.activeAlgorithmPreviewStreamsPeak ?? '-'}</td>
        <td>${m.srsStreamsPeak ?? '-'}/${m.srsClientsPeak ?? '-'}</td>
        <td>${m.previewStartsDelta ?? '-'}/${m.previewStopsDelta ?? '-'}/${m.previewFailuresDelta ?? '-'}</td>
      </tr>`;
    }).join('');

    return `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>压测报告 - ${esc(r.scenarioName)}</title>
<style>
  body{font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;margin:24px;color:#1a1a1a}
  h1{font-size:20px} h2{font-size:16px;margin-top:28px;border-bottom:1px solid #ddd;padding-bottom:4px}
  table{border-collapse:collapse;width:100%;margin:8px 0}
  th,td{border:1px solid #ddd;padding:6px 10px;text-align:left;vertical-align:top}
  th{background:#f5f5f5}.pass{color:#16a34a;font-weight:600}
  .fail{color:#dc2626;font-weight:600}.na{color:#888}
  .badge{display:inline-block;padding:4px 12px;border-radius:4px;color:#fff;font-weight:600}
  .badge.pass{background:#16a34a}.badge.fail{background:#dc2626}.badge.warn{background:#f59e0b}
  .summary{background:#f8fafc;border:1px solid #cbd5e1;padding:12px 14px;border-radius:4px;margin:12px 0}
  .banner{padding:10px 14px;border-radius:4px;margin:12px 0}.warn{background:#fef3c7;border:1px solid #f59e0b}.error{background:#fee2e2;border:1px solid #dc2626}
  .note-table th{width:140px}
</style></head><body>
<h1>压测报告</h1>
<p>总体结果: <span class="badge ${runBadge.className}">${runBadge.label}</span>${r.status === 'aborted' ? ' <span class="badge fail">ABORTED</span>' : ''}</p>
<div class="summary"><strong>结论</strong><br>${esc(summary.conclusion)}</div>
${abortedBanner}
${bottleneckBanner}
<h2>判定口径</h2>
<table class="note-table">${interpretationRows.map(([k, v]) => `<tr><th>${esc(k)}</th><td>${esc(v)}</td></tr>`).join('')}</table>
<h2>基础信息</h2>
<table>${baseRows.map(([k, v]) => `<tr><th>${esc(k)}</th><td>${esc(v)}</td></tr>`).join('')}</table>
<h2>路数结果</h2>
<table>
  <tr><th>序号</th><th>路数</th><th>保持</th><th>目标FPS(参考)</th><th>${throughputHeader}</th><th>关键/端到端延时ms</th><th>主节点延时ms</th><th>平均丢弃率</th><th>最差通道丢弃率</th><th>加速器峰值</th><th>加速器内存峰值</th><th>CPU峰值</th><th>内存峰值</th><th>磁盘峰值</th><th>内存池在用/分配MiB/占用率</th><th>结果</th><th>失败原因</th></tr>
  ${stepRows}
</table>
<h2>媒体与预览分阶段指标</h2>
<table>
  <tr><th>路数</th><th>Preprocess ms</th><th>Infer ms</th><th>Postprocess ms</th><th>颜色/Blob ms</th><th>Graph/Parse ms</th><th>RKNN准备/送入 ms</th><th>RKNN执行/取回/释放/转换 ms</th><th>RKNN总计/失败</th><th>Detector总计/等待/失败</th><th>Fast Fill/Resize/Crop次数/CropDMA/CropHost/Map/命中/失败</th><th>Fallback Resize/Crop/Normalize/Compat</th><th>绑定输入 Bind/失败/帧/Copy ms/Sync ms/CopyMiB/Copy失败/Sync失败</th><th>RGA直绑 Bind/失败/Import次数/ms/失败/帧/UINT8融合帧/INT8回退帧/Requant次数/ms/失败/Normalize绕过</th><th>MPP DMA-BUF Import/ms/失败/帧/回退/源MiB</th><th>输出 Native/Float/Compat/NativeMiB/FloatMiB</th><th>量化 DFL/Class ms</th><th>直接候选 调用/失败/扫描/解码/Sum早筛/省略MiB</th><th>YOLO Post/NMS</th><th>RGA/失败</th><th>MPP编码 ms/失败/RGA帧/RGA失败/CPU回退</th><th>MPP解码/失败/回退</th><th>Copy-out ms/解码/复制/RGA帧/CPU回退/早丢/失败</th><th>OSD ms</th><th>Publish ms</th><th>首帧平均/进程最大ms</th><th>预览流/发布器峰值</th><th>原始/算法预览峰值</th><th>SRS流/客户端峰值</th><th>启动/停止/失败增量</th></tr>
  ${mediaRows}
</table>
<h2>分任务汇总</h2>
<table>
  <tr><th>路数</th><th>任务</th><th>策略</th><th>算法ID</th><th>绑定数</th><th>最低处理FPS</th><th>最低FPS达标率</th><th>最大缺失率</th><th>平均丢弃率</th><th>最大主节点ms</th><th>最大关键/端到端ms</th></tr>
  ${taskRows}
</table>
<h2>阈值判定明细</h2>
<table>
  <tr><th>路数</th><th>任务</th><th>策略</th><th>指标</th><th>阈值</th><th>实测</th><th>结果</th></tr>
  ${verdictRows}
</table>
</body></html>`;
  }
}

function formatTaskList(tasks, legacyAlgorithmId, legacyAlgorithmName) {
  if (!Array.isArray(tasks) || !tasks.length) {
    return `${legacyAlgorithmId ?? '-'} (${legacyAlgorithmName ?? '-'})`;
  }
  return tasks
    .map((task) => `${task.id}: ${task.algorithmId}${task.displayName ? ` (${task.displayName})` : ''}`)
    .join('; ');
}

function formatTargetFps(tasks, legacyTargetFps) {
  if (!Array.isArray(tasks) || !tasks.length) {
    return legacyTargetFps ?? '未提取到';
  }
  return tasks.map((task) => `${task.id}=${task.targetFps ?? 'N/A'}`).join('; ');
}

function formatTaskStrategies(tasks) {
  if (!Array.isArray(tasks) || !tasks.length) return strategyForTaskType('cv').id;
  return tasks
    .map((task) => `${task.id}=${strategyForTaskType(task.type).id}`)
    .join('; ');
}

function formatPreviewProfile(profile) {
  if (!profile || profile.mode === 'none') return 'none（后台推理）';
  return `${profile.mode}; streams=${profile.streamLimit ?? 'all'}; clients/stream=${profile.clientsPerStream ?? 1}`;
}

function metric(value, suffix = '') {
  return typeof value === 'number' && Number.isFinite(value) ? `${round(value, 3)}${suffix}` : '-';
}

function resolveSampleIntervalSec(runResult, samples) {
  const configured = Number(runResult.sampleIntervalSec);
  if (Number.isFinite(configured) && configured > 0) {
    return { seconds: configured, source: 'configured' };
  }

  // Legacy metrics did not persist sampleIntervalSec. Accept an inferred value
  // only when there are enough explicit hold deltas across the run to make the
  // estimate resistant to one partial/slow fuse window. New runs always use
  // the configured value above.
  const groups = new Map();
  for (const sample of samples ?? []) {
    if (sample?.phase !== 'hold') continue;
    const key = `${sample.stepIndex}::${sample.activeChannels}`;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(Number(sample.ts));
  }
  const deltas = [];
  for (const timestamps of groups.values()) {
    for (let index = 1; index < timestamps.length; index++) {
      const delta = timestamps[index] - timestamps[index - 1];
      if (Number.isFinite(delta) && delta > 0) deltas.push(delta / 1000);
    }
  }
  if (deltas.length < 10) return { seconds: null, source: 'unavailable' };
  deltas.sort((a, b) => a - b);
  const lowQuantile = deltas[Math.floor((deltas.length - 1) * 0.1)];
  return Number.isFinite(lowQuantile) && lowQuantile > 0
    ? { seconds: lowQuantile, source: 'legacy-inferred' }
    : { seconds: null, source: 'unavailable' };
}

function evaluateHoldWindow(step, samples, intervalInfo) {
  const sampleStepIndex = step.sampleStepIndex ?? step.index;
  const sampleChannels = Number(step.sampleChannels ?? step.channels);
  const holdSamples = (samples ?? []).filter((sample) =>
    sample?.stepIndex === sampleStepIndex
      && sample?.phase === 'hold'
      && Number(sample.activeChannels) === sampleChannels);
  const holdSec = Number(step.holdSec);
  const sampleIntervalSec = Number(intervalInfo?.seconds);
  const timestamps = holdSamples.map((sample) => Number(sample.ts));
  const timestampsValid = timestamps.length > 0
    && timestamps.every(Number.isFinite)
    && timestamps.every((value, index) => index === 0 || value > timestamps[index - 1]);
  const observedSpanSec = timestampsValid && timestamps.length > 1
    ? (timestamps.at(-1) - timestamps[0]) / 1000
    : 0;
  const gapsSec = timestampsValid
    ? timestamps.slice(1).map((value, index) => (value - timestamps[index]) / 1000)
    : [];
  const maxGapSec = gapsSec.length ? Math.max(...gapsSec) : null;
  const configured = Number.isFinite(holdSec) && holdSec > 0
    && Number.isFinite(sampleIntervalSec) && sampleIntervalSec > 0;
  const expectedSampleCount = configured
    ? Math.max(1, Math.floor((holdSec / sampleIntervalSec) + 1e-6))
    : null;
  const countComplete = expectedSampleCount != null
    && holdSamples.length >= expectedSampleCount;
  const jitterSec = configured ? Math.max(0.75, sampleIntervalSec * 0.35) : null;
  const minimumSpanSec = configured
    ? Math.max(0, holdSec - sampleIntervalSec - jitterSec)
    : null;
  const durationComplete = configured && timestampsValid
    && (expectedSampleCount === 1 || observedSpanSec >= minimumSpanSec);
  const cadenceComplete = configured && timestampsValid
    && (maxGapSec == null || maxGapSec <= (sampleIntervalSec * 2.5) + 0.25);
  const reasons = [];
  if (!configured) reasons.push('hold-configuration-unavailable');
  if (!holdSamples.length) reasons.push('no-explicit-hold-samples');
  if (holdSamples.length && !timestampsValid) reasons.push('invalid-hold-timestamps');
  if (configured && !countComplete) reasons.push('hold-sample-count-incomplete');
  if (configured && timestampsValid && !durationComplete) reasons.push('hold-duration-incomplete');
  if (configured && timestampsValid && !cadenceComplete) reasons.push('hold-sampling-gap');

  return {
    complete: configured && countComplete && durationComplete && cadenceComplete,
    configuredHoldSec: Number.isFinite(holdSec) ? holdSec : null,
    sampleIntervalSec: Number.isFinite(sampleIntervalSec) ? sampleIntervalSec : null,
    sampleIntervalSource: intervalInfo?.source ?? 'unavailable',
    sampleCount: holdSamples.length,
    expectedSampleCount,
    observedSpanSec: round(observedSpanSec, 3),
    maxGapSec: maxGapSec == null ? null : round(maxGapSec, 3),
    reasons,
  };
}

function assessStepExecutionEvidence(runResult, summary) {
  const holdWindow = summary.holdWindow
    ?? (summary.holdWindowComplete === true
      ? { complete: true, reasons: [] }
      : evaluateHoldWindow(
          summary.step ?? {},
          runResult.samples ?? [],
          resolveSampleIntervalSec(runResult, runResult.samples ?? []),
        ));
  const issues = [];
  if (holdWindow.complete !== true) {
    issues.push(...(holdWindow.reasons?.length
      ? holdWindow.reasons
      : ['incomplete-hold-window']));
  }

  const hasVlm = (runResult.tasks ?? []).some((task) => strategyForTask(task).id === 'vlm');
  const stepConfig = summary.step ?? {};
  const currentVlmBindings = stepConfig.currentVlmBindings;
  const needsVlmReadiness = hasVlm
    && (!Array.isArray(currentVlmBindings) || currentVlmBindings.length > 0);
  const readiness = stepConfig.vlmReadiness
    ?? (runResult.steps ?? []).find((step) =>
      step.index === (stepConfig.sourceIndex ?? stepConfig.index))?.vlmReadiness
    ?? null;
  const readinessComplete = !needsVlmReadiness
    || (readiness?.ready === true && readiness?.status === 'ready');
  if (!readinessComplete) issues.push('vlm-readiness-incomplete');

  return {
    complete: holdWindow.complete === true && readinessComplete,
    holdWindow,
    readinessComplete,
    issues,
  };
}

function isRuntimeThresholdBottleneck(bottleneck) {
  if (!bottleneck || bottleneck.phase !== 'hold') return false;
  if (bottleneck.source != null) return bottleneck.source === 'runtime-threshold';
  const reason = String(bottleneck.reason ?? '');
  if (/RunningDetail|unavailable|consecutive samples|60s average|\bmemory\b|\bCPU\b|\bNPU\b|discardRate\s*>|\bdisk\b|task.*bind|readiness|timed out|任务绑定/i.test(reason)) {
    return false;
  }
  return gateKindsFromBottleneck(bottleneck).size > 0;
}

function runtimeBottleneckMatchesFailure(bottleneck, failure) {
  if (!isRuntimeThresholdBottleneck(bottleneck) || !failure) return false;
  if (Number(bottleneck.channels) !== Number(failure.channels)) return false;
  const expectedStepIndex = failure.step?.sourceIndex ?? failure.step?.index;
  if (Number.isInteger(Number(bottleneck.stepIndex))
      && Number.isInteger(Number(expectedStepIndex))
      && Number(bottleneck.stepIndex) !== Number(expectedStepIndex)) {
    return false;
  }
  const runtimeKinds = gateKindsFromBottleneck(bottleneck);
  const failureKinds = gateKindsFromFailure(failure);
  return [...runtimeKinds].some((kind) => failureKinds.has(kind));
}

function gateKindsFromBottleneck(bottleneck) {
  const kinds = new Set();
  for (const gate of bottleneck?.gates ?? []) {
    const kind = normalizeGateKind(gate?.name);
    if (kind) kinds.add(kind);
  }
  for (const kind of gateKindsFromText(bottleneck?.reason)) kinds.add(kind);
  return kinds;
}

function gateKindsFromFailure(failure) {
  const kinds = new Set();
  for (const check of failure?.perThreshold ?? []) {
    if (check?.result !== 'FAIL') continue;
    const kind = normalizeGateKind(check.name);
    if (kind) kinds.add(kind);
  }
  for (const reason of failure?.reasons ?? []) {
    for (const kind of gateKindsFromText(reason)) kinds.add(kind);
  }
  return kinds;
}

function gateKindsFromText(value) {
  const text = String(value ?? '');
  const kinds = new Set();
  if (/fpsRatio|FPS达标率/i.test(text)) kinds.add('fps-ratio');
  if (/missingRate|采样缺失|telemetry.*missing/i.test(text)) kinds.add('missing');
  if (/meanDiscard|avgDiscard|平均丢弃/i.test(text)) kinds.add('discard');
  if (/packetDiscard|网络丢包/i.test(text)) kinds.add('packet-discard');
  if (/\bdisk\b|磁盘/i.test(text)) kinds.add('disk');
  if (/latency|延时|延迟/i.test(text)) kinds.add('latency');
  if (!kinds.has('fps-ratio') && /\bfps\b/i.test(text)) kinds.add('throughput');
  return kinds;
}

function normalizeGateKind(name) {
  switch (name) {
    case 'minFpsRatio': return 'fps-ratio';
    case 'minThroughputFps':
    case 'baselineFpsFuse': return 'throughput';
    case 'maxMissingRate': return 'missing';
    case 'avgDiscardRate':
    case 'maxDiscardRate':
    case 'runtimeDiscardFuse': return 'discard';
    case 'maxPacketDiscardRate': return 'packet-discard';
    case 'maxDiskUsedPercent': return 'disk';
    case 'maxPrimaryLatencyMs':
    case 'maxAnalysisLatencyMs':
    case 'maxCriticalPathLatencyMs':
    case 'maxDetectorLatencyMs':
    case 'maxEndToEndLatencyMs': return 'latency';
    default: return null;
  }
}

function continuousPassingPrefix(stepSummaries) {
  const byChannels = new Map();
  for (const summary of stepSummaries) {
    if (!Number.isInteger(summary.channels) || summary.channels <= 0) continue;
    if (!byChannels.has(summary.channels)) byChannels.set(summary.channels, []);
    byChannels.get(summary.channels).push(summary);
  }

  const prefix = [];
  for (let channels = 1; byChannels.has(channels); channels++) {
    const rows = byChannels.get(channels);
    if (!rows.length || rows.some(
      (row) => row.pass !== true || row.executionEvidence?.complete === false,
    )) break;
    prefix.push(rows[0]);
  }
  return prefix;
}

function maxConfiguredChannels(runResult) {
  for (const profile of [runResult.steps, runResult.loadProfile]) {
    const channels = (profile ?? [])
      .map((step) => Number(step.channels))
      .filter((value) => Number.isInteger(value) && value > 0);
    if (channels.length) return Math.max(...channels);
  }
  return null;
}

function isContinuousChannelProfile(stepSummaries) {
  const channels = stepSummaries
    .map((s) => s.channels)
    .filter((v) => Number.isInteger(v))
    .sort((a, b) => a - b);
  if (!channels.length || channels[0] !== 1) return false;
  for (let i = 1; i < channels.length; i++) {
    if (channels[i] !== channels[i - 1] + 1) return false;
  }
  return true;
}

function buildReportSteps(runResult) {
  const samples = runResult.samples ?? [];
  const steps = runResult.steps ?? [];
  const reportSteps = [];
  const seenChannels = new Set();

  for (const step of steps) {
    const stepSamples = samples.filter((sample) => sample.stepIndex === step.index);
    const observedChannels = uniqueObservedChannels(stepSamples);

    const shouldExpand = observedChannels.length > 1
      && observedChannels.some((channels) => channels < step.channels);
    const channelsToReport = shouldExpand ? observedChannels : [step.channels];

    for (const channels of channelsToReport) {
      if (seenChannels.has(channels)) continue;
      seenChannels.add(channels);
      const qualified = stepSamples.some((sample) =>
        Number(sample.activeChannels) === channels && sample.phase === 'hold');
      reportSteps.push({
        ...step,
        index: channels - 1,
        sourceIndex: step.index,
        channels,
        targetChannels: step.channels,
        sampleStepIndex: step.index,
        sampleChannels: channels,
        qualified,
      });
    }
  }

  return reportSteps.sort((a, b) => a.channels - b.channels);
}

function uniqueObservedChannels(samples) {
  return [...new Set(
    samples
      .map((s) => Number(s.activeChannels))
      .filter((v) => Number.isInteger(v) && v > 0),
  )].sort((a, b) => a - b);
}

function normalizeBottleneck(bottleneck, stepSummaries) {
  if (!bottleneck) return null;
  const stepRows = stepSummaries.filter((s) => !s.skipped);
  if (!stepRows.length) return bottleneck;

  const channels = stepRows.map((s) => s.channels).filter((v) => Number.isInteger(v));
  if (!channels.length) return bottleneck;

  const matchingRow = stepRows.find((s) => s.channels === bottleneck.channels);
  if (matchingRow) {
    return {
      ...bottleneck,
      stepIndex: matchingRow.step.index,
      stepNumber: matchingRow.step.index + 1,
    };
  }

  const maxObserved = Math.max(...channels);
  if (Number.isInteger(bottleneck.channels) && maxObserved < bottleneck.channels) {
    const observedRow = stepRows.find((s) => s.channels === maxObserved);
    return {
      ...bottleneck,
      stepIndex: observedRow?.step.index ?? bottleneck.stepIndex,
      stepNumber: observedRow ? observedRow.step.index + 1 : bottleneck.stepNumber,
      targetChannels: bottleneck.targetChannels ?? bottleneck.channels,
      channels: maxObserved,
    };
  }

  return bottleneck;
}

function reportBaselineFps(runResult, stepSummaries) {
  const oneChannel = stepSummaries.find((s) => s.channels === 1 && !s.skipped);
  return oneChannel?.minFpsAcross ?? runResult.baselineFps;
}

function formatPercent(v) {
  return `${round(Number(v) * 100, 2)}%`;
}

function formatMib(bytes) {
  return typeof bytes === 'number' ? round(bytes / (1024 * 1024), 1) : '-';
}

function round(v, digits) {
  const f = 10 ** digits;
  return Math.round(v * f) / f;
}

function estimateSampleIntervalSec(samples) {
  const deltas = [];
  for (let i = 1; i < samples.length; i++) {
    const prev = samples[i - 1];
    const curr = samples[i];
    if (prev?.stepIndex !== curr?.stepIndex) continue;
    const delta = Number(curr?.ts) - Number(prev?.ts);
    if (Number.isFinite(delta) && delta > 0) deltas.push(delta / 1000);
  }
  if (!deltas.length) return NaN;

  deltas.sort((a, b) => a - b);
  const mid = Math.floor(deltas.length / 2);
  return deltas.length % 2 ? deltas[mid] : (deltas[mid - 1] + deltas[mid]) / 2;
}
