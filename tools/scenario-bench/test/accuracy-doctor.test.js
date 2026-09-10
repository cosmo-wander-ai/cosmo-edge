import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { stringify as stringifyYaml } from 'yaml';

import {
  queryAccuracyLeftoverChecks,
  runAccuracyDoctor,
  summarizeAccuracyAdmission,
} from '../src/accuracy/doctor.js';
import { sanitizeSummary } from '../src/accuracy/evidence.js';

function fixture() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-doctor-'));
  const suiteDir = path.join(root, 'suite');
  const dataRoot = path.join(root, 'data');
  fs.mkdirSync(path.join(suiteDir, 'task-configs'), { recursive: true });
  fs.mkdirSync(path.join(dataRoot, 'videos'), { recursive: true });
  fs.writeFileSync(path.join(dataRoot, 'videos', 'positive.mp4'), 'positive');
  fs.writeFileSync(path.join(dataRoot, 'videos', 'negative.mp4'), 'negative');
  fs.writeFileSync(path.join(dataRoot, 'videos', 'positive-2.mp4'), 'positive2');
  fs.writeFileSync(path.join(dataRoot, 'videos', 'negative-2.mp4'), 'negative2');
  fs.writeFileSync(path.join(suiteDir, 'task-configs', 'task.json'), JSON.stringify({
    params: [{ key: 'param.videoRepeatCount', value: '0' }], areas: [],
  }));
  fs.writeFileSync(path.join(suiteDir, 'cases.jsonl'), [
    JSON.stringify({
      id: 'positive', task: 'task', file: 'videos/positive.mp4',
      sha256: hash('positive'), expectation: { minEvents: 1 },
    }),
    JSON.stringify({
      id: 'negative', task: 'task', file: 'videos/negative.mp4',
      sha256: hash('negative'), expectation: { maxEvents: 0 },
    }),
    JSON.stringify({
      id: 'positive-2', task: 'task', file: 'videos/positive-2.mp4',
      sha256: hash('positive2'), expectation: { minEvents: 1 },
    }),
    JSON.stringify({
      id: 'negative-2', task: 'task', file: 'videos/negative-2.mp4',
      sha256: hash('negative2'), expectation: { maxEvents: 0 },
    }),
  ].join('\n') + '\n');
  fs.writeFileSync(path.join(suiteDir, 'suite.yml'), stringifyYaml({
    schemaVersion: 3,
    id: 'doctor-suite',
    sourceMode: 'local',
    targetPlatforms: ['bm1688'],
    tasks: [{
      id: 'task', kind: 'cv', algorithmId: 'internal-15', algorithmCode: '15',
      scheduleId: 'always', taskConfig: 'task-configs/task.json',
    }],
    dataset: { manifest: 'cases.jsonl' },
    gates: { tasks: { task: {
      minPositiveHitRate: 0.5, minNegativeCleanRate: 0.5,
      maxErrors: 0, maxFlaky: 0,
      requirePositiveCases: true, requireNegativeCases: true,
    } } },
  }));
  return { root, dataRoot, suitePath: path.join(suiteDir, 'suite.yml') };
}

