#!/usr/bin/env node

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { ReportWriter } from '../tools/scenario-bench/src/report-writer.js';
import { ScenarioPackage } from '../tools/scenario-bench/src/scenario-package.js';
import {
  normalizeTaskType,
  resolveTaskThresholds,
} from '../tools/scenario-bench/src/task-strategies.js';

export const PLATFORM_IDS = Object.freeze(['bm1688', 'cv186x', 'rk3576']);
export const EXPECTED_CHANNELS = Object.freeze([1, 2, 3, 4, 5, 6, 7, 8]);
export const ALLOWED_COMPLETION_ACTION_IDS = Object.freeze(['BA_00004', 'PDA_00003']);
export const E2E_ACCEPTANCE_KEYS = Object.freeze([
  'modelLoad',
  'taskCreation',
  'inferenceBeforeRestart',
  'eventOrAlarmOutput',
  'serviceRestart',
  'taskRecoveryAfterRestart',
  'cleanup',
  'layoutIntegrity',
]);

const E2E_PRIVACY_KEYS = Object.freeze([
  'endpointStored',
  'credentialsStored',
  'serialNumberStored',
  'internalIdentifiersStored',
  'inferenceTextStored',
  'eventRowsStored',
]);
const E2E_IMPLEMENTATION_KEYS = Object.freeze(['helper', 'privilegedWrapper', 'tests']);
const E2E_IDENTITY_KEYS = Object.freeze(['packageManifest', 'postinstall', 'finalization']);
const CANONICAL_E2E_STAGE_KEYS = Object.freeze([
  'modelLoad',
  'taskCreation',
  'validInferenceResult',
  'eventOrAlarmOutput',
  'taskRecoveryAfterServiceRestart',
]);

const EXPECTED_RUNTIME_INVENTORY = Object.freeze({
  bm1688: Object.freeze(['libbmlib.so', 'libbmrt.so', 'libbmrt.so.1.0']),
  cv186x: Object.freeze(['libbmlib.so', 'libbmrt.so', 'libbmrt.so.1.0']),
  rk3576: Object.freeze(['librkllmrt.so', 'librknnrt.so']),
});
const EXPECTED_VLM_INVENTORY = Object.freeze({
  bm1688: Object.freeze({ model: 'model.nn', tokenizer: 'tokenizer.json', config: 'config.json' }),
  cv186x: Object.freeze({ model: 'model.nn', tokenizer: 'tokenizer.json', config: 'config.json' }),
  rk3576: Object.freeze({
    model: 'model.rkllm',
    vision: 'vision.rknn',
    tokenizer: 'tokenizer.json',
    config: 'config.json',
  }),
});

const EXPECTED = Object.freeze({
  holdSec: 60,
  sampleIntervalSec: 3,
  targetFps: 0.1,
  minFpsRatio: 0.8,
  maxMissingRate: 0,
  avgDiscardRate: 0.05,
  maxPacketDiscardRate: 0.01,
  maxDiskUsedPercent: 99,
  videoMode: 'local',
  repeatCount: 0,
});

class VerificationError extends Error {
  constructor(message) {
    super(message);
    this.name = 'VerificationError';
  }
}

export function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 1) {
    const flag = argv[index];
    if (flag === '--evidence-root' || flag === '--canonical') {
      if (!argv[index + 1]) fail('arguments', flag + ' requires a value');
      options[flag.slice(2).replace(/-([a-z])/g, (_, letter) => letter.toUpperCase())] = argv[index + 1];
      index += 1;
    } else if (flag === '--help' || flag === '-h') {
      options.help = true;
    } else {
      fail('arguments', 'unknown option');
    }
  }
  return options;
}

export function usage() {
  return [
    'Usage: node scripts/verify-v1.1-vlm-private.mjs --evidence-root <private-root>',
    '       [--canonical <vlm-canonical.json>]',
    '',
    'Required private layout:',
    '  candidates/package-manifest.private.json',
    '  identities/postinstall.private.json',
    '  identities/finalization.private.json',
    '  protocol/protocol-manifest.private.json',
    '  platforms/{bm1688,cv186x,rk3576}/capacity/{metrics.json,summary.json,report.html}',
    '',
    'Optional integrity file:',
    '  evidence-integrity.private.json',
    '  files["platforms/<platform>/capacity/<file>"] = { sha256, sizeBytes? }',
    '',
    'Optional end-to-end integrity file:',
    '  e2e-integrity.private.json',
    '  Required when canonical claim.threePlatformEndToEndAccepted is true.',
    '',
    'The verifier is read-only. Output is sanitized and never prints private paths,',
    'device identifiers, task identifiers, or channel identifiers.',
  ].join('\n');
}

function fail(label, detail) {
  throw new VerificationError(label + ': ' + detail);
}

function check(label, condition, detail = 'unexpected value') {
  if (!condition) fail(label, detail);
}

function equalNumber(actual, expected, tolerance = 1e-9) {
  if (actual == null || expected == null || actual === '' || expected === '') return false;
  return Number.isFinite(Number(actual))
    && Number.isFinite(Number(expected))
    && Math.abs(Number(actual) - Number(expected)) <= tolerance;
}

function equalNullableNumber(actual, expected, tolerance = 1e-9) {
  if (actual == null || expected == null) return actual == null && expected == null;
  return equalNumber(actual, expected, tolerance);
}

function sameJson(actual, expected) {
  return JSON.stringify(actual) === JSON.stringify(expected);
}

function exactObjectKeys(value, expected, label, detail) {
  check(label, value && typeof value === 'object' && !Array.isArray(value), detail);
  check(label, sameJson(Object.keys(value).sort(), [...expected].sort()), detail);
}

function readJson(file, label) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    fail(label, 'missing or invalid JSON');
  }
}

function requireFile(file, label) {
  try {
    const stat = fs.statSync(file);
    check(label, stat.isFile(), 'missing file');
    return stat;
  } catch {
    fail(label, 'missing file');
  }
}

async function sha256File(file, label) {
  requireFile(file, label);
  const hash = crypto.createHash('sha256');
  try {
    await new Promise((resolve, reject) => {
      const stream = fs.createReadStream(file);
      stream.on('data', (chunk) => hash.update(chunk));
      stream.on('error', reject);
      stream.on('end', resolve);
    });
  } catch {
    fail(label, 'could not read file');
  }
  return hash.digest('hex');
}

function resolvePrivatePath(root, reference, label) {
  check(label, typeof reference === 'string' && reference.length > 0, 'invalid file reference');
  check(label, !path.isAbsolute(reference), 'file reference must be relative');
  const resolved = path.resolve(root, reference);
  const relative = path.relative(root, resolved);
  check(label, relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative),
    'file reference escapes evidence root');
  return resolved;
}

function exactPlatformInventory(values, label) {
  const actual = [...new Set(values)].sort();
  check(label, values.length === PLATFORM_IDS.length
    && sameJson(actual, [...PLATFORM_IDS].sort()),
  'platform inventory must be exactly three supported platforms');
}

function platformIdFromManifestEntry(entry) {
  const raw = String(entry?.platformId ?? entry?.platform ?? '').trim().toLowerCase();
  if (raw.includes('bm1688')) return 'bm1688';
  if (raw.includes('cv186x')) return 'cv186x';
  if (raw.includes('rk3576')) return 'rk3576';
  return null;
}

function validateChannelWindow(profile, label) {
  check(label, Array.isArray(profile) && profile.length === EXPECTED_CHANNELS.length,
    'load profile must contain eight levels');
  check(label, sameJson(profile.map((step) => Number(step.channels)), EXPECTED_CHANNELS),
    'load profile must be the continuous 1..8 sequence');
  check(label, profile.every((step) => Number(step.holdSec ?? step.holdSeconds) === EXPECTED.holdSec),
    'each load level must hold for 60 seconds');
}

function templateVlmIdentity(template, label) {
  let actions;
  try {
    actions = typeof template?.algorithmProcessdata === 'string'
      ? JSON.parse(template.algorithmProcessdata)
      : template?.algorithmProcessdata;
  } catch {
    fail(label, 'algorithmProcessdata is invalid');
  }
  check(label, Array.isArray(actions), 'algorithmProcessdata must be an array');
  const action = actions.find((item) =>
    ['DA_00003', 'PDA_00003'].includes(String(item?.actionId ?? '').toUpperCase()));
  check(label, action != null, 'VLM action is missing');
  const params = Object.fromEntries((action?.configObject?.params ?? [])
    .map((item) => [String(item?.key ?? ''), String(item?.value ?? '')]));
  check(label, equalNumber(params.fps, EXPECTED.targetFps), 'template target FPS must be 0.1');
  check(label, typeof params.keywords === 'string' && params.keywords.length > 0, 'template prompt is missing');
  return {
    prompt: params.keywords,
    advancedMode: params.advanced_mode,
    generationStyle: params.generationStyle,
    provider: params.vlmProvider,
  };
}

async function verifyReferencedFile(root, record, label) {
  check(label, record && typeof record === 'object', 'artifact record is missing');
  const file = resolvePrivatePath(root, record.path, label);
  const stat = requireFile(file, label);
  let realRoot;
  let realFile;
  try {
    realRoot = fs.realpathSync(root);
    realFile = fs.realpathSync(file);
  } catch {
    fail(label, 'could not resolve file reference');
  }
  const realRelative = path.relative(realRoot, realFile);
  check(label, realRelative !== '..'
    && !realRelative.startsWith('..' + path.sep)
    && !path.isAbsolute(realRelative),
  'file reference escapes evidence root');
  check(label, Number.isInteger(record.sizeBytes) && record.sizeBytes >= 0,
    'artifact size is missing');
  check(label, stat.size === record.sizeBytes, 'size mismatch');
  check(label, /^[0-9a-f]{64}$/i.test(record.sha256 ?? ''), 'invalid SHA-256');
  check(label, await sha256File(file, label) === record.sha256.toLowerCase(), 'SHA-256 mismatch');
  return file;
}

function validSha256(value) {
  return /^[0-9a-f]{64}$/i.test(String(value ?? ''));
}

function validGitObject(value) {
  return /^[0-9a-f]{40}$/i.test(String(value ?? ''));
}

function timestampMs(value, label, detail = 'timestamp is invalid') {
  const parsed = Date.parse(value);
  check(label, Number.isFinite(parsed), detail);
  return parsed;
}

function verifySourceIdentity(source, label, expected = null) {
  check(label, source && typeof source === 'object', 'source identity is missing');
  check(label, typeof source.repository === 'string' && source.repository.length > 0,
    'repository identity is missing');
  check(label, typeof source.ref === 'string' && source.ref.length > 0,
    'source ref is missing');
  check(label, validGitObject(source.commit), 'source commit is invalid');
  check(label, validGitObject(source.tree), 'source tree is invalid');
  check(label, typeof source.declaredVersion === 'string' && source.declaredVersion.length > 0,
    'declared version is missing');
  check(label, typeof source.releaseStateAtFreeze === 'string'
    && source.releaseStateAtFreeze.length > 0, 'release state is missing');
  check(label, typeof source.qualification === 'string' && source.qualification.length > 0,
    'qualification is missing');
  if (expected != null) {
    for (const key of [
      'repository',
      'ref',
      'commit',
      'tree',
      'declaredVersion',
      'releaseStateAtFreeze',
      'qualification',
    ]) {
      check(label, source[key] === expected[key], key + ' differs from candidate manifest');
    }
  }
  return source;
}

function requireDirectory(directory, label) {
  try {
    const stat = fs.statSync(directory);
    check(label, stat.isDirectory(), 'missing directory');
  } catch {
    fail(label, 'missing directory');
  }
}

function filesNamedInside(directory, fileName) {
  const matches = [];
  const pending = [directory];
  while (pending.length > 0) {
    const current = pending.pop();
    let entries;
    try {
      entries = fs.readdirSync(current, { withFileTypes: true });
    } catch {
      return [];
    }
    for (const entry of entries) {
      const candidate = path.join(current, entry.name);
      if (entry.isSymbolicLink()) continue;
      if (entry.isDirectory()) pending.push(candidate);
      else if (entry.isFile() && entry.name === fileName) matches.push(candidate);
    }
  }
  return matches;
}

function sameRealFile(left, right) {
  try {
    const leftReal = fs.realpathSync(left);
    const rightReal = fs.realpathSync(right);
    return process.platform === 'win32'
      ? leftReal.toLowerCase() === rightReal.toLowerCase()
      : leftReal === rightReal;
  } catch {
    return false;
  }
}

