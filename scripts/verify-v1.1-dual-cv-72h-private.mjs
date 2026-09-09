import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';

const EXPECTED = {
  window: {
    startedAt: '2026-08-20T17:44:30.341Z',
    endedAt: '2026-08-23T17:44:30.341Z',
    durationHours: 72,
    sampleIntervalSeconds: 60,
    expectedSamples: 4320,
  },
  source: {
    commit: '44209759f450e96cda265acfa8bc6d17a1138888',
    tree: '5cbdefeaefaf642407356c22c271ccc7d57935b0',
  },
  videoSha256: '3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92',
  hashes: {
    runManifest: 'faaa6fd15c03843d44068b6da9ae7aee30f40f79442d694b7fe9c0a65de6afdb',
    suiteState: '9becf4cb17064ba515646d309b082136f9882f2d85d0449d651317116c21ccd7',
    suiteSummary: 'd28144149aeee9d693e7537b578c736d65daaecdf8d8c81be2047d7767167fb8',
    projectionTool: '7305f6811b53f70eb0e2fd52ce14c194da0345205df306fc63e39ca783bf4448',
  },
  platforms: {
    bm1688: {
      channels: 8,
      bindings: 16,
      samples: 4316,
      fps: { minimum: 4.68, average: 5.086, maximum: 5.49 },
      resources: { cpu: 30, memory: 44, disk: 96 },
      disk: { first: 96, last: 96, minimum: 96, maximum: 96, changes: 0 },
      window: { firstSampleLagSeconds: 111.988, finalSampleLagSeconds: 59.868, maximumGapSeconds: 60.058 },
      hashes: {
        metrics: 'bb72a12f3dda0972df5d11f6ad04647fe9c35ba345019ae36a31c7c1cb030fea',
        summary: '67f2f30e9cfdfaab05e1d7476eab177686875317a1dbc0f1db52b376de81bbb7',
        report: 'bca4122862edfc872a25008089942e3f1b901b41c778547e1834f733564fea7f',
        restartGuard: '7f31ef8cc504eb9636a0f6a10d42f32166638c455dd8fe2897438730e4e96995',
        cleanup: '6e273e4c0c3feda89c085ff0c62593d024e12ca9eef2527df5f2382ae3562b22',
      },
    },
    cv186x: {
      channels: 8,
      bindings: 16,
      samples: 4316,
      fps: { minimum: 4.54, average: 5.085, maximum: 5.29 },
      resources: { cpu: 43, memory: 44, disk: 96 },
      disk: { first: 96, last: 96, minimum: 96, maximum: 96, changes: 0 },
      window: { firstSampleLagSeconds: 111.993, finalSampleLagSeconds: 59.868, maximumGapSeconds: 60.061 },
      hashes: {
        metrics: '940e2ad4333f9fff7d3f60a2009981231d677e76872494d60b9b01bf68ded989',
        summary: 'bf14f42fa981f9bb023ca69d7e4881cbcaf04ea85e478e153ed26204182db8f2',
        report: '95b387f880957a99e3b1fcf846995de41bca3f4e1c542ceae5a5a28fa1fc537c',
        restartGuard: '669928a6f158b8b45a074eedf277275ebe0ce16f467f95d16ab0d261c3565694',
        cleanup: '785af78ee73edace6bd85ea4f6b0d2d899c0862b8775a27b0a57148f4490ef0b',
      },
    },
    rk3576: {
      channels: 8,
      bindings: 16,
      samples: 4316,
      fps: { minimum: 5, average: 5.098, maximum: 5.17 },
      resources: { cpu: 46, memory: 30, disk: 15 },
      disk: { first: 14, last: 15, minimum: 14, maximum: 15, changes: 5 },
      window: { firstSampleLagSeconds: 111.99, finalSampleLagSeconds: 59.867, maximumGapSeconds: 60.058 },
      hashes: {
        metrics: '7095b3a119bfebe508fb5d4d38306287c928e0467ae6087df434aba499ca7c5b',
        summary: 'e44698bb3dd6eee21fa6f7fa765f398957cacb697196e6eb037d5946bc1d6fb5',
        report: 'fa79c8c08f505881b5bc4f237d01e430d630c2596d17419dda24d93fa354e8ef',
        restartGuard: '3d9401895a5946583194b5fd41054c40d47a704315412fbe65c0650fee7fbb84',
        cleanup: '763eaa2e869d3950619d7408092cd59bef8d9d60fdfeb448d808e4052b271dd6',
      },
    },
    rv1126b: {
      channels: 4,
      bindings: 8,
      samples: 4316,
      fps: { minimum: 4.85, average: 5.23, maximum: 5.37 },
      resources: { cpu: 41, memory: 41, disk: 47 },
      disk: { first: 46, last: 47, minimum: 46, maximum: 47, changes: 7 },
      window: { firstSampleLagSeconds: 111.995, finalSampleLagSeconds: 59.866, maximumGapSeconds: 60.067 },
      hashes: {
        metrics: '613675df48c840486b495c7e897742332229c1ff8f2a6b81fa9029bff7516052',
        summary: '59517e9b3f609cae55f122f2f3bfe44f33d1497faf3fe536135149c4033218ab',
        report: 'e4afff34a773414e009642d8201e2a1148e4f09d9c453901776753e666194734',
        restartGuard: '80aa5e31f8e07adfd595c2b2aa09f2c66d899d06d960ce548bda6109b71bcf6b',
        cleanup: 'b84a2b820666981fbc72b1175ad14391e873d454ce472115e61c5b6eb75bbeeb',
      },
    },
  },
};

