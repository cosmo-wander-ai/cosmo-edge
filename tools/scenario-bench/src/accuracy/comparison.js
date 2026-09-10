import fs from 'node:fs';
import path from 'node:path';

import { summarizeAccuracyMeasurements } from './evaluator.js';
import { escapeHtml, formatDuration } from './report.js';
import { ensurePrivateDir, writePrivateFile } from './private-files.js';
import { ACCURACY_PROTOCOL_VERSION, ACCURACY_SUMMARY_SCHEMA_VERSION } from './protocol.js';
import { assertAccuracySummary } from './summary.js';
import { sha256File, stableStringify } from './utils.js';

export function normalizeAccuracyComparisonSummary(value) {
  if (value?.protocolVersion !== ACCURACY_PROTOCOL_VERSION
      || value?.schemaVersion !== ACCURACY_SUMMARY_SCHEMA_VERSION) {
    throw new Error('accuracy comparison accepts only v4 summaries');
  }
  assertAccuracySummary(value);
  return normalizeCommon(value);
}

export function compareAccuracySummaries(referenceValue, candidateValues) {
  if (!Array.isArray(candidateValues) || candidateValues.length === 0) {
    throw new Error('compare requires at least one candidate');
  }
  const reference = normalizeAccuracyComparisonSummary(referenceValue);
  const candidates = candidateValues.map(normalizeAccuracyComparisonSummary);
  const compared = candidates.map((candidate, index) => {
    const mismatches = comparisonIdentityMismatches(reference, candidate);
    if (mismatches.length) {
      throw new Error(`accuracy comparison sample mismatch for candidate ${index + 1}: ${mismatches.join(', ')}`);
    }
    return compareCandidate(reference, candidate, index + 1);
  });
  return {
    schemaVersion: 1,
    protocolVersion: ACCURACY_PROTOCOL_VERSION,
    evidenceKind: 'cosmo-accuracy-comparison',
    identity: structuredClone(reference.identity),
    reference: publicSnapshot(reference),
    candidates: compared,
  };
}

export function writeAccuracyComparison(outputDir, comparison, sources) {
  const normalizedSources = validateComparisonSources(sources);
  const document = { ...comparison, sources: normalizedSources };
  const output = path.resolve(outputDir);
  if (fs.existsSync(output) && fs.readdirSync(output).length) {
    throw new Error(`accuracy comparison output directory must be new and empty: ${output}`);
  }
  ensurePrivateDir(output);
  const jsonPath = path.join(output, 'comparison.json');
  const htmlPath = path.join(output, 'report.html');
  const integrityPath = path.join(output, 'integrity.json');
  writePrivateFile(jsonPath, `${JSON.stringify(document, null, 2)}\n`);
  writePrivateFile(htmlPath, renderAccuracyComparisonHtml(document));
  const artifacts = [jsonPath, htmlPath].map((file) => ({
    path: path.basename(file),
    sha256: sha256File(file),
    sizeBytes: fs.statSync(file).size,
  }));
  writePrivateFile(integrityPath, `${JSON.stringify({
    schemaVersion: 1,
    evidenceKind: 'cosmo-accuracy-comparison-integrity',
    sourceSummaries: normalizedSources,
    artifacts,
  }, null, 2)}\n`);
  return { outputDir: output, jsonPath, htmlPath, integrityPath };
}

function validateComparisonSources(sources) {
  if (!Array.isArray(sources) || sources.length < 2) {
    throw new Error('accuracy comparison source summaries are required');
  }
  const roles = new Set();
  return sources.map((source, index) => {
    const role = String(source?.role ?? '');
    const sha256 = String(source?.sha256 ?? '');
    if (!role || roles.has(role) || !/^[a-f0-9]{64}$/u.test(sha256)) {
      throw new Error(`accuracy comparison source summaries are invalid at index ${index}`);
    }
    roles.add(role);
    return { role, sha256 };
  });
}