async function verifyPackageManifest(root) {
  const manifestFile = path.join(root, 'candidates', 'package-manifest.private.json');
  const manifestStat = requireFile(manifestFile, 'candidate package manifest');
  const manifestSha256 = await sha256File(manifestFile, 'candidate package manifest');
  const manifest = readJson(manifestFile, 'candidate package manifest');
  check('candidate package manifest', manifest?.schemaVersion === '1.0',
    'schemaVersion must be 1.0');
  check('candidate package manifest', manifest?.visibility === 'private',
    'visibility must be private');
  check('candidate package manifest', manifest?.status === 'three-local-verified',
    'status must be three-local-verified');
  timestampMs(manifest.generatedAt, 'candidate package manifest', 'generatedAt is invalid');
  const source = verifySourceIdentity(manifest.source, 'candidate package source');

  exactObjectKeys(manifest?.packages, PLATFORM_IDS, 'candidate package inventory',
    'package inventory must be exactly three supported platforms');
  const packages = {};
  for (const platformId of PLATFORM_IDS) {
    const label = 'candidate package ' + platformId;
    const record = manifest.packages[platformId];
    check(label, record && typeof record === 'object', 'package record is missing');
    check(label, typeof record.directory === 'string' && record.directory.length > 0,
      'candidate directory is missing');
    check(label, typeof record.file === 'string' && record.file.length > 0,
      'package file is missing');
    check(label, path.basename(record.file) === record.file, 'package file must be a basename');
    check(label, Number.isInteger(Number(record.sizeBytes)) && Number(record.sizeBytes) > 0,
      'package size is invalid');
    check(label, validSha256(record.sha256), 'package SHA-256 is invalid');
    check(label, record.targetChipSidecar === platformId
      && record.archiveTargetChip === platformId, 'target chip identity differs');
    check(label, record.filenameMd5Verified === true
      && record.contentPolicyVerified === true
      && record.localFileVerified === true, 'package verification flags are incomplete');
    if (platformId === 'rk3576') {
      check(label, record.workflowArtifactVerified === true,
        'workflow artifact verification is missing');
    }

    const directory = resolvePrivatePath(root, record.directory, label);
    requireDirectory(directory, label);
    const matches = filesNamedInside(directory, record.file);
    check(label, matches.length === 1, 'package file must resolve uniquely inside its candidate directory');
    const file = matches[0];
    const rootReal = fs.realpathSync(root);
    const fileReal = fs.realpathSync(file);
    const relative = path.relative(rootReal, fileReal);
    check(label, relative !== '..' && !relative.startsWith('..' + path.sep)
      && !path.isAbsolute(relative), 'package file escapes evidence root');
    const stat = requireFile(file, label);
    check(label, stat.size === Number(record.sizeBytes), 'package size mismatch');
    check(label, await sha256File(file, label) === record.sha256.toLowerCase(),
      'package SHA-256 mismatch');
    packages[platformId] = { ...record, file, sha256: record.sha256.toLowerCase() };
  }

  check('candidate builders', manifest?.builders && typeof manifest.builders === 'object',
    'builder inventory is missing');
  for (const key of ['sophon', 'rockchip']) {
    const builder = manifest.builders[key];
    check('candidate builder ' + key, builder && typeof builder === 'object',
      'builder identity is missing');
    check('candidate builder ' + key, typeof builder.execution === 'string'
      && builder.execution.length > 0, 'builder execution identity is missing');
    check('candidate builder ' + key, typeof builder.image === 'string' && builder.image.length > 0,
      'builder image identity is missing');
    check('candidate builder ' + key, /^sha256:[0-9a-f]{64}$/i.test(builder.digest ?? ''),
      'builder digest is invalid');
    check('candidate builder ' + key, typeof builder.buildProfile === 'string'
      && builder.buildProfile.length > 0, 'builder profile is missing');
  }
  check('candidate deployment policy', manifest?.deploymentPolicy?.requireSidecarAndArchiveChipMatch === true
    && manifest?.deploymentPolicy?.requireInstalledBinaryHashEvidence === true,
  'required identity policy is not enabled');

  return {
    manifest,
    source,
    packages,
    file: manifestFile,
    sha256: manifestSha256,
    sizeBytes: manifestStat.size,
  };
}

function evidenceLineContains(lines, sha256, fileName) {
  const expectedHash = String(sha256).toLowerCase();
  const expectedName = String(fileName).toLowerCase();
  return lines.some((line) => {
    const normalized = line.trim().replaceAll('\\', '/').toLowerCase();
    return normalized.startsWith(expectedHash)
      && (normalized.endsWith('/' + expectedName) || normalized.endsWith(' ' + expectedName));
  });
}

async function verifyPostinstallManifest(root, candidate) {
  const postinstallFile = path.join(root, 'identities', 'postinstall.private.json');
  const postinstallStat = requireFile(postinstallFile, 'postinstall identity');
  const postinstallSha256 = await sha256File(postinstallFile, 'postinstall identity');
  const postinstall = readJson(postinstallFile, 'postinstall identity');
  check('postinstall identity', postinstall?.schemaVersion === '1.0',
    'schemaVersion must be 1.0');
  check('postinstall identity', postinstall?.visibility === 'private',
    'visibility must be private');
  check('postinstall identity', postinstall?.status === 'verified-postinstall-exact-candidate',
    'status must verify the exact candidate');
  timestampMs(postinstall.generatedAt, 'postinstall identity', 'generatedAt is invalid');
  check('postinstall identity', postinstall?.captureTimeBasis
    === 'evidence-file-last-write-time-utc', 'capture time basis is invalid');
  const source = verifySourceIdentity(
    postinstall.candidateSource,
    'postinstall candidate source',
    candidate.source,
  );
  const manifestEvidence = source.manifestEvidence;
  check('postinstall candidate source', manifestEvidence?.path
    === 'candidates/package-manifest.private.json', 'package manifest reference differs');
  check('postinstall candidate source', manifestEvidence?.sha256 === candidate.sha256,
    'package manifest SHA-256 differs');
  check('postinstall candidate source', Date.parse(manifestEvidence?.generatedAt)
    === Date.parse(candidate.manifest.generatedAt), 'package manifest timestamp differs');

  exactObjectKeys(postinstall?.platforms, PLATFORM_IDS, 'postinstall platform inventory',
    'platform inventory must be exactly three supported platforms');
  const platforms = {};
  const seenDeviceLabels = new Set();
  for (const platformId of PLATFORM_IDS) {
    const label = 'postinstall ' + platformId;
    const snapshot = postinstall.platforms[platformId];
    check(label, snapshot && typeof snapshot === 'object', 'platform snapshot is missing');
    check(label, typeof snapshot.deviceLabel === 'string' && snapshot.deviceLabel.length > 0,
      'device label is missing');
    check(label, !seenDeviceLabels.has(snapshot.deviceLabel), 'device label is duplicated');
    seenDeviceLabels.add(snapshot.deviceLabel);
    const capturedAtMs = timestampMs(snapshot.capturedAt, label, 'capturedAt is invalid');

    const candidatePackage = candidate.packages[platformId];
    const packageRecord = snapshot.package;
    check(label, packageRecord && typeof packageRecord === 'object', 'package snapshot is missing');
    check(label, packageRecord.manifestSha256MatchesLocalArtifact === true,
      'package-to-manifest match is false');
    check(label, Number(packageRecord.sizeBytes) === Number(candidatePackage.sizeBytes),
      'package size differs from candidate manifest');
    check(label, String(packageRecord.sha256 ?? '').toLowerCase() === candidatePackage.sha256,
      'package SHA-256 differs from candidate manifest');
    const packageFile = resolvePrivatePath(root, packageRecord.artifactPath, label + ' package');
    const packageStat = requireFile(packageFile, label + ' package');
    check(label, packageStat.size === Number(packageRecord.sizeBytes), 'installed package size mismatch');
    check(label, await sha256File(packageFile, label + ' package') === candidatePackage.sha256,
      'installed package SHA-256 mismatch');
    check(label, sameRealFile(packageFile, candidatePackage.file),
      'postinstall package is not the candidate package artifact');

    const installed = snapshot.installed;
    check(label, installed && typeof installed === 'object', 'installed identity is missing');
    check(label, installed.version === candidate.source.declaredVersion,
      'installed version differs from candidate');
    const engine = installed.engine;
    check(label, engine?.name === 'cosmo-engine' && validSha256(engine?.sha256),
      'installed engine identity is invalid');
    check(label, typeof engine?.candidateArchiveMember === 'string'
      && engine.candidateArchiveMember.endsWith('/bin/cosmo-engine')
      && engine?.matchesCandidatePackage === true, 'engine candidate-package binding is invalid');

    check(label, Array.isArray(installed.runtime), 'runtime inventory is missing');
    const runtimeNames = installed.runtime.map((item) => item?.name).sort();
    check(label, sameJson(runtimeNames, [...EXPECTED_RUNTIME_INVENTORY[platformId]].sort()),
      'runtime inventory differs from the platform contract');
    for (const runtime of installed.runtime) {
      check(label, validSha256(runtime?.sha256), 'runtime SHA-256 is invalid');
      check(label, typeof runtime?.candidateArchiveMember === 'string'
        && runtime.candidateArchiveMember.endsWith('/lib/' + runtime.name)
        && runtime?.matchesCandidatePackage === true, 'runtime candidate-package binding is invalid');
    }

    const expectedVlm = EXPECTED_VLM_INVENTORY[platformId];
    exactObjectKeys(installed.vlm, Object.keys(expectedVlm), label + ' VLM inventory',
      'VLM model inventory differs from the platform contract');
    for (const [key, expectedName] of Object.entries(expectedVlm)) {
      const item = installed.vlm[key];
      check(label, item?.name === expectedName && validSha256(item?.sha256),
        'VLM ' + key + ' identity is invalid');
    }
    if (platformId === 'rk3576') {
      check(label, installed.vlm.model.freshlyRecomputedInPostinstallEvidence === true,
        'RK3576 model SHA-256 was not freshly recomputed');
    }

    check(label, Array.isArray(snapshot.evidence) && snapshot.evidence.length === 3,
      'postinstall evidence inventory is incomplete');
    const evidenceFiles = [];
    const seenEvidence = new Set();
    let newestEvidenceAt = null;
    for (const record of snapshot.evidence) {
      const file = await verifyReferencedFile(root, record, label + ' evidence');
      const real = fs.realpathSync(file);
      check(label, !seenEvidence.has(real), 'postinstall evidence artifact is duplicated');
      seenEvidence.add(real);
      evidenceFiles.push(file);
      if (record.capturedAt != null) {
        const evidenceAt = timestampMs(record.capturedAt, label, 'evidence capturedAt is invalid');
        check(label, evidenceAt <= capturedAtMs, 'evidence timestamp is later than platform capture');
        newestEvidenceAt = Math.max(newestEvidenceAt ?? evidenceAt, evidenceAt);
      }
    }
    check(label, newestEvidenceAt === capturedAtMs,
      'platform capturedAt is not bound to the newest raw evidence');
    const rawLines = evidenceFiles
      .filter((file) => file.toLowerCase().endsWith('.tsv'))
      .flatMap((file) => fs.readFileSync(file, 'utf8').split(/\r?\n/));
    const componentRecords = [engine, ...installed.runtime, ...Object.values(installed.vlm)];
    for (const component of componentRecords) {
      check(label, evidenceLineContains(rawLines, component.sha256, component.name),
        'installed component identity is not present in raw postinstall evidence');
    }
    check(label, snapshot.evidence.some((record) => record.sha256.toLowerCase()
      === engine.sha256.toLowerCase()), 'candidate engine artifact is not bound to postinstall evidence');

    platforms[platformId] = {
      snapshot,
      capturedAtMs,
      package: candidatePackage,
      installed,
    };
  }

  const crossChecks = postinstall.crossChecks;
  for (const key of [
    'candidateSourceCommitAndTreePresent',
    'allLocalPackageSha256MatchManifest',
    'allInstalledEngineSha256MatchCandidateArchive',
    'allInstalledRuntimeSha256MatchCandidateArchive',
    'allRequiredArtifactsPresent',
    'allRequiredHashesConsistent',
  ]) {
    check('postinstall cross-checks', crossChecks?.[key] === true, key + ' must be true');
  }
  check('postinstall cross-checks', crossChecks.bm1688AndCv186xEngineSha256
    === platforms.bm1688.installed.engine.sha256
    && crossChecks.bm1688AndCv186xEngineSha256
      === platforms.cv186x.installed.engine.sha256,
  'Sophon engine cross-check differs');
  check('postinstall cross-checks', crossChecks.rk3576EngineSha256
    === platforms.rk3576.installed.engine.sha256, 'RK3576 engine cross-check differs');
  check('postinstall cross-checks', crossChecks.rk3576ModelSha256FreshlyRecomputed
    === platforms.rk3576.installed.vlm.model.sha256, 'RK3576 model cross-check differs');
  check('postinstall privacy', postinstall?.privacy?.containsCredentials === false
    && postinstall?.privacy?.containsNetworkEndpoints === false
    && postinstall?.privacy?.containsDeviceSerials === false
    && postinstall?.privacy?.containsStreamAddresses === false,
  'private identity document contains prohibited data');
  check('postinstall privacy', sameJson(
    [...(postinstall?.privacy?.deviceReferencesLimitedToLabels ?? [])].sort(),
    [...seenDeviceLabels].sort(),
  ), 'device label inventory differs from platform snapshots');

  return {
    manifest: postinstall,
    source,
    platforms,
    file: postinstallFile,
    sha256: postinstallSha256,
    sizeBytes: postinstallStat.size,
  };
}

