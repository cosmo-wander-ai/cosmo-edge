import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  DeviceTrialExecutor,
  observeUntilDecisiveEvent,
  waitForAccuracyTaskReady,
} from '../src/accuracy/device-trial.js';
import { AccuracyEvidenceWriter } from '../src/accuracy/evidence.js';

function fixtureClient() {
  let deleted = false;
  let taskDeleted = false;
  let channelName = '';
  const calls = [];
  return {
    calls,
    beginCleanup() {},
    async uploadCapabilities() {
      return { maxChunkSize: String(8 * 1024 * 1024), maxTotalSize: '1000000', availableForNewUploadsBytes: '1000000' };
    },
    async uploadTempChunk(_buffer, _name, meta) {
      return { resData: { uploadId: 'upload-id', nextChunkIndex: meta.totalChunks, complete: true } };
    },
    async cameraAddVideo(payload) {
      calls.push({ route: 'cameraAddVideo', payload });
      channelName = payload.channelName;
      return { resData: { id: 'channel-1' } };
    },
    async taskSaveOrUpdate(payload) {
      calls.push({ route: 'taskSaveOrUpdate', payload });
      return { resData: {} };
    },
    async cameraPage() {
      return { rows: deleted ? [] : [{
        videoChannelId: 'channel-1', channelName, channelStatus: 1,
      }] };
    },
    async taskRunningDetail() {
      if (taskDeleted) return { status: [] };
      return { status: [{
        taskId: 'channel-1_15', channelId: 'channel-1',
        actionStatus: [{ name: 'Decode', actionId: 'decode', processCountPeriod: 1 }],
      }] };
    },
    async eventPage() {
      return { total: 1, rows: [{
        id: 'event-1', videoChannelId: 'channel-1', channelName,
        algorithmCode: '15', timestamp: 1_500, detectedPicture: '/event/one.jpg',
      }] };
    },
    async taskBatchSwitch(payload) {
      calls.push({ route: 'taskBatchSwitch', payload });
      return { failedList: [] };
    },
    async downloadArtifact() {
      return { buffer: Buffer.from('alert-image'), contentType: 'image/jpeg' };
    },
    async taskDelete(payload) {
      calls.push({ route: 'taskDelete', payload });
      taskDeleted = true;
      return { resData: {} };
    },
    async cameraBatchDelete(ids) {
      calls.push({ route: 'cameraBatchDelete', ids });
      deleted = true;
    },
  };
}