function normalizeCommon(value) {
  const measurements = summarizeAccuracyMeasurements({ tasks: value.tasks, cases: value.cases });
  const execution = structuredClone(value.execution);
  const identity = {
    selectionSha256: execution.selection?.sha256,
    caseIds: value.cases.map((item) => item.id).sort(),
  };
  return {
    runId: value.runId ?? null,
    identity,
    execution,
    suite: structuredClone(value.suite),
    metrics: measurements.metrics,
    tasks: measurements.tasks,
    cases: value.cases.map((item) => ({
      id: item.id,
      task: item.task,
      status: item.status,
      eventConvergence: item.eventConvergence ?? null,
      cleanup: item.cleanup ?? null,
      trialCount: item.trialCount ?? 0,
    })),
    timing: {
      wallDurationMs: numberOrNull(value.wallDurationMs),
      activeDurationMs: numberOrNull(value.activeDurationMs),
      trialWorkMs: numberOrNull(value.performance?.trialDurationMs?.total),
      admissionMs: numberOrNull(value.admissionMs),
    },
    dimensions: {
      concurrency: execution.concurrency,
      profile: execution.profile,
      sourceMode: value.suite.sourceMode,
      targetChip: value.suite.targetChip,
      measurementConfigSha256: execution.measurementConfigSha256,
      softwareVersion: value.device?.softwareVersion ?? null,
      suiteId: value.suite.id,
      suiteSha256: value.suite.suiteSha256,
      algorithms: value.tasks.map((task) => ({
        id: task.id,
        algorithmId: task.algorithmId,
        algorithmCode: task.algorithmCode,
        configSource: task.configSource,
        taskConfigHashes: [...task.taskConfigHashes],
      })),
      repositoryIdentity: value.repository ?? null,
      toolIdentitySha256: value.toolIdentitySha256 ?? null,
    },
    health: summarizeHealth(value, value.executionOutcome),
  };
}

function comparisonIdentityMismatches(reference, candidate) {
  const mismatches = [];
  if (reference.identity.selectionSha256 !== candidate.identity.selectionSha256) {
    mismatches.push('selectionSha256');
  }
  if (stableStringify(reference.identity.caseIds) !== stableStringify(candidate.identity.caseIds)) {
    mismatches.push('caseIds');
  }
  return mismatches;
}

function compareCandidate(reference, candidate, index) {
  const beforeById = new Map(reference.cases.map((item) => [item.id, item]));
  const allChanges = candidate.cases
    .map((item) => ({
      id: item.id,
      task: item.task,
      before: beforeById.get(item.id)?.status,
      after: item.status,
    }))
    .filter((item) => item.before !== item.after);
  const beforeTasks = new Map(reference.tasks.map((task) => [task.id, task]));
  const tasks = candidate.tasks.map((task) => {
    const before = beforeTasks.get(task.id);
    return {
      id: task.id,
      displayName: task.displayName,
      referenceAlgorithmId: before?.algorithmId ?? null,
      algorithmId: task.algorithmId,
      referenceAlgorithmCode: before?.algorithmCode ?? null,
      algorithmCode: task.algorithmCode,
      referenceTaskConfigHashes: before?.taskConfigHashes ?? [],
      taskConfigHashes: task.taskConfigHashes,
      positiveHitRate: task.positive.rate,
      positiveHitRateDeltaPoints: pointDelta(task.positive.rate, before?.positive.rate),
      negativeCleanRate: task.negative.rate,
      negativeCleanRateDeltaPoints: pointDelta(task.negative.rate, before?.negative.rate),
    };
  });
  const referenceWall = reference.timing.wallDurationMs;
  const candidateWall = candidate.timing.wallDurationMs;
  return {
    index,
    runId: candidate.runId,
    dimensions: structuredClone(candidate.dimensions),
    contextChanges: comparisonContextChanges(reference.dimensions, candidate.dimensions),
    timing: {
      referenceWallMs: referenceWall,
      wallDurationMs: candidateWall,
      trialWorkMs: candidate.timing.trialWorkMs,
      speedup: referenceWall != null && candidateWall > 0 ? referenceWall / candidateWall : null,
      savedWallMs: referenceWall != null && candidateWall != null ? referenceWall - candidateWall : null,
    },
    metrics: {
      micro: metricDelta(reference.metrics.micro, candidate.metrics.micro),
      macro: metricDelta(reference.metrics.macro, candidate.metrics.macro),
      coverage: structuredClone(candidate.metrics.coverage),
    },
    tasks,
    transitions: {
      allChanges,
      passToNonPass: allChanges
        .filter((item) => item.before === 'PASS' && item.after !== 'PASS')
        .map((item) => item.id),
      nonPassToPass: allChanges
        .filter((item) => item.before !== 'PASS' && item.after === 'PASS')
        .map((item) => item.id),
    },
    health: structuredClone(candidate.health),
  };
}