async function verifyCandidateIdentity(root) {
  const candidate = await verifyPackageManifest(root);
  const postinstall = await verifyPostinstallManifest(root, candidate);
  return {
    status: 'pending-finalization',
    source: candidate.source,
    candidate,
    postinstall,
  };
}

async function verifyProtocol(root) {
  const manifestFile = path.join(root, 'protocol', 'protocol-manifest.private.json');
  const manifestStat = requireFile(manifestFile, 'protocol manifest');
  const manifestSha256 = await sha256File(manifestFile, 'protocol manifest');
  const manifest = readJson(manifestFile, 'protocol manifest');
  check('protocol manifest', manifest?.visibility === 'private', 'visibility must be private');
  const workload = manifest?.workload;
  check('protocol workload', workload && typeof workload === 'object', 'workload is missing');
  check('protocol workload', sameJson(workload.channelSequence, EXPECTED_CHANNELS),
    'channel sequence must be 1..8');
  check('protocol workload', Number(workload.holdSecPerLevel) === EXPECTED.holdSec,
    'hold window must be 60 seconds');
  check('protocol workload', Number(workload.sampleIntervalSec) === EXPECTED.sampleIntervalSec,
    'sample interval must be 3 seconds');
  check('protocol workload', workload.channelMode === EXPECTED.videoMode, 'channel mode must be local');
  check('protocol workload', equalNumber(workload.repeatCount, EXPECTED.repeatCount),
    'repeatCount must be 0');
  check('protocol workload', equalNumber(workload.targetFps, EXPECTED.targetFps), 'target FPS must be 0.1');
  check('protocol workload', equalNumber(workload?.thresholds?.vlm?.minFpsRatio, EXPECTED.minFpsRatio),
    'VLM minFpsRatio must be 0.8');
  check('protocol workload', equalNumber(workload?.thresholds?.vlm?.maxMissingRate, EXPECTED.maxMissingRate),
    'VLM maxMissingRate must be 0');
  check('protocol workload', equalNumber(workload?.thresholds?.vlm?.avgDiscardRate, EXPECTED.avgDiscardRate),
    'VLM avgDiscardRate must be 0.05');
  check('protocol workload', equalNumber(workload?.thresholds?.pass?.maxPacketDiscardRate,
    EXPECTED.maxPacketDiscardRate), 'maxPacketDiscardRate must be 0.01');
  check('protocol workload', equalNumber(workload?.thresholds?.pass?.maxDiskUsedPercent,
    EXPECTED.maxDiskUsedPercent), 'maxDiskUsedPercent must be 99');
  check('protocol prompt', typeof workload?.prompt?.text === 'string' && workload.prompt.text.length > 0,
    'prompt is missing');

  const video = await verifyReferencedFile(root, workload.video, 'protocol video');
  const entries = Array.isArray(manifest.platforms) ? manifest.platforms : [];
  const ids = entries.map(platformIdFromManifestEntry);
  check('protocol manifest', ids.every(Boolean), 'unknown platform entry');
  exactPlatformInventory(ids, 'protocol manifest');

  const byPlatform = new Map();
  for (const entry of entries) {
    const platformId = platformIdFromManifestEntry(entry);
    check('protocol ' + platformId, !byPlatform.has(platformId), 'duplicate platform entry');
    const scenarioFile = await verifyReferencedFile(root, entry.scenario, 'protocol ' + platformId + ' scenario');
    const templateFile = await verifyReferencedFile(root, entry.template, 'protocol ' + platformId + ' template');
    let pkg;
    try {
      pkg = new ScenarioPackage(path.dirname(scenarioFile)).load();
    } catch {
      fail('protocol ' + platformId + ' scenario', 'scenario package is invalid');
    }
    validateChannelWindow(pkg.loadProfile, 'protocol ' + platformId + ' scenario');
    check('protocol ' + platformId + ' scenario', pkg.sampleIntervalSec === EXPECTED.sampleIntervalSec,
      'sample interval must be 3 seconds');
    check('protocol ' + platformId + ' scenario', pkg.videoMode === EXPECTED.videoMode,
      'video mode must be local');
    check('protocol ' + platformId + ' scenario', pkg.videoRepeatCount === EXPECTED.repeatCount,
      'repeatCount must be 0');
    check('protocol ' + platformId + ' scenario', equalNumber(pkg.targetFps, EXPECTED.targetFps),
      'target FPS must be 0.1');
    check('protocol ' + platformId + ' scenario',
      path.resolve(pkg.videos.local?.[0]?.file ?? '') === path.resolve(video),
      'scenario video differs from protocol video');
    const task = pkg.tasks[0];
    check('protocol ' + platformId + ' scenario', pkg.tasks.length === 1
      && normalizeTaskType(task?.taskType ?? task?.type) === 'vlm',
    'scenario must contain exactly one VLM task');
    check('protocol ' + platformId + ' scenario',
      ALLOWED_COMPLETION_ACTION_IDS.includes(String(task?.vlmCompletionActionId ?? '').toUpperCase()),
      'scenario must expose a task-local completion counter');
    const declaredAlgorithmId = String(entry?.template?.algorithmId ?? '');
    check('protocol ' + platformId, declaredAlgorithmId.length > 0,
      'template algorithm identity is missing');
    check('protocol ' + platformId + ' scenario', String(task.algorithmId) === declaredAlgorithmId,
      'scenario algorithm identity differs from protocol');
    const rules = resolveTaskThresholds(pkg.thresholds, task);
    check('protocol ' + platformId + ' scenario', equalNumber(rules.minFpsRatio, EXPECTED.minFpsRatio),
      'effective minFpsRatio must be 0.8');
    check('protocol ' + platformId + ' scenario', equalNumber(rules.maxMissingRate, EXPECTED.maxMissingRate),
      'effective maxMissingRate must be 0');
    check('protocol ' + platformId + ' scenario', equalNumber(rules.avgDiscardRate, EXPECTED.avgDiscardRate),
      'effective avgDiscardRate must be 0.05');

    const template = readJson(templateFile, 'protocol ' + platformId + ' template');
    check('protocol ' + platformId + ' template',
      String(template.algorithmId ?? template.algorithmCode ?? '') === declaredAlgorithmId,
      'template algorithm identity differs from protocol');
    const identity = templateVlmIdentity(template, 'protocol ' + platformId + ' template');
    check('protocol ' + platformId + ' template', identity.prompt === workload.prompt.text,
      'prompt differs from protocol');
    check('protocol ' + platformId + ' template',
      String(identity.advancedMode) === String(workload.prompt.advancedMode),
      'advanced mode differs from protocol');
    check('protocol ' + platformId + ' template',
      identity.generationStyle === workload.prompt.generationStyle,
      'generation style differs from protocol');
    check('protocol ' + platformId + ' template', identity.provider === workload.prompt.provider,
      'provider differs from protocol');
    byPlatform.set(platformId, { entry, pkg, identity });
  }
  return {
    manifest,
    workload,
    video,
    byPlatform,
    file: manifestFile,
    sha256: manifestSha256,
    sizeBytes: manifestStat.size,
  };
}

function verifyProtocolCandidateIdentity(protocol, identity) {
  const manifest = protocol.manifest;
  const label = 'protocol candidate identity';
  check(label, manifest?.tool?.commit === identity.source.commit
    && manifest?.tool?.tree === identity.source.tree,
  'tool commit or tree differs from the frozen candidate');
  const source = manifest?.candidate?.sourceIdentity;
  check(label, source && typeof source === 'object', 'candidate source identity is missing');
  for (const key of ['repository', 'ref', 'commit', 'tree', 'declaredVersion']) {
    check(label, source[key] === identity.source[key], key + ' differs from the frozen candidate');
  }
  check(label, manifest?.candidate?.status === 'pending-final-rc'
    && manifest?.candidate?.finalPackage?.status === 'not-frozen'
    && manifest?.identityAssertions?.finalRcClaimed === false,
  'historical preparation state was not preserved');
}

async function verifyFinalization(root, identity, protocol) {
  const label = 'identity finalization';
  const finalizationFile = path.join(root, 'identities', 'finalization.private.json');
  const finalizationStat = requireFile(finalizationFile, label);
  const finalizationSha256 = await sha256File(finalizationFile, label);
  const finalization = readJson(finalizationFile, label);
  check(label, finalization?.schemaVersion === '1.0', 'schemaVersion must be 1.0');
  check(label, finalization?.visibility === 'private', 'visibility must be private');
  check(label, finalization?.status === 'final-rc-bound-and-executed',
    'status must be final-rc-bound-and-executed');
  const finalizedAtMs = timestampMs(finalization.finalizedAt, label, 'finalizedAt is invalid');
  check(label, finalizedAtMs >= Date.parse(protocol.manifest.generatedAt)
    && finalizedAtMs >= Date.parse(identity.candidate.manifest.generatedAt)
    && finalizedAtMs >= Date.parse(identity.postinstall.manifest.generatedAt),
  'finalization precedes a bound identity artifact');
  check(label, finalization?.source?.commit === identity.source.commit
    && finalization?.source?.tree === identity.source.tree,
  'finalized commit or tree differs from the candidate');

  const bindings = {
    preparationProtocol: {
      record: finalization.preparationProtocol,
      path: 'protocol/protocol-manifest.private.json',
      file: protocol.file,
      sha256: protocol.sha256,
      sizeBytes: protocol.sizeBytes,
      status: protocol.manifest.status,
    },
    packageManifest: {
      record: finalization.packageManifest,
      path: 'candidates/package-manifest.private.json',
      file: identity.candidate.file,
      sha256: identity.candidate.sha256,
      sizeBytes: identity.candidate.sizeBytes,
      status: identity.candidate.manifest.status,
    },
    postinstall: {
      record: finalization.postinstall,
      path: 'identities/postinstall.private.json',
      file: identity.postinstall.file,
      sha256: identity.postinstall.sha256,
      sizeBytes: identity.postinstall.sizeBytes,
      status: identity.postinstall.manifest.status,
    },
  };
  const seen = new Set();
  for (const [key, binding] of Object.entries(bindings)) {
    const record = binding.record;
    check(label, record?.path === binding.path && record?.status === binding.status,
      key + ' path or status differs');
    const file = await verifyReferencedFile(root, record, label + ' ' + key);
    check(label, sameRealFile(file, binding.file)
      && record.sha256.toLowerCase() === binding.sha256
      && Number(record.sizeBytes) === binding.sizeBytes,
    key + ' SHA-256 or size differs');
    const real = fs.realpathSync(file);
    check(label, !seen.has(real), 'bound identity artifact is duplicated');
    seen.add(real);
  }

  exactObjectKeys(finalization?.packages, PLATFORM_IDS, label,
    'finalized package inventory must be exactly three supported platforms');
  for (const platformId of PLATFORM_IDS) {
    check(label, finalization.packages[platformId]?.sha256
      === identity.candidate.packages[platformId].sha256
      && Number(finalization.packages[platformId]?.sizeBytes)
        === Number(identity.candidate.packages[platformId].sizeBytes),
    'finalized platform package identity differs');
  }
  return {
    manifest: finalization,
    file: finalizationFile,
    sha256: finalizationSha256,
    sizeBytes: finalizationStat.size,
    finalizedAtMs,
  };
}

export function summarizeAllBindingGate(stepSummary) {
  const stats = Array.isArray(stepSummary?.channelStats) ? stepSummary.channelStats : [];
  const ratios = stats.map((item) => item.minFpsRatio).filter(Number.isFinite);
  const missing = stats.map((item) => item.missingRate).filter(Number.isFinite);
  const discard = stats.map((item) => item.avgDiscardRate).filter(Number.isFinite);
  return {
    bindingCount: stats.length,
    minFpsRatio: ratios.length ? Math.min(...ratios) : null,
    maxMissingRate: missing.length ? Math.max(...missing) : null,
    avgDiscardRate: discard.length
      ? discard.reduce((sum, value) => sum + value, 0) / discard.length
      : null,
  };
}

function categoryForCheck(name) {
  const normalized = String(name ?? '').toLowerCase();
  if (normalized.includes('fps')) return 'fps-ratio';
  if (normalized.includes('missing')) return 'missing';
  if (normalized.includes('discard') && normalized.includes('packet')) return 'packet-discard';
  if (normalized.includes('discard')) return 'discard';
  if (normalized.includes('latency')) return 'latency';
  if (normalized.includes('disk')) return 'disk';
  return 'threshold';
}

