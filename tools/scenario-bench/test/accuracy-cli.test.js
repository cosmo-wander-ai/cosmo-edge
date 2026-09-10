import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  accuracyExecutionOptions,
  assertV4ResumePartial,
  main,
  parseArgs,
} from '../src/accuracy-cli.js';
import { summarizeAccuracyRun } from '../src/accuracy/evaluator.js';
import { makeAccuracyFixture, makeMeasuredCase } from './helpers/accuracy-v4-fixture.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

test('accuracy help exposes the focused measurement commands without gate options', () => {
  const result = spawnSync(process.execPath, ['src/accuracy-cli.js', '--help'], {
    cwd: ROOT, encoding: 'utf8',
  });
  assert.equal(result.status, 0);
  assert.match(result.stdout, /protocol v4/i);
  for (const command of [
    'doctor', 'init-suite', 'run', 'compare', 'render', 'diagnose-threshold',
  ]) {
    assert.match(result.stdout, new RegExp(command));
  }
  assert.equal(result.stdout.includes('--password <'), false);
  assert.match(result.stdout, /--password-stdin/);
  assert.match(result.stdout, /--profile <full\|quick>/);
  assert.match(result.stdout, /--concurrency <1\|2\|4>/);
  for (const removed of ['--purpose', '--baseline', '--exploratory', 'compare-survey', 'import-legacy']) {
    assert.equal(result.stdout.includes(removed), false);
  }
});

test('accuracy parser preserves repeated comparison candidates', () => {
  const args = parseArgs([
    'compare', '--reference', 'serial.json',
    '--candidate', 'two.json', '--candidate', 'four.json', '--output', 'comparison',
  ]);
  assert.deepEqual(args.candidate, ['two.json', 'four.json']);
});

test('accuracy argument parser refuses a plaintext password flag', () => {
  assert.throws(() => parseArgs(['run', '--password', 'secret']), /does not accept --password/);
});

test('execution options preserve quick selection, filters, and concurrency', () => {
  assert.deepEqual(accuracyExecutionOptions({
    profile: 'quick', concurrency: '4',
    case: 'case-a,case-b', task: 'helmet', tag: 'night,quick',
  }), {
    profile: 'quick',
    concurrency: 4,
    selection: {
      profile: 'quick',
      caseIds: ['case-a', 'case-b'],
      taskIds: ['helmet'],
      tags: ['night', 'quick'],
    },
  });
  assert.equal(accuracyExecutionOptions({}).profile, 'full');
});

test('v4 resume refuses pre-v4 or identity-less partial evidence', () => {
  for (const protocolVersion of [2, 3]) {
    assert.throws(
      () => assertV4ResumePartial({ identity: { protocolVersion } }),
      /pre-v4 partial.*cannot be resumed/i,
    );
  }
  assert.throws(() => assertV4ResumePartial({}), /cannot be resumed/i);
  const v4 = { identity: { protocolVersion: 4 } };
  assert.equal(assertV4ResumePartial(v4), v4);
  assert.throws(
    () => assertV4ResumePartial({ protocolVersion: 4 }),
    /cannot be resumed/i,
  );
});

test('init-suite creates a review-required draft and hashed legacy cases', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-init-cli-'));
  try {
    const input = path.join(root, 'input');
    const output = path.join(root, 'output');
    const folder = path.join(input, '未戴安全帽');
    fs.mkdirSync(folder, { recursive: true });
    fs.writeFileSync(path.join(folder, '未戴安全帽（正检）_1.mp4'), 'positive');
    fs.writeFileSync(path.join(folder, '未戴安全帽（误检）_1.mp4'), 'negative');
    assert.equal(await main(['init-suite', '--input-root', input, '--output', output]), 0);
    const draft = fs.readFileSync(path.join(output, 'suite.draft.yml'), 'utf8');
    const cases = fs.readFileSync(path.join(output, 'cases.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
    assert.match(draft, /REVIEW_REQUIRED/);
    assert.match(draft, /schemaVersion: 3/);
    assert.equal(cases.length, 2);
    assert.ok(cases.every((item) => /^[a-f0-9]{64}$/.test(item.sha256)));
    assert.deepEqual(fs.readdirSync(path.join(output, 'task-configs')), []);
    const readme = fs.readFileSync(path.join(output, 'README.md'), 'utf8');
    assert.match(readme, /create each referenced taskConfig/);
    assert.match(readme, /each video sample is measured once/i);
    assert.doesNotMatch(draft, /gates:|trials:/i);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('render works entirely offline', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-offline-cli-'));
  try {
    const summary = path.join(root, 'summary.json');
    const report = path.join(root, 'report.html');
    fs.writeFileSync(summary, JSON.stringify(renderableSummary()));
    assert.equal(await main(['render', '--input', summary, '--output', report]), 0);
    assert.match(fs.readFileSync(report, 'utf8'), /CosmoEdge 视频样本级算法效果评测/);
    assert.match(fs.readFileSync(report, 'utf8'), /算法 ID/);

    const invalid = path.join(root, 'invalid.json');
    const invalidReport = path.join(root, 'invalid.html');
    fs.writeFileSync(invalid, JSON.stringify({ protocolVersion: 3 }));
    assert.equal(await main(['render', '--input', invalid, '--output', invalidReport]), 2);
    assert.equal(fs.existsSync(invalidReport), false);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('compare accepts repeated candidates and writes the private comparison bundle', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-compare-cli-'));
  try {
    const reference = renderableSummary();
    reference.wallDurationMs = 10_000;
    const candidate = structuredClone(reference);
    candidate.execution.concurrency = 2;
    candidate.admission.concurrency = 2;
    candidate.wallDurationMs = 6_000;
    const referencePath = path.join(root, 'reference.json');
    const candidatePath = path.join(root, 'candidate.json');
    const output = path.join(root, 'comparison');
    fs.writeFileSync(referencePath, JSON.stringify(reference));
    fs.writeFileSync(candidatePath, JSON.stringify(candidate));

    assert.equal(await main([
      'compare', '--reference', referencePath,
      '--candidate', candidatePath, '--candidate', candidatePath,
      '--output', output,
    ]), 0);
    const comparison = JSON.parse(fs.readFileSync(path.join(output, 'comparison.json')));
    assert.equal(comparison.protocolVersion, 4);
    assert.equal(comparison.candidates.length, 2);
    assert.equal(comparison.candidates[0].dimensions.concurrency, 2);
    assert.deepEqual(comparison.sources.map((item) => item.role), [
      'reference', 'candidate-1', 'candidate-2',
    ]);
    assert.ok(comparison.sources.every((item) => /^[a-f0-9]{64}$/u.test(item.sha256)));
    for (const name of ['report.html', 'integrity.json']) {
      assert.equal(fs.existsSync(path.join(output, name)), true);
    }
    assert.equal(fs.existsSync(path.join(output, 'report.md')), false);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

function renderableSummary() {
  const { execution, suite, admission } = makeAccuracyFixture({
    caseCount: 1,
  });
  return summarizeAccuracyRun({
    suite,
    cases: [makeMeasuredCase({ id: 'case', expectation: { minEvents: 1 } })],
    targetChip: 'bm1688',
    execution,
    admission,
  });
}
