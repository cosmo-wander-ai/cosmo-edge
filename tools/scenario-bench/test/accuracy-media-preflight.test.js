import assert from 'node:assert/strict';
import test from 'node:test';

import { inspectLocalMedia } from '../src/accuracy/media-preflight.js';

test('media preflight reports invalid videos, durations, and same-task duplicate content', async () => {
  const cases = [
    { id: 'a', task: 'helmet', sha256: 'same', absoluteFile: '/data/a.mp4' },
    { id: 'b', task: 'helmet', sha256: 'same', absoluteFile: '/data/b.mp4' },
    { id: 'broken', task: 'helmet', sha256: 'broken', absoluteFile: '/data/broken.mp4' },
  ];
  const execute = async (_command, args) => {
    if (args[0] === '-version') return { stdout: 'ffprobe version test' };
    const file = args.at(-1);
    if (file.endsWith('broken.mp4')) {
      const error = new Error('probe failed');
      error.stderr = `${file}: moov atom not found`;
      throw error;
    }
    const duration = file.endsWith('a.mp4') ? '8.5' : '12.25';
    return { stdout: JSON.stringify({
      streams: [{ codec_type: 'video', codec_name: 'h264', width: 1920, height: 1080 }],
      format: { duration },
    }) };
  };
  const result = await inspectLocalMedia(cases, { execute, concurrency: 2 });
  assert.equal(result.checked, 3);
  assert.equal(result.uniqueContent, 2);
  assert.equal(result.valid, 2);
  assert.deepEqual(result.invalid.map((item) => item.id), ['broken']);
  assert.equal(result.invalid[0].reason.includes('/data/'), false);
  assert.equal(result.durationSec.max, 8.5);
  assert.equal(result.duplicateContent.extraCases, 1);
  assert.deepEqual(result.duplicateContent.groupsByTask[0].ids, ['a', 'b']);
});
