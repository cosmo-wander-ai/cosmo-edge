import {
  ACCURACY_PROTOCOL_VERSION,
  ACCURACY_SUMMARY_SCHEMA_VERSION,
} from './protocol.js';

export function evaluateExpectation(expectation, eventCount) {
  if (!expectation || (expectation.minEvents == null && expectation.maxEvents == null)) {
    throw new Error('expectation requires minEvents or maxEvents');
  }
  if (!Number.isInteger(eventCount) || eventCount < 0) {
    throw new Error('eventCount must be a non-negative integer');
  }
  const reasons = [];
  if (expectation.minEvents != null && eventCount < expectation.minEvents) {
    reasons.push(`eventCount ${eventCount} < minEvents ${expectation.minEvents}`);
  }
  if (expectation.maxEvents != null && eventCount > expectation.maxEvents) {
    reasons.push(`eventCount ${eventCount} > maxEvents ${expectation.maxEvents}`);
  }
  return { status: reasons.length ? 'FAIL' : 'PASS', eventCount, reasons };
}

export function summarizeAccuracyRun({
  suite,
  cases = [],
  targetChip,
  execution,
  admission = null,
  executionBlockReasons = [],
} = {}) {
  if (!suite) throw new Error('suite is required');
  if (!execution) throw new Error('execution is required');
  if (!suite.identity?.taskConfigSha256) throw new Error('suite taskConfig identity is required');
  const resolvedExecution = execution;
  const measurements = summarizeAccuracyMeasurements({ tasks: suite.tasks, cases });
  const executionReasons = [...executionBlockReasons];
  const cleanupBlockedCases = cases.filter((item) => item.cleanupBlocked === true);
  executionReasons.push(...cleanupBlockedCases.map((item) => `case:${item.id}:cleanup-blocked`));
  for (const item of cases) {
    if (item.status === 'ERROR' && item.cleanupBlocked !== true) {
      executionReasons.push(`case:${item.id}:${item.status}`);
    }
  }
  const executionOutcome = executionBlockReasons.length || cleanupBlockedCases.length
    ? 'BLOCKED'
    : cases.some((item) => item.status === 'ERROR')
      ? 'ERROR'
      : 'COMPLETED';
  return {
    schemaVersion: ACCURACY_SUMMARY_SCHEMA_VERSION,
    protocolVersion: ACCURACY_PROTOCOL_VERSION,
    executionOutcome,
    executionReasons: [...new Set(executionReasons)],
    execution: structuredClone(resolvedExecution),
    suite: {
      id: suite.id,
      displayName: suite.displayName ?? suite.id,
      suiteSha256: suite.identity.suiteSha256,
      caseManifestSha256: suite.identity.caseManifestSha256,
      caseSetSha256: suite.identity.caseSetSha256,
      sourceMode: suite.sourceMode,
      targetChip,
    },
    tasks: measurements.tasks,
    cases: cases.map(sanitizeCaseSummary),
    metrics: measurements.metrics,
    admission: admission == null ? null : structuredClone(admission),
    performance: summarizePerformance(cases),
  };
}

export function summarizeAccuracyMeasurements({ tasks = [], cases = [] } = {}) {
  const taskSummaries = tasks
    .filter((task) => cases.some((item) => item.task === task.id))
    .map((task) => summarizeTaskMeasurement(task, cases));
  return {
    tasks: taskSummaries,
    metrics: summarizeOverallMetrics(taskSummaries),
  };
}

export function summarizePerformance(cases = []) {
  const trials = cases.flatMap((item) => item.trials ?? []);
  const timedTrials = trials.filter((trial) => Number.isFinite(trial?.timingMs?.total));
  const stageNames = [...new Set(timedTrials.flatMap((trial) => Object.keys(trial.timingMs)))]
    .filter((name) => name !== 'total')
    .sort();
  const observations = trials.map((trial) => trial.observation).filter(Boolean);
  const requestedObservationMs = observations.reduce(
    (sum, item) => sum + Number(item.requestedSec ?? 0) * 1000,
    0,
  );
  const actualObservationMs = observations.reduce(
    (sum, item) => sum + Number(item.actualSec ?? 0) * 1000,
    0,
  );
  return {
    trialCount: trials.length,
    timedTrialCount: timedTrials.length,
    trialDurationMs: statistics(timedTrials.map((trial) => trial.timingMs.total)),
    stages: Object.fromEntries(stageNames.map((name) => [
      name,
      statistics(timedTrials
        .map((trial) => trial.timingMs[name])
        .filter((value) => Number.isFinite(value))),
    ])),
    observation: {
      count: observations.length,
      earlyStopped: observations.filter((item) => item.earlyStopped === true).length,
      requestedMs: roundMetric(requestedObservationMs),
      actualMs: roundMetric(actualObservationMs),
      savedMs: roundMetric(Math.max(0, requestedObservationMs - actualObservationMs)),
    },
  };
}

