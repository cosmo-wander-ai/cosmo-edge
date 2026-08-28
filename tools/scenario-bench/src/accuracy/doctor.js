import net from 'node:net';
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';

import { CosmoClient } from '../cosmo-client.js';
import { loadAccuracySuite } from './suite.js';
import { sanitizedDeviceIdentity } from './identity.js';
import { inspectLocalMedia } from './media-preflight.js';
import { selectAccuracyCases, selectedCaseIdentity } from './selection.js';

export async function runAccuracyDoctor({
  suitePath,
  dataRoot,
  targetChip,
  device = null,
  auth = null,
  lang = 'zh-CN',
  clientFactory = null,
  mediaProbe = inspectLocalMedia,
  selection = null,
  concurrency = 1,
} = {}) {
  const checkedAt = new Date().toISOString();
  const checks = [];
  const warnings = [];
  let suite;
  try {
    suite = loadAccuracySuite({ suitePath, dataRoot, selection });
    checks.push(ok('suite', `${suite.id} (${suite.cases.length} cases, ${suite.tasks.length} tasks)`));
  } catch (error) {
    checks.push(fail('suite', error.message));
    return doctorResult({ checks, warnings, suite: null, device: null, checkedAt, selection, concurrency });
  }
  let admittedCases;
  try {
    admittedCases = selection ? selectAccuracyCases(suite, selection) : suite.cases;
    if (selection) checks.push(ok('selection', `${admittedCases.length} case(s) selected`));
  } catch (error) {
    checks.push(fail('selection', error.message));
    return doctorResult({ checks, warnings, suite, media: null, device: null, checkedAt, selection, concurrency });
  }
  const admittedTaskIds = new Set(admittedCases.map((item) => item.task));
  const admittedTasks = suite.tasks.filter((task) => admittedTaskIds.has(task.id));
  if (![1, 2, 4].includes(Number(concurrency))) {
    checks.push(fail('concurrency', 'concurrency must be 1, 2, or 4'));
  } else {
    checks.push(ok('concurrency', String(concurrency)));
  }
  if (!targetChip || !suite.targetPlatforms.includes(targetChip)) {
    checks.push(fail('target-chip', `target chip must be one of ${suite.targetPlatforms.join(', ')}`));
  } else {
    checks.push(ok('target-chip', targetChip));
  }
  checks.push(ok('sample-hashes', `${admittedCases.length} selected input hash(es) verified`));
  checks.push(ok('task-configs', `${suite.tasks.filter((task) => task.taskConfigSha256).length} frozen config(s)`));
  let media = null;
  if (suite.sourceMode === 'local') {
    try {
      media = await mediaProbe(admittedCases);
      if (media.invalid.length) {
        const examples = media.invalid.slice(0, 12).map((item) => `${item.id}: ${item.reason}`);
        checks.push(fail(
          'media-preflight',
          `${media.invalid.length}/${media.checked} invalid sample(s): ${examples.join('; ')}`,
        ));
      } else {
        checks.push(ok('media-preflight', `${media.valid}/${media.checked} video sample(s) probeable`));
      }
      if (media.durationSec) {
        checks.push(ok(
          'media-duration',
          `min ${media.durationSec.min}s, median ${media.durationSec.median}s, p95 ${media.durationSec.p95}s, max ${media.durationSec.max}s`,
        ));
      }
      if (media.duplicateContent.extraCases > 0) {
        warnings.push(
          `${media.duplicateContent.extraCases} same-task case(s) duplicate existing video content`,
        );
      }
      const mediaById = new Map(media.cases.map((item) => [item.id, item]));
      suite.cases = suite.cases.map((item) => ({ ...item, media: mediaById.get(item.id) ?? null }));
    } catch (error) {
      checks.push(fail('media-preflight', `ffprobe unavailable or failed: ${error.code ?? error.message}`));
    }
  }
  if (suite.sourceMode === 'rtsp-deterministic') {
    await checkRtspPreflight(suite, checks, admittedCases);
  }

  if (!device) {
    warnings.push('device checks skipped');
    return doctorResult({
      checks, warnings, suite, media, device: null, admittedCases,
      checkedAt, selection, concurrency,
    });
  }
  if (!auth?.token && (!auth?.user || !auth?.password)) {
    checks.push(fail('device-auth', 'token or user/password-stdin is required'));
    return doctorResult({
      checks, warnings, suite, media, device: null, admittedCases,
      checkedAt, selection, concurrency,
    });
  }

  const client = clientFactory
    ? clientFactory({ base: device, ...auth, lang })
    : new CosmoClient({ base: device, ...auth, lang });
  let deviceIdentity = null;
  try {
    await client.login();
    checks.push(ok('device-login', 'authenticated'));
    const info = await client.queryDeviceInfo();
    deviceIdentity = sanitizedDeviceIdentity(info);
    checks.push(ok('device-info', `${deviceIdentity.model ?? 'unknown'} / ${deviceIdentity.softwareVersion ?? 'unknown'}`));
    const chipText = JSON.stringify(info).toLowerCase();
    const knownChips = ['bm1688', 'cv186x', 'rk3576', 'rv1126b'].filter((chip) => chipText.includes(chip));
    if (knownChips.length && !knownChips.includes(String(targetChip).toLowerCase())) {
      checks.push(fail('device-chip', `device reports ${knownChips.join(', ')}, expected ${targetChip}`));
    } else if (!knownChips.length) {
      warnings.push(`device chip could not be independently derived; requested ${targetChip}`);
    } else {
      checks.push(ok('device-chip', targetChip));
    }

    const algorithms = await collectPages((payload) => client.algorithmPage(payload));
    const algorithmIds = new Set(algorithms.map((item) => String(item.algorithmId ?? item.id)));
    const missingAlgorithms = admittedTasks.filter((task) => !algorithmIds.has(task.algorithmId));
    checks.push(missingAlgorithms.length
      ? fail('algorithms', `missing: ${missingAlgorithms.map((task) => task.id).join(', ')}`)
      : ok('algorithms', `${admittedTasks.length} required algorithm(s) available`));

    const schedules = await collectPages((payload) => client.schedulePage(payload));
    const scheduleIds = new Set(schedules.map((item) => String(item.id ?? item.scheduleId)));
    const missingSchedules = admittedTasks.filter((task) => !scheduleIds.has(task.scheduleId));
    checks.push(missingSchedules.length
      ? fail('schedules', `missing: ${missingSchedules.map((task) => task.id).join(', ')}`)
      : ok('schedules', `${new Set(admittedTasks.map((task) => task.scheduleId)).size} schedule(s) available`));

    if (suite.sourceMode === 'local') {
      const capabilities = await client.uploadCapabilities();
      const taskKind = new Map(suite.tasks.map((task) => [task.id, task.kind]));
      const sampleSizes = admittedCases.map((item) => ({
        size: fs.statSync(item.absoluteFile).size,
        kind: taskKind.get(item.task) ?? 'cv',
      }));
      const orderedSizes = sampleSizes.map((item) => item.size).sort((a, b) => b - a);
      const cvSizes = sampleSizes
        .filter((item) => item.kind !== 'vlm')
        .map((item) => item.size)
        .sort((a, b) => b - a);
      const vlmSizes = sampleSizes
        .filter((item) => item.kind === 'vlm')
        .map((item) => item.size);
      const largestSample = orderedSizes[0];
      const cvUploadBytes = cvSizes
        .slice(0, Math.min(Number(concurrency), cvSizes.length))
        .reduce((sum, size) => sum + BigInt(size), 0n);
      const vlmUploadBytes = BigInt(vlmSizes.length ? Math.max(...vlmSizes) : 0);
      const concurrentUploadBytes = cvUploadBytes > vlmUploadBytes ? cvUploadBytes : vlmUploadBytes;
      const available = nonNegativeBigInt(capabilities?.availableForNewUploadsBytes);
      const maxTotal = nonNegativeBigInt(capabilities?.maxTotalSize);
      if ((available != null && concurrentUploadBytes > available)
          || (maxTotal != null && maxTotal > 0n && BigInt(largestSample) > maxTotal)) {
        checks.push(fail(
          'upload-capabilities',
          `concurrency ${concurrency} requires ${concurrentUploadBytes} bytes of upload headroom`,
        ));
      } else {
        checks.push(ok(
          'upload-capabilities',
          `largest sample ${largestSample} bytes; concurrent headroom ${concurrentUploadBytes} bytes is admissible`,
        ));
      }
      const hardware = await client.queryHardwareResource();
      const disk = (hardware?.itemList ?? []).find((item) => item.key === 'eMMCUtilization');
      checks.push(ok('device-disk', disk?.available === 0
        ? 'disk utilization unavailable'
        : `observed usage ${Number(disk?.usedPercent ?? 0)}%`));
    }
    await client.eventPage({
      timeBegin: 0, timeEnd: 1, pageNum: 1, pageSize: 1,
      videoChannelName: `acc-doctor-${Date.now()}`, algorithmCodes: [], reportStatus: -1,
    });
    checks.push(ok('event-page', 'readable'));

    checks.push(...await queryAccuracyLeftoverChecks(client));
  } catch (error) {
    checks.push(fail('device-api', error.message));
  }
  return doctorResult({
    checks, warnings, suite, media, device: deviceIdentity, admittedCases,
    checkedAt, selection, concurrency,
  });
}

