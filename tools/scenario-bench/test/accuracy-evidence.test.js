import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { AccuracyEvidenceWriter, sanitizeSummary } from '../src/accuracy/evidence.js';
import { summarizeAccuracyRun } from '../src/accuracy/evaluator.js';
import { sha256File } from '../src/accuracy/utils.js';
import {
  HASH_A,
  HASH_B,
  HASH_C,
  HASH_D,
  makeAccuracyFixture,
  makeMeasuredCase,
} from './helpers/accuracy-v4-fixture.js';

function identity(overrides = {}) {
  return {
    protocolVersion: 4,
    schemaVersion: 3,
    suiteSha256: HASH_A,
    caseManifestSha256: HASH_B,
    caseSetSha256: HASH_C,
    taskConfigSha256: { helmet: HASH_D },
    sourceMode: 'local',
    targetChip: 'bm1688',
    deviceFingerprint: 'device-fingerprint',
    softwareVersion: 'V1.1.0.0',
    execution: execution(),
    ...overrides,
  };
}

function execution() {
  return makeAccuracyFixture({ caseCount: 1 }).execution;
}

function admission() {
  const value = makeAccuracyFixture({ caseCount: 1 }).admission;
  return sanitizeSummary({
    ...value,
    checks: [{
      name: 'device-info', status: 'PASS', blocking: true,
      detail: 'http://device.invalid /private/video.mp4 deviceSn=secret',
    }],
    warnings: ['rtsp://device.invalid/private'],
  });
}

function validSummary() {
  const fixture = makeAccuracyFixture({
    caseCount: 1,
    suiteId: '<script>alert(1)</script>',
    suiteDisplayName: 'Warehouse <script>',
    taskDisplayName: 'Helmet <b>',
  });
  const item = makeMeasuredCase({
    id: '<case>',
    expectation: { minEvents: 1 },
    totalMs: 5_100,
    events: [{ detectedPictureArtifact: {
      path: `artifacts/alerts/${HASH_A}.jpg`, sha256: HASH_A,
      contentType: 'image/jpeg', sizeBytes: 10,
    } }],
  });
  item.trials[0].observation = { requestedSec: 45, actualSec: 5, earlyStopped: true };
  const summary = summarizeAccuracyRun({
    suite: fixture.suite,
    targetChip: 'bm1688',
    execution: fixture.execution,
    admission: admission(),
    cases: [item],
  });
  return {
    ...summary,
    wallDurationMs: 3_661_000,
    activeDurationMs: 3_600_000,
    admissionMs: 1_000,
    deviceSn: 'private-sn',
    deviceAddress: 'device.invalid',
    absoluteFile: '/private/video.mp4',
  };
}