function hash(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

async function mediaProbe(cases) {
  return {
    checked: cases.length,
    valid: cases.length,
    invalid: [],
    durationSec: { min: 5, median: 6, p95: 7, max: 7 },
    duplicateContent: { groups: 0, extraCases: 0, groupsByTask: [] },
    cases: cases.map((item) => ({
      id: item.id, durationSec: 6, codec: 'h264', width: 1920, height: 1080,
    })),
  };
}

function client({
  algorithms = ['internal-15'],
  cameras = [],
  tasks = [],
  taskPage = null,
} = {}) {
  const calls = [];
  return {
    calls,
    async login() { calls.push('login'); },
    async queryDeviceInfo() {
      calls.push('queryDeviceInfo');
      return { devInfoList: [
        { key: 'deviceType', value: 'BM1688 test device' },
        { key: 'softwareVersion', value: 'V1' },
        { key: 'deviceSn', value: 'private-sn' },
      ] };
    },
    async algorithmPage() {
      calls.push('algorithmPage');
      return { total: algorithms.length, rows: algorithms.map((algorithmId) => ({ algorithmId })) };
    },
    async schedulePage() {
      calls.push('schedulePage');
      return { total: 1, rows: [{ id: 'always' }] };
    },
    async uploadCapabilities() {
      calls.push('uploadCapabilities');
      return { maxTotalSize: '1000', availableForNewUploadsBytes: '1000' };
    },
    async queryHardwareResource() {
      calls.push('queryHardwareResource');
      return { itemList: [{ key: 'eMMCUtilization', usedPercent: 10, available: 1 }] };
    },
    async eventPage() { calls.push('eventPage'); return { total: 0, rows: [] }; },
    async cameraPage() { calls.push('cameraPage'); return { total: cameras.length, rows: cameras }; },
    async taskPage(payload) {
      calls.push('taskPage');
      if (taskPage) return taskPage(payload);
      return { total: tasks.length, rows: tasks };
    },
  };
}

test('device doctor performs only read-side admission checks and sanitizes device identity', async () => {
  const f = fixture();
  try {
    const fake = client();
    const result = await runAccuracyDoctor({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      targetChip: 'bm1688',
      device: 'http://device',
      auth: { token: 'token' },
      clientFactory: () => fake,
      mediaProbe,
    });
    assert.equal(result.status, 'PASS');
    assert.deepEqual(result.admittedCases.map((item) => item.id), [
      'positive', 'negative', 'positive-2', 'negative-2',
    ]);
    assert.equal(result.checks.some((check) => Object.hasOwn(check, 'ok')), false);
    assert.match(result.device.deviceFingerprint, /^[a-f0-9]{64}$/);
    assert.equal(JSON.stringify(result).includes('private-sn'), false);
    assert.equal(fake.calls.some((name) => /add|save|delete|switch/iu.test(name)), false);
    const admission = sanitizeSummary(summarizeAccuracyAdmission({
      ...result,
      warnings: ['http://device.invalid/private/video.mp4'],
    }));
    assert.equal(admission.profile, 'full');
    assert.equal(admission.concurrency, 1);
    assert.equal(admission.selection.count, 4);
    assert.equal(JSON.stringify(admission).includes('device.invalid'), false);
    assert.equal(JSON.stringify(admission).includes('/private/'), false);
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('device doctor fails before mutation when a required algorithm is missing', async () => {
  const f = fixture();
  try {
    const result = await runAccuracyDoctor({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      targetChip: 'bm1688',
      device: 'http://device',
      auth: { token: 'token' },
      clientFactory: () => client({ algorithms: [] }),
      mediaProbe,
    });
    assert.equal(result.status, 'FAIL');
    assert.ok(result.checks.some((check) => check.name === 'algorithms' && check.status === 'FAIL'));
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('doctor blocks invalid media before a run can mutate the device', async () => {
  const f = fixture();
  try {
    const result = await runAccuracyDoctor({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      targetChip: 'bm1688',
      mediaProbe: async (cases) => ({
        checked: cases.length,
        valid: cases.length - 1,
        invalid: [{ id: 'positive', reason: 'moov atom not found' }],
        durationSec: null,
        duplicateContent: { groups: 0, extraCases: 0, groupsByTask: [] },
        cases: [],
      }),
    });
    assert.equal(result.status, 'FAIL');
    assert.ok(result.checks.some((check) =>
      check.name === 'media-preflight' && check.status === 'FAIL'));
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('filtered doctor probes only selected media', async () => {
  const f = fixture();
  try {
    let probed = [];
    const result = await runAccuracyDoctor({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      targetChip: 'bm1688',
      selection: { profile: 'full', caseIds: ['positive'] },
      mediaProbe: async (cases) => {
        probed = cases.map((item) => item.id);
        return mediaProbe(cases);
      },
    });
    assert.equal(result.status, 'PASS');
    assert.deepEqual(probed, ['positive']);
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('concurrency two admission reserves headroom for two simultaneous uploads', async () => {
  const f = fixture();
  try {
    const fake = client();
    fake.uploadCapabilities = async () => ({
      maxTotalSize: '1000', availableForNewUploadsBytes: '10',
    });
    const result = await runAccuracyDoctor({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      targetChip: 'bm1688',
      device: 'http://device',
      auth: { token: 'token' },
      clientFactory: () => fake,
      mediaProbe,
      concurrency: 2,
    });
    assert.equal(result.status, 'FAIL');
    assert.ok(result.checks.some((check) =>
      check.name === 'upload-capabilities'
      && check.status === 'FAIL'
      && /concurrency 2/.test(check.detail)));
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('concurrency four admission reserves headroom for four simultaneous CV uploads', async () => {
  const f = fixture();
  try {
    const fake = client();
    fake.uploadCapabilities = async () => ({
      maxTotalSize: '1000', availableForNewUploadsBytes: '32',
    });
    const result = await runAccuracyDoctor({
      suitePath: f.suitePath,
      dataRoot: f.dataRoot,
      targetChip: 'bm1688',
      device: 'http://device',
      auth: { token: 'token' },
      clientFactory: () => fake,
      mediaProbe,
      concurrency: 4,
    });
    assert.equal(result.status, 'FAIL');
    assert.ok(result.checks.some((check) =>
      check.name === 'upload-capabilities'
      && check.status === 'FAIL'
      && /concurrency 4/.test(check.detail)));
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('doctor reports orphan accuracy tasks and cameras as non-blocking warnings', async () => {
  const f = fixture();
  try {
    const orphanTask = await runAccuracyDoctor({
      suitePath: f.suitePath, dataRoot: f.dataRoot, targetChip: 'bm1688',
      device: 'http://device', auth: { token: 'token' }, mediaProbe,
      clientFactory: () => client({ tasks: [{ channelName: 'acc-orphan-task' }] }),
    });
    assert.equal(orphanTask.status, 'PASS');
    assert.equal(check(orphanTask, 'accuracy-channel-leftovers').status, 'PASS');
    assert.equal(check(orphanTask, 'accuracy-task-leftovers').status, 'FAIL');
    assert.equal(check(orphanTask, 'accuracy-task-leftovers').blocking, false);

    const orphanCamera = await runAccuracyDoctor({
      suitePath: f.suitePath, dataRoot: f.dataRoot, targetChip: 'bm1688',
      device: 'http://device', auth: { token: 'token' }, mediaProbe,
      clientFactory: () => client({ cameras: [{ channelName: 'acc-orphan-camera', videoChannelId: 'c1' }] }),
    });
    assert.equal(orphanCamera.status, 'PASS');
    assert.equal(check(orphanCamera, 'accuracy-channel-leftovers').status, 'FAIL');
    assert.equal(check(orphanCamera, 'accuracy-task-leftovers').status, 'PASS');
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('doctor correlates task channel IDs, paginates task page, and warns on unknown ownership', async () => {
  const f = fixture();
  try {
    const correlated = await runAccuracyDoctor({
      suitePath: f.suitePath, dataRoot: f.dataRoot, targetChip: 'bm1688',
      device: 'http://device', auth: { token: 'token' }, mediaProbe,
      clientFactory: () => client({
        cameras: [{ channelName: 'acc-camera', videoChannelId: 'acc-id' }],
        tasks: [{ videoChannelId: 'acc-id' }],
      }),
    });
    assert.equal(check(correlated, 'accuracy-task-leftovers').status, 'FAIL');

    let pages = 0;
    const pageOne = Array.from({ length: 500 }, (_, index) => ({ channelName: `prod-${index}` }));
    const paged = await runAccuracyDoctor({
      suitePath: f.suitePath, dataRoot: f.dataRoot, targetChip: 'bm1688',
      device: 'http://device', auth: { token: 'token' }, mediaProbe,
      clientFactory: () => client({ taskPage: ({ pageNum }) => {
        pages += 1;
        return pageNum === 1
          ? { total: 501, rows: pageOne }
          : { total: 501, rows: [{ videoChannelName: 'acc-page-two' }] };
      } }),
    });
    assert.equal(pages, 2);
    assert.equal(check(paged, 'accuracy-task-leftovers').status, 'FAIL');

    const unknown = await runAccuracyDoctor({
      suitePath: f.suitePath, dataRoot: f.dataRoot, targetChip: 'bm1688',
      device: 'http://device', auth: { token: 'token' }, mediaProbe,
      clientFactory: () => client({ tasks: [{ id: 'opaque-task' }] }),
    });
    assert.equal(unknown.status, 'PASS');
    assert.equal(check(unknown, 'accuracy-task-leftovers').status, 'UNVERIFIED');
    assert.equal(check(unknown, 'accuracy-leftovers').status, 'UNVERIFIED');
  } finally {
    fs.rmSync(f.root, { recursive: true, force: true });
  }
});

test('final leftover query fails closed when task or camera inventory is unreadable', async () => {
  const checks = await queryAccuracyLeftoverChecks({
    async cameraPage() { return { total: 0, rows: [] }; },
    async taskPage() { throw new Error('non-JSON response'); },
  });
  assert.equal(checks.find((item) => item.name === 'accuracy-channel-leftovers').status, 'PASS');
  assert.equal(checks.find((item) => item.name === 'accuracy-task-inventory').status, 'UNVERIFIED');
  assert.equal(checks.find((item) => item.name === 'accuracy-task-leftovers').status, 'UNVERIFIED');
  assert.equal(checks.find((item) => item.name === 'accuracy-leftovers').status, 'UNVERIFIED');
});

test('task inventory falls back only to explicit camera task lists when task page is unsupported', async () => {
  const unsupported = new Error('HTTP 400 on POST /task/page');
  unsupported.httpStatus = 400;
  unsupported.routeUnsupported = true;
  const complete = await queryAccuracyLeftoverChecks({
    async cameraPage() {
      return {
        total: 1,
        rows: [{ videoChannelId: 'prod-1', channelName: 'production', taskList: [] }],
      };
    },
    async taskPage() { throw unsupported; },
  });
  assert.equal(check({ checks: complete }, 'accuracy-task-inventory').status, 'PASS');
  assert.match(check({ checks: complete }, 'accuracy-task-inventory').detail, /camera-page/i);
  assert.equal(check({ checks: complete }, 'accuracy-leftovers').status, 'PASS');

  const incomplete = await queryAccuracyLeftoverChecks({
    async cameraPage() {
      return { total: 1, rows: [{ videoChannelId: 'prod-1', channelName: 'production' }] };
    },
    async taskPage() { throw unsupported; },
  });
  assert.equal(check({ checks: incomplete }, 'accuracy-task-inventory').status, 'UNVERIFIED');
  assert.equal(check({ checks: incomplete }, 'accuracy-task-leftovers').status, 'UNVERIFIED');
  assert.equal(check({ checks: incomplete }, 'accuracy-leftovers').status, 'UNVERIFIED');

  const structuredBadRequest = new Error('HTTP 400 on POST /task/page');
  structuredBadRequest.httpStatus = 400;
  structuredBadRequest.routeUnsupported = false;
  const noFallback = await queryAccuracyLeftoverChecks({
    async cameraPage() {
      return {
        total: 1,
        rows: [{ videoChannelId: 'prod-1', channelName: 'production', taskList: [] }],
      };
    },
    async taskPage() { throw structuredBadRequest; },
  });
  assert.equal(check({ checks: noFallback }, 'accuracy-task-inventory').status, 'UNVERIFIED');
  assert.equal(check({ checks: noFallback }, 'accuracy-leftovers').status, 'UNVERIFIED');
});

function check(result, name) {
  return result.checks.find((item) => item.name === name);
}