function gateFailureCategory(stepSummary) {
  const categories = [...new Set((stepSummary?.perThreshold ?? [])
    .filter((item) => item?.result === 'FAIL')
    .map((item) => categoryForCheck(item.name)))].sort();
  return categories.length ? categories.join('+') : 'threshold';
}

function continuousPassingPrefix(stepSummaries) {
  const sorted = stepSummaries
    .filter((item) => !item.skipped && item.qualified !== false)
    .sort((left, right) => left.channels - right.channels);
  const prefix = [];
  for (let expected = 1; expected <= EXPECTED_CHANNELS.length; expected += 1) {
    const item = sorted.find((step) => step.channels === expected);
    if (!item || item.pass !== true) break;
    prefix.push(item);
  }
  return prefix;
}

function bindingIdentity(binding) {
  return [binding?.taskId ?? '', binding?.taskKey ?? '', binding?.channelId ?? '']
    .map((value) => String(value))
    .join('\u0000');
}

function holdSamplesForStep(metrics, step) {
  return (metrics.samples ?? []).filter((sample) =>
    Number(sample?.stepIndex) === Number(step?.sampleStepIndex ?? step?.index)
      && sample?.phase !== 'ramp');
}

function verifyStepWindows(platformId, metrics, stepSummaries, boundary) {
  const label = platformId + ' hold window';
  const expectedTicks = Math.floor(EXPECTED.holdSec / EXPECTED.sampleIntervalSec);
  const knownIndexes = new Set(metrics.steps.map((step) => Number(step.index)));
  let previousTimestamp = null;
  for (const sample of metrics.samples) {
    const timestamp = Number(sample?.ts);
    check(label, Number.isFinite(timestamp), 'sample timestamp is invalid');
    check(label, previousTimestamp == null || timestamp > previousTimestamp,
      'sample timestamps are not strictly increasing');
    previousTimestamp = timestamp;
    check(label, knownIndexes.has(Number(sample?.stepIndex)), 'sample refers to an unknown step');
  }

  const completeChannels = new Set();
  let incompleteStep = null;
  for (const summary of stepSummaries.filter((item) => !item.skipped && item.qualified !== false)) {
    const samples = holdSamplesForStep(metrics, summary.step);
    check(label, samples.length > 0, 'executed step has no hold samples');
    if (samples.length !== expectedTicks) {
      check(label, boundary.classification === 'execution-block'
        && incompleteStep == null
        && Number(summary.channels) === Number(boundary.firstFailureChannels)
        && samples.length < expectedTicks,
      'hold sample count differs from the 60-second/3-second protocol');
      incompleteStep = Number(summary.channels);
    } else {
      completeChannels.add(Number(summary.channels));
    }

    const expectedBindings = Number(summary.channels) * metrics.tasks.length;
    let expectedIdentitySet = null;
    for (const sample of samples) {
      check(label, sample.phase === 'hold', 'non-hold sample entered the hold window');
      check(label, Number(sample.activeChannels) === Number(summary.channels)
        && Number(sample.targetChannels) === Number(summary.channels),
      'sample channel coverage differs from the step');
      if (sample.activeTaskBindings != null) {
        check(label, Number(sample.activeTaskBindings) === expectedBindings,
          'sample task-binding coverage differs from the step');
      }
      check(label, Array.isArray(sample.channels) && sample.channels.length === expectedBindings,
        'sample binding count differs from the step');
      const identities = sample.channels.map(bindingIdentity).sort();
      check(label, new Set(identities).size === identities.length,
        'sample contains duplicate task bindings');
      if (expectedIdentitySet == null) expectedIdentitySet = identities;
      else check(label, sameJson(identities, expectedIdentitySet),
        'sample binding inventory changed inside a hold window');
    }

    if (samples.length >= 2) {
      const timestamps = samples.map((sample) => Number(sample.ts));
      const gaps = timestamps.slice(1).map((timestamp, index) => timestamp - timestamps[index]);
      const minimumSpanMs = (samples.length - 1) * EXPECTED.sampleIntervalSec * 1000 * 0.8;
      const observedSpanMs = timestamps.at(-1) - timestamps[0];
      check(label, observedSpanMs >= minimumSpanMs
        && observedSpanMs <= EXPECTED.holdSec * 1000 * 1.5,
        'hold timestamps do not cover the configured sampling window');
      check(label, gaps.every((gap) => gap <= EXPECTED.sampleIntervalSec * 1000 * 3),
        'hold sample gap exceeds the 3-second cadence tolerance');
    }
  }

  if (incompleteStep != null) {
    const laterSamples = stepSummaries
      .filter((item) => Number(item.channels) > incompleteStep)
      .some((item) => !item.skipped);
    check(label, !laterSamples, 'sampling continued after an incomplete blocked step');
  }
  return completeChannels;
}

export function deriveCapacityBoundary(metrics, stepSummaries) {
  const executed = stepSummaries
    .filter((item) => !item.skipped && item.qualified !== false)
    .sort((left, right) => left.channels - right.channels);
  const prefix = continuousPassingPrefix(stepSummaries);
  const passedChannels = prefix.at(-1)?.channels ?? 0;
  const firstGateFailure = executed.find((item) => item.pass === false) ?? null;
  const runtimeBottleneckChannels = Number(metrics?.bottleneck?.channels);
  const bottleneckMatchesGate = firstGateFailure != null
    && Number.isFinite(runtimeBottleneckChannels)
    && runtimeBottleneckChannels === firstGateFailure.channels;
  if (firstGateFailure != null && metrics?.status !== 'aborted') {
    check('capacity boundary', passedChannels + 1 === firstGateFailure.channels,
      'first gate failure is not adjacent to the continuous passing prefix');
    check('capacity boundary', executed.at(-1) === firstGateFailure,
      'formal execution continued after the first gate failure');
    check('capacity boundary', metrics?.bottleneck && typeof metrics.bottleneck === 'object',
      'raw runtime bottleneck marker is missing');
    check('capacity boundary', bottleneckMatchesGate,
      'raw runtime bottleneck marker differs from the first gate failure');
    check('capacity boundary', Number(metrics.bottleneck.targetChannels)
      === firstGateFailure.channels, 'runtime stop target differs from the first gate failure');
    check('capacity boundary', Number(metrics.bottleneck.stepIndex)
      === Number(firstGateFailure.step?.sourceIndex ?? firstGateFailure.step?.index),
    'runtime stop step differs from the first gate failure');
    check('capacity boundary', Number(metrics.bottleneck.stepNumber)
      === Number(firstGateFailure.step?.sourceIndex ?? firstGateFailure.step?.index) + 1,
    'runtime stop step number differs from the first gate failure');
    check('capacity boundary', metrics.bottleneck.phase === 'hold',
      'runtime stop marker was not emitted by the formal hold gate');
    check('capacity boundary', typeof metrics.bottleneck.reason === 'string'
      && metrics.bottleneck.reason.trim().length > 0, 'runtime stop reason is missing');
  }
  const executionBlocked = metrics?.status === 'aborted'
    || (metrics?.bottleneck != null && !bottleneckMatchesGate)
    || (!firstGateFailure && executed.length !== EXPECTED_CHANNELS.length);

  if (executionBlocked) {
    const firstSkipped = EXPECTED_CHANNELS.find((channels) =>
      !executed.some((step) => step.channels === channels));
    const blockedAt = Number(metrics?.error?.atChannels)
      || (Number.isFinite(runtimeBottleneckChannels) ? runtimeBottleneckChannels : null)
      || firstSkipped
      || null;
    return {
      classification: 'execution-block',
      verifiedPassingChannels: passedChannels,
      firstFailureChannels: blockedAt,
      firstFailureCategory: 'execution-block',
      capacityExact: false,
      lowerBound: null,
    };
  }
  if (firstGateFailure) {
    return {
      classification: 'gate-first-failure',
      verifiedPassingChannels: passedChannels,
      firstFailureChannels: firstGateFailure.channels,
      firstFailureCategory: gateFailureCategory(firstGateFailure),
      capacityExact: passedChannels > 0,
      lowerBound: null,
    };
  }
  check('capacity boundary', executed.length === EXPECTED_CHANNELS.length
    && executed.every((item) => item.pass === true),
  'completed evidence has an ambiguous boundary');
  return {
    classification: 'all-pass-lower-bound',
    verifiedPassingChannels: EXPECTED_CHANNELS.at(-1),
    firstFailureChannels: null,
    firstFailureCategory: 'none',
    capacityExact: false,
    lowerBound: EXPECTED_CHANNELS.at(-1),
  };
}

function runtimeGateIdentity(check) {
  return {
    scope: check.taskKey === '*' ? (check.strategy ?? 'system') : 'task',
    taskKey: check.taskKey,
    taskType: check.taskType,
    name: check.name,
    actual: check.actual,
    threshold: check.threshold,
  };
}

function runtimeGateReason(check) {
  const taskKey = check.taskKey;
  const actual = Number(check.actual);
  const threshold = Number(check.threshold);
  const actualFixed = Number.isFinite(actual) ? actual : check.actual;
  const thresholdFixed = Number.isFinite(threshold) ? threshold : check.threshold;
  switch (check.name) {
    case 'minFpsRatio':
      return `${taskKey} fpsRatio ${actual.toFixed(3)} < ${thresholdFixed}`;
    case 'minThroughputFps':
      return `${taskKey} fps ${actual.toFixed(2)} < ${thresholdFixed}`;
    case 'maxMissingRate':
      return `${taskKey} missingRate ${actual.toFixed(3)} > ${thresholdFixed}`;
    case 'avgDiscardRate':
    case 'maxDiscardRate':
      return `${taskKey} meanDiscard ${actual.toFixed(3)} > ${thresholdFixed}`;
    case 'maxDiskUsedPercent':
      return `disk ${actualFixed}% > ${thresholdFixed}%`;
    case 'maxPacketDiscardRate':
      return `packetDiscard ${actual.toFixed(3)} > ${thresholdFixed}`;
    default:
      return `${taskKey} ${check.name} ${actualFixed} > ${thresholdFixed}`;
  }
}

function verifyRuntimeGateRecord(actual, expected, label) {
  check(label, actual && typeof actual === 'object', 'runtime gate identity is missing');
  for (const key of ['scope', 'taskKey', 'taskType', 'name']) {
    check(label, actual[key] === expected[key], 'runtime gate ' + key + ' differs');
  }
  check(label, equalNullableNumber(actual.actual, expected.actual, 1e-9),
    'runtime gate actual value differs');
  check(label, equalNullableNumber(actual.threshold, expected.threshold, 1e-9),
    'runtime gate threshold differs');
}

function controlledLegacyRuntimeMarker(metrics, protocolManifest) {
  const bottleneck = metrics?.bottleneck;
  return metrics?.sampleIntervalSec == null
    && protocolManifest?.protocolId === 'cosmoedge-v1.1-vlm-controlled-1to8-20260824'
    && protocolManifest?.tool?.workingTreePatchStatus
      === 'frozen-uncommitted-validation-tool-patch'
    && validSha256(protocolManifest?.tool?.workingTreePatchSha256)
    && bottleneck?.source == null
    && bottleneck?.gates == null
    && sameJson(Object.keys(bottleneck ?? {}).sort(), [
      'channels',
      'phase',
      'reason',
      'stepIndex',
      'stepNumber',
      'targetChannels',
    ].sort());
}

function verifyRuntimeBoundaryEvidence(
  platformId,
  metrics,
  stepSummaries,
  boundary,
  protocolManifest,
) {
  if (boundary.classification !== 'gate-first-failure') return;
  const label = platformId + ' runtime gate';
  const failure = stepSummaries.find((item) => Number(item.channels)
    === Number(boundary.firstFailureChannels));
  check(label, failure?.pass === false, 'first failure is not present in raw gate evaluation');
  const failedChecks = (failure.perThreshold ?? []).filter((item) => item?.result === 'FAIL');
  check(label, failedChecks.length > 0, 'first failure has no executed threshold failure');
  check(label, failedChecks.every((item) => typeof item?.taskKey === 'string'
    && item.taskKey.length > 0
    && typeof item?.taskType === 'string'
    && item.taskType.length > 0
    && typeof item?.name === 'string'
    && item.name.length > 0
    && Number.isFinite(Number(item.actual))
    && Number.isFinite(Number(item.threshold))),
  'recomputed threshold gate identity is incomplete');
  const expectedGates = failedChecks.map(runtimeGateIdentity);
  const expectedReason = failedChecks.map(runtimeGateReason).join('; ');
  const bottleneck = metrics.bottleneck;
  const legacy = controlledLegacyRuntimeMarker(metrics, protocolManifest);
  if (legacy) {
    check(label, bottleneck.reason === expectedReason,
      'legacy runtime stop reason differs from the recomputed threshold gates');
  } else {
    check(label, bottleneck.source === 'runtime-threshold',
      'runtime stop source must be runtime-threshold');
    check(label, Array.isArray(bottleneck.gates)
      && bottleneck.gates.length === expectedGates.length,
    'runtime gate inventory differs from the recomputed first failure');
    for (let index = 0; index < expectedGates.length; index += 1) {
      verifyRuntimeGateRecord(bottleneck.gates[index], expectedGates[index], label);
    }
    check(label, bottleneck.reason === expectedReason,
      'runtime stop reason differs from the recomputed threshold gates');
  }

  const failureStepIndex = Number(failure.step?.sampleStepIndex
    ?? failure.step?.sourceIndex ?? failure.step?.index);
  check(label, !(metrics.samples ?? []).some((sample) =>
    Number(sample?.stepIndex) > failureStepIndex),
  'raw sampling continued after the runtime gate failure');
  check(label, !(metrics.steps ?? []).some((step) =>
    Number(step?.index) > failureStepIndex && step?.vlmReadiness != null),
  'a later staircase step executed readiness after the runtime gate failure');
  check(label, !stepSummaries.some((step) => Number(step.channels) > boundary.firstFailureChannels
    && !step.skipped), 'a later staircase step executed after the runtime gate failure');
}