async function collectPages(fetchPage, pageSize = 500) {
  const rows = [];
  for (let pageNum = 1; ; pageNum += 1) {
    const response = await fetchPage({ pageNum, pageSize });
    const page = response?.rows ?? [];
    rows.push(...page);
    const total = Number(response?.total ?? rows.length);
    if (!page.length || page.length < pageSize || rows.length >= total) break;
  }
  return rows;
}

function ok(name, detail) { return { name, status: 'PASS', detail }; }
function fail(name, detail) { return { name, status: 'FAIL', detail }; }
function unverified(name, detail) { return { name, status: 'UNVERIFIED', detail }; }

function doctorResult({
  checks,
  warnings,
  suite,
  media = null,
  device = null,
  admittedCases = null,
  checkedAt,
  selection,
  concurrency,
}) {
  const normalizedChecks = checks.map((check) => ({
    ...check,
    blocking: !check.name.startsWith('accuracy-'),
  }));
  const advisoryWarnings = normalizedChecks
    .filter((check) => check.blocking === false && check.status !== 'PASS')
    .map((check) => `${check.name}: ${check.detail}`);
  return {
    status: normalizedChecks.every((check) =>
      check.status === 'PASS' || check.blocking === false) ? 'PASS' : 'FAIL',
    checkedAt,
    profile: selection?.profile ?? 'full',
    concurrency: Number(concurrency),
    selection: admittedCases ? selectedCaseIdentity(admittedCases) : null,
    checks: normalizedChecks,
    warnings: [...warnings, ...advisoryWarnings],
    suite,
    media,
    device,
    admittedCases,
  };
}

