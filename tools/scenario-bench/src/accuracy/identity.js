import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { sha256Buffer, sha256File } from './utils.js';

const MODULE_DIR = path.dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = path.resolve(MODULE_DIR, '..', '..');
const REPOSITORY_ROOT = path.resolve(PACKAGE_ROOT, '..', '..');

export function flattenDeviceInfo(raw) {
  const flat = {};
  for (const item of raw?.devInfoList ?? []) {
    if (item?.key) flat[item.key] = item.value;
  }
  return flat;
}

export function sanitizedDeviceIdentity(raw) {
  const flat = raw?.devInfoList ? flattenDeviceInfo(raw) : raw ?? {};
  const model = flat.deviceType ?? flat.deviceModel ?? null;
  const softwareVersion = flat.softwareVersion ?? null;
  const hardwareVersion = flat.hardwareVersion ?? null;
  const privateIdentity = flat.deviceSn ?? flat.sn ?? model ?? 'unknown-device';
  return {
    model,
    softwareVersion,
    hardwareVersion,
    deviceFingerprint: sha256Buffer(Buffer.from(String(privateIdentity))),
  };
}

export function buildToolIdentity() {
  const accuracyFiles = fs.readdirSync(path.join(PACKAGE_ROOT, 'src', 'accuracy'))
    .filter((name) => name.endsWith('.js'))
    .map((name) => `src/accuracy/${name}`);
  const files = [...new Set([
    'src/channel-manager.js',
    'src/cosmo-client.js',
    'src/accuracy-cli.js',
    'src/logger.js',
    'src/metrics-sampler.js',
    'src/shutdown-signal.js',
    'src/stdin-secret.js',
    'src/vlm-readiness.js',
    ...accuracyFiles,
    'package-lock.json',
  ])].sort();
  const hashes = {};
  for (const relative of files) {
    const absolute = path.join(PACKAGE_ROOT, relative);
    hashes[relative] = fs.existsSync(absolute) ? sha256File(absolute) : null;
  }
  return {
    nodeVersion: process.version,
    files: hashes,
    repository: repositoryIdentity(),
  };
}

export function buildRunIdentity({
  suite,
  targetChip,
  device,
  runId,
  execution,
  tool = buildToolIdentity(),
}) {
  if (!execution) throw new Error('run identity requires execution');
  return {
    runId,
    protocolVersion: suite.protocolVersion,
    schemaVersion: suite.schemaVersion,
    suiteId: suite.id,
    suiteSha256: suite.identity.suiteSha256,
    caseManifestSha256: suite.identity.caseManifestSha256,
    caseSetSha256: suite.identity.caseSetSha256,
    taskConfigSha256: suite.identity.taskConfigSha256,
    sourceMode: suite.sourceMode,
    targetChip,
    deviceFingerprint: device.deviceFingerprint,
    softwareVersion: device.softwareVersion,
    execution: structuredClone(execution),
    tool: structuredClone(tool),
  };
}

export function newRunId(now = new Date()) {
  const stamp = now.toISOString().replace(/[-:.TZ]/gu, '').slice(0, 14);
  return `acc-${stamp}-${crypto.randomBytes(4).toString('hex')}`;
}

function repositoryIdentity() {
  try {
    const status = execFileSync('git', ['status', '--porcelain', '--untracked-files=normal'], {
      cwd: REPOSITORY_ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'],
    }).trim();
    return {
      commit: execFileSync('git', ['rev-parse', 'HEAD'], {
        cwd: REPOSITORY_ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'],
      }).trim(),
      tree: execFileSync('git', ['rev-parse', 'HEAD^{tree}'], {
        cwd: REPOSITORY_ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'],
      }).trim(),
      dirty: Boolean(status),
    };
  } catch {
    return { commit: null, tree: null, dirty: null };
  }
}
