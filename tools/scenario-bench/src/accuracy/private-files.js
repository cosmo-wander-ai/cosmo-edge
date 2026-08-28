import fs from 'node:fs';
import path from 'node:path';

export const PRIVATE_DIR_MODE = 0o700;
export const PRIVATE_FILE_MODE = 0o600;

export function ensurePrivateDir(dir) {
  const absolute = path.resolve(dir);
  fs.mkdirSync(absolute, { recursive: true, mode: PRIVATE_DIR_MODE });
  chmodBestEffort(absolute, PRIVATE_DIR_MODE);
  return absolute;
}

export function writePrivateJson(file, value) {
  writePrivateFile(file, `${JSON.stringify(value, null, 2)}\n`);
}

export function writePrivateFile(file, content) {
  const absolute = path.resolve(file);
  ensurePrivateDir(path.dirname(absolute));
  const temporary = `${absolute}.tmp`;
  fs.writeFileSync(temporary, content, { mode: PRIVATE_FILE_MODE });
  chmodBestEffort(temporary, PRIVATE_FILE_MODE);
  fs.renameSync(temporary, absolute);
  chmodBestEffort(absolute, PRIVATE_FILE_MODE);
  return absolute;
}

export function chmodBestEffort(target, mode) {
  try { fs.chmodSync(target, mode); } catch { /* Non-POSIX filesystems may not support modes. */ }
}