export function accuracyLeftoverChecks(cameras = [], tasks = []) {
  const cameraIds = new Map();
  const accuracyCameras = [];
  for (const camera of cameras) {
    const name = firstText(camera.channelName, camera.videoChannelName);
    const ids = values(camera.channelId, camera.videoChannelId, camera.id);
    for (const id of ids) cameraIds.set(id, name);
    if (name?.startsWith('acc-')) accuracyCameras.push(camera);
  }
  const accuracyCameraIds = new Set(accuracyCameras.flatMap((camera) =>
    values(camera.channelId, camera.videoChannelId, camera.id)));
  const accuracyTasks = [];
  const unknownTasks = [];
  for (const task of tasks) {
    const names = values(task.channelName, task.videoChannelName);
    const ids = values(task.channelId, task.videoChannelId);
    if (names.some((name) => name.startsWith('acc-'))
        || ids.some((id) => accuracyCameraIds.has(id))) {
      accuracyTasks.push(task);
      continue;
    }
    const explicitNonAccuracyName = names.some((name) => !name.startsWith('acc-'));
    const correlatedNonAccuracyId = ids.some((id) => cameraIds.has(id));
    if (!explicitNonAccuracyName && !correlatedNonAccuracyId) unknownTasks.push(task);
  }
  const channelCheck = accuracyCameras.length
    ? fail('accuracy-channel-leftovers', `${accuracyCameras.length} existing acc-* channel(s) found`)
    : ok('accuracy-channel-leftovers', 'none');
  const taskCheck = unknownTasks.length
    ? unverified(
        'accuracy-task-leftovers',
        `${unknownTasks.length} task row(s) lack usable channel ownership fields`,
      )
    : accuracyTasks.length
      ? fail('accuracy-task-leftovers', `${accuracyTasks.length} existing acc-* task(s) found`)
      : ok('accuracy-task-leftovers', 'none');
  const combined = taskCheck.status === 'UNVERIFIED'
    ? unverified('accuracy-leftovers', 'task ownership could not be verified')
    : channelCheck.status === 'FAIL' || taskCheck.status === 'FAIL'
      ? fail('accuracy-leftovers', 'existing acc-* device objects found')
      : ok('accuracy-leftovers', 'none');
  return [channelCheck, taskCheck, combined];
}

export async function queryAccuracyLeftoverChecks(client) {
  let cameras;
  try {
    cameras = await collectPages((payload) => client.cameraPage({
      channelName: '', channelStatus: -1, ...payload,
    }));
  } catch (error) {
    const detail = `camera inventory unreadable: ${error.message}`;
    return [
      unverified('accuracy-task-inventory', detail),
      unverified('accuracy-channel-leftovers', detail),
      unverified('accuracy-task-leftovers', detail),
      unverified('accuracy-leftovers', 'existing acc-* device objects could not be verified'),
    ];
  }
  try {
    const tasks = await collectPages((payload) => client.taskPage(payload));
    return [
      ok('accuracy-task-inventory', 'task-page'),
      ...accuracyLeftoverChecks(cameras, tasks),
    ];
  } catch (error) {
    if (isUnsupportedTaskPage(error)) {
      const fallback = taskInventoryFromCameras(cameras);
      if (fallback.complete) {
        return [
          ok('accuracy-task-inventory', 'camera-page nested taskList fallback'),
          ...accuracyLeftoverChecks(cameras, fallback.tasks),
        ];
      }
    }
    const [channelCheck] = accuracyLeftoverChecks(cameras, []);
    const detail = `task inventory unreadable: ${error.message}`;
    return [
      unverified('accuracy-task-inventory', detail),
      channelCheck,
      unverified('accuracy-task-leftovers', detail),
      unverified('accuracy-leftovers', 'existing acc-* device objects could not be verified'),
    ];
  }
}