test('evidence writer atomically checkpoints and rejects mismatched resume identity', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-evidence-'));
  const output = path.join(root, 'run');
  try {
    const writer = new AccuracyEvidenceWriter(output);
    writer.initialize(identity());
    assert.equal(fs.existsSync(path.join(output, 'logs')), false);
    assert.equal(fs.existsSync(path.join(output, 'artifacts', 'alerts')), false);
    assert.equal(typeof writer.writeJsonArtifact, 'undefined');
    await writer.writePartial({ cases: [{ id: 'one' }] });
    assert.equal(fs.existsSync(path.join(output, 'run.partial.json.tmp')), false);
    assert.equal(JSON.parse(fs.readFileSync(path.join(output, 'run.partial.json'))).cases[0].id, 'one');
    assert.deepEqual(writer.loadPartial(identity()).cases, [{ id: 'one' }]);
    assert.throws(
      () => writer.loadPartial(identity({ softwareVersion: 'different' })),
      /resume identity mismatch/i,
    );
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('partial checkpoints are rate-limited unless explicitly forced', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-partial-rate-'));
  const output = path.join(root, 'run');
  let now = 1_000;
  try {
    const writer = new AccuracyEvidenceWriter(output, {
      partialWriteIntervalMs: 30_000,
      now: () => now,
    });
    writer.initialize(identity());
    const first = await writer.writePartial({ cases: [{ id: 'one' }] });
    now += 1_000;
    const skipped = await writer.writePartial({ cases: [{ id: 'one' }, { id: 'two' }] });
    assert.equal(first.skipped, false);
    assert.equal(skipped.skipped, true);
    assert.equal(JSON.parse(fs.readFileSync(first.path)).cases.length, 1);

    const forced = await writer.writePartial(
      { cases: [{ id: 'one' }, { id: 'two' }] },
      { force: true },
    );
    assert.equal(forced.skipped, false);
    assert.equal(JSON.parse(fs.readFileSync(forced.path)).cases.length, 2);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('final evidence validates v4 summary, persists sanitized admission, and inventories outputs', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-final-'));
  const output = path.join(root, 'run');
  try {
    const writer = new AccuracyEvidenceWriter(output);
    writer.initialize(identity());
    await writer.writePartial({ cases: [{ id: 'completed-case' }] });
    const summary = validSummary();
    const paths = await writer.finalize({
      privateRun: { admission: summary.admission, cases: [] },
      summary,
    });
    const savedSummary = fs.readFileSync(paths.summaryPath, 'utf8');
    const privateRun = fs.readFileSync(paths.privatePath, 'utf8');
    const html = fs.readFileSync(paths.htmlPath, 'utf8');
    const integrity = JSON.parse(fs.readFileSync(paths.integrityPath, 'utf8'));
    for (const text of [savedSummary, privateRun]) {
      assert.equal(text.includes('private-sn'), false);
      assert.equal(text.includes('device.invalid'), false);
      assert.equal(text.includes('/private/'), false);
    }
    assert.equal(html.includes('<script>alert(1)</script>'), false);
    assert.ok(html.includes('&lt;script&gt;alert(1)&lt;/script&gt;'));
    assert.ok(html.includes(`<img class="alarm" src="artifacts/alerts/${HASH_A}.jpg"`));
    assert.match(html, /执行耗时/);
    assert.match(html, /1 小时 1 分钟/);
    assert.match(html, /执行结果<\/b>：<span class="completed">COMPLETED<\/span>/);
    assert.ok(integrity.artifacts.some((item) => item.path === 'summary.json'));
    assert.ok(integrity.artifacts.every((item) => /^[a-f0-9]{64}$/.test(item.sha256)));
    assert.equal(integrity.inputs.suiteSha256, HASH_A);
    assert.equal(integrity.inputs.caseManifestSha256, HASH_B);
    assert.deepEqual(integrity.inputs.taskConfigSha256, { helmet: [HASH_D] });
    const completedPartial = JSON.parse(fs.readFileSync(path.join(output, 'run.partial.json')));
    assert.equal(completedPartial.status, 'completed');
    assert.equal(Object.hasOwn(completedPartial, 'cases'), false);
    assert.equal(Object.hasOwn(completedPartial, 'identity'), false);
    assert.equal(completedPartial.privateRunSha256, sha256File(paths.privatePath));
    assert.equal(completedPartial.resultPath, 'summary.json');
    assert.equal(completedPartial.resultSha256, sha256File(paths.summaryPath));
    assert.equal(completedPartial.integritySha256, sha256File(paths.integrityPath));
    assert.equal(
      integrity.artifacts.some((item) => item.path === 'run.partial.json'),
      false,
    );
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('threshold evidence finalizes without creating an accuracy summary', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-diagnostic-final-'));
  const output = path.join(root, 'run');
  const hash = HASH_A;
  try {
    const writer = new AccuracyEvidenceWriter(output);
    writer.initialize(identity());
    await writer.writePartial({ points: [{ threshold: 0.4 }] });
    const diagnostic = {
      schemaVersion: 1,
      protocolVersion: 4,
      evidenceKind: 'cosmo-accuracy-threshold-diagnostic',
      evidenceStatus: 'DIAGNOSTIC',
      executionOutcome: 'COMPLETED',
      executionReasons: [],
      appliedAutomatically: false,
      runId: 'run',
      sourceRunSha256: hash,
      caseId: 'case',
      parameterKeys: ['aiParam.detector.confidence'],
      execution: { profile: 'full', concurrency: 1 },
      admission: { status: 'PASS' },
      suite: { id: 'suite' },
      points: [{ threshold: 0.4, status: 'STABLE_PASS', trialStatuses: ['PASS', 'PASS', 'PASS'] }],
      stablePassValues: [0.4], stableFailValues: [], flakyValues: [],
      device: { model: 'BM1688' },
      toolIdentitySha256: hash,
      repository: { commit: null, tree: null, dirty: true },
    };
    const paths = await writer.finalizeDiagnostic({ privateRun: { points: [] }, diagnostic });
    assert.equal(fs.existsSync(path.join(output, 'summary.json')), false);
    assert.equal(fs.existsSync(paths.diagnosticPath), true);
    assert.match(fs.readFileSync(paths.htmlPath, 'utf8'), /阈值诊断/);
    const marker = JSON.parse(fs.readFileSync(path.join(output, 'run.partial.json')));
    assert.equal(marker.resultPath, 'threshold-diagnostic.json');
    assert.equal(marker.resultSha256, sha256File(paths.diagnosticPath));
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('finalize preserves raw measurement when derived summary validation fails', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-invalid-final-'));
  const output = path.join(root, 'run');
  try {
    const writer = new AccuracyEvidenceWriter(output);
    writer.initialize(identity());
    const summary = validSummary();
    delete summary.tasks[0].displayName;
    const paths = await writer.finalize({ privateRun: { cases: [] }, summary });
    assert.equal(fs.existsSync(paths.privatePath), true);
    assert.equal(fs.existsSync(paths.summaryPath), true);
    assert.ok(paths.warnings.some((warning) => /validation failed/i.test(warning)));
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('alert images are downloaded once by content hash and stored as relative artifacts', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-images-'));
  const output = path.join(root, 'run');
  try {
    const writer = new AccuracyEvidenceWriter(output);
    writer.initialize(identity());
    let downloads = 0;
    const client = {
      async downloadArtifact() {
        downloads += 1;
        return { buffer: Buffer.from('same-image'), contentType: 'image/jpeg' };
      },
    };
    const events = await writer.archiveAlertImages(client, [
      { id: '1', detectedPicture: '/one.jpg' },
      { id: '2', detectedPicture: '/two.jpg' },
    ]);
    assert.equal(downloads, 2);
    assert.equal(fs.existsSync(path.join(output, 'artifacts', 'alerts')), true);
    assert.equal(events[0].detectedPictureArtifact.path, events[1].detectedPictureArtifact.path);
    assert.equal(events[0].detectedPicture, undefined);
    assert.equal(fs.existsSync(path.join(output, events[0].detectedPictureArtifact.path)), true);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
