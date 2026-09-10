import assert from 'node:assert/strict';
import test from 'node:test';

import { readSecretLine } from '../src/stdin-secret.js';

function fakeReader(chunks) {
  let index = 0;
  return (_fd, buffer, offset, length) => {
    if (index >= chunks.length) return 0;
    const chunk = Buffer.from(chunks[index++]);
    assert.ok(chunk.length <= length);
    chunk.copy(buffer, offset);
    return chunk.length;
  };
}

test('secret input returns at the first newline without waiting for EOF', () => {
  let reads = 0;
  const readSync = (...args) => {
    reads += 1;
    return fakeReader(['secret\nignored'])(...args);
  };
  assert.equal(readSecretLine({ readSync }), 'secret');
  assert.equal(reads, 1);
});

test('secret input supports fragmented CRLF data and enforces a byte limit', () => {
  assert.equal(readSecretLine({
    readSync: fakeReader(['sec', 'ret\r\n']),
  }), 'secret');
  assert.throws(
    () => readSecretLine({ readSync: fakeReader(['1234', '5']), maxBytes: 4 }),
    /exceeds 4 bytes/i,
  );
  assert.throws(() => readSecretLine({ readSync: fakeReader(['\n']) }), /was empty/i);
});
