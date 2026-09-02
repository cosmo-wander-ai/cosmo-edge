import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { ChannelCleanupError, ChannelManager } from '../src/channel-manager.js';

test('strict channel cleanup verifies that run-owned channels disappeared', async () => {
  const deleted = [];
  const client = {
    async cameraAdd() { return { resData: { id: 'created-1' } }; },
    async cameraBatchDelete(ids) { deleted.push(...ids); },
    async cameraPage() { return { rows: [] }; },
  };
  const manager = new ChannelManager(client, {
    channelPrefix: 'acc-run', reuse: false, cleanup: true, strictCleanup: true,
    cleanupVerifyDelayMs: 0,
  });
  await manager.ensureChannels({ mode: 'rtsp-deterministic', rtsp: [{ url: 'rtsp://media/test' }] }, 1);
  const result = await manager.finish();
  assert.deepEqual(deleted, ['created-1']);
  assert.equal(result.verified, true);
  assert.deepEqual(result.remaining, []);
});

test('strict channel cleanup throws structured evidence instead of swallowing a leak', async () => {
  const client = {
    async cameraAdd() { return { resData: { id: 'created-1' } }; },
    async cameraBatchDelete() {},
    async cameraPage() {
      return { rows: [{ videoChannelId: 'created-1', channelName: 'acc-run-01' }] };
    },
  };
  const manager = new ChannelManager(client, {
    channelPrefix: 'acc-run', reuse: false, cleanup: true, strictCleanup: true,
    cleanupVerifyDelayMs: 0,
  });
  await manager.ensureChannels({ mode: 'rtsp-deterministic', rtsp: [{ url: 'rtsp://media/test' }] }, 1);
  await assert.rejects(manager.finish(), (error) => {
    assert.ok(error instanceof ChannelCleanupError);
    assert.deepEqual(error.result.remaining, ['created-1']);
    return true;
  });
});

test('a completed upload is cancelled when AddVideo fails before channel ownership exists', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-upload-cleanup-'));
  try {
    const video = path.join(root, 'sample.mp4');
    fs.writeFileSync(video, 'video');
    let cancelled = null;
    const client = {
      async uploadCapabilities() { return { maxChunkSize: '1024' }; },
      async uploadTempChunk(_buffer, _name, meta) {
        return { resData: { uploadId: 'orphan-upload', nextChunkIndex: meta.totalChunks, complete: true } };
      },
      async cameraAddVideo() { throw new Error('camera capacity reached'); },
      async cancelUploadBestEffort(uploadId) { cancelled = uploadId; },
    };
    const manager = new ChannelManager(client, {
      channelPrefix: 'acc-run', reuse: false, cleanup: true, strictCleanup: true,
    });
    await assert.rejects(
      manager.ensureChannels({ mode: 'local', local: [{ file: video, name: 'src' }] }, 1),
      /camera capacity reached/,
    );
    assert.equal(cancelled, 'orphan-upload');
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test('an interrupted multipart upload is cancelled through a detached cleanup client', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'accuracy-upload-abort-'));
  try {
    const video = path.join(root, 'sample.mp4');
    fs.writeFileSync(video, '12345');
    const cancelled = [];
    let chunks = 0;
    const cleanupClient = {
      async cancelUpload(uploadId) { cancelled.push(uploadId); },
    };
    const client = {
      async uploadCapabilities() { return { maxChunkSize: '4' }; },
      async uploadTempChunk(_buffer, _name, meta) {
        chunks += 1;
        if (chunks === 2) throw new Error('run aborted');
        return {
          resData: {
            uploadId: 'interrupted-upload',
            nextChunkIndex: 1,
            complete: false,
          },
        };
      },
      detachedCleanupClient() { return cleanupClient; },
    };
    const manager = new ChannelManager(client, {
      channelPrefix: 'acc-run', reuse: false, cleanup: true, strictCleanup: true,
    });
    await assert.rejects(
      manager.ensureChannels({ mode: 'local', local: [{ file: video, name: 'src' }] }, 1),
      /run aborted/,
    );
    const result = await manager.finish({ client: cleanupClient });
    assert.deepEqual(cancelled, ['interrupted-upload']);
    assert.deepEqual(result.upload.cancelled, ['interrupted-upload']);
    assert.equal(result.verified, true);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
