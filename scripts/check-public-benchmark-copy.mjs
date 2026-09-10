import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const distRoot = path.resolve(repositoryRoot, process.argv[2] ?? 'docs/.vitepress/dist');
const benchmarkRoot = path.join(distRoot, 'benchmarks', 'scenario-bench', 'v1.1');
const failures = [];

const internalPhrases = [
  'without rewriting the executed gates',
  'capacity staircase',
  'launch-time controller bytes',
  'independent final-state artifact',
  'independent final-state sidecar',
  'conservative-post-evaluation',
  'conservative post-evaluation',
  'conservatively post-evaluating',
  'current conservative publication evaluation',
  'conservative 80% reference',
  'conservative multi-platform performance display',
  'vlm raw runs did not enable fps pass/fail',
  'startup-sensitive window',
  'productReleaseQualified',
  '不改写实际执行门禁',
  '容量阶梯',
  '启动时控制器字节',
  '独立最终状态文件',
  '独立 final-state',
  '保守回算',
  '当前保守发布评估',
  '80% 保守发布参考',
  '多平台保守性能展示',
  'vlm 原始运行没有启用 fps pass/fail',
  '启动敏感窗口',
  '产品发布资格字段',
];

const sourceEntryFiles = [
  'README.md',
  'README.zh-CN.md',
  'Readme.osc.md',
  'CHANGELOG.md',
  '.github/release-notes/v1.1.0.md',
  '.github/release-notes/v1.1.0.zh-CN.md',
  'docs/benchmarks/scenario-bench/v1.1/README.md',
  'docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md',
  'docs/benchmarks/scenario-bench/current/README.md',
  'docs/benchmarks/scenario-bench/current/README.zh-CN.md',
  'docs/benchmarks/scenario-bench/v1.0/README.md',
  'docs/benchmarks/scenario-bench/v1.0/README.zh-CN.md',
];

for (const relative of sourceEntryFiles) {
  checkFile(path.join(repositoryRoot, ...relative.split('/')), relative);
}

for (const relative of [
  'report.html',
  'report.zh-CN.html',
  'results/dual-cv-72h/report.html',
  'results/dual-cv-72h/report.zh-CN.html',
]) {
  const file = path.join(benchmarkRoot, ...relative.split('/'));
  const allowEvidenceNotes = relative.startsWith('results/dual-cv-72h/');
  checkFile(file, `generated:${relative}`, { allowEvidenceNotes });
}

const manifestPath = path.join(benchmarkRoot, 'release-manifest.json');
if (!fs.existsSync(manifestPath)) {
  failures.push('generated:release-manifest.json is missing');
} else {
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  const vlmIds = new Set(
    JSON.parse(fs.readFileSync(path.join(benchmarkRoot, 'results', 'vlm-observations.json'), 'utf8'))
      .observations.map((item) => item.platformId),
  );
  for (const platform of manifest.platforms ?? []) {
    if (!vlmIds.has(platform.id)) continue;
    for (const suffix of ['', '.zh-CN']) {
      const relative = `results/${platform.id}/vlm-observation/report${suffix}.html`;
      checkFile(path.join(benchmarkRoot, ...relative.split('/')), `generated:${relative}`);
    }
  }
}

if (failures.length) {
  console.error(`Public benchmark copy check failed with ${failures.length} issue(s):`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(`Public benchmark copy check passed for ${sourceEntryFiles.length} source entry files and generated benchmark entry reports.`);

function checkFile(file, label, { allowEvidenceNotes = false } = {}) {
  if (!fs.existsSync(file)) {
    failures.push(`${label}: file is missing`);
    return;
  }
  let source = fs.readFileSync(file, 'utf8');
  if (allowEvidenceNotes) {
    source = source.replace(/<details\b[^>]*class=["'][^"']*\bevidence-notes\b[^"']*["'][^>]*>[\s\S]*?<\/details>/giu, '');
  }
  const lower = source.toLocaleLowerCase('en-US');
  for (const phrase of internalPhrases) {
    if (lower.includes(phrase.toLocaleLowerCase('en-US'))) {
      failures.push(`${label}: internal audit phrase appears in entry copy: ${phrase}`);
    }
  }
}
