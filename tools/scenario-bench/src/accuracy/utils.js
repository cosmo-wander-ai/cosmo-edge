import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

export function sha256Buffer(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

export function sha256File(file) {
  return sha256Buffer(fs.readFileSync(file));
}

export function stableStringify(value, space = 0) {
  return JSON.stringify(sortValue(value), null, space);
}

export function sortValue(value) {
  if (Array.isArray(value)) return value.map(sortValue);
  if (!value || typeof value !== 'object') return value;
  return Object.fromEntries(
    Object.keys(value).sort().map((key) => [key, sortValue(value[key])]),
  );
}

export function resolveContainedPath(root, relative, label) {
  if (typeof relative !== 'string' || !relative.trim()) {
    throw new Error(`${label} must be a non-empty relative path`);
  }
  if (path.isAbsolute(relative)) throw new Error(`${label} must be relative`);
  const absoluteRoot = path.resolve(root);
  const absolute = path.resolve(absoluteRoot, relative);
  const rel = path.relative(absoluteRoot, absolute);
  if (rel === '..' || rel.startsWith(`..${path.sep}`) || path.isAbsolute(rel)) {
    throw new Error(`${label} escapes data root`);
  }
  return absolute;
}

export function assertFileContained(root, relative, label) {
  const absolute = resolveContainedPath(root, relative, label);
  if (!fs.existsSync(absolute)) throw new Error(`${label} not found: ${relative}`);
  const stat = fs.statSync(absolute);
  if (!stat.isFile()) throw new Error(`${label} must be a regular file: ${relative}`);
  const realRoot = fs.realpathSync(root);
  const realFile = fs.realpathSync(absolute);
  const rel = path.relative(realRoot, realFile);
  if (rel === '..' || rel.startsWith(`..${path.sep}`) || path.isAbsolute(rel)) {
    throw new Error(`${label} escapes data root through a symbolic link`);
  }
  return absolute;
}

export function assertSha256(value, label) {
  if (!/^[a-f0-9]{64}$/i.test(String(value ?? ''))) {
    throw new Error(`${label} must be a 64-character SHA-256`);
  }
  return String(value).toLowerCase();
}

export function requirePositiveNumber(value, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) throw new Error(`${label} must be positive`);
  return number;
}

export function requireNonNegativeInteger(value, label) {
  const number = Number(value);
  if (!Number.isInteger(number) || number < 0) {
    throw new Error(`${label} must be a non-negative integer`);
  }
  return number;
}

export function requireRate(value, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number < 0 || number > 1) {
    throw new Error(`${label} must be between 0 and 1`);
  }
  return number;
}

export function isPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}