function parseArgs(argv) {
  const result = {};
  for (let index = 0; index < argv.length; index += 1) {
    const flag = argv[index];
    if (flag === '--evidence-root' || flag === '--projection-tool' || flag === '--canonical') {
      if (!argv[index + 1]) throw new Error(`${flag} requires a value`);
      result[flag.slice(2)] = argv[index + 1];
      index += 1;
    } else if (flag === '--help' || flag === '-h') {
      result.help = true;
    } else {
      throw new Error(`unknown option: ${flag}`);
    }
  }
  return result;
}

function usage() {
  return [
    'Usage: node scripts/verify-v1.1-dual-cv-72h-private.mjs --evidence-root <private-run-root>',
    '       [--projection-tool <file>] [--canonical <dual-cv-72h.json>]',
    '',
    'The verifier is read-only and emits sanitized status without printing private paths or identifiers.',
  ].join('\n');
}

function fail(label, detail) {
  throw new Error(`${label}: ${detail}`);
}

function assert(label, condition, detail = 'unexpected value') {
  if (!condition) fail(label, detail);
}

function equalNumber(actual, expected, precision = 6) {
  return Number.isFinite(actual) && Math.abs(actual - expected) <= 10 ** (-precision);
}

function round(value, digits = 3) {
  return Number(value.toFixed(digits));
}

function readJson(file, label) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    fail(label, 'missing or invalid JSON');
  }
}

async function sha256File(file, label) {
  if (!fs.existsSync(file) || !fs.statSync(file).isFile()) fail(label, 'missing file');
  const hash = crypto.createHash('sha256');
  await new Promise((resolve, reject) => {
    const stream = fs.createReadStream(file);
    stream.on('data', (chunk) => hash.update(chunk));
    stream.on('error', reject);
    stream.on('end', resolve);
  }).catch(() => fail(label, 'could not read file'));
  return hash.digest('hex');
}

async function requireHash(file, expected, label) {
  const actual = await sha256File(file, label);
  assert(label, actual === expected, 'SHA-256 mismatch');
}

