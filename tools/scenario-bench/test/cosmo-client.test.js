import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test from 'node:test';

import { CosmoClient } from '../src/cosmo-client.js';

test('existing device token bypasses password login', async () => {
  const client = new CosmoClient({ base: 'http://device', token: 'short-lived-token' });
  client._post = async () => {
    throw new Error('login endpoint must not be called');
  };

  assert.deepEqual(await client.login(), { mtk: 'short-lived-token' });
});

test('login rejects missing credentials when no token is supplied', async () => {
  const client = new CosmoClient({ base: 'http://device' });
  await assert.rejects(client.login(), /requires user\/password or an existing token/);
});

test('external shutdown aborts an in-flight device request without rewriting the reason', async () => {
  const controller = new AbortController();
  const reason = Object.assign(new Error('received SIGTERM; shutting down'), { exitCode: 143 });
  let requestSignal = null;
  const client = new CosmoClient({
    base: 'http://device',
    token: 'token',
    signal: controller.signal,
    fetchImpl: async (_url, options) => {
      requestSignal = options.signal;
      return new Promise((_resolve, reject) => {
        options.signal.addEventListener('abort', () => reject(options.signal.reason), { once: true });
      });
    },
  });

  const request = client.queryHardwareResource();
  controller.abort(reason);

  await assert.rejects(request, (error) => error === reason && error.exitCode === 143);
  assert.equal(requestSignal.aborted, true);
});

test('cleanup requests remain available after the run signal is aborted', async () => {
  const controller = new AbortController();
  controller.abort(Object.assign(new Error('received SIGTERM; shutting down'), { exitCode: 143 }));
  let requestSignal = null;
  const client = new CosmoClient({
    base: 'http://device',
    token: 'token',
    signal: controller.signal,
    fetchImpl: async (_url, options) => {
      requestSignal = options.signal;
      return {
        ok: true,
        async json() {
          return { resCode: 1, resData: { failedList: [] } };
        },
      };
    },
  });

  client.beginCleanup();
  const result = await client.taskBatchSwitch([
    { id: 'task-1', channelId: 'channel-1', algorithmId: '7463', enable: 0 },
  ]);

  assert.deepEqual(result, { failedList: [] });
  assert.equal(requestSignal.aborted, false);
});

test('detached cleanup client preserves the active signal on concurrent work', () => {
  const controller = new AbortController();
  const client = new CosmoClient({
    base: 'http://device', token: 'token', signal: controller.signal,
    fetchImpl: async () => { throw new Error('not used'); },
  });
  const cleanup = client.detachedCleanupClient();
  assert.equal(client.signal, controller.signal);
  assert.equal(cleanup.signal, null);
  assert.equal(cleanup.mtk, 'token');
});

test('batch task switch uses the wire-level switch field', async () => {
  const client = new CosmoClient({ base: 'http://device', token: 'token' });
  let request = null;
  client._post = async (route, payload) => {
    request = { route, payload };
    return { resData: { failedList: [] } };
  };

  await client.taskBatchSwitch([{
    id: 'LX1_7463',
    channelId: 'LX1',
    algorithmId: '7463',
    enable: 1,
  }]);

  assert.deepEqual(request, {
    route: '/Task/BatchSwitchTask',
    payload: {
      tasks: [{ id: 'LX1_7463', channelId: 'LX1', algorithmId: '7463', switch: 1 }],
    },
  });
});

function successResponse(uploadId = crypto.randomUUID()) {
  return new Response(JSON.stringify({
    resCode: 1,
    resData: { uploadId, complete: true },
    resMsg: [],
  }), { status: 200, headers: { 'Content-Type': 'application/json' } });
}

test('multipart uploads respect the two-request client concurrency boundary', async () => {
  let active = 0;
  let maxActive = 0;
  const releases = [];
  const client = new CosmoClient({
    base: 'http://device',
    token: 'token',
    fetchImpl: async () => {
      active += 1;
      maxActive = Math.max(maxActive, active);
      await new Promise((resolve) => releases.push(resolve));
      active -= 1;
      return successResponse();
    },
  });
  const startUpload = (index) => client.uploadTempChunk(Buffer.from(`image-${index}`), `${index}.jpg`, {
    clientRequestId: crypto.randomUUID(),
    purpose: 'image',
    chunkIndex: 0,
    totalChunks: 1,
    totalSize: 7,
    chunkSize: 7,
  });

  const uploads = [0, 1, 2, 3].map(startUpload);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(active, 2);
  releases.splice(0).forEach((release) => release());
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(active, 2);
  releases.splice(0).forEach((release) => release());
  await Promise.all(uploads);

  assert.equal(maxActive, 2);
  assert.equal(client.uploadTelemetry().maxActive, 2);
  assert.equal(client.uploadTelemetry().queued, 0);
});

test('multipart uploads back off and retry a structured HTTP 503', async () => {
  let calls = 0;
  const delays = [];
  const client = new CosmoClient({
    base: 'http://device',
    token: 'token',
    uploadBackoffMs: 25,
    sleepImpl: async (delay) => delays.push(delay),
    fetchImpl: async () => {
      calls += 1;
      if (calls === 1) {
        return new Response(JSON.stringify({
          resCode: 0,
          resMsg: [{
            msgCode: 'HTTP_SERVICE_BUSY',
            retryable: true,
            retryAfterSeconds: 0,
          }],
        }), { status: 503, headers: { 'Content-Type': 'application/json' } });
      }
      return successResponse('recovered-upload');
    },
  });

  const response = await client.uploadTempChunk(Buffer.from('image'), 'image.jpg', {
    clientRequestId: 'stable-request-id',
    purpose: 'image',
    chunkIndex: 0,
    totalChunks: 1,
    totalSize: 5,
    chunkSize: 5,
  });

  assert.equal(response.resData.uploadId, 'recovered-upload');
  assert.equal(calls, 2);
  assert.deepEqual(delays, [25]);
  assert.equal(client.uploadTelemetry().retries, 1);
  assert.equal(client.uploadTelemetry().busyResponses, 1);
});