function verifyReadiness(
  platformId,
  metrics,
  steps,
  stepSummaries,
  vlmTaskCount,
  expectedCompletionActionId,
) {
  const executed = stepSummaries
    .filter((item) => !item.skipped && item.qualified !== false)
    .sort((left, right) => left.channels - right.channels);
  let previousChannels = 0;
  let previousHoldIdentities = new Set();
  for (const summary of executed) {
    const sourceIndex = summary.step.sourceIndex ?? summary.step.index;
    const step = steps.find((item) => item.index === sourceIndex);
    const readiness = step?.vlmReadiness;
    const label = platformId + ' readiness';
    check(label, readiness && typeof readiness === 'object', 'executed step lacks structured readiness');
    check(label, readiness.ready === true && readiness.status === 'ready', 'executed step is not ready');
    check(label, Number(readiness.stepIndex) === Number(step.index), 'readiness step index differs');
    check(label, Number(readiness.targetChannels) === Number(step.channels), 'readiness channel target differs');
    check(label, Array.isArray(readiness.bindings), 'readiness bindings are missing');
    const expectedNewBindings = (Number(step.channels) - previousChannels) * vlmTaskCount;
    check(label, readiness.bindings.length === expectedNewBindings,
      'readiness binding count differs from newly added routes');
    check(label, Number.isInteger(Number(readiness.probes)) && Number(readiness.probes) > 0,
      'readiness probe count is invalid');
    check(label, Number(readiness.timeoutSec) > 0
      && equalNumber(readiness.pollIntervalSec, EXPECTED.sampleIntervalSec),
    'readiness timing contract is invalid');
    const readinessStartedAt = Date.parse(readiness.startedAt);
    const readinessEndedAt = Date.parse(readiness.endedAt);
    check(label, Number.isFinite(readinessStartedAt)
      && Number.isFinite(readinessEndedAt)
      && readinessEndedAt >= readinessStartedAt,
    'readiness timestamps are invalid');
    check(label, readiness.elapsedMs != null
      && Number.isFinite(Number(readiness.elapsedMs))
      && Math.abs(Number(readiness.elapsedMs) - (readinessEndedAt - readinessStartedAt)) <= 1000,
    'readiness elapsed time differs from timestamps');
    const firstHoldSample = holdSamplesForStep(metrics, step)[0];
    check(label, firstHoldSample != null && readinessEndedAt <= Number(firstHoldSample.ts),
      'readiness did not finish before hold sampling');
    check(label, Array.isArray(firstHoldSample?.channels),
      'first hold sample binding inventory is missing');
    const firstHoldByIdentity = new Map(firstHoldSample.channels
      .map((binding) => [bindingIdentity(binding), binding]));
    check(label, firstHoldByIdentity.size === firstHoldSample.channels.length,
      'first hold sample contains duplicate task bindings');
    check(label, firstHoldByIdentity.size === Number(step.channels) * vlmTaskCount,
      'first hold sample binding inventory differs from the step');
    check(label, [...previousHoldIdentities].every((identity) => firstHoldByIdentity.has(identity)),
      'a previously ready binding disappeared from the next hold step');
    const actualNewIdentities = [...firstHoldByIdentity.keys()]
      .filter((identity) => !previousHoldIdentities.has(identity))
      .sort();
    check(label, actualNewIdentities.length === expectedNewBindings,
      'actual hold inventory does not contain the expected newly added routes');
    check(label, Array.isArray(step.currentVlmBindings)
      && step.currentVlmBindings.length === expectedNewBindings,
    'new VLM binding inventory is missing');
    for (const binding of step.currentVlmBindings) {
      check(label, String(binding?.completionActionId ?? '').toUpperCase()
        === expectedCompletionActionId,
      'declared new VLM binding completion action differs from the protocol task');
    }
    const declaredNewIdentities = step.currentVlmBindings.map(bindingIdentity).sort();
    const readinessIdentities = readiness.bindings.map(bindingIdentity).sort();
    check(label, sameJson(declaredNewIdentities, actualNewIdentities),
      'declared new VLM bindings differ from the actual hold inventory');
    check(label, sameJson(readinessIdentities, actualNewIdentities),
      'readiness bindings differ from the actual newly added hold routes');
    for (const binding of readiness.bindings) {
      const actionId = String(binding?.completionActionId ?? '').toUpperCase();
      check(label, ALLOWED_COMPLETION_ACTION_IDS.includes(actionId),
        'completion action is not task-local');
      check(label, actionId === expectedCompletionActionId,
        'completion action differs from the protocol task');
      check(label, binding.completionAdvanced === true, 'completion counter did not advance');
      check(label, binding.baselineTotal != null
        && binding.currentTotal != null
        && Number.isFinite(Number(binding.baselineTotal))
        && Number.isFinite(Number(binding.currentTotal))
        && Number(binding.currentTotal) > Number(binding.baselineTotal),
      'completion counter evidence is invalid');
      check(label, binding.qwenLatencyMs != null
        && Number.isFinite(Number(binding.qwenLatencyMs))
        && Number(binding.qwenLatencyMs) > 0,
      'direct Qwen latency is missing');
      check(label, binding.ready === true, 'binding readiness is false');
      check(label, Array.isArray(binding.pendingReasons) && binding.pendingReasons.length === 0,
        'binding has pending reasons');
      const holdBinding = firstHoldByIdentity.get(bindingIdentity(binding));
      check(label, holdBinding != null, 'ready binding is absent from the first hold sample');
      const holdTelemetryUnavailable = holdBinding?.missing === true
        || holdBinding?.telemetryMissing === true;
      const missingGateFailed = summary.pass === false
        && (summary.perThreshold ?? []).some((item) =>
          item?.name === 'maxMissingRate' && item?.result === 'FAIL');
      const unavailableExplainedByGate = holdTelemetryUnavailable && missingGateFailed;
      check(label, String(holdBinding?.expectedCompletionActionId
        ?? holdBinding?.vlmCompletionActionId ?? '').toUpperCase() === actionId,
      'first hold sample expected completion action differs from readiness');
      check(label, unavailableExplainedByGate
        || String(holdBinding?.completionActionId ?? '').toUpperCase() === actionId,
      'first hold sample observed completion action differs from readiness');
      check(label, unavailableExplainedByGate
        || (Number.isFinite(Number(holdBinding?.primaryProcessTotal))
          && Number(holdBinding.primaryProcessTotal) >= Number(binding.currentTotal)
          && Number(holdBinding.primaryProcessTotal) > Number(binding.baselineTotal)),
      'first hold sample does not continue the task-local readiness counter');
    }
    previousHoldIdentities = new Set(firstHoldByIdentity.keys());
    previousChannels = Number(step.channels);
  }
}

function verifyRawCompletionTelemetry(platformId, metrics, expectedCompletionActionId) {
  for (const sample of metrics.samples ?? []) {
    for (const channel of sample.channels ?? []) {
      check(platformId + ' raw metrics', normalizeTaskType(channel?.taskType) === 'vlm',
        'non-VLM task found in capacity metrics');
      check(platformId + ' raw metrics', equalNumber(channel?.targetFps, EXPECTED.targetFps),
        'binding target FPS must be 0.1');
      check(platformId + ' raw metrics',
        String(channel?.algorithmId ?? '') === String(metrics.tasks[0]?.algorithmId ?? ''),
        'binding algorithm identity differs from the capacity task');
      for (const value of [
        channel.completionActionId,
        channel.expectedCompletionActionId,
        channel.vlmCompletionActionId,
      ]) {
        if (value == null || value === '') continue;
        check(platformId + ' raw metrics',
          ALLOWED_COMPLETION_ACTION_IDS.includes(String(value).toUpperCase()),
          'completion action is not task-local');
      }
      const declaredCompletionActionId = channel.expectedCompletionActionId
        ?? channel.vlmCompletionActionId;
      check(platformId + ' raw metrics',
        String(declaredCompletionActionId ?? '').toUpperCase() === expectedCompletionActionId,
        'expected completion action differs from the protocol task');
      const telemetryUnavailable = channel.missing === true || channel.telemetryMissing === true;
      if (!telemetryUnavailable) {
        check(platformId + ' raw metrics',
          ALLOWED_COMPLETION_ACTION_IDS.includes(String(channel.completionActionId ?? '').toUpperCase()),
          'completion action is missing after readiness');
        check(platformId + ' raw metrics',
          String(channel.completionActionId ?? '').toUpperCase() === expectedCompletionActionId,
          'observed completion action differs from the protocol task');
      }
    }
  }
}

function verifyAllBindingAggregation(platformId, stepSummary, expectedBindings) {
  const label = platformId + ' all-binding aggregation';
  const aggregate = summarizeAllBindingGate(stepSummary);
  const missingGateFailed = (stepSummary.perThreshold ?? []).some((item) =>
    item?.name === 'maxMissingRate' && item?.result === 'FAIL');
  const unavailableExplainedByMissingFailure = stepSummary.pass === false && missingGateFailed;
  check(label, aggregate.bindingCount === expectedBindings, 'binding coverage differs');
  check(label, unavailableExplainedByMissingFailure
    || stepSummary.channelStats.every((item) => Number.isFinite(item.minFpsRatio)),
    'binding FPS ratio is missing');
  check(label, stepSummary.channelStats.every((item) => Number.isFinite(item.missingRate)),
    'binding missing rate is unavailable');
  check(label, unavailableExplainedByMissingFailure
    || stepSummary.channelStats.every((item) => Number.isFinite(item.avgDiscardRate)),
    'binding discard rate is unavailable');
  check(label, unavailableExplainedByMissingFailure
    || stepSummary.channelStats.every((item) => Number.isFinite(item.avgPrimaryLatencyMs)),
    'binding Qwen latency is unavailable');
  check(label, stepSummary.taskStats.length === 1, 'expected one VLM task aggregate');
  const task = stepSummary.taskStats[0];
  check(label, equalNullableNumber(task.minFpsRatio, aggregate.minFpsRatio, 0.0005),
    'task minimum FPS ratio is not the all-binding minimum');
  check(label, equalNullableNumber(task.maxMissingRate, aggregate.maxMissingRate, 0.00005),
    'task missing rate is not the all-binding maximum');
  check(label, equalNullableNumber(task.avgDiscardRate, aggregate.avgDiscardRate, 0.00005),
    'task discard rate is not the all-binding average');
  return aggregate;
}