function canonicalExpected(canonical) {
  assert('canonical', canonical?.schemaVersion === 2, 'schemaVersion must be 2');
  assert('canonical', canonical?.window?.startedAt === EXPECTED.window.startedAt, 'window start differs');
  assert('canonical', canonical?.window?.endedAt === EXPECTED.window.endedAt, 'window end differs');
  assert('canonical', canonical?.source?.commit === EXPECTED.source.commit, 'source commit differs');
  assert('canonical', canonical?.source?.tree === EXPECTED.source.tree, 'source tree differs');
  assert('canonical', canonical?.input?.sha256 === EXPECTED.videoSha256, 'input hash differs');
  assert('canonical', canonical?.privateEvidence?.runManifestSha256 === EXPECTED.hashes.runManifest, 'run-manifest hash differs');
  assert('canonical', canonical?.privateEvidence?.suiteStateSha256 === EXPECTED.hashes.suiteState, 'suite-state hash differs');
  assert('canonical', canonical?.privateEvidence?.suiteSummarySha256 === EXPECTED.hashes.suiteSummary, 'suite-summary hash differs');
  assert('canonical', canonical?.privateEvidence?.projectionToolSha256 === EXPECTED.hashes.projectionTool, 'projection-tool hash differs');
  assert('canonical', canonical?.executionPolicy?.monitor?.launchBytesFrozen === false, 'launch-byte limitation differs');
  assert('canonical', canonical?.executionPolicy?.disk?.executedIntegrityGateEnabled === false, 'executed disk policy differs');
  assert('canonical', canonical?.executionPolicy?.disk?.projectionReportThresholdPercent === 99, 'projection disk policy differs');
  assert('canonical', canonical?.executionPolicy?.disk?.futureSafeguardThresholdPercent === 90, 'future disk policy differs');
  assert('canonical', canonical?.executionPolicy?.disk?.futureSafeguardAppliedRetroactively === false, 'disk policy must be non-retroactive');
  assert('canonical', canonical?.executionPolicy?.scheduledRestart?.initialStateDisabledOnAllPlatforms === true, 'restart initial state differs');
  assert('canonical', canonical?.executionPolicy?.scheduledRestart?.restartResilienceClaimAllowed === false, 'restart claim boundary differs');

  const observations = new Map((canonical?.observations ?? []).map((item) => [item.platformId, item]));
  for (const [platformId, expected] of Object.entries(EXPECTED.platforms)) {
    const item = observations.get(platformId);
    assert(`canonical ${platformId}`, item?.configuredChannels === expected.channels, 'channel count differs');
    assert(`canonical ${platformId}`, item?.businessTaskBindings === expected.bindings, 'binding count differs');
    assert(`canonical ${platformId}`, item?.samples?.observed === expected.samples, 'sample count differs');
    assert(`canonical ${platformId}`, JSON.stringify(item?.fps) === JSON.stringify(expected.fps), 'FPS summary differs');
    assert(`canonical ${platformId}`, JSON.stringify(item?.observedResourcePeaksPercent) === JSON.stringify(expected.resources), 'resource peaks differ');
    assert(`canonical ${platformId}`, JSON.stringify(item?.diskTrendPercent) === JSON.stringify(expected.disk), 'disk trend differs');
    const hashes = item?.sourceArtifacts ?? {};
    assert(`canonical ${platformId}`, hashes.metricsJsonlSha256 === expected.hashes.metrics, 'metrics hash differs');
    assert(`canonical ${platformId}`, hashes.summarySha256 === expected.hashes.summary, 'summary hash differs');
    assert(`canonical ${platformId}`, hashes.reportSha256 === expected.hashes.report, 'report hash differs');
    assert(`canonical ${platformId}`, hashes.restartGuardSha256 === expected.hashes.restartGuard, 'restart-guard hash differs');
    assert(`canonical ${platformId}`, hashes.cleanupSha256 === expected.hashes.cleanup, 'cleanup hash differs');
  }
}

