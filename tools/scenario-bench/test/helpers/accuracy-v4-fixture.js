export const HASH_A = 'a'.repeat(64);
export const HASH_B = 'b'.repeat(64);
export const HASH_C = 'c'.repeat(64);
export const HASH_D = 'd'.repeat(64);

export function makeAccuracyFixture({
  concurrency = 1,
  caseCount = 2,
  suiteId = 'suite',
  suiteDisplayName = 'Suite',
  taskDisplayName = 'Helmet',
  algorithmId = '15',
  algorithmCode = algorithmId,
  configSource = 'frozen',
  taskConfigHashes = [HASH_D],
} = {}) {
  const execution = {
    profile: 'full',
    concurrency,
    selection: { count: caseCount, sha256: HASH_C },
    measurementConfigSha256: HASH_A,
  };
  const suite = {
    schemaVersion: 3,
    protocolVersion: 4,
    id: suiteId,
    displayName: suiteDisplayName,
    sourceMode: 'local',
    identity: {
      suiteSha256: HASH_A,
      caseManifestSha256: HASH_B,
      caseSetSha256: HASH_C,
      taskConfigSha256: { helmet: HASH_D },
    },
    tasks: [{
      id: 'helmet', displayName: taskDisplayName, kind: 'cv', algorithmId,
      algorithmCode, configSource, taskConfigSha256: taskConfigHashes[0] ?? null,
      taskConfigHashes,
    }],
  };
  const admission = {
    status: 'PASS',
    checkedAt: '2026-08-28T00:00:00.000Z',
    profile: execution.profile,
    concurrency,
    selection: structuredClone(execution.selection),
    media: { checked: caseCount, valid: caseCount, invalid: 0 },
    checks: [{ name: 'suite', status: 'PASS', blocking: true }],
    warnings: [],
  };
  return { execution, suite, admission };
}

export function makeMeasuredCase({
  id,
  expectation,
  status = 'PASS',
  task = 'helmet',
  critical = false,
  totalMs = null,
  events = [],
} = {}) {
  const trialStatuses = totalMs == null ? [] : [status];
  const trialTotalMs = trialStatuses.length ? totalMs / trialStatuses.length : 0;
  const observationMs = Math.max(0, trialTotalMs - 100);
  const trials = trialStatuses.map((trialStatus) => ({
    status: trialStatus,
    eventCount: measuredEventCount(expectation, trialStatus),
    taskConfigSha256: HASH_D,
    timingMs: { total: trialTotalMs, observation: observationMs },
    observation: {
      requestedSec: 5,
      actualSec: observationMs / 1_000,
      earlyStopped: observationMs < 5_000,
    },
    eventCollection: { settled: true },
    cleanup: { task: { verified: true }, channel: { verified: true }, rtsp: null },
    events,
  }));
  const eventCount = trials[0]?.eventCount ?? measuredEventCount(
    expectation,
    status,
  );
  return { id, task, expectation, status, critical, eventCount, trials };
}

function measuredEventCount(expectation, status) {
  if (status !== 'PASS' && status !== 'FAIL') return null;
  if (status === 'PASS') return expectation?.minEvents ?? 0;
  if (expectation?.minEvents != null) return Math.max(0, expectation.minEvents - 1);
  return (expectation?.maxEvents ?? 0) + 1;
}
