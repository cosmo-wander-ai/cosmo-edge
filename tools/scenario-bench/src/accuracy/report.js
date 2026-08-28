export function renderAccuracyHtml(summary) {
  const taskRows = (summary.tasks ?? []).map((task) => `<tr>
    <td>${escapeHtml(task.displayName)}<br><span class="note">${escapeHtml(task.id)}</span></td>
    <td>${escapeHtml(task.kind)}</td>
    <td>${escapeHtml(task.algorithmId)}</td>
    <td>${escapeHtml(task.algorithmCode)}</td>
    <td>${escapeHtml(task.configSource)}<br><span class="note">${escapeHtml(shortHashes(task.taskConfigHashes))}</span></td>
    <td>${formatMetric(task.positive)}</td>
    <td>${formatMetric(task.negative)}</td>
    <td>${formatRate(task.falsePositiveRate)}</td>
    <td>${escapeHtml(task.errors ?? 0)}</td>
    <td>${formatCoverage(task.coverage)}</td>
  </tr>`).join('\n');
  const taskNames = new Map((summary.tasks ?? []).map((task) => [task.id, task.displayName]));
  const caseRows = (summary.cases ?? []).map((item) => `<tr>
    <td>${escapeHtml(item.id)}</td>
    <td>${escapeHtml(taskNames.get(item.task) ?? item.task)}<br><span class="note">${escapeHtml(item.task)}</span></td>
    <td>${escapeHtml(item.eventCount ?? '—')}</td>
    <td class="${statusClass(item.status)}">${escapeHtml(item.status)}</td>
    <td>${renderArtifacts(item.alertArtifacts)}</td>
  </tr>`).join('\n');
  const reasons = [
    ...(summary.executionReasons ?? []).map((reason) => `执行：${reason}`),
  ];
  const reasonHtml = reasons.length
    ? `<ul>${reasons.map((reason) => `<li>${escapeHtml(reason)}</li>`).join('')}</ul>`
    : '<p>无执行问题。</p>';
  const performance = renderPerformance(summary.performance);
  const execution = summary.execution ?? {};
  const metrics = summary.metrics ?? {};
  const coverage = metrics.coverage ?? {};
  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta http-equiv="Content-Security-Policy" content="default-src 'self'; img-src 'self' data:; style-src 'unsafe-inline'; object-src 'none'">
  <title>CosmoEdge 视频样本级算法效果评测</title>
  <style>
    body{font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif;margin:24px;color:#1f2937;max-width:1280px}
    h1{font-size:24px}h2{font-size:18px;margin-top:28px;border-left:4px solid #2563eb;padding-left:8px}
    .meta{background:#f8fafc;border:1px solid #e2e8f0;border-radius:6px;padding:12px;line-height:1.7}
    .metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}.card{border:1px solid #cbd5e1;border-radius:6px;padding:12px}
    table{border-collapse:collapse;width:100%;font-size:13px}th,td{border:1px solid #cbd5e1;padding:7px;text-align:center}
    th{background:#f1f5f9}.pass,.completed{color:#15803d}.fail{color:#b91c1c}.blocked,.flaky,.error{color:#b45309}.not-evaluated{color:#475569}
    img.alarm{max-width:140px;max-height:96px;border:1px solid #cbd5e1}
    .note{font-size:12px;color:#64748b}
  </style>
</head>
<body>
  <h1>CosmoEdge 视频样本级算法效果评测</h1>
  <div class="meta">
    <b>Suite</b>：${escapeHtml(summary.suite?.displayName ?? summary.suite?.id ?? '?')} <span class="note">(${escapeHtml(summary.suite?.id ?? '?')})</span><br>
    <b>Profile</b>：${escapeHtml(execution.profile ?? '?')} &nbsp; <b>并发</b>：${escapeHtml(execution.concurrency ?? '?')}<br>
    <b>选中样本</b>：${escapeHtml(execution.selection?.count ?? '?')}<br>
    <b>目标芯片</b>：${escapeHtml(summary.suite?.targetChip ?? '?')} &nbsp; <b>视频协议</b>：${escapeHtml(summary.suite?.sourceMode ?? '?')}<br>
    <b>墙钟耗时</b>：${formatDuration(summary.wallDurationMs)} &nbsp; <b>活跃耗时</b>：${formatDuration(summary.activeDurationMs)} &nbsp; <b>准入耗时</b>：${formatDuration(summary.admissionMs)}<br>
    <b>执行结果</b>：<span class="${statusClass(summary.executionOutcome)}">${escapeHtml(summary.executionOutcome ?? '?')}</span><br>
    <b>结果版本</b>：protocol ${escapeHtml(summary.protocolVersion ?? '?')}
  </div>
	  <p class="note">本报告是事件级样本测量，不代表帧级 Precision、Recall、F1 或 mAP。FAIL 是算法测量结果；ERROR 单独显示且不进入命中率分母。</p>
  <h2>整体指标</h2>
  <div class="metrics">
    <div class="card"><b>Micro</b><br>正检命中：${formatRate(metrics.micro?.positiveHitRate)}<br>误检无告警：${formatRate(metrics.micro?.negativeCleanRate)}<br>误报率：${formatRate(metrics.micro?.negativeFalsePositiveRate)}</div>
    <div class="card"><b>Macro</b><br>正检命中：${formatRate(metrics.macro?.positiveHitRate)}<br>误检无告警：${formatRate(metrics.macro?.negativeCleanRate)}<br>误报率：${formatRate(metrics.macro?.negativeFalsePositiveRate)}</div>
    <div class="card"><b>Coverage</b><br>任务：${escapeHtml(coverage.taskCount ?? 0)}<br>缺正检：${escapeHtml((coverage.missingPositiveTasks ?? []).join(', ') || '无')}<br>缺误检：${escapeHtml((coverage.missingNegativeTasks ?? []).join(', ') || '无')}</div>
  </div>
  <h2>算法汇总</h2>
	  <table><thead><tr><th>任务</th><th>类型</th><th>算法 ID</th><th>算法 Code</th><th>配置</th><th>正检命中</th><th>误检无告警</th><th>误报率</th><th>ERROR</th><th>覆盖</th></tr></thead>
  <tbody>${taskRows}</tbody></table>
  ${performance}
  <h2>用例明细</h2>
  <table><thead><tr><th>用例</th><th>任务</th><th>事件数</th><th>状态</th><th>告警图片</th></tr></thead>
  <tbody>${caseRows}</tbody></table>
  <h2>结论依据</h2>${reasonHtml}
</body>
</html>\n`;
}

export function renderThresholdDiagnosticHtml(diagnostic) {
  const rows = diagnostic.points.map((point) => `<tr>
    <td>${escapeHtml(point.threshold)}</td>
    <td class="${statusClass(point.status)}">${escapeHtml(point.status)}</td>
    <td>${escapeHtml(point.trialStatuses.join(', '))}</td>
  </tr>`).join('\n');
  const reasons = diagnostic.executionReasons.length
    ? `<ul>${diagnostic.executionReasons.map((reason) => `<li>${escapeHtml(reason)}</li>`).join('')}</ul>`
    : '<p>无执行阻断。</p>';
  return `<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'self'; style-src 'unsafe-inline'; object-src 'none'">
<title>CosmoEdge 阈值诊断</title><style>body{font-family:system-ui,"Microsoft YaHei",sans-serif;margin:24px;color:#1f2937;max-width:960px}table{border-collapse:collapse;width:100%}th,td{border:1px solid #cbd5e1;padding:8px;text-align:center}th{background:#f1f5f9}.stable-pass{color:#15803d}.stable-fail,.error{color:#b91c1c}.flaky,.blocked{color:#b45309}.note{color:#64748b}</style></head>
<body><h1>CosmoEdge 阈值诊断</h1>
<p><b>Case</b>：${escapeHtml(diagnostic.caseId)}　<b>执行结果</b>：${escapeHtml(diagnostic.executionOutcome)}</p>
<p class="note">参数：${escapeHtml(diagnostic.parameterKeys.join(', '))}。诊断不会自动修改配置或基线。</p>
<table><thead><tr><th>阈值</th><th>稳定状态</th><th>Trial 状态</th></tr></thead><tbody>${rows}</tbody></table>
<p><b>稳定通过</b>：${escapeHtml(diagnostic.stablePassValues.join(', ') || '无')}<br>
<b>稳定失败</b>：${escapeHtml(diagnostic.stableFailValues.join(', ') || '无')}<br>
<b>波动</b>：${escapeHtml(diagnostic.flakyValues.join(', ') || '无')}</p>
<h2>执行依据</h2>${reasons}</body></html>\n`;
}

function renderPerformance(performance) {
  if (!performance || performance.timedTrialCount === 0) return '';
  const rows = [
    ['trialTotal', performance.trialDurationMs],
    ...Object.entries(performance.stages ?? {}),
  ].filter(([, metric]) => metric).map(([name, metric]) => `<tr>
    <td>${escapeHtml(name)}</td><td>${escapeHtml(metric.count)}</td><td>${formatDuration(metric.total)}</td>
    <td>${formatDuration(metric.mean)}</td><td>${formatDuration(metric.p50)}</td>
    <td>${formatDuration(metric.p90)}</td><td>${formatDuration(metric.max)}</td>
  </tr>`).join('');
  const observation = performance.observation ?? {};
  return `<h2>执行耗时</h2>
  <p class="note">有效耗时 trial：${escapeHtml(performance.timedTrialCount)}/${escapeHtml(performance.trialCount)}；提前结束：${escapeHtml(observation.earlyStopped ?? 0)}；节省观察时间：${formatDuration(observation.savedMs)}</p>
  <table><thead><tr><th>阶段</th><th>trial 数</th><th>累计</th><th>平均</th><th>P50</th><th>P90</th><th>最大</th></tr></thead>
  <tbody>${rows}</tbody></table>`;
}

export function formatDuration(value) {
  if (value == null || !Number.isFinite(Number(value))) return '—';
  const milliseconds = Math.max(0, Number(value));
  if (milliseconds < 1000) return `${milliseconds.toFixed(0)} ms`;
  const totalSeconds = Math.round(milliseconds / 1000);
  if (totalSeconds < 60) return `${(milliseconds / 1000).toFixed(2)} s`;
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return [
    hours ? `${hours} 小时` : null,
    minutes ? `${minutes} 分钟` : null,
    seconds || (!hours && !minutes) ? `${seconds} 秒` : null,
  ].filter(Boolean).join(' ');
}

export function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function formatMetric(metric) {
  if (!metric || metric.total === 0 || metric.rate == null) return '—';
  return `${escapeHtml(metric.passed)}/${escapeHtml(metric.total)} (${(metric.rate * 100).toFixed(1)}%)`;
}

function formatRate(value) {
  return value == null || !Number.isFinite(Number(value)) ? '—' : `${(Number(value) * 100).toFixed(1)}%`;
}

function formatCoverage(coverage) {
  if (!coverage) return '—';
  return `正检:${coverage.positive ? '有' : '无'} / 误检:${coverage.negative ? '有' : '无'}`;
}

function shortHashes(values) {
  return (values ?? []).map((value) => String(value).slice(0, 12)).join(', ') || '—';
}

function statusClass(status) {
  const normalized = String(status ?? '').toLowerCase().replaceAll('_', '-').replace(/[^a-z-]/gu, '');
  return normalized || 'blocked';
}

function renderArtifacts(artifacts) {
  const safe = (artifacts ?? []).filter((artifact) =>
    /^artifacts\/alerts\/[a-f0-9]{64}\.(?:jpg|png|webp|gif)$/u.test(String(artifact?.path ?? '')));
  if (!safe.length) return '—';
  return safe.slice(0, 6).map((artifact) =>
    `<a href="${escapeHtml(artifact.path)}" target="_blank" rel="noopener noreferrer"><img class="alarm" src="${escapeHtml(artifact.path)}" alt="告警图片"></a>`).join(' ');
}