function verifyGlobalSemantics(root) {
  const manifest = readJson(path.join(root, 'run-manifest.json'), 'run manifest');
  assert('run manifest', manifest?.contract?.durationHours === EXPECTED.window.durationHours, 'duration differs');
  assert('run manifest', manifest?.contract?.sampleIntervalSec === EXPECTED.window.sampleIntervalSeconds, 'sample interval differs');
  assert('run manifest', manifest?.contract?.targetFps === 5, 'target FPS differs');
  assert('run manifest', manifest?.contract?.tasksPerChannel === 2, 'tasks-per-channel differs');
  assert('run manifest', manifest?.source?.commit === EXPECTED.source.commit, 'source commit differs');
  assert('run manifest', manifest?.source?.tree === EXPECTED.source.tree, 'source tree differs');
  assert('run manifest', manifest?.video?.sha256 === EXPECTED.videoSha256, 'video hash differs');

  const state = readJson(path.join(root, 'suite-state.json'), 'suite state');
  assert('suite state', state?.status === 'completed', 'status is not completed');
  assert('suite state', state?.soakStartedAt === EXPECTED.window.startedAt, 'window start differs');
  assert('suite state', state?.stopAt === EXPECTED.window.endedAt, 'window end differs');
  assert('suite state', state?.source?.commit === EXPECTED.source.commit, 'source commit differs');
  assert('suite state', state?.source?.tree === EXPECTED.source.tree, 'source tree differs');
  assert('suite state', state?.video?.sha256 === EXPECTED.videoSha256, 'video hash differs');
  for (const [platformId, expected] of Object.entries(EXPECTED.platforms)) {
    const target = state?.targets?.[platformId];
    assert(`suite state ${platformId}`, target?.status === 'completed', 'platform status is not completed');
    assert(`suite state ${platformId}`, target?.samples === expected.samples, 'sample count differs');
    assert(`suite state ${platformId}`, target?.expectedChannels === expected.channels, 'channel count differs');
    assert(`suite state ${platformId}`, target?.expectedTaskBindings === expected.bindings, 'binding count differs');
    assert(`suite state ${platformId}`, Array.isArray(target?.hourlyChecks) && target.hourlyChecks.length === 71, 'hourly-check count differs');
    assert(`suite state ${platformId}`, Array.isArray(target?.incidents) && target.incidents.length === 0, 'incidents are not empty');
    assert(`suite state ${platformId}`, target?.restart?.original?.isTimingRestart === 0, 'scheduled restart was not initially disabled');
    assert(`suite state ${platformId}`, target?.restart?.verified?.isTimingRestart === 0, 'scheduled restart did not remain disabled');
    assert(`suite state ${platformId}`, target?.cleanup?.status === 'completed', 'cleanup status differs');
    assert(`suite state ${platformId}`, target?.cleanup?.errors?.length === 0, 'cleanup errors are not empty');
    assert(`suite state ${platformId}`, target?.cleanup?.remainingOwnedChannels?.length === 0, 'owned channels remain');
  }

  const suite = readJson(path.join(root, 'checkpoints', '72h', 'summary.json'), '72-hour suite summary');
  assert('72-hour suite summary', suite?.checkpointHours === 72, 'endpoint is not 72 hours');
  assert('72-hour suite summary', suite?.soakStartedAt === EXPECTED.window.startedAt, 'window start differs');
  assert('72-hour suite summary', suite?.evaluatedAt === EXPECTED.window.endedAt, 'window end differs');
  assert('72-hour suite summary', suite?.source?.commit === EXPECTED.source.commit, 'source commit differs');
  assert('72-hour suite summary', suite?.source?.tree === EXPECTED.source.tree, 'source tree differs');
  assert('72-hour suite summary', Object.keys(suite?.platforms ?? {}).sort().join(',') === Object.keys(EXPECTED.platforms).sort().join(','), 'platform inventory differs');
}

