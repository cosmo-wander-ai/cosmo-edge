import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { loadAccuracySuite } from '../src/accuracy/suite.js';

function sha256(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

function fixture({ mutateSuite, cases } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-suite-'));
  const suiteDir = path.join(root, 'suite');
  const dataRoot = path.join(root, 'data');
  fs.mkdirSync(path.join(suiteDir, 'task-configs'), { recursive: true });
  fs.mkdirSync(path.join(dataRoot, 'videos'), { recursive: true });
  const positive = Buffer.from('positive-video');
  const negative = Buffer.from('negative-video');
  fs.writeFileSync(path.join(dataRoot, 'videos', 'positive.mp4'), positive);
  fs.writeFileSync(path.join(dataRoot, 'videos', 'negative.mp4'), negative);
  fs.writeFileSync(path.join(suiteDir, 'task-configs', 'helmet.json'), JSON.stringify({
    params: [{ key: 'param.videoRepeatCount', value: '0' }],
    areas: [],
  }));

  const rows = cases ?? [
    {
      id: 'helmet-positive-1', task: 'helmet', file: 'videos/positive.mp4',
      sha256: sha256(positive), expectation: { minEvents: 1 }, critical: true,
    },
    {
      id: 'helmet-negative-1', task: 'helmet', file: 'videos/negative.mp4',
      sha256: sha256(negative), expectation: { maxEvents: 0 },
    },
  ];
  fs.writeFileSync(
    path.join(suiteDir, 'cases.jsonl'),
    `${rows.map((row) => JSON.stringify(row)).join('\n')}\n`,
  );

  const suite = {
    schemaVersion: 3,
    id: 'helmet-measurement-v3',
    displayName: 'Helmet measurement',
    sourceMode: 'local',
    targetPlatforms: ['bm1688'],
    defaults: {
      observeSec: 45,
      eventFlushTimeoutSec: 15,
      eventSettleMinSec: 5,
      readyTimeoutSec: 120,
      infrastructureRetriesPerTrial: 1,
    },
    tasks: [{
      id: 'helmet', kind: 'cv', algorithmId: '15', scheduleId: 'always',
      taskConfig: 'task-configs/helmet.json',
      thresholdDiagnostic: {
        parameterKeys: ['aiParam.detector.confidence'], values: [0.5, 0.4],
      },
    }],
    dataset: { manifest: 'cases.jsonl' },
  };
  mutateSuite?.(suite);
  fs.writeFileSync(path.join(suiteDir, 'suite.yml'), toYaml(suite));
  return { root, suiteDir, dataRoot, suitePath: path.join(suiteDir, 'suite.yml') };
}

function toYaml(value, indent = 0) {
  const pad = ' '.repeat(indent);
  if (Array.isArray(value)) {
    return value.map((item) => {
      if (item && typeof item === 'object') {
        const lines = toYaml(item, indent + 2).split('\n');
        return `${pad}- ${lines[0].trimStart()}\n${lines.slice(1).join('\n')}`.trimEnd();
      }
      return `${pad}- ${JSON.stringify(item)}`;
    }).join('\n');
  }
  if (value && typeof value === 'object') {
    return Object.entries(value).map(([key, item]) => {
      if (item && typeof item === 'object') return `${pad}${key}:\n${toYaml(item, indent + 2)}`;
      return `${pad}${key}: ${JSON.stringify(item)}`;
    }).join('\n');
  }
  return `${pad}${JSON.stringify(value)}`;
}

test('loads and fingerprints a measurement suite without leaking absolute paths', () => {
  const f = fixture();
  try {
    const loaded = loadAccuracySuite({ suitePath: f.suitePath, dataRoot: f.dataRoot });
    assert.equal(loaded.id, 'helmet-measurement-v3');
    assert.equal(loaded.protocolVersion, 4);
    for (const removed of ['purpose', 'eligibility', 'eligibilityReasons', 'trials', 'gates']) {
      assert.equal(Object.hasOwn(loaded, removed), false);
    }
    assert.equal(loaded.defaults.readyPollIntervalSec, 1);
    assert.equal(loaded.defaults.eventPollIntervalSec, 1);
    assert.equal(loaded.defaults.earlyStopPollIntervalSec, 5);
    assert.match(loaded.identity.suiteSha256, /^[a-f0-9]{64}$/);
    assert.match(loaded.identity.caseManifestSha256, /^[a-f0-9]{64}$/);
    assert.match(loaded.tasks[0].taskConfigSha256, /^[a-f0-9]{64}$/);
    assert.equal(loaded.cases[0].absoluteFile, path.join(f.dataRoot, 'videos', 'positive.mp4'));
    assert.equal(JSON.stringify(loaded.identity).includes(f.root), false);
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('rejects duplicate case ids and paths that escape the private data root', () => {
  const duplicate = fixture({ cases: [
    { id: 'same', task: 'helmet', file: 'videos/positive.mp4', sha256: sha256(Buffer.from('positive-video')), expectation: { minEvents: 1 } },
    { id: 'same', task: 'helmet', file: 'videos/negative.mp4', sha256: sha256(Buffer.from('negative-video')), expectation: { maxEvents: 0 } },
  ] });
  try {
    assert.throws(
      () => loadAccuracySuite({ suitePath: duplicate.suitePath, dataRoot: duplicate.dataRoot }),
      /duplicate case id/i,
    );
  } finally {
    fs.rmSync(duplicate.root, { recursive: true, force: true });
  }

  const escaped = fixture({ cases: [{
    id: 'escape', task: 'helmet', file: '../outside.mp4', sha256: '0'.repeat(64),
    expectation: { minEvents: 1 },
  }] });
  try {
    assert.throws(
      () => loadAccuracySuite({ suitePath: escaped.suitePath, dataRoot: escaped.dataRoot }),
      /escapes data root/i,
    );
  } finally {
    fs.rmSync(escaped.root, { recursive: true, force: true });
  }
});

test('fails closed on an input hash mismatch', () => {
  const mismatch = fixture({ cases: [{
    id: 'bad-hash', task: 'helmet', file: 'videos/positive.mp4', sha256: '0'.repeat(64),
    expectation: { minEvents: 1 },
  }] });
  try {
    assert.throws(
      () => loadAccuracySuite({ suitePath: mismatch.suitePath, dataRoot: mismatch.dataRoot }),
      /sha256 mismatch/i,
    );
  } finally {
    fs.rmSync(mismatch.root, { recursive: true, force: true });
  }

});

test('legacy trials and gates are accepted as ignored input compatibility fields', () => {
  const legacyFields = fixture({ mutateSuite: (suite) => {
    suite.trials = { normal: 1, failureConfirmation: 1, critical: 3 };
    suite.gates = { tasks: { helmet: { minPositiveHitRate: 1 } } };
  } });
  try {
    const loaded = loadAccuracySuite({
      suitePath: legacyFields.suitePath,
      dataRoot: legacyFields.dataRoot,
    });
    assert.equal(Object.hasOwn(loaded, 'trials'), false);
    assert.equal(Object.hasOwn(loaded, 'gates'), false);
  } finally {
    fs.rmSync(legacyFields.root, { recursive: true, force: true });
  }
});

test('suite loader rejects v2 suites with migration guidance', () => {
  const old = fixture({ mutateSuite: (suite) => { suite.schemaVersion = 2; } });
  try {
    assert.throws(
      () => loadAccuracySuite({ suitePath: old.suitePath, dataRoot: old.dataRoot }),
      /schemaVersion 2.*migrate.*v3/i,
    );
  } finally {
    fs.rmSync(old.root, { recursive: true, force: true });
  }

});

test('quick admission hashes only the selected case while preserving full suite identity', () => {
  const f = fixture();
  try {
    fs.writeFileSync(path.join(f.dataRoot, 'videos', 'negative.mp4'), 'changed-negative');
    const loaded = loadAccuracySuite({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      selection: { profile: 'full', caseIds: ['helmet-positive-1'] },
    });
    assert.equal(loaded.cases.length, 2);
    assert.equal(loaded.cases[0].id, 'helmet-positive-1');
    assert.throws(
      () => loadAccuracySuite({ suitePath: f.suitePath, dataRoot: f.dataRoot }),
      /sha256 mismatch/i,
    );
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('device-default configuration works without a second opt-in flag', () => {
  const f = fixture({ mutateSuite: (suite) => {
    delete suite.tasks[0].taskConfig;
    suite.tasks[0].configSource = 'device-default';
  } });
  try {
    const loaded = loadAccuracySuite({ suitePath: f.suitePath, dataRoot: f.dataRoot });
    assert.equal(loaded.tasks[0].configSource, 'device-default');
    assert.equal(loaded.tasks[0].taskConfig, null);
    assert.equal(loaded.tasks[0].taskConfigSha256, null);
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('local frozen configs require video looping so the sample remains observable', () => {
  const f = fixture();
  try {
    fs.writeFileSync(
      path.join(f.suiteDir, 'task-configs', 'helmet.json'),
      JSON.stringify({ params: [], areas: [] }),
    );
    assert.throws(
      () => loadAccuracySuite({ suitePath: f.suitePath, dataRoot: f.dataRoot }),
      /videoRepeatCount=0/,
    );
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});