function compareSummaryBoundary(platformId, summary, metrics, stepSummaries, boundary) {
  const label = platformId + ' summary';
  const executed = stepSummaries.filter((item) => !item.skipped && item.qualified !== false);
  const firstFailed = executed.find((item) => item.pass === false) ?? null;
  check(label, summary?.scenarioName === metrics.scenarioName
    && String(summary?.algorithmId ?? '') === String(metrics.algorithmId ?? metrics.tasks[0]?.algorithmId ?? '')
    && equalNumber(summary?.targetFps, metrics.targetFps)
    && summary?.videoMode === metrics.videoMode
    && summary?.status === metrics.status,
  'run identity differs from raw metrics');
  check(label, Number(summary?.sampleCount) === (metrics.samples ?? []).length,
    'sample count differs from raw metrics');
  check(label, summary?.startedAt === metrics.startedAt && summary?.endedAt === metrics.endedAt,
    'run timestamps differ from raw metrics');
  check(label, sameJson(
    summary?.vlmReadiness,
    (metrics.steps ?? []).filter((step) => step.vlmReadiness).map((step) => step.vlmReadiness),
  ), 'readiness inventory differs from raw metrics');
  check(label, summary?.allRanStepsPass === (executed.length > 0
    && executed.every((item) => item.pass === true)), 'all-step result differs from raw metrics');
  check(label, summary?.hasBottleneck === Boolean(metrics.bottleneck ?? firstFailed),
    'bottleneck presence differs from raw metrics');
  check(label, summary?.capacityBound == null, 'continuous capacity scan cannot have an interval bound');
  check(label, typeof summary?.conclusion === 'string' && summary.conclusion.length > 0,
    'conclusion is missing');
  if (boundary.classification === 'gate-first-failure') {
    check(platformId + ' boundary', summary.capacityExecutionBlocked === false,
      'gate failure was mislabeled as an execution block');
    check(platformId + ' boundary', summary.overallPass === false
      && summary.capacityMeasured === true
      && summary.capacityExclusionReason == null
      && Number(summary.maxStableChannels) === boundary.verifiedPassingChannels
      && summary.maxStableChannelsExact === true,
    'gate failure capacity semantics differ from raw evidence');
    check(platformId + ' boundary',
      Number(summary.firstFailedStep?.channels) === boundary.firstFailureChannels,
      'first gate failure channel differs');
    check(platformId + ' boundary', Number(summary.firstFailedStep?.stepIndex)
      === Number(firstFailed?.step?.sourceIndex ?? firstFailed?.step?.index)
      && sameJson(summary.firstFailedStep?.reasons, firstFailed?.reasons),
    'first gate failure detail differs from raw evidence');
    check(platformId + ' boundary',
      Number(summary.maxVerifiedPassedChannels ?? 0) === boundary.verifiedPassingChannels,
      'verified passing prefix differs');
    check(platformId + ' boundary', sameJson(summary.bottleneck, metrics.bottleneck),
      'runtime bottleneck differs from raw evidence');
  } else if (boundary.classification === 'all-pass-lower-bound') {
    check(platformId + ' boundary', summary.capacityExecutionBlocked === false,
      'all-pass result was mislabeled as an execution block');
    check(platformId + ' boundary', summary.overallPass === true
      && summary.capacityMeasured === true
      && summary.capacityExclusionReason == null
      && summary.maxStableChannelsExact === false
      && summary.maxStableChannels === boundary.lowerBound,
    'all-pass result must be a lower bound, not an exact capacity');
    check(platformId + ' boundary', Number(summary.maxVerifiedPassedChannels)
      === boundary.verifiedPassingChannels, 'verified passing prefix differs');
    check(platformId + ' boundary', summary.firstFailedStep == null,
      'all-pass lower bound cannot have a first failure');
    check(platformId + ' boundary', summary.bottleneck == null,
      'all-pass lower bound cannot have a bottleneck');
  } else {
    check(platformId + ' boundary', summary.capacityExecutionBlocked === true,
      'execution block was mislabeled as a capacity failure');
    check(platformId + ' boundary', summary.overallPass === false
      && summary.capacityMeasured === false
      && summary.maxStableChannels == null
      && summary.maxStableChannelsExact === false,
    'execution block must not produce a capacity value');
    check(platformId + ' boundary', Number(summary.maxVerifiedPassedChannels ?? 0)
      === boundary.verifiedPassingChannels, 'verified passing prefix differs');
  }
}

function htmlEscape(value) {
  return String(value ?? '').replace(/[&<>\"]/g, (character) => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
  })[character]);
}

function verifyFrozenReport(platformId, report, metrics, summary, boundary) {
  const label = platformId + ' report';
  check(label, report.includes(htmlEscape(metrics.scenarioName)),
    'scenario identity differs from raw metrics');
  check(label, report.includes(htmlEscape(summary.conclusion)),
    'summary conclusion differs from the frozen summary');
  check(label, report.includes(String(EXPECTED.targetFps)),
    'target FPS is missing');
  check(label, report.includes(String(EXPECTED.holdSec) + 's'),
    'hold-window identity is missing');
  if (boundary.classification === 'gate-first-failure') {
    check(label, report.includes(htmlEscape(metrics.bottleneck.reason)),
      'runtime gate reason differs from raw metrics');
    check(label, report.includes(String(boundary.verifiedPassingChannels))
      && report.includes(String(boundary.firstFailureChannels)),
    'capacity boundary differs from raw metrics');
  }
}

async function calculatePlatformHashes(root, platformId) {
  const base = path.join(root, 'platforms', platformId, 'capacity');
  const files = {};
  for (const name of ['metrics.json', 'summary.json', 'report.html']) {
    const file = path.join(base, name);
    const stat = requireFile(file, platformId + ' ' + name.replace(/\..+$/, ''));
    files[name] = {
      file,
      sizeBytes: stat.size,
      sha256: await sha256File(file, platformId + ' ' + name.replace(/\..+$/, '')),
    };
  }
  return files;
}

async function verifyPlatform(root, platformId, protocolEntry, protocolManifest) {
  const hashes = await calculatePlatformHashes(root, platformId);
  const metrics = readJson(hashes['metrics.json'].file, platformId + ' metrics');
  const summary = readJson(hashes['summary.json'].file, platformId + ' summary');
  check(platformId + ' report', hashes['report.html'].sizeBytes > 0, 'report is empty');
  const reportHead = fs.readFileSync(hashes['report.html'].file, 'utf8').slice(0, 512).toLowerCase();
  check(platformId + ' report', reportHead.includes('<!doctype') || reportHead.includes('<html'),
    'report is not HTML');

  check(platformId + ' metrics', Array.isArray(metrics.tasks) && metrics.tasks.length === 1,
    'expected exactly one task');
  if (metrics.platform != null) {
    check(platformId + ' metrics', platformIdFromManifestEntry({ platform: metrics.platform })
      === platformId, 'platform identity differs from evidence directory');
  }
  check(platformId + ' metrics',
    metrics.tasks.every((task) => normalizeTaskType(task?.taskType ?? task?.type) === 'vlm'),
    'capacity task must be VLM');
  check(platformId + ' metrics', equalNumber(metrics.targetFps, EXPECTED.targetFps),
    'target FPS must be 0.1');
  check(platformId + ' metrics',
    metrics.tasks.every((task) => equalNumber(task.targetFps, EXPECTED.targetFps)),
    'task target FPS must be 0.1');
  const expectedCompletionActionId = String(
    protocolEntry?.pkg?.tasks?.[0]?.vlmCompletionActionId ?? '',
  ).toUpperCase();
  check(platformId + ' metrics',
    ALLOWED_COMPLETION_ACTION_IDS.includes(expectedCompletionActionId),
    'protocol completion action is invalid');
  check(platformId + ' metrics',
    String(metrics.tasks[0]?.vlmCompletionActionId ?? '').toUpperCase()
      === expectedCompletionActionId,
    'task completion action differs from protocol');
  check(platformId + ' metrics', metrics.videoMode === EXPECTED.videoMode, 'video mode must be local');
  check(platformId + ' metrics', ['completed', 'aborted'].includes(metrics.status),
    'run status is invalid');
  check(platformId + ' metrics', Array.isArray(metrics.samples), 'samples are missing');
  check(platformId + ' metrics', Array.isArray(metrics.steps), 'steps are missing');
  validateChannelWindow(metrics.configuredLoadProfile ?? metrics.loadProfile, platformId + ' metrics');
  validateChannelWindow(metrics.steps, platformId + ' metrics');

  for (const task of metrics.tasks) {
    const rules = resolveTaskThresholds(metrics.thresholds ?? {}, task);
    check(platformId + ' metrics', equalNumber(rules.minFpsRatio, EXPECTED.minFpsRatio),
      'effective minFpsRatio must be 0.8');
    check(platformId + ' metrics', equalNumber(rules.maxMissingRate, EXPECTED.maxMissingRate),
      'effective maxMissingRate must be 0');
    check(platformId + ' metrics', equalNumber(rules.avgDiscardRate, EXPECTED.avgDiscardRate),
      'effective avgDiscardRate must be 0.05');
  }
  check(platformId + ' metrics',
    equalNumber(metrics?.thresholds?.pass?.maxPacketDiscardRate, EXPECTED.maxPacketDiscardRate),
    'maxPacketDiscardRate must be 0.01');
  check(platformId + ' metrics',
    equalNumber(metrics?.thresholds?.pass?.maxDiskUsedPercent, EXPECTED.maxDiskUsedPercent),
    'maxDiskUsedPercent must be 99');

  verifyRawCompletionTelemetry(platformId, metrics, expectedCompletionActionId);
  const writer = new ReportWriter('.');
  const stepSummaries = writer._summarizeSteps(metrics);
  const executed = stepSummaries.filter((item) => !item.skipped && item.qualified !== false);
  check(platformId + ' metrics', executed.length > 0, 'no executed capacity step');
  const executedChannels = executed.map((item) => item.channels);
  check(platformId + ' metrics',
    sameJson(executedChannels, EXPECTED_CHANNELS.slice(0, executed.length)),
    'executed steps must form a continuous prefix');
  for (const step of executed) {
    verifyAllBindingAggregation(platformId, step, step.channels * metrics.tasks.length);
  }
  verifyReadiness(
    platformId,
    metrics,
    metrics.steps,
    stepSummaries,
    metrics.tasks.length,
    expectedCompletionActionId,
  );

  let boundary = deriveCapacityBoundary(metrics, stepSummaries);
  verifyRuntimeBoundaryEvidence(
    platformId,
    metrics,
    stepSummaries,
    boundary,
    protocolManifest,
  );
  const completeChannels = verifyStepWindows(platformId, metrics, stepSummaries, boundary);
  if (boundary.classification === 'execution-block') {
    let verifiedPassingChannels = 0;
    for (const channels of EXPECTED_CHANNELS) {
      const summary = stepSummaries.find((item) => Number(item.channels) === channels);
      if (!summary || summary.pass !== true || !completeChannels.has(channels)) break;
      verifiedPassingChannels = channels;
    }
    boundary = { ...boundary, verifiedPassingChannels };
  }
  compareSummaryBoundary(platformId, summary, metrics, stepSummaries, boundary);
  verifyFrozenReport(
    platformId,
    fs.readFileSync(hashes['report.html'].file, 'utf8'),
    metrics,
    summary,
    boundary,
  );
  check(platformId + ' summary', equalNumber(summary.targetFps, EXPECTED.targetFps),
    'summary target FPS differs');
  check(platformId + ' summary', summary.videoMode === EXPECTED.videoMode,
    'summary video mode differs');

  const protocolAlgorithm = String(protocolEntry?.entry?.template?.algorithmId ?? '');
  if (protocolAlgorithm) {
    check(platformId + ' metrics',
      String(metrics.tasks[0]?.algorithmId ?? metrics.algorithmId ?? '') === protocolAlgorithm,
      'algorithm identity differs from protocol');
  }

  return {
    platformId,
    metrics,
    summary,
    stepSummaries,
    boundary,
    hashes,
  };
}

function verifyCapacityCandidateChronology(identity, results) {
  for (const result of results) {
    const label = result.platformId + ' capacity identity';
    const startedAt = timestampMs(result.metrics.startedAt, label, 'run start is invalid');
    const endedAt = timestampMs(result.metrics.endedAt, label, 'run end is invalid');
    check(label, endedAt >= startedAt, 'run end precedes run start');
    check(label, identity.postinstall.platforms[result.platformId].capturedAtMs <= startedAt,
      'postinstall candidate identity was captured after the capacity run started');
    check(label, endedAt <= identity.finalization.finalizedAtMs,
      'capacity run ended after identity finalization');
  }
}

function integrityRecord(integrity, platformId, fileName) {
  const relative = ['platforms', platformId, 'capacity', fileName].join('/');
  const candidates = [
    integrity?.files?.[relative],
    integrity?.platforms?.[platformId]?.capacity?.[fileName],
    integrity?.platforms?.[platformId]?.capacity?.[fileName.replace(/\..+$/, '')],
    integrity?.platforms?.[platformId]?.[fileName],
    (integrity?.entries ?? []).find((item) => item?.path === relative),
  ];
  const record = candidates.find((item) => item != null);
  if (typeof record === 'string') return { sha256: record };
  return record;
}

function verifyIntegrityDocument(integrity, results) {
  for (const result of results) {
    for (const fileName of ['metrics.json', 'summary.json', 'report.html']) {
      const label = result.platformId + ' integrity';
      const record = integrityRecord(integrity, result.platformId, fileName);
      check(label, record && typeof record === 'object', fileName + ' hash is missing');
      check(label, /^[0-9a-f]{64}$/i.test(record.sha256 ?? ''), fileName + ' SHA-256 is invalid');
      check(label,
        record.sha256.toLowerCase() === result.hashes[fileName].sha256,
        fileName + ' SHA-256 mismatch');
      if (record.sizeBytes != null) {
        check(label, Number(record.sizeBytes) === result.hashes[fileName].sizeBytes,
          fileName + ' size mismatch');
      }
    }
  }
}