function verifySummaryAndCleanup(root, platformId, expected) {
  const summary = readJson(path.join(root, platformId, 'summary.json'), `${platformId} summary`);
  const longRun = summary?.longRun;
  const aggregate = longRun?.aggregate;
  const window = longRun?.integrity?.window;
  const restart = longRun?.timedRestart;
  assert(`${platformId} summary`, summary?.status === 'completed' && summary?.overallPass === true, 'overall status is not PASS');
  assert(`${platformId} summary`, summary?.sampleCount === expected.samples && longRun?.observedSamples === expected.samples, 'sample count differs');
  assert(`${platformId} summary`, longRun?.expectedChannels === expected.channels, 'channel count differs');
  assert(`${platformId} summary`, longRun?.expectedTaskBindings === expected.bindings, 'binding count differs');
  assert(`${platformId} summary`, longRun?.integrity?.pass === true, 'integrity status is not PASS');
  assert(`${platformId} summary`, longRun?.integrity?.checks?.length === 8 && longRun.integrity.checks.every((item) => item?.pass === true), 'not all eight checks passed');
  assert(`${platformId} summary`, longRun?.integrity?.openCriticalIncidents?.length === 0, 'open critical incidents are not empty');
  assert(`${platformId} summary`, aggregate?.sampleCount === expected.samples, 'aggregate sample count differs');
  assert(`${platformId} summary`, equalNumber(aggregate?.minFps, expected.fps.minimum), 'minimum FPS differs');
  assert(`${platformId} summary`, equalNumber(aggregate?.avgFps, expected.fps.average), 'average FPS differs');
  assert(`${platformId} summary`, equalNumber(aggregate?.maxFps, expected.fps.maximum), 'maximum FPS differs');
  assert(`${platformId} summary`, aggregate?.maxCpu === expected.resources.cpu, 'CPU peak differs');
  assert(`${platformId} summary`, aggregate?.maxMemory === expected.resources.memory, 'memory peak differs');
  assert(`${platformId} summary`, aggregate?.maxDisk === expected.resources.disk, 'disk peak differs');
  for (const key of ['maxDiscard', 'collectorErrorSamples', 'incompleteBindingSamples', 'missingBindingSamples']) {
    assert(`${platformId} summary`, aggregate?.[key] === 0, `${key} is not zero`);
  }
  assert(`${platformId} summary`, window?.expectedSamples === EXPECTED.window.expectedSamples, 'expected samples differ');
  assert(`${platformId} summary`, window?.observedSamples === expected.samples, 'observed samples differ');
  assert(`${platformId} summary`, equalNumber(window?.firstSampleLagSec, expected.window.firstSampleLagSeconds), 'first-sample lag differs');
  assert(`${platformId} summary`, equalNumber(window?.finalSampleLagSec, expected.window.finalSampleLagSeconds), 'final-sample lag differs');
  assert(`${platformId} summary`, equalNumber(window?.maxGapSec, expected.window.maximumGapSeconds), 'maximum gap differs');
  assert(`${platformId} summary`, restart?.checks === 80 && restart?.failures === 0 && restart?.corrections === 0, 'scheduled-restart checks differ');
  assert(`${platformId} summary`, restart?.latest?.original?.isTimingRestart === 0, 'scheduled restart was not initially disabled');
  assert(`${platformId} summary`, restart?.latest?.verified?.isTimingRestart === 0, 'scheduled restart did not remain disabled');

  const cleanup = readJson(path.join(root, platformId, 'cleanup.json'), `${platformId} cleanup`);
  assert(`${platformId} cleanup`, cleanup?.status === 'completed', 'status is not completed');
  assert(`${platformId} cleanup`, cleanup?.errors?.length === 0, 'errors are not empty');
  assert(`${platformId} cleanup`, cleanup?.disabledTaskBindings === expected.bindings, 'disabled binding count differs');
  assert(`${platformId} cleanup`, cleanup?.requestedDeletedChannels?.length === expected.channels, 'deleted-channel request count differs');
  assert(`${platformId} cleanup`, cleanup?.remainingOwnedChannels?.length === 0, 'owned channels remain');
  assert(`${platformId} cleanup`, cleanup?.restoredLayouts === true, 'layouts were not restored');
  assert(`${platformId} cleanup`, cleanup?.originalRestartRetainedForAudit?.isTimingRestart === 0, 'initial restart state differs');
}

async function verifyRestartGuard(root, platformId) {
  const file = path.join(root, platformId, 'restart-guard.jsonl');
  const lines = readline.createInterface({ input: fs.createReadStream(file), crlfDelay: Infinity });
  let count = 0;
  let finalCleanup = 0;
  try {
    for await (const line of lines) {
      if (!line.trim()) continue;
      count += 1;
      let item;
      try {
        item = JSON.parse(line);
      } catch {
        fail(`${platformId} restart guard`, 'invalid JSONL');
      }
      if (item?.reason === 'final-cleanup') finalCleanup += 1;
      assert(`${platformId} restart guard`, item?.action === 'verified-disabled', 'unexpected action');
      assert(`${platformId} restart guard`, item?.disabled === true, 'disabled verification failed');
      assert(`${platformId} restart guard`, item?.original?.isTimingRestart === 0, 'initial state was not disabled');
      assert(`${platformId} restart guard`, item?.observedBefore?.isTimingRestart === 0, 'observed state was not disabled');
      assert(`${platformId} restart guard`, item?.verified?.isTimingRestart === 0, 'verified state was not disabled');
    }
  } catch (error) {
    if (error instanceof Error && error.message.startsWith(`${platformId} restart guard:`)) throw error;
    fail(`${platformId} restart guard`, 'could not read JSONL');
  }
  assert(`${platformId} restart guard`, count === 81, 'record count differs');
  assert(`${platformId} restart guard`, finalCleanup === 1, 'final-cleanup record count differs');
}