function publicSnapshot(value) {
  return {
    runId: value.runId,
    dimensions: structuredClone(value.dimensions),
    timing: structuredClone(value.timing),
    metrics: structuredClone(value.metrics),
    health: structuredClone(value.health),
  };
}

function comparisonContextChanges(reference, candidate) {
  const fields = [
    'concurrency', 'profile', 'sourceMode', 'targetChip', 'measurementConfigSha256',
    'softwareVersion', 'suiteId', 'suiteSha256', 'algorithms',
    'repositoryIdentity', 'toolIdentitySha256',
  ];
  return fields
    .filter((field) => stableStringify(reference[field]) !== stableStringify(candidate[field]))
    .map((field) => ({
      field,
      reference: structuredClone(reference[field] ?? null),
      candidate: structuredClone(candidate[field] ?? null),
    }));
}

function metricDelta(reference, candidate) {
  return {
    positiveHitRate: candidate.positiveHitRate,
    positiveHitRateDeltaPoints: pointDelta(candidate.positiveHitRate, reference.positiveHitRate),
    negativeCleanRate: candidate.negativeCleanRate,
    negativeCleanRateDeltaPoints: pointDelta(candidate.negativeCleanRate, reference.negativeCleanRate),
    negativeFalsePositiveRate: candidate.negativeFalsePositiveRate,
    negativeFalsePositiveRateDeltaPoints: pointDelta(
      candidate.negativeFalsePositiveRate,
      reference.negativeFalsePositiveRate,
    ),
  };
}

function pointDelta(candidate, reference) {
  return candidate == null || reference == null ? null : round((candidate - reference) * 100);
}

function summarizeHealth(value, executionOutcome) {
  const cases = value.cases ?? [];
  const convergence = cases.map((item) => item.eventConvergence).filter(Boolean);
  const cleanup = cases.map((item) => item.cleanup).filter(Boolean);
  const knownConvergenceTrials = convergence.reduce(
    (sum, item) => sum + item.settledTrials + item.unsettledTrials + item.unknownTrials,
    0,
  );
  const knownCleanupTrials = cleanup.reduce(
    (sum, item) => sum + item.verifiedTrials + item.failedTrials + item.unknownTrials,
    0,
  );
  const totalTrials = cases.reduce((sum, item) => sum + Number(item.trialCount ?? 0), 0);
  return {
    executionOutcome,
    admissionStatus: value.admission?.status ?? 'UNVERIFIED',
    errorCases: cases.filter((item) => item.status === 'ERROR').length,
    settledTrials: convergence.reduce((sum, item) => sum + item.settledTrials, 0),
    unsettledTrials: convergence.reduce((sum, item) => sum + item.unsettledTrials, 0),
    eventConvergenceUnknownTrials: convergence.reduce((sum, item) => sum + item.unknownTrials, 0)
      + Math.max(0, totalTrials - knownConvergenceTrials),
    cleanupVerifiedTrials: cleanup.reduce((sum, item) => sum + item.verifiedTrials, 0),
    cleanupFailedTrials: cleanup.reduce((sum, item) => sum + item.failedTrials, 0),
    cleanupUnknownTrials: cleanup.reduce((sum, item) => sum + item.unknownTrials, 0)
      + Math.max(0, totalTrials - knownCleanupTrials),
  };
}