function checkFileInsideDirectory(file, directory, label) {
  const relative = path.relative(path.resolve(directory), path.resolve(file));
  check(label, relative !== '..'
    && !relative.startsWith('..' + path.sep)
    && !path.isAbsolute(relative),
  'file reference is outside the platform E2E directory');
}

function verifySanitizedRestartLog(file, label) {
  let lines;
  try {
    lines = fs.readFileSync(file, 'utf8').trim().split(/\r?\n/);
  } catch {
    fail(label, 'restart log could not be read');
  }
  const structured = [
    'schemaVersion=1',
    'mode=RestartAndVerify',
    'wrapperExitCode=0',
    'serviceActiveMarkerObserved=true',
    'failureClass=NONE',
    'sshCredentialTransport=restricted-askpass',
    'sudoCredentialTransport=stdin',
    'rawRemoteOutputStored=false',
    'COSMO_SERVICE_ACTIVE',
  ];
  const legacySanitized = [
    'Opening task-scoped SSH. Entered credentials are handled by OpenSSH and are not written to run records.',
    'COSMO_SERVICE_ACTIVE',
  ];
  check(label, sameJson(lines, structured) || sameJson(lines, legacySanitized),
    'restart log is not in an approved sanitized form');
}

async function verifyE2EPlatform(
  root,
  platformId,
  entry,
  seenPaths,
  identity,
  protocol,
  implementation,
) {
  const label = 'E2E ' + platformId;
  exactObjectKeys(entry, ['result', 'restartLog'], label,
    'platform entry must contain exactly result and restartLog');
  const resultFile = await verifyReferencedFile(root, entry.result, label + ' result');
  const restartFile = await verifyReferencedFile(root, entry.restartLog, label + ' restart log');
  const platformDirectory = path.join(root, 'platforms', platformId, 'e2e');
  checkFileInsideDirectory(resultFile, platformDirectory, label + ' result');
  checkFileInsideDirectory(restartFile, platformDirectory, label + ' restart log');
  check(label + ' result', resultFile.toLowerCase().endsWith('.private.json'),
    'result must be a private JSON file');
  check(label + ' restart log', restartFile.toLowerCase().endsWith('.private.log'),
    'restart evidence must be a private log file');
  for (const file of [resultFile, restartFile]) {
    const real = fs.realpathSync(file);
    check(label, !seenPaths.has(real), 'artifact reference is duplicated');
    seenPaths.add(real);
  }
  check(label + ' restart log', entry.restartLog.sanitized === true,
    'restart log must be marked sanitized');
  verifySanitizedRestartLog(restartFile, label + ' restart log');

  const evidence = readJson(resultFile, label + ' result');
  check(label + ' result', evidence?.evidenceKind === 'cosmoedge-v1.1-vlm-minimal-e2e',
    'unexpected evidence kind');
  check(label + ' result', evidence?.privateEvidence === true, 'evidence must be private');
  check(label + ' result', evidence?.platform === platformId, 'platform identity differs');
  check(label + ' result', evidence?.deviceLabel
    === identity.postinstall.platforms[platformId].snapshot.deviceLabel,
  'device label differs from the postinstall platform snapshot');
  check(label + ' result', evidence?.status === 'PASS' && evidence?.errorCode == null,
    'status must be PASS without an error');
  const startedAt = timestampMs(evidence?.startedAtUtc, label + ' result', 'startedAtUtc is invalid');
  const finishedAt = timestampMs(evidence?.finishedAtUtc, label + ' result', 'finishedAtUtc is invalid');
  check(label + ' result', finishedAt >= startedAt, 'finishedAtUtc precedes startedAtUtc');
  check(label + ' result', identity.postinstall.platforms[platformId].capturedAtMs <= startedAt,
    'postinstall candidate identity was captured after E2E started');
  check(label + ' result', finishedAt <= identity.finalization.finalizedAtMs,
    'E2E finished after identity finalization');
  const source = evidence?.source;
  check(label + ' source', source && typeof source === 'object', 'source identity is missing');
  check(label + ' source', source.commit === identity.source.commit
    && source.tree === identity.source.tree, 'source commit or tree differs from the candidate');
  check(label + ' source', source.helperSha256 === implementation.helper.sha256,
    'helper identity differs from the E2E manifest');
  check(label + ' source', source.privilegedServiceWrapperSha256
    === implementation.privilegedWrapper.sha256,
  'privileged wrapper identity differs from the E2E manifest');
  const protocolEntry = protocol.byPlatform.get(platformId).entry;
  check(label + ' source', source.scenarioSha256 === protocolEntry.scenario.sha256,
    'scenario identity differs from the capacity protocol');
  check(label + ' source', source.templateSha256 === protocolEntry.template.sha256,
    'template identity differs from the capacity protocol');
  check(label + ' source', source.videoSha256 === protocol.workload.video.sha256,
    'video identity differs from the capacity protocol');
  exactObjectKeys(source.importedToolSha256,
    ['cosmoClient', 'channelManager', 'scenarioPackage', 'taskStrategies'],
    label + ' source', 'imported tool inventory is incomplete');
  check(label + ' source', Object.values(source.importedToolSha256).every(validSha256),
    'imported tool SHA-256 is invalid');
  check(label + ' counter', evidence?.protocol?.completionCounterScope === 'task-local',
    'protocol counter scope must be task-local');

  const acceptance = evidence?.acceptance;
  exactObjectKeys(acceptance, E2E_ACCEPTANCE_KEYS, label + ' acceptance',
    'acceptance inventory must contain exactly eight required stages');
  check(label + ' acceptance', E2E_ACCEPTANCE_KEYS.every((key) => acceptance[key]?.status === 'PASS'),
    'all eight acceptance stages must be PASS');

  const before = acceptance.inferenceBeforeRestart;
  const after = acceptance.taskRecoveryAfterRestart;
  for (const [phase, value] of [['before', before], ['after', after]]) {
    check(label + ' ' + phase, value?.completionCounterScope === 'task-local',
      'counter scope must be task-local');
    check(label + ' ' + phase, Number.isFinite(Number(value?.counterDelta))
      && Number(value.counterDelta) > 0
      && value?.completionAdvanced === true,
    'task-local completion counter delta must be positive');
    check(label + ' ' + phase, value?.taskObserved === true
      && value?.qwenLatencyObserved === true
      && Number.isFinite(Number(value?.qwenLatencyMs))
      && Number(value.qwenLatencyMs) > 0,
    'valid task-local Qwen inference evidence is missing');
  }

  const event = acceptance.eventOrAlarmOutput;
  check(label + ' event', event?.observed === true
    && Number.isInteger(Number(event?.matchingEventCount))
    && Number(event.matchingEventCount) > 0
    && event?.inferencePayloadObserved === true,
  'event or alarm output was not observed');
  check(label + ' event', event?.inferenceTextPersisted === false
    && event?.eventRowsPersisted === false,
  'raw event or inference content was persisted');

  const restart = acceptance.serviceRestart;
  check(label + ' restart', restart?.wrapperExitCode === 0
    && restart?.serviceActiveMarkerObserved === true,
  'service restart active verification failed');
  check(label + ' restart', restart?.privateRemoteEvidenceSha256 === entry.restartLog.sha256,
    'restart log hash is not bound to the result');

  const cleanup = acceptance.cleanup;
  check(label + ' cleanup', cleanup?.taskDisabled === true
    && cleanup?.ownedChannelDeleted === true
    && cleanup?.httpRecoverySucceeded === true,
  'owned resource cleanup did not complete');
  const layout = acceptance.layoutIntegrity;
  check(label + ' layout', layout?.initialLayoutMatched === true
    && layout?.finalLayoutMatched === true
    && layout?.globalLayoutMutationApplied === false,
  'layout integrity verification failed');

  exactObjectKeys(evidence?.privacy, E2E_PRIVACY_KEYS, label + ' privacy',
    'privacy inventory is incomplete');
  check(label + ' privacy', E2E_PRIVACY_KEYS.every((key) => evidence.privacy[key] === false),
    'all privacy storage flags must be false');

  return {
    platformId,
    sourceEvidenceSha256: entry.result.sha256.toLowerCase(),
    sourceEvidenceSizeBytes: entry.result.sizeBytes,
    restartLogSha256: entry.restartLog.sha256.toLowerCase(),
    restartLogSizeBytes: entry.restartLog.sizeBytes,
    identityChainVerified: true,
  };
}

async function verifyE2EManifest(root, manifestFile, identity, protocol) {
  const manifestStat = requireFile(manifestFile, 'E2E integrity');
  const manifest = readJson(manifestFile, 'E2E integrity');
  check('E2E integrity', manifest?.schemaVersion === 1, 'schemaVersion must be 1');
  check('E2E integrity', manifest?.evidenceKind
    === 'cosmoedge-v1.1-vlm-three-platform-e2e-integrity', 'unexpected evidence kind');
  check('E2E integrity', manifest?.privateEvidence === true, 'manifest must be private');
  check('E2E integrity', manifest?.conclusion === 'all-platforms-pass',
    'conclusion must be all-platforms-pass');
  exactObjectKeys(manifest?.inventory, PLATFORM_IDS, 'E2E integrity inventory',
    'platform inventory must be exactly three supported platforms');

  const seenPaths = new Set();
  exactObjectKeys(manifest?.implementation, E2E_IMPLEMENTATION_KEYS, 'E2E implementation',
    'implementation inventory must contain helper, privilegedWrapper, and tests');
  const expectedImplementationPaths = {
    helper: 'invoke-vlm-e2e.mjs',
    privilegedWrapper: 'invoke-cosmo-service.ps1',
    tests: 'invoke-vlm-e2e.test.mjs',
  };
  for (const key of E2E_IMPLEMENTATION_KEYS) {
    check('E2E implementation ' + key,
      manifest.implementation[key]?.path === expectedImplementationPaths[key],
      'implementation path differs from the fixed private helper inventory');
    const file = await verifyReferencedFile(
      root,
      manifest.implementation[key],
      'E2E implementation ' + key,
    );
    const real = fs.realpathSync(file);
    check('E2E implementation', !seenPaths.has(real), 'artifact reference is duplicated');
    seenPaths.add(real);
  }

  exactObjectKeys(manifest?.identity, E2E_IDENTITY_KEYS, 'E2E identity chain',
    'identity inventory must contain packageManifest and postinstall');
  const expectedIdentity = {
    packageManifest: {
      path: 'candidates/package-manifest.private.json',
      file: identity.candidate.file,
      sha256: identity.candidate.sha256,
      sizeBytes: identity.candidate.sizeBytes,
    },
    postinstall: {
      path: 'identities/postinstall.private.json',
      file: identity.postinstall.file,
      sha256: identity.postinstall.sha256,
      sizeBytes: identity.postinstall.sizeBytes,
    },
    finalization: {
      path: 'identities/finalization.private.json',
      file: identity.finalization.file,
      sha256: identity.finalization.sha256,
      sizeBytes: identity.finalization.sizeBytes,
    },
  };
  for (const key of E2E_IDENTITY_KEYS) {
    const expected = expectedIdentity[key];
    const record = manifest.identity[key];
    check('E2E identity ' + key, record?.path === expected.path,
      'identity path differs from the fixed private inventory');
    const file = await verifyReferencedFile(root, record, 'E2E identity ' + key);
    check('E2E identity ' + key, sameRealFile(file, expected.file),
      'identity artifact differs from the verified source');
    check('E2E identity ' + key, record.sha256.toLowerCase() === expected.sha256
      && Number(record.sizeBytes) === expected.sizeBytes,
    'identity SHA-256 or size differs from the verified source');
    const real = fs.realpathSync(file);
    check('E2E identity chain', !seenPaths.has(real), 'artifact reference is duplicated');
    seenPaths.add(real);
  }

  const platforms = [];
  for (const platformId of PLATFORM_IDS) {
    platforms.push(await verifyE2EPlatform(
      root,
      platformId,
      manifest.inventory[platformId],
      seenPaths,
      identity,
      protocol,
      manifest.implementation,
    ));
  }

  return {
    status: 'verified',
    manifestSha256: await sha256File(manifestFile, 'E2E integrity'),
    manifestSizeBytes: manifestStat.size,
    conclusion: manifest.conclusion,
    platforms,
    identityChainVerified: true,
  };
}

function firstDefined(object, keys) {
  for (const key of keys) {
    if (object?.[key] !== undefined) return object[key];
  }
  return undefined;
}

function normalizedRepository(value) {
  return String(value ?? '').trim().replace(/\/+$/, '').replace(/\.git$/i, '').toLowerCase();
}

