import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { page, table, notice } from './generate-v1.1-benchmark-pages.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = path.join(root, 'docs/benchmarks/scenario-bench/ax650n/short-capacity.json');
const output = path.join(root, 'docs/.vitepress/dist/benchmarks/scenario-bench/ax650n');
const digest = /^[a-f0-9]{64}$/;

export function validateShortCapacity(data) {
  if (data.$schema !== 'short-capacity.schema.json' || data.schemaVersion !== 1 || data.platform !== 'AX650N' || !Array.isArray(data.observations) || data.observations.length === 0 || !Array.isArray(data.series) || data.series.length === 0) throw new Error('invalid AX report');
  if (!digest.test(data.input?.sha256) || !digest.test(data.input?.referenceV11Sha256)) throw new Error('missing input identity');
  if (!digest.test(data.models?.personSha256) || !digest.test(data.models?.helmetSha256)) throw new Error('missing model identity');
  if (data.input.sha256 === data.input.referenceV11Sha256) throw new Error('input comparison must match evidence');
  const series = new Set(data.series.map((entry) => entry.id));
  if (series.size !== data.series.length) throw new Error('duplicate series');
  if (data.series.some((entry) => !/^[a-z0-9-]+$/.test(entry.id))) throw new Error('invalid series identity');
  if (data.series.some((entry) => entry.engineSha256 !== undefined && !digest.test(entry.engineSha256))) throw new Error('invalid engine identity');
  if (!digest.test(data.series.find((entry) => entry.id === 'v7')?.engineSha256)) throw new Error('missing v7 engine identity');
  const ids = new Set();
  for (const item of data.observations) {
    if (ids.has(item.id) || !series.has(item.series) || !['person', 'helmet', 'mixed'].includes(item.workload) || !['PASS', 'FAIL', 'INCOMPLETE'].includes(item.result) || !Number.isInteger(item.channels) || item.channels < 1 || !(item.targetFps > 0) || !(item.plannedHoldSeconds > 0) || !digest.test(item.sourceSha256)) throw new Error(`invalid observation: ${item.id}`);
    for (const field of ['sourceMetricsSha256', 'sourceAuditSha256']) {
      if (item[field] !== null && !digest.test(item[field])) throw new Error(`invalid ${field}: ${item.id}`);
    }
    if (item.sampleSpanSeconds !== null && !(item.sampleSpanSeconds >= 0)) throw new Error(`invalid sample span: ${item.id}`);
    if (item.minimumNodeFps !== null && !(item.minimumNodeFps >= 0)) throw new Error(`invalid FPS: ${item.id}`);
    for (const field of ['cpuPeakPercent', 'memoryPeakPercent', 'acceleratorPeakPercent', 'diskPeakPercent', 'decodeDiscardDelta', 'classifierForwardSuccessDelta', 'classifierForwardFailureDelta', 'classifierNonForwardFailureDelta']) {
      if (item[field] !== null && !(Number.isFinite(item[field]) && item[field] >= 0)) throw new Error(`invalid ${field}: ${item.id}`);
      if (item[field] !== null && field.endsWith('Percent') && item[field] > 100) throw new Error(`invalid ${field}: ${item.id}`);
      if (item[field] !== null && field.endsWith('Delta') && !Number.isInteger(item[field])) throw new Error(`invalid ${field}: ${item.id}`);
    }
    if (item.result === 'PASS' && item.minimumNodeFps === null) throw new Error(`missing passing FPS: ${item.id}`);
    if (item.result === 'PASS' && item.reason !== null) throw new Error(`passing point has a stop reason: ${item.id}`);
    if (item.result !== 'PASS' && !item.reason) throw new Error(`missing stop reason: ${item.id}`);
    ids.add(item.id);
  }
  return data;
}