function renderAccuracyComparisonHtml(comparison) {
  const rows = comparison.candidates.map((candidate) => `<tr>
    <td>candidate ${candidate.index}</td><td>${escapeHtml(candidate.dimensions.concurrency)}</td>
    <td>${formatDuration(candidate.timing.wallDurationMs)}</td>
    <td>${formatDuration(candidate.timing.trialWorkMs)}</td>
    <td>${candidate.timing.speedup == null ? '—' : `${candidate.timing.speedup.toFixed(2)}x`}</td>
    <td>${formatSignedDuration(candidate.timing.savedWallMs)}</td>
    <td>${formatPoints(candidate.metrics.micro.positiveHitRateDeltaPoints)}</td>
    <td>${formatPoints(candidate.metrics.micro.negativeCleanRateDeltaPoints)}</td>
    <td>${escapeHtml(candidate.transitions.allChanges.length)}</td>
    <td>${escapeHtml(candidate.health.errorCases)}</td>
    <td>${escapeHtml(candidate.health.unsettledTrials)}</td><td>${escapeHtml(candidate.health.cleanupFailedTrials)}</td>
  </tr>`).join('');
  const details = comparison.candidates.map((candidate) => {
    const taskRows = candidate.tasks.map((task) => `<tr><td>${escapeHtml(task.displayName)}<br><span class="note">${escapeHtml(task.id)}</span></td><td>${escapeHtml(task.referenceAlgorithmId ?? '—')} → ${escapeHtml(task.algorithmId)}</td><td>${escapeHtml(shortHashes(task.referenceTaskConfigHashes))} → ${escapeHtml(shortHashes(task.taskConfigHashes))}</td><td>${formatPoints(task.positiveHitRateDeltaPoints)}</td><td>${formatPoints(task.negativeCleanRateDeltaPoints)}</td></tr>`).join('');
    return `<h2>candidate ${candidate.index}</h2>
      <p>测量环境/算法变化：${escapeHtml(candidate.contextChanges.map((item) => item.field).join(', ') || '无')}。</p>
      <p>PASS → 非 PASS：${candidate.transitions.passToNonPass.length}；非 PASS → PASS：${candidate.transitions.nonPassToPass.length}。完整 case 差异见 comparison.json。</p>
      <table><thead><tr><th>任务</th><th>算法 ID</th><th>配置 hash</th><th>正检 Δpp</th><th>误检无告警 Δpp</th></tr></thead><tbody>${taskRows}</tbody></table>`;
  }).join('');
  return `<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'self'; style-src 'unsafe-inline'; object-src 'none'">
<title>CosmoEdge 视频样本测量对比</title><style>body{font-family:system-ui,"Microsoft YaHei",sans-serif;margin:24px;color:#1f2937}table{border-collapse:collapse;width:100%}th,td{border:1px solid #cbd5e1;padding:8px;text-align:center}th{background:#f1f5f9}.note{color:#64748b;font-size:12px}</style></head>
<body><h1>CosmoEdge 视频样本测量对比</h1>
<p class="note">参考墙钟耗时：${formatDuration(comparison.reference.timing.wallDurationMs)}；protocol v4</p>
<table><thead><tr><th>结果</th><th>并发</th><th>Wall</th><th>Trial work</th><th>加速</th><th>Saved wall</th><th>正检变化</th><th>误检变化</th><th>状态变化</th><th>ERROR</th><th>Unsettled</th><th>Cleanup failed</th></tr></thead><tbody>${rows}</tbody></table>
${details}
</body></html>\n`;
}

function formatPoints(value) {
  return value == null ? '—' : `${value >= 0 ? '+' : ''}${round(value)} pp`;
}

function shortHashes(values) {
  return (values ?? []).map((value) => String(value).slice(0, 12)).join(', ') || '—';
}

function formatSignedDuration(value) {
  if (value == null || !Number.isFinite(Number(value))) return '—';
  const number = Number(value);
  return `${number < 0 ? '-' : ''}${formatDuration(Math.abs(number))}`;
}

function numberOrNull(value) {
  if (value == null || value === '') return null;
  return Number.isFinite(Number(value)) ? Number(value) : null;
}

function round(value) {
  return Math.round(Number(value) * 1_000_000) / 1_000_000;
}
