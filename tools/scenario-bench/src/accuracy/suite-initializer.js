import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { stringify as stringifyYaml } from 'yaml';

import { sha256File } from './utils.js';

const LEGACY_NAME = /[（(](正检|误检)[)）][_\s]*(\d+)/u;

export function initializeAccuracySuite({ inputRoot, outputDir, targetChip = 'bm1688' } = {}) {
  const source = path.resolve(inputRoot);
  const output = path.resolve(outputDir);
  if (!fs.statSync(source).isDirectory()) throw new Error('inputRoot must be a directory');
  if (fs.existsSync(output) && fs.readdirSync(output).length) {
    throw new Error('init-suite output directory must be new and empty');
  }
  fs.mkdirSync(path.join(output, 'task-configs'), { recursive: true, mode: 0o700 });
  const tasks = [];
  const cases = [];
  const caseIds = new Set();
  for (const folder of fs.readdirSync(source, { withFileTypes: true }).filter((entry) => entry.isDirectory())) {
    const files = fs.readdirSync(path.join(source, folder.name), { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.toLowerCase().endsWith('.mp4'));
    const matched = files.map((entry) => ({ entry, match: LEGACY_NAME.exec(entry.name) })).filter((item) => item.match);
    if (!matched.length) continue;
    const taskId = `task-${crypto.createHash('sha256').update(folder.name).digest('hex').slice(0, 8)}`;
    const taskConfigFile = `task-configs/${taskId}.json`;
    tasks.push({
      id: taskId,
      displayName: folder.name,
      kind: folder.name.includes('视觉语言') ? 'vlm' : 'cv',
      algorithmId: 'REVIEW_REQUIRED',
      scheduleId: 'REVIEW_REQUIRED',
      taskConfig: taskConfigFile,
    });
    for (const { entry, match } of matched) {
      const relative = path.posix.join(folder.name, entry.name);
      const absolute = path.join(source, folder.name, entry.name);
      const positive = match[1] === '正检';
      const caseId = `${taskId}-${positive ? 'positive' : 'negative'}-${String(match[2]).padStart(4, '0')}`;
      if (caseIds.has(caseId)) throw new Error(`legacy samples produce duplicate case id: ${caseId}`);
      caseIds.add(caseId);
      cases.push({
        id: caseId,
        task: taskId,
        file: relative,
        sha256: sha256File(absolute),
        expectation: positive ? { minEvents: 1 } : { maxEvents: 0 },
        tags: [],
      });
    }
  }
  if (!tasks.length) throw new Error('no legacy positive/negative MP4 samples found');
  cases.sort((a, b) => a.id.localeCompare(b.id));
  const draft = {
    schemaVersion: 3,
    id: 'REVIEW_REQUIRED',
    displayName: 'REVIEW_REQUIRED',
    sourceMode: 'local',
    targetPlatforms: [targetChip],
    defaults: {
      observeSec: 45,
      eventFlushTimeoutSec: 15,
      eventPollIntervalSec: 1,
      eventSettleMinSec: 5,
      earlyStopPollIntervalSec: 5,
      readyTimeoutSec: 120,
      readyPollIntervalSec: 1,
      infrastructureRetriesPerTrial: 1,
    },
    tasks,
    dataset: { manifest: 'cases.jsonl' },
  };
  fs.writeFileSync(path.join(output, 'suite.draft.yml'), stringifyYaml(draft), { mode: 0o600 });
  fs.writeFileSync(
    path.join(output, 'cases.jsonl'),
    `${cases.map((item) => JSON.stringify(item)).join('\n')}\n`,
    { mode: 0o600 },
  );
  fs.writeFileSync(path.join(output, 'README.md'), [
    '# Accuracy suite draft',
    '',
    'Replace every `REVIEW_REQUIRED` value and create each referenced taskConfig from an exported',
    'frozen configuration. Review case IDs/expectations, then rename `suite.draft.yml` to `suite.yml`.',
    'Each video sample is measured once; infrastructure errors may retry according to defaults.',
    'Private sample videos remain outside Git.',
    '',
  ].join('\n'), { mode: 0o600 });
  return { outputDir: output, taskCount: tasks.length, caseCount: cases.length };
}