function verifyCanonicalEndToEnd(canonical, e2e, identity) {
  const accepted = canonical?.claim?.threePlatformEndToEndAccepted === true;
  if (accepted) {
    check('canonical E2E', e2e?.status === 'verified',
      'three-platform E2E claim requires a verified private E2E manifest');
  }
  if (e2e == null) return;

  check('canonical E2E', accepted, 'verified private E2E evidence requires an accepted canonical claim');
  const acceptance = canonical?.endToEndAcceptance;
  check('canonical E2E', acceptance && typeof acceptance === 'object',
    'endToEndAcceptance is missing');
  check('canonical E2E', identity?.status === 'verified'
    && e2e?.identityChainVerified === true
    && e2e.platforms.every((item) => item.identityChainVerified === true),
  'candidate package identity chain is not verified');
  check('canonical E2E', acceptance.candidatePackageSharedWithCapacityRun === true,
    'candidate package binding is missing');
  const platforms = Array.isArray(acceptance.platforms) ? acceptance.platforms : [];
  exactPlatformInventory(platforms.map((item) => item?.platformId), 'canonical E2E');
  const byPlatform = new Map(platforms.map((item) => [item.platformId, item]));
  for (const source of e2e.platforms) {
    const item = byPlatform.get(source.platformId);
    const label = 'canonical E2E ' + source.platformId;
    check(label, item?.status === 'PASS', 'platform status must be PASS');
    check(label, CANONICAL_E2E_STAGE_KEYS.every((key) => item?.[key] === 'PASS'),
      'all canonical E2E stages must be PASS');
    const evidenceSha256 = firstDefined(item, ['evidenceSha256', 'sourceEvidenceSha256']);
    const evidenceSizeBytes = firstDefined(item, ['evidenceSizeBytes', 'sourceEvidenceSizeBytes']);
    check(label, evidenceSha256 === source.sourceEvidenceSha256,
      'source evidence SHA-256 differs');
    check(label, Number(evidenceSizeBytes) === source.sourceEvidenceSizeBytes,
      'source evidence size differs');
  }
}

function verifyCanonical(canonical, results, protocol, e2e, identity) {
  const canonicalSource = canonical?.source;
  check('canonical source', canonicalSource && typeof canonicalSource === 'object',
    'source identity is missing');
  check('canonical source', normalizedRepository(canonicalSource.repository)
    === normalizedRepository(identity.source.repository)
    && canonicalSource.commit === identity.source.commit
    && canonicalSource.tree === identity.source.tree
    && canonicalSource.candidateVersion === identity.source.declaredVersion
    && canonicalSource.releaseStateAtFreeze === identity.source.releaseStateAtFreeze,
  'candidate source identity differs from private evidence');
  const observations = Array.isArray(canonical?.observations) ? canonical.observations : [];
  exactPlatformInventory(observations.map((item) => item?.platformId), 'canonical');
  const byPlatform = new Map(observations.map((item) => [item.platformId, item]));
  for (const result of results) {
    const observation = byPlatform.get(result.platformId);
    const label = 'canonical ' + result.platformId;
    check(label, observation.sourceMetricsSha256 === result.hashes['metrics.json'].sha256,
      'metrics hash differs');
    check(label, observation.sourceSummarySha256 === result.hashes['summary.json'].sha256,
      'summary hash differs');
    check(label, observation.sourceReportSha256 === result.hashes['report.html'].sha256,
      'report hash differs');
    const privatePlatform = identity.postinstall.platforms[result.platformId];
    const packageIdentity = observation?.package;
    check(label, packageIdentity?.version === identity.source.declaredVersion
      && packageIdentity?.sha256 === privatePlatform.package.sha256
      && Number(packageIdentity?.sizeBytes) === Number(privatePlatform.package.sizeBytes)
      && packageIdentity?.targetChip === result.platformId,
    'package identity differs from candidate and postinstall evidence');
    const runtimeIdentity = observation?.runtimeIdentity;
    check(label, runtimeIdentity?.engineSha256 === privatePlatform.installed.engine.sha256,
      'engine identity differs from postinstall evidence');
    check(label, Array.isArray(runtimeIdentity?.libraries),
      'runtime library identity is missing');
    const canonicalRuntime = runtimeIdentity.libraries
      .map((item) => ({ name: item?.name, sha256: item?.sha256 }))
      .sort((left, right) => String(left.name).localeCompare(String(right.name)));
    const privateRuntime = privatePlatform.installed.runtime
      .map((item) => ({ name: item.name, sha256: item.sha256 }))
      .sort((left, right) => left.name.localeCompare(right.name));
    check(label, sameJson(canonicalRuntime, privateRuntime),
      'runtime library inventory differs from postinstall evidence');
    const expectedModelKeys = Object.keys(EXPECTED_VLM_INVENTORY[result.platformId]);
    exactObjectKeys(observation?.modelIdentity, expectedModelKeys, label + ' model identity',
      'model identity inventory differs from postinstall evidence');
    for (const key of expectedModelKeys) {
      check(label, observation.modelIdentity[key]?.name
        === privatePlatform.installed.vlm[key].name
        && observation.modelIdentity[key]?.sha256
          === privatePlatform.installed.vlm[key].sha256,
      'model identity differs from postinstall evidence');
    }
    check(label, equalNumber(observation?.workload?.targetFpsPerChannel, EXPECTED.targetFps),
      'target FPS differs');
    check(label, equalNumber(observation?.gates?.minimumFpsRatio, EXPECTED.minFpsRatio),
      'minimum FPS ratio differs');
    check(label, equalNumber(observation?.gates?.maximumMissingRate, EXPECTED.maxMissingRate),
      'maximum missing rate differs');
    check(label, equalNumber(observation?.gates?.maximumAverageDiscardRate, EXPECTED.avgDiscardRate),
      'discard gate differs');

    const canonicalBoundary = observation.capacityBoundary
      ?? observation.observedBoundary
      ?? observation.boundary;
    check(label, canonicalBoundary && typeof canonicalBoundary === 'object',
      'capacity boundary is missing');
    const classification = firstDefined(canonicalBoundary,
      ['classification', 'boundaryClass', 'kind', 'type', 'claimClass']);
    const passing = firstDefined(canonicalBoundary,
      ['verifiedPassingChannels', 'highestPassingChannels', 'maxVerifiedPassedChannels']);
    const firstFailure = firstDefined(canonicalBoundary,
      ['firstFailureChannels', 'firstGateFailureChannels', 'firstFailedChannels']);
    const category = firstDefined(canonicalBoundary,
      ['firstFailureCategory', 'firstGateFailureCategory']);
    check(label, classification === result.boundary.classification,
      'boundary classification differs');
    check(label, Number(passing) === result.boundary.verifiedPassingChannels,
      'verified passing channels differ');
    if (result.boundary.firstFailureChannels == null) {
      check(label, firstFailure == null, 'unexpected first failure');
    } else {
      check(label, Number(firstFailure) === result.boundary.firstFailureChannels,
        'first failure channels differ');
    }
    check(label, String(category ?? 'none') === result.boundary.firstFailureCategory,
      'first failure category differs');
    check(label, canonicalBoundary.capacityExact === result.boundary.capacityExact,
      'capacity exactness differs');

    check(label, Array.isArray(observation.steps), 'canonical step evidence is missing');
    if (Array.isArray(observation.steps)) {
      const executedSteps = result.stepSummaries.filter((step) => !step.skipped);
      check(label, observation.steps.length === executedSteps.length, 'step count differs');
      for (const step of observation.steps) {
        const raw = result.stepSummaries.find((item) => item.channels === Number(step.channels));
        check(label, raw && !raw.skipped, 'canonical step was not executed');
        const aggregate = summarizeAllBindingGate(raw);
        check(label, Number(step.holdSeconds ?? step.holdSec) === EXPECTED.holdSec,
          'step hold window differs');
        check(label, equalNumber(step.targetFpsPerChannel ?? step.targetFps, EXPECTED.targetFps),
          'step target FPS differs');
        check(label, equalNumber(step.minimumActiveRouteFpsRatioObserved,
          aggregate.minFpsRatio, 0.0005), 'step minimum FPS ratio differs');
        check(label, equalNumber(step.telemetryMissingRate, aggregate.maxMissingRate, 0.00005),
          'step missing rate differs');
        check(label, equalNumber(step.averageDiscardRate, aggregate.avgDiscardRate, 0.00005),
          'step discard rate differs');
        check(label, step.result === (raw.pass ? 'PASS' : 'FAIL'),
          'step gate result differs from raw execution');
        const readiness = raw.step?.vlmReadiness;
        check(label, step?.readiness?.status === 'PASS'
          && Number(step.readiness.probes) === Number(readiness?.probes)
          && Number(step.readiness.elapsedMs) === Number(readiness?.elapsedMs)
          && step.readiness.taskLocalCompletionCounterAdvanced === true
          && step.readiness.qwenLatencyObserved === true,
        'step readiness differs from raw execution');
      }
    }
  }
  if (canonical?.input?.sha256 != null) {
    check('canonical', canonical.input.sha256 === protocol.workload.video.sha256,
      'video hash differs from protocol');
  }
  if (canonical?.prompt?.text != null) {
    check('canonical', canonical.prompt.text === protocol.workload.prompt.text,
      'prompt differs from protocol');
  }
  verifyCanonicalEndToEnd(canonical, e2e, identity);
}

export async function verifyPrivateEvidence({ evidenceRoot, canonicalPath } = {}) {
  check('arguments', typeof evidenceRoot === 'string' && evidenceRoot.length > 0,
    '--evidence-root is required');
  const root = path.resolve(evidenceRoot);
  let rootStat;
  try {
    rootStat = fs.statSync(root);
  } catch {
    fail('evidence root', 'missing directory');
  }
  check('evidence root', rootStat.isDirectory(), 'missing directory');

  let evidencePlatformIds;
  try {
    evidencePlatformIds = fs.readdirSync(path.join(root, 'platforms'), { withFileTypes: true })
      .filter((entry) => entry.isDirectory())
      .map((entry) => entry.name.toLowerCase());
  } catch {
    fail('evidence platforms', 'missing directory');
  }
  exactPlatformInventory(evidencePlatformIds, 'evidence platforms');

  const identity = await verifyCandidateIdentity(root);
  const protocol = await verifyProtocol(root);
  verifyProtocolCandidateIdentity(protocol, identity);
  identity.finalization = await verifyFinalization(root, identity, protocol);
  identity.status = 'verified';
  const results = [];
  for (const platformId of PLATFORM_IDS) {
    results.push(await verifyPlatform(
      root,
      platformId,
      protocol.byPlatform.get(platformId),
      protocol.manifest,
    ));
  }
  verifyCapacityCandidateChronology(identity, results);

  const integrityFile = path.join(root, 'evidence-integrity.private.json');
  let integrityStatus = 'not-provided';
  if (fs.existsSync(integrityFile)) {
    verifyIntegrityDocument(readJson(integrityFile, 'evidence integrity'), results);
    integrityStatus = 'verified';
  }

  const e2eFile = path.join(root, 'e2e-integrity.private.json');
  let e2e = null;
  let e2eStatus = 'not-provided';
  if (fs.existsSync(e2eFile)) {
    e2e = await verifyE2EManifest(root, e2eFile, identity, protocol);
    e2eStatus = 'verified';
  }

  let canonicalStatus = 'not-requested';
  if (canonicalPath != null) {
    verifyCanonical(
      readJson(path.resolve(canonicalPath), 'canonical'),
      results,
      protocol,
      e2e,
      identity,
    );
    canonicalStatus = 'verified';
  }
  return {
    identityStatus: identity.status,
    identity,
    protocol,
    results,
    integrityStatus,
    e2eStatus,
    e2e,
    canonicalStatus,
  };
}

function sanitizedLine(result) {
  return [
    result.platformId,
    'passedRoutes=' + result.boundary.verifiedPassingChannels,
    'boundary=' + result.boundary.classification,
    'firstFailure=' + result.boundary.firstFailureCategory,
  ].join(' ');
}

async function main(argv) {
  const options = parseArgs(argv);
  if (options.help) {
    console.log(usage());
    return;
  }
  const verified = await verifyPrivateEvidence({
    evidenceRoot: options.evidenceRoot,
    canonicalPath: options.canonical,
  });
  console.log('VLM private evidence verification PASS');
  for (const result of verified.results) console.log(sanitizedLine(result));
  console.log('identity=' + verified.identityStatus
    + ' integrity=' + verified.integrityStatus
    + ' e2e=' + verified.e2eStatus
    + ' canonical=' + verified.canonicalStatus);
}

const invoked = process.argv[1] != null
  && path.resolve(process.argv[1]).toLowerCase() === fileURLToPath(import.meta.url).toLowerCase();
if (invoked) {
  main(process.argv.slice(2)).catch((error) => {
    const detail = error instanceof VerificationError ? error.message : 'unexpected verification error';
    console.error('VLM private evidence verification FAIL: ' + detail);
    process.exitCode = 1;
  });
}