function taskInventoryFromCameras(cameras) {
  if (cameras.some((camera) => !Array.isArray(camera.taskList))) {
    return { complete: false, tasks: [] };
  }
  return {
    complete: true,
    tasks: cameras.flatMap((camera) => camera.taskList.map((task) => ({
      ...task,
      channelId: camera.channelId ?? camera.videoChannelId ?? camera.id,
      videoChannelId: camera.videoChannelId ?? camera.channelId ?? camera.id,
      channelName: camera.channelName ?? camera.videoChannelName,
      videoChannelName: camera.videoChannelName ?? camera.channelName,
    }))),
  };
}

function isUnsupportedTaskPage(error) {
  return error?.routeUnsupported === true;
}

export function summarizeAccuracyAdmission(result) {
  if (!result?.selection) throw new Error('doctor result has no admitted selection');
  const media = result.media ?? {};
  return {
    status: result.status === 'PASS' ? 'PASS' : 'FAIL',
    checkedAt: result.checkedAt,
    profile: result.profile,
    concurrency: result.concurrency,
    selection: structuredClone(result.selection),
    media: {
      checked: Number(media.checked ?? result.selection.count),
      valid: Number(media.valid ?? (result.suite?.sourceMode === 'local' ? 0 : result.selection.count)),
      invalid: Array.isArray(media.invalid) ? media.invalid.length : Number(media.invalid ?? 0),
      ...(media.durationSec ? { durationSec: structuredClone(media.durationSec) } : {}),
    },
    checks: (result.checks ?? []).map((check) => ({
      name: check.name,
      status: check.status,
      blocking: check.blocking !== false,
      detail: String(check.detail ?? ''),
    })),
    warnings: (result.warnings ?? []).map(String),
  };
}

function values(...items) {
  return items
    .filter((item) => item !== null && item !== undefined && String(item).trim())
    .map((item) => String(item));
}

function firstText(...items) {
  return values(...items)[0] ?? null;
}

async function checkRtspPreflight(suite, checks, cases = suite.cases) {
  for (const [name, executable] of [['ffmpeg', suite.rtsp.ffmpeg], ['ffprobe', suite.rtsp.ffprobe]]) {
    try {
      execFileSync(executable, ['-version'], { stdio: 'ignore', timeout: 5_000 });
      checks.push(ok(name, 'available'));
    } catch (error) {
      checks.push(fail(name, `not executable (${error.code ?? 'error'})`));
    }
  }
  try {
    const first = cases[0].file.split('/').map((segment) => encodeURIComponent(segment)).join('/');
    const response = await fetch(`${suite.rtsp.httpBase}/${first}`, {
      method: 'GET',
      headers: { Range: 'bytes=0-0' },
      signal: AbortSignal.timeout(5_000),
    });
    if (!response.ok && response.status !== 206) throw new Error(`HTTP ${response.status}`);
    await response.body?.cancel?.();
    checks.push(ok('rtsp-http-source', 'first sample is readable'));
  } catch (error) {
    checks.push(fail('rtsp-http-source', `first sample is unavailable (${error.message})`));
  }
  try {
    await probeTcp(suite.rtsp.mediaMtxBase, 5_000);
    checks.push(ok('mediamtx', 'TCP endpoint is reachable'));
  } catch (error) {
    checks.push(fail('mediamtx', error.message));
  }
}

function probeTcp(resource, timeoutMs) {
  const url = new URL(resource);
  const port = Number(url.port || 8554);
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: url.hostname, port });
    const timer = setTimeout(() => {
      socket.destroy();
      reject(new Error('MediaMTX TCP endpoint timed out'));
    }, timeoutMs);
    socket.once('connect', () => {
      clearTimeout(timer);
      socket.destroy();
      resolve();
    });
    socket.once('error', (error) => {
      clearTimeout(timer);
      reject(new Error(`MediaMTX TCP endpoint is unreachable (${error.code ?? 'error'})`));
    });
  });
}

function nonNegativeBigInt(value) {
  if (value == null || value === '') return null;
  try {
    const parsed = BigInt(value);
    return parsed >= 0n ? parsed : null;
  } catch {
    return null;
  }
}
