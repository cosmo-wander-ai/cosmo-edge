import assert from 'node:assert/strict';
import test from 'node:test';

import { DeviceTrialExecutor } from '../src/accuracy/device-trial.js';
import { buildAccuracyFfmpegCommand, ManagedRtspPusher } from '../src/accuracy/rtsp-pusher.js';

test('deterministic RTSP command preserves the device decoder compatibility contract', () => {
  const command = buildAccuracyFfmpegCommand({
    ffmpeg: 'ffmpeg', sourceUrl: 'http://files/sample.mp4', targetUrl: 'rtsp://media/run/case',
  });
  assert.equal(command.file, 'ffmpeg');
  assert.ok(command.args.includes('-re'));
  assert.ok(command.args.includes('-stream_loop'));
  assert.ok(command.args.includes('repeat-headers=1'));
  assert.ok(command.args.includes('-rtsp_transport'));
  assert.ok(command.args.includes('tcp'));
  assert.equal(command.args.at(-1), 'rtsp://media/run/case');
});

test('managed RTSP pusher fails when ffmpeg exits during warmup and always stops cleanly', async () => {
  let terminated = false;
  const process = {
    exitCode: 1,
    stderr: { read: () => 'failed' },
    kill(signal) { terminated = signal === 'SIGTERM'; },
  };
  const pusher = new ManagedRtspPusher({
    spawnImpl: () => process,
    sleep: async () => {},
  });
  await assert.rejects(
    pusher.start({ sourceUrl: 'http://files/sample.mp4', targetUrl: 'rtsp://media/test' }),
    /ffmpeg exited/i,
  );
  await pusher.stop();
  assert.equal(terminated, false);
});

test('RTSP trials use a unique accuracy-prefixed channel name', async () => {
  let started = null;
  const executor = new DeviceTrialExecutor({
    client: {},
    suite: {
      sourceMode: 'rtsp-deterministic',
      rtsp: {
        httpBase: 'http://files/root', mediaMtxBase: 'rtsp://media', ffmpeg: 'ffmpeg',
      },
    },
    runId: 'run',
    rtspPusherFactory: () => ({
      async start(value) { started = value; },
      async stop() {},
    }),
  });
  const source = await executor._sourceForTrial({ file: '目录/sample one.mp4' }, 'acc-abc-01');
  assert.equal(source.videos.rtsp[0].name, 'acc-abc-01-src');
  assert.equal(started.sourceUrl, 'http://files/root/%E7%9B%AE%E5%BD%95/sample%20one.mp4');
  assert.equal(started.targetUrl, 'rtsp://media/acc-abc-01');
});
