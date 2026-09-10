import fs from 'node:fs';

const DEFAULT_MAX_SECRET_BYTES = 4096;
const READ_CHUNK_BYTES = 256;

export function readSecretLine({
  fd = 0,
  maxBytes = DEFAULT_MAX_SECRET_BYTES,
  readSync = fs.readSync,
} = {}) {
  if (!Number.isInteger(maxBytes) || maxBytes < 1) {
    throw new Error('maxBytes must be a positive integer');
  }
  const chunks = [];
  let total = 0;
  while (true) {
    const remaining = maxBytes + 1 - total;
    if (remaining <= 0) throw new Error(`secret input exceeds ${maxBytes} bytes`);
    const buffer = Buffer.allocUnsafe(Math.min(READ_CHUNK_BYTES, remaining));
    const bytesRead = readSync(fd, buffer, 0, buffer.length, null);
    if (bytesRead === 0) break;
    const received = buffer.subarray(0, bytesRead);
    const newline = received.indexOf(0x0a);
    const accepted = newline >= 0 ? received.subarray(0, newline) : received;
    total += accepted.length;
    if (total > maxBytes) throw new Error(`secret input exceeds ${maxBytes} bytes`);
    chunks.push(Buffer.from(accepted));
    if (newline >= 0) break;
  }
  const value = Buffer.concat(chunks, total).toString('utf8').replace(/\r$/u, '');
  if (!value) throw new Error('secret input was empty');
  return value;
}