test('device trial uploads, binds, collects persisted events, archives images, and verifies cleanup', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-device-trial-'));
  try {
    const video = path.join(root, 'sample.mp4');
    fs.writeFileSync(video, 'video');
    const output = path.join(root, 'run');
    const evidence = new AccuracyEvidenceWriter(output);
    evidence.initialize({ protocolVersion: 4 });
    const client = fixtureClient();
    let now = 1_000;
    const executor = new DeviceTrialExecutor({
      client,
      suite: {
        id: 'suite', sourceMode: 'local',
        defaults: {
          observeSec: 1, readyTimeoutSec: 10, readyPollIntervalSec: 1,
          eventFlushTimeoutSec: 5, eventPollIntervalSec: 1,
        },
      },
      runId: 'run-1',
      evidenceWriter: evidence,
      now: () => now,
      monotonicNow: () => now,
      sleep: async (ms) => { now += ms; },
    });
    const result = await executor.executeTrial({
      task: {
        id: 'helmet', kind: 'cv', algorithmId: '15', algorithmCode: '15', scheduleId: 'always',
        configSource: 'frozen',
        taskConfig: { params: [{ key: 'param.videoRepeatCount', value: '0' }], areas: [] },
      },
      case: {
        id: 'positive', task: 'helmet', absoluteFile: video,
        sha256: crypto.createHash('sha256').update('video').digest('hex'),
        expectation: { minEvents: 1 },
      },
      attemptNumber: 1,
      validTrialNumber: 1,
    });
    assert.equal(result.status, 'PASS');
    assert.equal(result.eventCount, 1);
    assert.equal(result.cleanup.channel.verified, true);
    assert.equal(result.timingMs.observation, 1000);
    assert.equal(Number.isFinite(result.timingMs.videoUpload), true);
    assert.equal(Number.isFinite(result.timingMs.channelCreate), true);
    assert.equal(result.timingMs.total >= result.timingMs.observation, true);
    assert.equal(result.events[0].detectedPicture, undefined);
    assert.match(result.events[0].detectedPictureArtifact.path, /^artifacts\/alerts\/[a-f0-9]{64}\.jpg$/);
    assert.deepEqual(client.calls.map((item) => item.route), [
      'cameraAddVideo', 'taskSaveOrUpdate', 'taskBatchSwitch', 'taskDelete', 'cameraBatchDelete',
    ]);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('observation stops only after Event/Page makes the expectation irreversible', async () => {
  let now = 1_000;
  let polls = 0;
  const result = await observeUntilDecisiveEvent({
    client: {},
    channelName: 'channel',
    channelId: 'id',
    algorithmCode: '15',
    expectation: { minEvents: 1 },
    timeBegin: now,
    observeSec: 45,
    pollIntervalSec: 5,
    now: () => now,
    monotonicNow: () => now,
    sleep: async (ms) => { now += ms; },
    querySnapshot: async () => {
      polls += 1;
      return polls === 1 ? [] : [{ id: 'persisted-event' }];
    },
  });
  assert.equal(result.earlyStopped, true);
  assert.equal(result.triggerStatus, 'PASS');
  assert.equal(result.actualSec, 10);
  assert.equal(result.queryCount, 2);
});

test('observation falls back to the full window when an early query fails', async () => {
  let now = 0;
  const result = await observeUntilDecisiveEvent({
    client: {}, channelName: 'channel', channelId: 'id', algorithmCode: '15',
    expectation: { minEvents: 1 }, timeBegin: 0, observeSec: 20, pollIntervalSec: 5,
    now: () => now,
    monotonicNow: () => now,
    sleep: async (ms) => { now += ms; },
    querySnapshot: async () => { throw new Error('temporary query failure'); },
  });
  assert.equal(result.earlyStopped, false);
  assert.equal(result.actualSec, 20);
  assert.match(result.queryError, /temporary query failure/);
  assert.equal(result.queryErrorCount, 3);
});

test('an Event/Page response after the deadline cannot trigger an early verdict', async () => {
  let now = 0;
  const result = await observeUntilDecisiveEvent({
    client: {}, channelName: 'channel', channelId: 'id', algorithmCode: '15',
    expectation: { minEvents: 1 }, timeBegin: 0, observeSec: 10, pollIntervalSec: 5,
    now: () => now,
    monotonicNow: () => now,
    sleep: async (ms) => { now += ms; },
    querySnapshot: async () => {
      now += 6_000;
      return [{ id: 'late-event' }];
    },
  });
  assert.equal(result.earlyStopped, false);
  assert.equal(result.actualSec, 11);
});

test('device trial fails closed when an early persisted event disappears at final settlement', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-early-settle-'));
  try {
    const video = path.join(root, 'sample.mp4');
    fs.writeFileSync(video, 'video');
    let now = 0;
    const executor = new DeviceTrialExecutor({
      client: fixtureClient(),
      suite: {
        id: 'suite', sourceMode: 'local',
        defaults: {
          observeSec: 10, earlyStopPollIntervalSec: 1,
          readyTimeoutSec: 2, readyPollIntervalSec: 1,
          eventFlushTimeoutSec: 2, eventPollIntervalSec: 1, eventSettleMinSec: 1,
        },
      },
      runId: 'run-1',
      now: () => now,
      monotonicNow: () => now,
      sleep: async (ms) => { now += ms; },
      eventSnapshot: async () => [{ id: 'early' }],
      eventCollector: async () => ({ settled: true, queryCount: 2, events: [] }),
    });
    const result = await executor.executeTrial({
      task: {
        id: 'helmet', kind: 'cv', algorithmId: '15', algorithmCode: '15', scheduleId: 'always',
        configSource: 'frozen', taskConfig: { params: [], areas: [] },
      },
      case: {
        id: 'positive', task: 'helmet', absoluteFile: video,
        sha256: crypto.createHash('sha256').update('video').digest('hex'),
        expectation: { minEvents: 1 },
      },
      attemptNumber: 1,
      validTrialNumber: 1,
    });
    assert.equal(result.status, 'ERROR');
    assert.match(result.error, /did not survive final settlement/);
    assert.equal(result.cleanup.channel.verified, true);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('readiness times out instead of treating an online camera without decoded frames as ready', async () => {
  let now = 0;
  const client = {
    async cameraPage() { return { rows: [{ videoChannelId: 'c', channelStatus: 1 }] }; },
    async taskRunningDetail() { return { status: [{ taskId: 'c_15', actionStatus: [] }] }; },
  };
  await assert.rejects(waitForAccuracyTaskReady({
    client, channelId: 'c', algorithmId: '15', timeoutSec: 2, pollIntervalSec: 1,
    now: () => now,
    sleep: async (ms) => { now += ms; },
  }), /readiness timed out/i);
});

test('readiness uses algorithmCode for the composed runtime task id', async () => {
  let requested = null;
  const client = {
    async cameraPage() { return { rows: [{ videoChannelId: 'c', channelStatus: 1 }] }; },
    async taskRunningDetail(ids) {
      requested = ids;
      return { status: [{
        taskId: 'c_99898', channelId: 'c',
        actionStatus: [{ name: 'Detect', processCountPeriod: 1 }],
      }] };
    },
  };
  const result = await waitForAccuracyTaskReady({
    client, channelId: 'c', algorithmId: 'internal-id', algorithmCode: '99898',
    timeoutSec: 1, pollIntervalSec: 1,
  });
  assert.equal(result.ready, true);
  assert.deepEqual(requested, ['c_99898']);
});

test('device trial rechecks the sample hash immediately before device mutation', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-device-hash-'));
  try {
    const video = path.join(root, 'sample.mp4');
    fs.writeFileSync(video, 'changed-video');
    const client = fixtureClient();
    const executor = new DeviceTrialExecutor({
      client,
      suite: {
        id: 'suite', sourceMode: 'local',
        defaults: {
          observeSec: 1, readyTimeoutSec: 1, readyPollIntervalSec: 1,
          eventFlushTimeoutSec: 1, eventPollIntervalSec: 1,
        },
      },
      runId: 'run-1',
      sleep: async () => {},
    });
    const result = await executor.executeTrial({
      task: {
        id: 'helmet', kind: 'cv', algorithmId: '15', algorithmCode: '15', scheduleId: 'always',
        configSource: 'frozen', taskConfig: { params: [], areas: [] },
      },
      case: {
        id: 'changed', task: 'helmet', absoluteFile: video,
        sha256: crypto.createHash('sha256').update('original-video').digest('hex'),
        expectation: { minEvents: 1 },
      },
      attemptNumber: 1,
      validTrialNumber: 1,
    });
    assert.equal(result.status, 'ERROR');
    assert.match(result.error, /SHA256 changed/);
    assert.equal(client.calls.length, 0);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