async function verifyMetrics(root, platformId, expected) {
  const file = path.join(root, platformId, 'metrics.jsonl');
  const lines = readline.createInterface({ input: fs.createReadStream(file), crlfDelay: Infinity });
  let samples = 0;
  let firstTime = null;
  let lastTime = null;
  let previousTime = null;
  let maximumGap = 0;
  let fpsMinimum = Infinity;
  let fpsMaximum = -Infinity;
  let fpsSum = 0;
  let fpsCount = 0;
  let maxCpu = -Infinity;
  let maxMemory = -Infinity;
  let maxDisk = -Infinity;
  let maxDiscard = -Infinity;
  let diskFirst = null;
  let diskLast = null;
  let diskMinimum = Infinity;
  let diskMaximum = -Infinity;
  let diskChanges = 0;
  let previousDisk = null;

  try {
    for await (const line of lines) {
      if (!line.trim()) continue;
      let item;
      try {
        item = JSON.parse(line);
      } catch {
        fail(`${platformId} metrics`, 'invalid JSONL');
      }
      samples += 1;
      const time = Date.parse(item?.iso);
      assert(`${platformId} metrics`, Number.isFinite(time), 'invalid sample timestamp');
      if (firstTime == null) firstTime = time;
      if (previousTime != null) {
        assert(`${platformId} metrics`, time > previousTime, 'sample timestamps are not strictly increasing');
        maximumGap = Math.max(maximumGap, (time - previousTime) / 1000);
      }
      previousTime = time;
      lastTime = time;
      assert(`${platformId} metrics`, item?.activeChannels === expected.channels, 'active channel count differs');
      assert(`${platformId} metrics`, item?.activeTaskBindings === expected.bindings, 'active binding count differs');
      assert(`${platformId} metrics`, item?.targetChannels === expected.channels, 'target channel count differs');
      assert(`${platformId} metrics`, item?.collectorError == null, 'collector error observed');
      assert(`${platformId} metrics`, Array.isArray(item?.channels) && item.channels.length === expected.bindings, 'binding telemetry count differs');
      for (const channel of item.channels) {
        assert(`${platformId} metrics`, channel?.missing === false && channel?.telemetryMissing === false && channel?.error == null, 'missing or invalid binding telemetry');
        assert(`${platformId} metrics`, Number.isFinite(channel?.measuredFps), 'invalid FPS sample');
        assert(`${platformId} metrics`, Number.isFinite(channel?.discardRate), 'invalid discard sample');
        fpsMinimum = Math.min(fpsMinimum, channel.measuredFps);
        fpsMaximum = Math.max(fpsMaximum, channel.measuredFps);
        fpsSum += channel.measuredFps;
        fpsCount += 1;
        maxDiscard = Math.max(maxDiscard, channel.discardRate);
      }
      const cpu = item?.hardware?.cpuUtilization?.usedPercent;
      const memory = item?.hardware?.generalMemoryUtilization?.usedPercent;
      const disk = item?.hardware?.eMMCUtilization?.usedPercent;
      assert(`${platformId} metrics`, [cpu, memory, disk].every(Number.isFinite), 'invalid resource sample');
      maxCpu = Math.max(maxCpu, cpu);
      maxMemory = Math.max(maxMemory, memory);
      maxDisk = Math.max(maxDisk, disk);
      if (diskFirst == null) diskFirst = disk;
      if (previousDisk != null && disk !== previousDisk) diskChanges += 1;
      diskLast = disk;
      diskMinimum = Math.min(diskMinimum, disk);
      diskMaximum = Math.max(diskMaximum, disk);
      previousDisk = disk;
    }
  } catch (error) {
    if (error instanceof Error && error.message.startsWith(`${platformId} metrics:`)) throw error;
    fail(`${platformId} metrics`, 'could not read JSONL');
  }

  const startedAt = Date.parse(EXPECTED.window.startedAt);
  const endedAt = Date.parse(EXPECTED.window.endedAt);
  assert(`${platformId} metrics`, samples === expected.samples, 'sample count differs');
  assert(`${platformId} metrics`, fpsCount === expected.samples * expected.bindings, 'FPS sample count differs');
  assert(`${platformId} metrics`, equalNumber(fpsMinimum, expected.fps.minimum), 'minimum FPS differs');
  assert(`${platformId} metrics`, equalNumber(round(fpsSum / fpsCount), expected.fps.average), 'average FPS differs');
  assert(`${platformId} metrics`, equalNumber(fpsMaximum, expected.fps.maximum), 'maximum FPS differs');
  assert(`${platformId} metrics`, maxCpu === expected.resources.cpu, 'CPU peak differs');
  assert(`${platformId} metrics`, maxMemory === expected.resources.memory, 'memory peak differs');
  assert(`${platformId} metrics`, maxDisk === expected.resources.disk, 'disk peak differs');
  assert(`${platformId} metrics`, maxDiscard === 0, 'discard is not zero');
  assert(`${platformId} metrics`, equalNumber(round((firstTime - startedAt) / 1000), expected.window.firstSampleLagSeconds), 'first-sample lag differs');
  assert(`${platformId} metrics`, equalNumber(round((endedAt - lastTime) / 1000), expected.window.finalSampleLagSeconds), 'final-sample lag differs');
  assert(`${platformId} metrics`, equalNumber(round(maximumGap), expected.window.maximumGapSeconds), 'maximum gap differs');
  assert(`${platformId} metrics`, JSON.stringify({ first: diskFirst, last: diskLast, minimum: diskMinimum, maximum: diskMaximum, changes: diskChanges }) === JSON.stringify(expected.disk), 'disk trend differs');
}