export function renderShortCapacity(data, locale) {
  validateShortCapacity(data);
  const zh = locale === 'zh-CN';
  const labels = zh ? { person: '人员检测', helmet: '安全帽分析', mixed: '混合任务' } : { person: 'Person detection', helmet: 'Helmet analysis', mixed: 'Mixed workload' };
  const seriesOrder = [...data.series].sort((a, b) => Number(b.id === 'v7') - Number(a.id === 'v7'));
  const sections = seriesOrder.map(({ id: series, engineSha256 }) => {
    const observations = data.observations.filter((entry) => entry.series === series);
    const passing = observations.filter((entry) => entry.workload === 'mixed' && entry.result === 'PASS');
    const maxPassing = passing.length ? Math.max(...passing.map((entry) => entry.channels)) : null;
    const rows = observations.map((entry) => [
      labels[entry.workload], entry.targetFps, entry.channels, `${entry.plannedHoldSeconds} s`,
      entry.sampleSpanSeconds === null ? '—' : `${entry.sampleSpanSeconds} s`,
      entry.minimumNodeFps === null ? '—' : entry.minimumNodeFps.toFixed(3),
      entry.cpuPeakPercent === null ? '—' : `${entry.cpuPeakPercent}%`,
      entry.memoryPeakPercent === null ? '—' : `${entry.memoryPeakPercent}%`,
      entry.acceleratorPeakPercent === null ? '—' : `${entry.acceleratorPeakPercent}%`,
      entry.diskPeakPercent === null ? '—' : `${entry.diskPeakPercent}%`,
      entry.decodeDiscardDelta ?? '—', `${entry.classifierForwardSuccessDelta ?? '—'} / ${entry.classifierForwardFailureDelta ?? '—'}`, entry.result, entry.reason ?? '—',
    ]);
    const title = series === 'v7' ? (zh ? '混合任务 · v7' : 'Mixed workload · v7') : series === 'legacy-d97e' ? (zh ? '此前单任务 · d97e' : 'Earlier single-task measurements · d97e') : series;
    const boundary = maxPassing === null ? '' : `<p>${zh ? `本构建最高已通过混合固定点：${maxPassing} 路` : `Highest passing fixed point: ${maxPassing} channels`}</p>`;
    const identity = engineSha256 ? `<p>SHA-256: <code>${engineSha256}</code></p>` : '';
    return `<h2>${title}</h2>` + identity + boundary +
      table(zh ? ['任务', '目标 FPS', '路数', '计划持载', '样本跨度', '最低独立节点 FPS', 'CPU 峰值', '内存峰值', '加速器峰值', '磁盘峰值', '解码新增丢弃', '分类前向成功 / 失败', '结果', '说明'] : ['Workload', 'Target FPS', 'Channels', 'Planned hold', 'Sample span', 'Minimum independent node FPS', 'CPU peak', 'Memory peak', 'Accelerator peak', 'Disk peak', 'Decode discard delta', 'Classifier forward success / failure', 'Result', 'Note'], rows);
  }).join('');
  const body = `<h1>AX650N · ${zh ? '短时容量观测' : 'Short-run capacity observation'}</h1>` +
    notice(zh ? '各档为独立固定点；通过点是已测下界，不是精确容量上限或推荐路数。不同构建的结果分开呈现。' : 'Each row is an independent fixed point. Passing points are observed lower bounds, not exact maximum capacity or recommended loads. Different builds are shown separately.') +
    `<p>${zh ? '混合任务每路同时执行人员检测和安全帽分析，各 5 FPS；保留正常追踪。未完成的固定点不形成容量上限。' : 'The mixed workload runs person detection and helmet analysis at 5 FPS each per channel with tracking. Incomplete points do not establish an upper bound.'}</p>` +
    `<p>${zh ? 'v7 使用完整 80 类后处理；此前 d97e 单任务采用历史 person-only 后处理。两组为不同引擎和配置，不应合并为同一容量曲线。' : 'v7 uses full 80-class postprocessing; the earlier d97e single-task measurements used the historical person-only postprocessing. They are different builds and configurations, not a single capacity curve.'}</p>` +
    `<p>${zh ? '固定点判据：每任务最低 FPS 不低于目标的 80%，遥测缺失率为 0，平均丢弃率不高于 5%，检测与关键路径延迟分别不高于 150 ms 和 200 ms；连续两个样本丢弃率超过 5% 或磁盘达到 96% 时触发保护。' : 'Fixed-point gates require each task to reach at least 80% of target FPS, zero missing telemetry, average discard at most 5%, detector latency at most 150 ms and critical-path latency at most 200 ms. Protection triggers after two consecutive discard samples above 5% or at 96% disk use.'}</p>` +
    `<p>${zh ? '本测试素材与 v1.1 四平台报告素材不同，结果不应视为完全同输入的直接对照。' : 'The input differs from the four-platform v1.1 material; these are not direct same-input comparisons.'}</p>` +
    table(zh ? ['素材 SHA-256', 'v1.1 素材 SHA-256'] : ['Input SHA-256', 'v1.1 input SHA-256'], [[data.input.sha256, data.input.referenceV11Sha256]]) +
    sections +
    `<h2>${zh ? '证据与范围' : 'Evidence and scope'}</h2>` +
    `<p>${zh ? '样本跨度是首末采样之差，不等同于计划持载时长。CPU、内存、加速器和磁盘是持载样本观测峰值；解码丢弃与分类前向是去重后的样本窗口增量，不代表逐帧模型精度。未取得的指标显示为破折号；INCOMPLETE 表示未满足完整测量条件。模型精度须单独验证。' : 'Sample span is the interval between the first and last samples, not the planned hold duration. CPU, memory, accelerator and disk values are observed hold-sample peaks; decode discard and classifier forward counts are deduplicated sample-window deltas. Missing metrics are shown as a dash; INCOMPLETE means the full measurement conditions were not met. Model accuracy requires separate validation.'}</p>` +
    table(zh ? ['人员模型 SHA-256', '安全帽模型 SHA-256', 'v7 引擎 SHA-256'] : ['Person model SHA-256', 'Helmet model SHA-256', 'v7 engine SHA-256'], [[data.models.personSha256, data.models.helmetSha256, data.series.find((entry) => entry.id === 'v7').engineSha256]]) +
    table(zh ? ['构建组', '证据哈希说明'] : ['Build series', 'Evidence hash scope'], data.series.map((entry) => [entry.id, entry.description])) +
    `<p><a href="short-capacity.json">${zh ? '机器可读数据' : 'Machine-readable data'}</a> · <a href="${zh ? 'report.html' : 'report.zh-CN.html'}">${zh ? 'English' : '中文'}</a></p>`;
  return page(locale, `AX650N ${zh ? '短时容量观测' : 'short-run capacity observation'}`, '', body);
}

export function generateShortCapacity({ destination = output } = {}) {
  const data = validateShortCapacity(JSON.parse(fs.readFileSync(source, 'utf8')));
  fs.mkdirSync(destination, { recursive: true });
  fs.copyFileSync(source, path.join(destination, 'short-capacity.json'));
  fs.copyFileSync(path.join(path.dirname(source), 'short-capacity.schema.json'), path.join(destination, 'short-capacity.schema.json'));
  for (const [locale, name] of [['en', 'report.html'], ['zh-CN', 'report.zh-CN.html']]) fs.writeFileSync(path.join(destination, name), renderShortCapacity(data, locale));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) generateShortCapacity();