function summarizeTaskMeasurement(task, cases) {
  const taskCases = cases.filter((item) => item.task === task.id);
  const positive = taskCases.filter((item) => item.expectation?.minEvents >= 1);
  const negative = taskCases.filter((item) => item.expectation?.maxEvents === 0);
  const custom = taskCases.filter((item) => !positive.includes(item) && !negative.includes(item));
  const errors = taskCases.filter((item) => item.status === 'ERROR').length;
  const taskConfigHashes = [...new Set([
    ...(Array.isArray(task.taskConfigHashes) ? task.taskConfigHashes : []),
    task.taskConfigSha256,
    ...taskCases.flatMap((item) =>
      (item.trials ?? []).map((trial) => trial.taskConfigSha256)),
  ].filter(Boolean))].sort();
  const positiveMetric = metric(positive);
  const negativeMetric = metric(negative);
  return {
    id: task.id,
    displayName: task.displayName ?? task.id,
    kind: task.kind,
    algorithmId: String(task.algorithmId ?? ''),
    algorithmCode: String(task.algorithmCode ?? task.algorithmId ?? ''),
    configSource: task.configSource ?? 'unknown',
    taskConfigHashes,
    positive: positiveMetric,
    negative: negativeMetric,
    custom: metric(custom),
    falsePositiveRate: negativeMetric.rate == null ? null : 1 - negativeMetric.rate,
    errors,
    coverage: {
      positive: positive.length > 0,
      negative: negative.length > 0,
      customCases: custom.length,
      totalCases: taskCases.length,
    },
  };
}

function summarizeOverallMetrics(tasks) {
  const positivePassed = tasks.reduce((sum, task) => sum + task.positive.passed, 0);
  const positiveTotal = tasks.reduce((sum, task) => sum + task.positive.total, 0);
  const negativePassed = tasks.reduce((sum, task) => sum + task.negative.passed, 0);
  const negativeTotal = tasks.reduce((sum, task) => sum + task.negative.total, 0);
  const positiveRates = tasks.map((task) => task.positive.rate).filter((rate) => rate != null);
  const negativeRates = tasks.map((task) => task.negative.rate).filter((rate) => rate != null);
  const microPositive = positiveTotal ? positivePassed / positiveTotal : null;
  const microNegative = negativeTotal ? negativePassed / negativeTotal : null;
  const macroPositive = mean(positiveRates);
  const macroNegative = mean(negativeRates);
  return {
    micro: {
      positiveHitRate: microPositive,
      negativeCleanRate: microNegative,
      negativeFalsePositiveRate: microNegative == null ? null : 1 - microNegative,
    },
    macro: {
      positiveHitRate: macroPositive,
      negativeCleanRate: macroNegative,
      negativeFalsePositiveRate: macroNegative == null ? null : 1 - macroNegative,
    },
    coverage: {
      taskCount: tasks.length,
      tasksWithPositive: positiveRates.length,
      tasksWithNegative: negativeRates.length,
      missingPositiveTasks: tasks.filter((task) => !task.coverage.positive).map((task) => task.id),
      missingNegativeTasks: tasks.filter((task) => !task.coverage.negative).map((task) => task.id),
    },
  };
}

function mean(values) {
  return values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : null;
}

function metric(items) {
  const measured = items.filter((item) => item.status === 'PASS' || item.status === 'FAIL');
  const passed = measured.filter((item) => item.status === 'PASS').length;
  return { passed, total: measured.length, rate: measured.length ? passed / measured.length : null };
}

function sanitizeCaseSummary(item) {
  const trials = item.trials ?? [];
  const alertArtifacts = [];
  const seen = new Set();
  for (const trial of trials) {
    for (const event of trial.events ?? []) {
      for (const key of ['detectedPictureArtifact', 'fullPictureArtifact']) {
        const artifact = event?.[key];
        if (!artifact?.path || seen.has(artifact.path)) continue;
        seen.add(artifact.path);
        alertArtifacts.push({
          path: artifact.path,
          sha256: artifact.sha256,
          contentType: artifact.contentType,
          sizeBytes: artifact.sizeBytes,
        });
      }
    }
  }
  return {
    id: item.id,
    task: item.task,
    expectation: item.expectation,
    status: item.status,
    cleanupBlocked: item.cleanupBlocked === true,
    eventCount: item.eventCount ?? null,
    trialCount: item.trials?.length ?? item.trialCount ?? null,
    reasons: item.reasons ?? [],
    alertArtifacts,
    eventConvergence: {
      settledTrials: trials.filter((trial) => trial.eventCollection?.settled === true).length,
      unsettledTrials: trials.filter((trial) => trial.eventCollection?.settled === false).length,
      unknownTrials: trials.filter((trial) => trial.eventCollection?.settled == null).length,
    },
    cleanup: summarizeCaseCleanup(trials),
  };
}

function summarizeCaseCleanup(trials) {
  let verifiedTrials = 0;
  let failedTrials = 0;
  let unknownTrials = 0;
  for (const trial of trials) {
    if (!trial.cleanup) {
      unknownTrials += 1;
      continue;
    }
    const verified = trial.cleanup.task?.verified === true
      && trial.cleanup.channel?.verified === true
      && trial.cleanup.rtsp?.stopped !== false;
    if (verified) verifiedTrials += 1;
    else failedTrials += 1;
  }
  return { verifiedTrials, failedTrials, unknownTrials };
}

function statistics(values) {
  if (!values.length) {
    return { count: 0, total: 0, mean: null, p50: null, p90: null, max: null };
  }
  const sorted = values.map(Number).sort((a, b) => a - b);
  const total = sorted.reduce((sum, value) => sum + value, 0);
  return {
    count: sorted.length,
    total: roundMetric(total),
    mean: roundMetric(total / sorted.length),
    p50: roundMetric(percentile(sorted, 0.5)),
    p90: roundMetric(percentile(sorted, 0.9)),
    max: roundMetric(sorted.at(-1)),
  };
}

function percentile(sorted, value) {
  return sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * value))];
}

function roundMetric(value) {
  return Math.round(Number(value) * 1000) / 1000;
}