async function verify() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) {
    console.log(usage());
    return;
  }
  if (!args['evidence-root']) throw new Error('--evidence-root is required');
  const root = path.resolve(args['evidence-root']);
  assert('evidence root', fs.existsSync(root) && fs.statSync(root).isDirectory(), 'missing directory');

  let canonicalPath = args.canonical ? path.resolve(args.canonical) : null;
  if (!canonicalPath && process.argv[1] && process.argv[1] !== '-') {
    canonicalPath = path.resolve(path.dirname(process.argv[1]), '..', 'docs', 'benchmarks', 'scenario-bench', 'v1.1', 'results', 'dual-cv-72h.json');
  }
  if (canonicalPath && fs.existsSync(canonicalPath)) canonicalExpected(readJson(canonicalPath, 'canonical'));

  const projectionTool = path.resolve(args['projection-tool'] ?? path.join(root, '..', '..', 'scripts', 'regenerate-multiplatform-dualcv-72h-checkpoint-20260820.mjs'));
  await requireHash(path.join(root, 'run-manifest.json'), EXPECTED.hashes.runManifest, 'run manifest');
  await requireHash(path.join(root, 'suite-state.json'), EXPECTED.hashes.suiteState, 'suite state');
  await requireHash(path.join(root, 'checkpoints', '72h', 'summary.json'), EXPECTED.hashes.suiteSummary, '72-hour suite summary');
  await requireHash(projectionTool, EXPECTED.hashes.projectionTool, 'projection tool');
  verifyGlobalSemantics(root);

  for (const [platformId, expected] of Object.entries(EXPECTED.platforms)) {
    await requireHash(path.join(root, platformId, 'metrics.jsonl'), expected.hashes.metrics, `${platformId} metrics`);
    await requireHash(path.join(root, platformId, 'summary.json'), expected.hashes.summary, `${platformId} summary`);
    await requireHash(path.join(root, platformId, 'report.html'), expected.hashes.report, `${platformId} report`);
    await requireHash(path.join(root, platformId, 'restart-guard.jsonl'), expected.hashes.restartGuard, `${platformId} restart guard`);
    await requireHash(path.join(root, platformId, 'cleanup.json'), expected.hashes.cleanup, `${platformId} cleanup`);
    verifySummaryAndCleanup(root, platformId, expected);
    await verifyRestartGuard(root, platformId);
    await verifyMetrics(root, platformId, expected);
    console.log(`PASS ${platformId}: hashes and semantics verified`);
  }
  console.log('PASS dual-CV 72-hour private evidence: 4 platforms, 24 source artifacts, one continuous 72-hour endpoint');
}

verify().catch((error) => {
  console.error(`FAIL dual-CV 72-hour private evidence: ${error instanceof Error ? error.message : 'unexpected verifier error'}`);
  process.exitCode = 1;
});
