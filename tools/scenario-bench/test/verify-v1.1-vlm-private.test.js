import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { ReportWriter } from '../src/report-writer.js';
import {
  deriveCapacityBoundary,
  verifyPrivateEvidence,
} from '../../../scripts/verify-v1.1-vlm-private.mjs';

const PLATFORMS = [
  { id: 'bm1688', name: 'BM1688', algorithmId: '67093', templateActionId: 'DA_00003', completionActionId: 'BA_00004' },
  { id: 'cv186x', name: 'CV186X', algorithmId: '67093', templateActionId: 'DA_00003', completionActionId: 'BA_00004' },
  { id: 'rk3576', name: 'RK3576', algorithmId: '78510', templateActionId: 'PDA_00003', completionActionId: 'PDA_00003' },
];

const CHANNELS = [1, 2, 3, 4, 5, 6, 7, 8];
const PROMPT = 'fixture-person-presence';
const RC_COMMIT = '1'.repeat(40);
const RC_TREE = '2'.repeat(40);
const RC_VERSION = 'V1.1.0';
const RELEASE_STATE = 'fixture-frozen-candidate';

test('capacity boundary keeps gate failure, lower bound, and execution block distinct', () => {
  const passing = (channels) => ({
    channels,
    step: { index: channels - 1 },
    skipped: false,
    qualified: true,
    pass: true,
    perThreshold: [],
  });
  const lowerBound = deriveCapacityBoundary(
    { status: 'completed', bottleneck: null },
    CHANNELS.map(passing),
  );
  assert.deepEqual(lowerBound, {
    classification: 'all-pass-lower-bound',
    verifiedPassingChannels: 8,
    firstFailureChannels: null,
    firstFailureCategory: 'none',
    capacityExact: false,
    lowerBound: 8,
  });

  const failed = {
    ...passing(5),
    pass: false,
    perThreshold: [{ name: 'minFpsRatio', result: 'FAIL' }],
  };
  const gate = deriveCapacityBoundary(
    {
      status: 'completed',
      bottleneck: {
        channels: 5,
        targetChannels: 5,
        stepIndex: 4,
        stepNumber: 5,
        phase: 'hold',
        reason: 'vlm fpsRatio 0.7 < 0.8',
      },
    },
    [...CHANNELS.slice(0, 4).map(passing), failed],
  );
  assert.equal(gate.classification, 'gate-first-failure');
  assert.equal(gate.verifiedPassingChannels, 4);
  assert.equal(gate.firstFailureChannels, 5);
  assert.equal(gate.firstFailureCategory, 'fps-ratio');

  const blocked = deriveCapacityBoundary(
    { status: 'aborted', error: { atChannels: 4 } },
    CHANNELS.slice(0, 3).map(passing),
  );
  assert.equal(blocked.classification, 'execution-block');
  assert.equal(blocked.verifiedPassingChannels, 3);
  assert.equal(blocked.firstFailureChannels, 4);
  assert.equal(blocked.firstFailureCategory, 'execution-block');
});

test('private fixture verifies protocol, raw VLM metrics, summaries, reports, and integrity', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root);

  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  assert.equal(verified.identityStatus, 'verified');
  assert.equal(verified.integrityStatus, 'verified');
  assert.equal(verified.e2eStatus, 'not-provided');
  assert.equal(verified.canonicalStatus, 'not-requested');
  assert.deepEqual(verified.results.map((item) => item.platformId), PLATFORMS.map((item) => item.id));
  for (const result of verified.results) {
    assert.equal(result.boundary.classification, 'all-pass-lower-bound');
    assert.equal(result.boundary.verifiedPassingChannels, 8);
    assert.equal(result.boundary.lowerBound, 8);
  }

  const canonicalPath = path.join(root, 'canonical.json');
  fs.writeFileSync(canonicalPath, JSON.stringify(canonicalFor(verified), null, 2), 'utf8');
  const canonicalVerified = await verifyPrivateEvidence({
    evidenceRoot: root,
    canonicalPath,
  });
  assert.equal(canonicalVerified.canonicalStatus, 'verified');
});

test('E2E manifest verifies all platforms and binds canonical PASS claims', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-e2e-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { withE2E: true });

  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  assert.equal(verified.e2eStatus, 'verified');
  assert.equal(verified.e2e.conclusion, 'all-platforms-pass');
  assert.deepEqual(verified.e2e.platforms.map((item) => item.platformId),
    PLATFORMS.map((item) => item.id));

  const canonicalPath = path.join(root, 'canonical-e2e.json');
  fs.writeFileSync(canonicalPath, JSON.stringify(canonicalFor(verified), null, 2), 'utf8');
  const canonicalVerified = await verifyPrivateEvidence({ evidenceRoot: root, canonicalPath });
  assert.equal(canonicalVerified.e2eStatus, 'verified');
  assert.equal(canonicalVerified.canonicalStatus, 'verified');
});

test('canonical three-platform E2E acceptance is rejected without the private manifest', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-e2e-required-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { withE2E: true });
  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  const canonicalPath = path.join(root, 'canonical-e2e.json');
  fs.writeFileSync(canonicalPath, JSON.stringify(canonicalFor(verified), null, 2), 'utf8');
  fs.rmSync(path.join(root, 'e2e-integrity.private.json'));

  await assert.rejects(
    verifyPrivateEvidence({ evidenceRoot: root, canonicalPath }),
    /canonical E2E: three-platform E2E claim requires a verified private E2E manifest/,
  );
});

test('canonical E2E platform PASS fields and source evidence identity are enforced', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-canonical-e2e-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { withE2E: true });
  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  const canonicalPath = path.join(root, 'canonical-e2e.json');
  const cases = [
    {
      mutate(value) { value.endToEndAcceptance.platforms[0].status = 'FAIL'; },
      expected: /canonical E2E bm1688: platform status must be PASS/,
    },
    {
      mutate(value) { value.endToEndAcceptance.platforms[1].validInferenceResult = 'FAIL'; },
      expected: /canonical E2E cv186x: all canonical E2E stages must be PASS/,
    },
    {
      mutate(value) { value.endToEndAcceptance.platforms[2].evidenceSha256 = '0'.repeat(64); },
      expected: /canonical E2E rk3576: source evidence SHA-256 differs/,
    },
  ];
  for (const item of cases) {
    const canonical = canonicalFor(verified);
    item.mutate(canonical);
    fs.writeFileSync(canonicalPath, JSON.stringify(canonical, null, 2), 'utf8');
    await assert.rejects(verifyPrivateEvidence({ evidenceRoot: root, canonicalPath }), item.expected);
  }
});

test('E2E manifest rejects inventory, path, hash, and size tampering', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-e2e-integrity-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { withE2E: true });
  const manifestPath = path.join(root, 'e2e-integrity.private.json');
  const original = fs.readFileSync(manifestPath, 'utf8');
  const cases = [
    {
      mutate(manifest) { delete manifest.inventory.rk3576; },
      expected: /platform inventory must be exactly three supported platforms/,
    },
    {
      mutate(manifest) { manifest.inventory.bm1688.result.path = '../outside.private.json'; },
      expected: /file reference escapes evidence root/,
    },
    {
      mutate(manifest) { manifest.inventory.cv186x.result.sha256 = '0'.repeat(64); },
      expected: /E2E cv186x result: SHA-256 mismatch/,
    },
    {
      mutate(manifest) { manifest.inventory.rk3576.restartLog.sizeBytes += 1; },
      expected: /E2E rk3576 restart log: size mismatch/,
    },
  ];
  for (const item of cases) {
    const manifest = JSON.parse(original);
    item.mutate(manifest);
    fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2), 'utf8');
    await assert.rejects(verifyPrivateEvidence({ evidenceRoot: root }), item.expected);
  }
  fs.writeFileSync(manifestPath, original, 'utf8');
});

test('E2E semantic gates fail closed for every required acceptance dimension', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-e2e-gates-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { withE2E: true });
  const manifestPath = path.join(root, 'e2e-integrity.private.json');
  const originalManifest = fs.readFileSync(manifestPath, 'utf8');
  const baselineManifest = JSON.parse(originalManifest);
  const evidencePath = path.join(root, ...baselineManifest.inventory.bm1688.result.path.split('/'));
  const originalEvidence = fs.readFileSync(evidencePath, 'utf8');
  const cases = [
    { mutate: (value) => { value.status = 'FAIL'; }, expected: /status must be PASS/ },
    { mutate: (value) => { value.acceptance.modelLoad.status = 'FAIL'; }, expected: /all eight acceptance stages must be PASS/ },
    { mutate: (value) => { delete value.acceptance.cleanup; }, expected: /acceptance inventory must contain exactly eight/ },
    { mutate: (value) => { value.protocol.completionCounterScope = 'global'; }, expected: /protocol counter scope must be task-local/ },
    { mutate: (value) => { value.acceptance.inferenceBeforeRestart.counterDelta = 0; }, expected: /before: task-local completion counter delta must be positive/ },
    { mutate: (value) => { value.acceptance.taskRecoveryAfterRestart.counterDelta = 0; }, expected: /after: task-local completion counter delta must be positive/ },
    { mutate: (value) => { value.acceptance.eventOrAlarmOutput.observed = false; }, expected: /event or alarm output was not observed/ },
    { mutate: (value) => { value.acceptance.serviceRestart.serviceActiveMarkerObserved = false; }, expected: /service restart active verification failed/ },
    { mutate: (value) => { value.acceptance.cleanup.ownedChannelDeleted = false; }, expected: /owned resource cleanup did not complete/ },
    { mutate: (value) => { value.acceptance.layoutIntegrity.globalLayoutMutationApplied = true; }, expected: /layout integrity verification failed/ },
    { mutate: (value) => { value.privacy.endpointStored = true; }, expected: /all privacy storage flags must be false/ },
  ];
  for (const item of cases) {
    const evidence = JSON.parse(originalEvidence);
    item.mutate(evidence);
    fs.writeFileSync(evidencePath, JSON.stringify(evidence, null, 2), 'utf8');
    const manifest = JSON.parse(originalManifest);
    manifest.inventory.bm1688.result = artifactRecord(root, evidencePath);
    fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2), 'utf8');
    await assert.rejects(verifyPrivateEvidence({ evidenceRoot: root }), item.expected);
  }
  fs.writeFileSync(evidencePath, originalEvidence, 'utf8');
  fs.writeFileSync(manifestPath, originalManifest, 'utf8');
});

test('candidate package and postinstall identity chain rejects tampering', async () => {
  const cases = [
    {
      mutate(root) {
        fs.rmSync(path.join(root, 'candidates', 'package-manifest.private.json'));
      },
      expected: /candidate package manifest: missing file/,
    },
    {
      mutate(root) {
        const manifest = readFixtureJson(root, 'candidates/package-manifest.private.json');
        const packagePath = path.join(root, manifest.packages.bm1688.directory,
          manifest.packages.bm1688.file);
        fs.appendFileSync(packagePath, 'tamper');
      },
      expected: /candidate package bm1688: package size mismatch/,
    },
    {
      mutate(root) {
        mutateFixtureJson(root, 'identities/postinstall.private.json', (value) => {
          delete value.platforms.rk3576;
        });
      },
      expected: /postinstall platform inventory: platform inventory must be exactly three/,
    },
    {
      mutate(root) {
        mutateFixtureJson(root, 'identities/postinstall.private.json', (value) => {
          value.platforms.cv186x.installed.runtime.pop();
        });
      },
      expected: /postinstall cv186x: runtime inventory differs/,
    },
    {
      mutate(root) {
        const postinstall = readFixtureJson(root, 'identities/postinstall.private.json');
        const evidencePath = path.join(root, postinstall.platforms.bm1688.evidence[0].path);
        fs.appendFileSync(evidencePath, 'tamper');
      },
      expected: /postinstall bm1688 evidence: size mismatch/,
    },
    {
      mutate(root) {
        mutateFixtureJson(root, 'identities/postinstall.private.json', (value) => {
          const capturedAt = '2026-08-24T00:10:00.000Z';
          value.platforms.rk3576.capturedAt = capturedAt;
          value.platforms.rk3576.evidence[1].capturedAt = capturedAt;
        });
      },
      expected: /identity finalization postinstall: SHA-256 mismatch/,
    },
  ];
  for (const item of cases) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-identity-tamper-'));
    try {
      await createFixture(root);
      item.mutate(root);
      await assert.rejects(verifyPrivateEvidence({ evidenceRoot: root }), item.expected);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  }
});

test('canonical package, runtime, model, and source identities are fully bound', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-canonical-identity-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { withE2E: true });
  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  const canonicalPath = path.join(root, 'canonical-identity.json');
  const cases = [
    {
      mutate(value) { value.source.tree = '0'.repeat(40); },
      expected: /canonical source: candidate source identity differs/,
    },
    {
      mutate(value) { value.observations[0].package.sha256 = '0'.repeat(64); },
      expected: /canonical bm1688: package identity differs/,
    },
    {
      mutate(value) { value.observations[1].runtimeIdentity.libraries[0].sha256 = '0'.repeat(64); },
      expected: /canonical cv186x: runtime library inventory differs/,
    },
    {
      mutate(value) { value.observations[2].modelIdentity.model.sha256 = '0'.repeat(64); },
      expected: /canonical rk3576: model identity differs/,
    },
  ];
  for (const item of cases) {
    const canonical = canonicalFor(verified);
    item.mutate(canonical);
    fs.writeFileSync(canonicalPath, JSON.stringify(canonical, null, 2), 'utf8');
    await assert.rejects(
      verifyPrivateEvidence({ evidenceRoot: root, canonicalPath }),
      item.expected,
    );
  }
});

test('E2E evidence is bound to postinstall snapshot, RC source, and protocol artifacts', async () => {
  const cases = [
    {
      mutate(root) {
        mutateFixtureJson(root, 'e2e-integrity.private.json', (value) => {
          value.identity.postinstall.sha256 = '0'.repeat(64);
        });
      },
      expected: /E2E identity postinstall: SHA-256 mismatch/,
    },
    {
      mutate(root) {
        mutateE2EResult(root, 'bm1688', (value) => { value.source.commit = '0'.repeat(40); });
      },
      expected: /E2E bm1688 source: source commit or tree differs/,
    },
    {
      mutate(root) {
        mutateE2EResult(root, 'cv186x', (value) => { value.deviceLabel = 'wrong-label'; });
      },
      expected: /E2E cv186x result: device label differs/,
    },
    {
      mutate(root) {
        mutateE2EResult(root, 'rk3576', (value) => {
          value.startedAtUtc = '2026-08-23T23:58:00.000Z';
        });
      },
      expected: /E2E rk3576 result: postinstall candidate identity was captured after/,
    },
    {
      mutate(root) {
        mutateE2EResult(root, 'bm1688', (value) => {
          value.source.scenarioSha256 = '0'.repeat(64);
        });
      },
      expected: /E2E bm1688 source: scenario identity differs/,
    },
  ];
  for (const item of cases) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-e2e-binding-'));
    try {
      await createFixture(root, { withE2E: true });
      item.mutate(root);
      await assert.rejects(verifyPrivateEvidence({ evidenceRoot: root }), item.expected);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  }
});

test('runtime gate evidence rejects post-hoc failure claims and execution after first failure', async () => {
  const cases = [
    {
      options: { failedPlatform: 'bm1688' },
      mutate(root) {
        mutateFixtureJson(root, 'platforms/bm1688/capacity/metrics.json', (value) => {
          value.bottleneck = null;
        });
      },
      expected: /capacity boundary: raw runtime bottleneck marker is missing/,
    },
    {
      options: { failedPlatform: 'cv186x' },
      mutate(root) {
        mutateFixtureJson(root, 'platforms/cv186x/capacity/metrics.json', (value) => {
          value.bottleneck.reason = 'vlm fpsRatio 0.7 < 0.8';
        });
      },
      expected: /cv186x runtime gate: runtime stop reason differs from the recomputed threshold gates/,
    },
    {
      options: {},
      mutate(root) {
        mutateFixtureJson(root, 'platforms/rk3576/capacity/metrics.json', (value) => {
          for (const sample of value.samples.filter((item) => item.stepIndex === 6)) {
            const channel = sample.channels[0];
            channel.missing = true;
            channel.telemetryMissing = true;
            channel.vlmCompletionActionId = 'PDA_00003';
            delete channel.primaryProcessTotal;
            delete channel.completionActionId;
            delete channel.expectedCompletionActionId;
            channel.nodeDurationInfos = [];
          }
          value.bottleneck = {
            stepIndex: 6,
            stepNumber: 7,
            channels: 7,
            targetChannels: 7,
            phase: 'hold',
            reason: 'vlm missingRate 1 > 0',
          };
        });
      },
      expected: /capacity boundary: formal execution continued after the first gate failure/,
    },
    {
      options: {},
      mutate(root) {
        mutateFixtureJson(root, 'platforms/bm1688/capacity/summary.json', (value) => {
          value.overallPass = false;
          value.capacityMeasured = false;
          value.capacityExecutionBlocked = true;
        });
      },
      expected: /bm1688 boundary: all-pass result was mislabeled as an execution block/,
    },
  ];
  for (const item of cases) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-runtime-gate-'));
    try {
      await createFixture(root, item.options);
      item.mutate(root);
      await assert.rejects(verifyPrivateEvidence({ evidenceRoot: root }), item.expected);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  }
});

test('readiness claims are bound to the actual first hold inventory and task-local counter', async () => {
  const cases = [
    {
      mutate(value) {
        value.steps[2].currentVlmBindings[0].channelId = 'declared-only';
        value.steps[2].vlmReadiness.bindings[0].channelId = 'declared-only';
      },
      expected: /declared new VLM bindings differ from the actual hold inventory/,
    },
    {
      mutate(value) {
        const step = value.steps[3];
        const binding = step.vlmReadiness.bindings[0];
        const first = value.samples.find((sample) => sample.stepIndex === step.index
          && sample.phase === 'hold');
        const channel = first.channels.find((item) => item.channelId === binding.channelId);
        channel.primaryProcessTotal = binding.baselineTotal;
      },
      expected: /first hold sample does not continue the task-local readiness counter/,
    },
    {
      mutate(value) {
        value.steps[4].vlmReadiness.bindings[0].taskId = 'readiness-only';
      },
      expected: /readiness bindings differ from the actual newly added hold routes/,
    },
  ];
  for (const item of cases) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-readiness-binding-'));
    try {
      await createFixture(root);
      mutateFixtureJson(root, 'platforms/bm1688/capacity/metrics.json', item.mutate);
      await assert.rejects(
        verifyPrivateEvidence({ evidenceRoot: root }),
        (error) => {
          assert.match(error.message, item.expected);
          assert.doesNotMatch(error.message, new RegExp(escapeRegExp(root), 'i'));
          return true;
        },
      );
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  }
});

test('integrity tampering is rejected without exposing the evidence root', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-tamper-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root);
  const integrityPath = path.join(root, 'evidence-integrity.private.json');
  const integrity = JSON.parse(fs.readFileSync(integrityPath, 'utf8'));
  integrity.files['platforms/rk3576/capacity/report.html'].sha256 = '0'.repeat(64);
  fs.writeFileSync(integrityPath, JSON.stringify(integrity, null, 2), 'utf8');

  await assert.rejects(
    verifyPrivateEvidence({ evidenceRoot: root }),
    (error) => {
      assert.match(error.message, /rk3576 integrity: report\.html SHA-256 mismatch/);
      assert.doesNotMatch(error.message, new RegExp(escapeRegExp(root), 'i'));
      return true;
    },
  );
});

test('readiness timeout remains an execution block and is not a capacity failure', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-block-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { blockedPlatform: 'rk3576' });

  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  const blocked = verified.results.find((item) => item.platformId === 'rk3576');
  assert.equal(blocked.boundary.classification, 'execution-block');
  assert.equal(blocked.boundary.verifiedPassingChannels, 7);
  assert.equal(blocked.boundary.firstFailureChannels, 8);
  assert.equal(blocked.summary.capacityExecutionBlocked, true);
  assert.equal(blocked.summary.capacityMeasured, false);
  assert.equal(blocked.summary.vlmReadiness.at(-1).status, 'timed-out');
});

test('missing hold telemetry remains a gate failure when readiness evidence was valid', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-gate-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root, { failedPlatform: 'cv186x' });

  const verified = await verifyPrivateEvidence({ evidenceRoot: root });
  const failed = verified.results.find((item) => item.platformId === 'cv186x');
  assert.equal(failed.boundary.classification, 'gate-first-failure');
  assert.equal(failed.boundary.verifiedPassingChannels, 7);
  assert.equal(failed.boundary.firstFailureChannels, 8);
  assert.equal(failed.boundary.firstFailureCategory, 'missing');
  assert.equal(failed.summary.capacityExecutionBlocked, false);
  assert.equal(failed.summary.maxStableChannelsExact, true);
});

test('an incomplete hold window is rejected without exposing the evidence root', async (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vlm-private-verifier-window-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  await createFixture(root);
  const metricsPath = path.join(root, 'platforms', 'bm1688', 'capacity', 'metrics.json');
  const metrics = JSON.parse(fs.readFileSync(metricsPath, 'utf8'));
  const removeAt = metrics.samples.findIndex((sample) => sample.stepIndex === 3 && sample.phase === 'hold');
  metrics.samples.splice(removeAt, 1);
  fs.writeFileSync(metricsPath, JSON.stringify(metrics, null, 2), 'utf8');

  await assert.rejects(
    verifyPrivateEvidence({ evidenceRoot: root }),
    (error) => {
      assert.match(error.message, /bm1688 hold window: hold sample count differs/);
      assert.doesNotMatch(error.message, new RegExp(escapeRegExp(root), 'i'));
      return true;
    },
  );
});

async function createFixture(root, {
  blockedPlatform = null,
  failedPlatform = null,
  withE2E = false,
} = {}) {
  createIdentityFixture(root);
  const protocolDir = path.join(root, 'protocol');
  const inputDir = path.join(protocolDir, 'inputs');
  fs.mkdirSync(inputDir, { recursive: true });
  const videoPath = path.join(inputDir, 'controlled.mp4');
  fs.writeFileSync(videoPath, 'private verifier fixture video');

  const platformRecords = [];
  for (const platform of PLATFORMS) {
    const platformProtocolDir = path.join(protocolDir, platform.id);
    fs.mkdirSync(platformProtocolDir, { recursive: true });
    const scenarioPath = path.join(platformProtocolDir, 'scenario.yml');
    const templatePath = path.join(platformProtocolDir, 'algorithm-template.json');
    fs.writeFileSync(scenarioPath, scenarioYaml(platform), 'utf8');
    fs.writeFileSync(templatePath, JSON.stringify(templateFor(platform), null, 2), 'utf8');
    platformRecords.push({
      platform: platform.name,
      scenario: artifactRecord(root, scenarioPath),
      template: {
        ...artifactRecord(root, templatePath),
        algorithmId: platform.algorithmId,
      },
    });
  }

  const manifest = {
    schemaVersion: '1.0',
    protocolId: 'fixture-vlm-protocol',
    generatedAt: '2026-08-23T23:40:00.000Z',
    visibility: 'private',
    status: 'prepared-not-executed',
    tool: {
      repository: 'https://example.invalid/cosmo-edge.git',
      commit: RC_COMMIT,
      tree: RC_TREE,
    },
    candidate: {
      status: 'pending-final-rc',
      sourceIdentity: {
        repository: 'https://example.invalid/cosmo-edge.git',
        ref: 'origin/main',
        commit: RC_COMMIT,
        tree: RC_TREE,
        declaredVersion: RC_VERSION,
      },
      finalPackage: { status: 'not-frozen', sha256: null, sizeBytes: null },
    },
    workload: {
      type: 'vlm',
      channelSequence: CHANNELS,
      holdSecPerLevel: 60,
      sampleIntervalSec: 3,
      channelMode: 'local',
      repeatCount: 0,
      targetFps: 0.1,
      prompt: {
        text: PROMPT,
        advancedMode: '1',
        generationStyle: 'standard',
        provider: 'local_model',
      },
      video: artifactRecord(root, videoPath),
      thresholds: {
        vlm: { minFpsRatio: 0.8, maxMissingRate: 0, avgDiscardRate: 0.05 },
        pass: { avgDiscardRate: 0.05, maxPacketDiscardRate: 0.01, maxDiskUsedPercent: 99 },
      },
    },
    platforms: platformRecords,
    identityAssertions: { finalRcClaimed: false },
  };
  fs.mkdirSync(path.join(root, 'protocol'), { recursive: true });
  fs.writeFileSync(
    path.join(root, 'protocol', 'protocol-manifest.private.json'),
    JSON.stringify(manifest, null, 2),
    'utf8',
  );

  for (const platform of PLATFORMS) {
    const capacityDir = path.join(root, 'platforms', platform.id, 'capacity');
    await new ReportWriter(capacityDir).write(metricsFor(platform, {
      blocked: platform.id === blockedPlatform,
      failed: platform.id === failedPlatform,
    }));
  }

  const files = {};
  for (const platform of PLATFORMS) {
    for (const fileName of ['metrics.json', 'summary.json', 'report.html']) {
      const relative = ['platforms', platform.id, 'capacity', fileName].join('/');
      const file = path.join(root, ...relative.split('/'));
      files[relative] = artifactRecord(root, file);
      delete files[relative].path;
    }
  }
  fs.writeFileSync(
    path.join(root, 'evidence-integrity.private.json'),
    JSON.stringify({ schemaVersion: '1.0', files }, null, 2),
    'utf8',
  );
  createFinalizationFixture(root);
  if (withE2E) createE2EFixture(root);
}

function createIdentityFixture(root) {
  const candidatesDir = path.join(root, 'candidates');
  const identitiesDir = path.join(root, 'identities');
  fs.mkdirSync(candidatesDir, { recursive: true });
  fs.mkdirSync(identitiesDir, { recursive: true });
  const source = {
    repository: 'https://example.invalid/cosmo-edge.git',
    ref: 'origin/main',
    commit: RC_COMMIT,
    tree: RC_TREE,
    declaredVersion: RC_VERSION,
    releaseStateAtFreeze: RELEASE_STATE,
    qualification: 'fixture exact release candidate',
  };
  const packages = {};
  const packageFiles = {};
  for (const platform of PLATFORMS) {
    const directory = path.join(candidatesDir, platform.id + '-candidate');
    fs.mkdirSync(directory, { recursive: true });
    const fileName = `cosmo-${RC_VERSION}-${platform.id}.tar.gz`;
    const file = path.join(directory, fileName);
    fs.writeFileSync(file, 'candidate package for ' + platform.id + '\n', 'utf8');
    const artifact = artifactRecord(root, file);
    packages[platform.id] = {
      directory: path.relative(root, directory).split(path.sep).join('/'),
      file: fileName,
      sizeBytes: artifact.sizeBytes,
      sha256: artifact.sha256,
      targetChipSidecar: platform.id,
      archiveTargetChip: platform.id,
      filenameMd5Verified: true,
      contentPolicyVerified: true,
      ...(platform.id === 'rk3576' ? { workflowArtifactVerified: true } : {}),
      localFileVerified: true,
    };
    packageFiles[platform.id] = { file, artifact };
  }
  const packageManifest = {
    schemaVersion: '1.0',
    generatedAt: '2026-08-23T23:50:00.000Z',
    status: 'three-local-verified',
    visibility: 'private',
    source,
    builders: {
      sophon: {
        execution: 'fixture-local-builder',
        image: 'fixture/sophon:latest',
        digest: 'sha256:' + '3'.repeat(64),
        buildProfile: 'public-runtime',
      },
      rockchip: {
        execution: 'fixture-workflow-builder',
        image: 'fixture/rockchip:latest',
        digest: 'sha256:' + '4'.repeat(64),
        buildProfile: 'public-runtime',
      },
    },
    packages,
    deploymentPolicy: {
      independentTargetsMayRunInParallel: true,
      requireSidecarAndArchiveChipMatch: true,
      allowEventMediaCleanupForSpace: false,
      requireInstalledBinaryHashEvidence: true,
    },
  };
  const packageManifestPath = path.join(candidatesDir, 'package-manifest.private.json');
  fs.writeFileSync(packageManifestPath, JSON.stringify(packageManifest, null, 2), 'utf8');
  const packageManifestArtifact = artifactRecord(root, packageManifestPath);

  const platforms = {};
  for (const [platformIndex, platform] of PLATFORMS.entries()) {
    const deviceLabel = 'fixture-' + (platformIndex + 1);
    const engineFile = path.join(
      candidatesDir,
      'extracted-identity',
      platform.id,
      RC_VERSION,
      'bin',
      'cosmo-engine',
    );
    fs.mkdirSync(path.dirname(engineFile), { recursive: true });
    fs.writeFileSync(engineFile,
      'fixture engine ' + (platform.id === 'rk3576' ? 'rockchip' : 'sophon') + '\n',
      'utf8');
    const engineArtifact = artifactRecord(root, engineFile);
    const runtimeNames = platform.id === 'rk3576'
      ? ['librkllmrt.so', 'librknnrt.so']
      : ['libbmlib.so', 'libbmrt.so', 'libbmrt.so.1.0'];
    const runtime = runtimeNames.map((name) => ({
      name,
      sha256: fixtureSha(platform.id + ':runtime:' + name),
      candidateArchiveMember: `${RC_VERSION}/lib/${name}`,
      matchesCandidatePackage: true,
    }));
    const modelNames = platform.id === 'rk3576'
      ? { model: 'model.rkllm', vision: 'vision.rknn', tokenizer: 'tokenizer.json', config: 'config.json' }
      : { model: 'model.nn', tokenizer: 'tokenizer.json', config: 'config.json' };
    const vlm = Object.fromEntries(Object.entries(modelNames).map(([key, name]) => [key, {
      name,
      sha256: fixtureSha(platform.id + ':vlm:' + key),
      ...(platform.id === 'rk3576' && key === 'model'
        ? { freshlyRecomputedInPostinstallEvidence: true }
        : {}),
    }]));
    const engine = {
      name: 'cosmo-engine',
      sha256: engineArtifact.sha256,
      candidateArchiveMember: `${RC_VERSION}/bin/cosmo-engine`,
      matchesCandidatePackage: true,
    };

    const identityEvidencePath = path.join(root, `${platform.id}-postinstall-identity.private.tsv`);
    const runtimeEvidencePath = path.join(root, `${platform.id}-postinstall-runtime.private.tsv`);
    fs.writeFileSync(identityEvidencePath, Object.values(vlm)
      .map((item) => `${item.sha256}  /private/${item.name}`)
      .join('\n') + '\n', 'utf8');
    fs.writeFileSync(runtimeEvidencePath, [engine, ...runtime]
      .map((item) => `${item.sha256}  /private/${item.name}`)
      .concat(RC_VERSION)
      .join('\n') + '\n', 'utf8');
    platforms[platform.id] = {
      deviceLabel,
      capturedAt: '2026-08-23T23:59:00.000Z',
      package: {
        artifactPath: packageFiles[platform.id].artifact.path,
        sizeBytes: packageFiles[platform.id].artifact.sizeBytes,
        sha256: packageFiles[platform.id].artifact.sha256,
        manifestSha256MatchesLocalArtifact: true,
      },
      installed: { version: RC_VERSION, engine, runtime, vlm },
      evidence: [
        { ...artifactRecord(root, identityEvidencePath), capturedAt: '2026-08-23T23:58:00.000Z' },
        { ...artifactRecord(root, runtimeEvidencePath), capturedAt: '2026-08-23T23:59:00.000Z' },
        engineArtifact,
      ],
    };
  }
  const postinstall = {
    schemaVersion: '1.0',
    visibility: 'private',
    status: 'verified-postinstall-exact-candidate',
    generatedAt: '2026-08-23T23:59:30.000Z',
    captureTimeBasis: 'evidence-file-last-write-time-utc',
    candidateSource: {
      ...source,
      manifestEvidence: {
        path: 'candidates/package-manifest.private.json',
        sha256: packageManifestArtifact.sha256,
        generatedAt: packageManifest.generatedAt,
      },
    },
    platforms,
    crossChecks: {
      candidateSourceCommitAndTreePresent: true,
      allLocalPackageSha256MatchManifest: true,
      allInstalledEngineSha256MatchCandidateArchive: true,
      allInstalledRuntimeSha256MatchCandidateArchive: true,
      bm1688AndCv186xEngineSha256: platforms.bm1688.installed.engine.sha256,
      rk3576EngineSha256: platforms.rk3576.installed.engine.sha256,
      rk3576ModelSha256FreshlyRecomputed: platforms.rk3576.installed.vlm.model.sha256,
      allRequiredArtifactsPresent: true,
      allRequiredHashesConsistent: true,
    },
    privacy: {
      containsCredentials: false,
      containsNetworkEndpoints: false,
      containsDeviceSerials: false,
      containsStreamAddresses: false,
      deviceReferencesLimitedToLabels: PLATFORMS.map((_, index) => 'fixture-' + (index + 1)),
    },
  };
  fs.writeFileSync(
    path.join(identitiesDir, 'postinstall.private.json'),
    JSON.stringify(postinstall, null, 2),
    'utf8',
  );
}

function createFinalizationFixture(root) {
  const protocolPath = path.join(root, 'protocol', 'protocol-manifest.private.json');
  const packagePath = path.join(root, 'candidates', 'package-manifest.private.json');
  const postinstallPath = path.join(root, 'identities', 'postinstall.private.json');
  const protocol = JSON.parse(fs.readFileSync(protocolPath, 'utf8'));
  const packages = JSON.parse(fs.readFileSync(packagePath, 'utf8'));
  const postinstall = JSON.parse(fs.readFileSync(postinstallPath, 'utf8'));
  fs.writeFileSync(
    path.join(root, 'identities', 'finalization.private.json'),
    JSON.stringify({
      schemaVersion: '1.0',
      visibility: 'private',
      status: 'final-rc-bound-and-executed',
      finalizedAt: '2026-08-24T00:40:00.000Z',
      source: { commit: RC_COMMIT, tree: RC_TREE },
      preparationProtocol: { ...artifactRecord(root, protocolPath), status: protocol.status },
      packageManifest: { ...artifactRecord(root, packagePath), status: packages.status },
      postinstall: { ...artifactRecord(root, postinstallPath), status: postinstall.status },
      packages: Object.fromEntries(PLATFORMS.map((platform) => [platform.id, {
        sha256: packages.packages[platform.id].sha256,
        sizeBytes: packages.packages[platform.id].sizeBytes,
      }])),
    }, null, 2),
    'utf8',
  );
}

function createE2EFixture(root) {
  const implementationFiles = {
    helper: path.join(root, 'invoke-vlm-e2e.mjs'),
    privilegedWrapper: path.join(root, 'invoke-cosmo-service.ps1'),
    tests: path.join(root, 'invoke-vlm-e2e.test.mjs'),
  };
  fs.writeFileSync(implementationFiles.helper, 'fixture helper\n', 'utf8');
  fs.writeFileSync(implementationFiles.privilegedWrapper, '# fixture fixed wrapper\n', 'utf8');
  fs.writeFileSync(implementationFiles.tests, 'fixture tests\n', 'utf8');
  const implementation = Object.fromEntries(Object.entries(implementationFiles)
    .map(([key, file]) => [key, artifactRecord(root, file)]));
  const protocol = JSON.parse(fs.readFileSync(
    path.join(root, 'protocol', 'protocol-manifest.private.json'),
    'utf8',
  ));
  const postinstall = JSON.parse(fs.readFileSync(
    path.join(root, 'identities', 'postinstall.private.json'),
    'utf8',
  ));

  const inventory = {};
  for (const [platformIndex, platform] of PLATFORMS.entries()) {
    const e2eDir = path.join(root, 'platforms', platform.id, 'e2e');
    fs.mkdirSync(e2eDir, { recursive: true });
    const restartPath = path.join(e2eDir, 'restart.private.log');
    fs.writeFileSync(restartPath, [
      'schemaVersion=1',
      'mode=RestartAndVerify',
      'wrapperExitCode=0',
      'serviceActiveMarkerObserved=true',
      'failureClass=NONE',
      'sshCredentialTransport=restricted-askpass',
      'sudoCredentialTransport=stdin',
      'rawRemoteOutputStored=false',
      'COSMO_SERVICE_ACTIVE',
      '',
    ].join('\n'), 'utf8');
    const restart = artifactRecord(root, restartPath);
    const resultPath = path.join(e2eDir, 'result.private.json');
    const result = {
      schemaVersion: 1,
      evidenceKind: 'cosmoedge-v1.1-vlm-minimal-e2e',
      privateEvidence: true,
      deviceLabel: postinstall.platforms[platform.id].deviceLabel,
      platform: platform.id,
      startedAtUtc: new Date(Date.UTC(2026, 7, 24, 0, 20 + platformIndex, 0)).toISOString(),
      finishedAtUtc: new Date(Date.UTC(2026, 7, 24, 0, 21 + platformIndex, 0)).toISOString(),
      source: {
        commit: RC_COMMIT,
        tree: RC_TREE,
        helperSha256: implementation.helper.sha256,
        scenarioSha256: protocol.platforms[platformIndex].scenario.sha256,
        templateSha256: protocol.platforms[platformIndex].template.sha256,
        videoSha256: protocol.workload.video.sha256,
        importedToolSha256: {
          cosmoClient: fixtureSha('cosmo-client'),
          channelManager: fixtureSha('channel-manager'),
          scenarioPackage: fixtureSha('scenario-package'),
          taskStrategies: fixtureSha('task-strategies'),
        },
        privilegedServiceWrapperSha256: implementation.privilegedWrapper.sha256,
      },
      status: 'PASS',
      errorCode: null,
      protocol: {
        completionCounterScope: 'task-local',
      },
      acceptance: {
        modelLoad: { status: 'PASS' },
        taskCreation: { status: 'PASS' },
        inferenceBeforeRestart: {
          status: 'PASS',
          taskObserved: true,
          completionCounterScope: 'task-local',
          counterDelta: 1,
          completionAdvanced: true,
          qwenLatencyObserved: true,
          qwenLatencyMs: 900,
        },
        eventOrAlarmOutput: {
          status: 'PASS',
          observed: true,
          matchingEventCount: 1,
          inferencePayloadObserved: true,
          inferenceTextPersisted: false,
          eventRowsPersisted: false,
        },
        serviceRestart: {
          status: 'PASS',
          wrapperExitCode: 0,
          serviceActiveMarkerObserved: true,
          privateRemoteEvidenceSha256: restart.sha256,
        },
        taskRecoveryAfterRestart: {
          status: 'PASS',
          taskObserved: true,
          completionCounterScope: 'task-local',
          counterDelta: 1,
          completionAdvanced: true,
          qwenLatencyObserved: true,
          qwenLatencyMs: 910,
        },
        cleanup: {
          status: 'PASS',
          taskDisabled: true,
          ownedChannelDeleted: true,
          httpRecoverySucceeded: true,
        },
        layoutIntegrity: {
          status: 'PASS',
          initialLayoutMatched: true,
          finalLayoutMatched: true,
          globalLayoutMutationApplied: false,
        },
      },
      privacy: {
        endpointStored: false,
        credentialsStored: false,
        serialNumberStored: false,
        internalIdentifiersStored: false,
        inferenceTextStored: false,
        eventRowsStored: false,
      },
    };
    fs.writeFileSync(resultPath, JSON.stringify(result, null, 2), 'utf8');
    inventory[platform.id] = {
      result: artifactRecord(root, resultPath),
      restartLog: { ...restart, sanitized: true },
    };
  }

  fs.writeFileSync(path.join(root, 'e2e-integrity.private.json'), JSON.stringify({
    schemaVersion: 1,
    evidenceKind: 'cosmoedge-v1.1-vlm-three-platform-e2e-integrity',
    privateEvidence: true,
    conclusion: 'all-platforms-pass',
    inventory,
    implementation,
    identity: {
      packageManifest: artifactRecord(
        root,
        path.join(root, 'candidates', 'package-manifest.private.json'),
      ),
      postinstall: artifactRecord(
        root,
        path.join(root, 'identities', 'postinstall.private.json'),
      ),
      finalization: artifactRecord(
        root,
        path.join(root, 'identities', 'finalization.private.json'),
      ),
    },
  }, null, 2), 'utf8');
}

function scenarioYaml(platform) {
  const profile = CHANNELS.map((channels) => `  - channels: ${channels}\n    holdSec: 60`).join('\n');
  return `name: ${platform.id}-vlm-private-fixture
sampleIntervalSec: 3
channels:
  mode: local
  repeatCount: 0
  sources:
    - name: controlled
      file: ../inputs/controlled.mp4
tasks:
  - id: vlm
    displayName: VLM
    type: vlm
    algorithmId: "${platform.algorithmId}"
    scheduleId: fixture
    template: algorithm-template.json
    targetFps: 0.1
bindings:
  - task: vlm
    channels: all
loadProfile:
${profile}
thresholds:
  pass:
    avgDiscardRate: 0.05
    maxPacketDiscardRate: 0.01
    maxDiskUsedPercent: 99
  taskTypes:
    vlm:
      minFpsRatio: 0.8
      maxMissingRate: 0
      avgDiscardRate: 0.05
`;
}

function templateFor(platform) {
  const actions = [{
    actionId: platform.templateActionId,
    name: 'Qwen3VLWorker',
    configObject: {
      params: [
        { key: 'vlmProvider', value: 'local_model' },
        { key: 'fps', value: '0.1' },
        { key: 'advanced_mode', value: '1' },
        { key: 'keywords', value: PROMPT },
        { key: 'generationStyle', value: 'standard' },
      ],
    },
  }];
  if (platform.completionActionId === 'BA_00004') {
    actions.push({ actionId: 'BA_00004', name: 'TaskLocalVlmCompletion' });
  }
  return {
    algorithmId: platform.algorithmId,
    algorithmCode: platform.algorithmId,
    algorithmName: 'VLM fixture',
    taskConfig: { params: [], areas: [] },
    algorithmProcessdata: JSON.stringify(actions),
  };
}

function metricsFor(platform, { blocked = false, failed = false } = {}) {
  const loadProfile = CHANNELS.map((channels, index) => ({ index, channels, holdSec: 60 }));
  const steps = loadProfile.map((step) => {
    const start = stepStart(step.index);
    const currentVlmBindings = [{
      taskId: 'private-task',
      taskKey: 'vlm',
      channelId: `private-channel-${step.channels}`,
      completionActionId: platform.completionActionId,
    }];
    return {
      ...step,
      currentVlmBindings,
      vlmReadiness: {
        stepIndex: step.index,
        stepNumber: step.index + 1,
        targetChannels: step.channels,
        ready: true,
        status: 'ready',
        probes: 2,
        timeoutSec: 180,
        pollIntervalSec: 3,
        startedAt: new Date(start - 3_500).toISOString(),
        endedAt: new Date(start - 500).toISOString(),
        elapsedMs: 3_000,
        bindings: [{
          ...currentVlmBindings[0],
          baselineTotal: 10,
          currentTotal: 11,
          completionAdvanced: true,
          qwenLatencyMs: 900,
          ready: true,
          pendingReasons: [],
        }],
      },
    };
  });
  const samples = steps.flatMap((step) => {
    const start = stepStart(step.index);
    return Array.from({ length: 20 }, (_, tickIndex) => ({
      ts: start + tickIndex * 3_000,
      stepIndex: step.index,
      phase: 'hold',
      activeChannels: step.channels,
      targetChannels: step.channels,
      activeTaskBindings: step.channels,
      channels: Array.from({ length: step.channels }, (_, channelIndex) => {
        const baseline = (step.index + 1) * 100 + channelIndex * 10;
        return {
          taskId: 'private-task',
          taskKey: 'vlm',
          taskDisplayName: 'VLM',
          taskType: 'vlm',
          algorithmId: platform.algorithmId,
          channelId: `private-channel-${channelIndex + 1}`,
          targetFps: 0.1,
          measuredFps: 0.1,
          telemetryMissing: false,
          missing: false,
          primaryProcessTotal: baseline + Math.floor((tickIndex * 6) / 19),
          completionActionId: platform.completionActionId,
          expectedCompletionActionId: platform.completionActionId,
          pipelineMinFps: 0.1,
          fpsRatio: 1,
          discardRate: 0,
          nodeDurationInfos: [{ name: 'Qwen3VLWorker', durationAvgUs: 900_000 }],
        };
      }),
      hardware: {
        packetDiscardUtilization: { usedPercent: 0 },
        eMMCUtilization: { usedPercent: 1 },
        cpuUtilization: { usedPercent: 1 },
        generalMemoryUtilization: { usedPercent: 1 },
      },
    }));
  });
  const metrics = {
    scenarioName: `${platform.id}-vlm-private-fixture`,
    platform: platform.name,
    algorithmId: platform.algorithmId,
    algorithmName: 'VLM fixture',
    targetFps: 0.1,
    sampleIntervalSec: 3,
    tasks: [{
      id: 'vlm',
      taskKey: 'vlm',
      displayName: 'VLM',
      type: 'vlm',
      algorithmId: platform.algorithmId,
      targetFps: 0.1,
      vlmCompletionActionId: platform.completionActionId,
    }],
    videoMode: 'local',
    status: 'completed',
    error: null,
    thresholds: {
      pass: { avgDiscardRate: 0.05, maxPacketDiscardRate: 0.01, maxDiskUsedPercent: 99 },
      taskTypes: { vlm: { minFpsRatio: 0.8, maxMissingRate: 0, avgDiscardRate: 0.05 } },
    },
    profileMode: 'capacity',
    loadProfile,
    configuredLoadProfile: loadProfile,
    steps,
    samples,
    bottleneck: null,
    startedAt: '2026-08-24T00:00:00.000Z',
    endedAt: '2026-08-24T00:16:00.000Z',
  };
  if (blocked) {
    metrics.status = 'aborted';
    metrics.error = {
      message: 'fixture readiness timeout',
      atChannels: 8,
      atStepIndex: 7,
    };
    metrics.samples = metrics.samples.filter((sample) => sample.stepIndex < 7);
    metrics.steps[7].vlmReadiness = {
      stepIndex: 7,
      stepNumber: 8,
      targetChannels: 8,
      ready: false,
      status: 'timed-out',
      probes: 60,
      timeoutSec: 180,
      pollIntervalSec: 3,
      startedAt: new Date(stepStart(7) - 180_000).toISOString(),
      endedAt: new Date(stepStart(7)).toISOString(),
      elapsedMs: 180_000,
      bindings: [{
        taskId: 'private-task',
        taskKey: 'vlm',
        channelId: 'private-channel-8',
        completionActionId: platform.completionActionId,
        baselineTotal: 10,
        currentTotal: 10,
        completionAdvanced: false,
        qwenLatencyMs: null,
        ready: false,
        pendingReasons: ['fixture timeout'],
      }],
    };
  } else if (failed) {
    for (const sample of metrics.samples.filter((item) => item.stepIndex === 7)) {
      const missing = sample.channels.at(-1);
      missing.missing = true;
      missing.telemetryMissing = true;
      missing.vlmCompletionActionId = platform.completionActionId;
      delete missing.primaryProcessTotal;
      delete missing.completionActionId;
      delete missing.expectedCompletionActionId;
      missing.nodeDurationInfos = [];
    }
    metrics.bottleneck = {
      stepIndex: 7,
      stepNumber: 8,
      channels: 8,
      targetChannels: 8,
      phase: 'hold',
      source: 'runtime-threshold',
      reason: 'vlm missingRate 1.000 > 0',
      gates: [{
        scope: 'task',
        taskKey: 'vlm',
        taskType: 'vlm',
        name: 'maxMissingRate',
        actual: 1,
        threshold: 0,
      }],
    };
  }
  return metrics;
}

function artifactRecord(root, file) {
  const body = fs.readFileSync(file);
  return {
    path: path.relative(root, file).split(path.sep).join('/'),
    sha256: crypto.createHash('sha256').update(body).digest('hex'),
    sizeBytes: body.length,
  };
}

function readFixtureJson(root, relative) {
  return JSON.parse(fs.readFileSync(path.join(root, ...relative.split('/')), 'utf8'));
}

function mutateFixtureJson(root, relative, mutate) {
  const file = path.join(root, ...relative.split('/'));
  const value = JSON.parse(fs.readFileSync(file, 'utf8'));
  mutate(value);
  fs.writeFileSync(file, JSON.stringify(value, null, 2), 'utf8');
}

function mutateE2EResult(root, platformId, mutate) {
  const manifestPath = path.join(root, 'e2e-integrity.private.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  const resultPath = path.join(root, ...manifest.inventory[platformId].result.path.split('/'));
  const result = JSON.parse(fs.readFileSync(resultPath, 'utf8'));
  mutate(result);
  fs.writeFileSync(resultPath, JSON.stringify(result, null, 2), 'utf8');
  manifest.inventory[platformId].result = artifactRecord(root, resultPath);
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2), 'utf8');
}

function fixtureSha(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

function canonicalFor(verified) {
  const canonical = {
    schemaVersion: 1,
    source: {
      repository: verified.identity.source.repository,
      branch: 'main',
      commit: verified.identity.source.commit,
      tree: verified.identity.source.tree,
      candidateVersion: verified.identity.source.declaredVersion,
      releaseStateAtFreeze: verified.identity.source.releaseStateAtFreeze,
    },
    input: { sha256: verified.protocol.workload.video.sha256 },
    prompt: { text: PROMPT },
    observations: verified.results.map((result) => {
      const privatePlatform = verified.identity.postinstall.platforms[result.platformId];
      const modelKeys = Object.keys(privatePlatform.installed.vlm);
      return {
        platformId: result.platformId,
        sourceMetricsSha256: result.hashes['metrics.json'].sha256,
        sourceSummarySha256: result.hashes['summary.json'].sha256,
        sourceReportSha256: result.hashes['report.html'].sha256,
        package: {
          version: verified.identity.source.declaredVersion,
          sha256: privatePlatform.package.sha256,
          sizeBytes: privatePlatform.package.sizeBytes,
          targetChip: result.platformId,
        },
        runtimeIdentity: {
          engineSha256: privatePlatform.installed.engine.sha256,
          libraries: privatePlatform.installed.runtime.map(({ name, sha256 }) => ({ name, sha256 })),
        },
        modelIdentity: Object.fromEntries(modelKeys.map((key) => [key, {
          name: privatePlatform.installed.vlm[key].name,
          sha256: privatePlatform.installed.vlm[key].sha256,
        }])),
        workload: { targetFpsPerChannel: 0.1 },
        gates: {
          minimumFpsRatio: 0.8,
          maximumMissingRate: 0,
          maximumAverageDiscardRate: 0.05,
        },
        capacityBoundary: { ...result.boundary },
        steps: result.stepSummaries.filter((step) => !step.skipped).map((step) => ({
          channels: step.channels,
          holdSeconds: 60,
          targetFpsPerChannel: 0.1,
          minimumActiveRouteFpsRatioObserved: step.taskStats[0].minFpsRatio,
          telemetryMissingRate: step.taskStats[0].maxMissingRate,
          averageDiscardRate: step.taskStats[0].avgDiscardRate,
          readiness: {
            status: 'PASS',
            probes: step.step.vlmReadiness.probes,
            elapsedMs: step.step.vlmReadiness.elapsedMs,
            taskLocalCompletionCounterAdvanced: true,
            qwenLatencyObserved: true,
          },
          result: step.pass ? 'PASS' : 'FAIL',
        })),
      };
    }),
  };
  if (verified.e2e?.status === 'verified') {
    canonical.claim = { threePlatformEndToEndAccepted: true };
    canonical.endToEndAcceptance = {
      candidatePackageSharedWithCapacityRun: true,
      platforms: verified.e2e.platforms.map((platform) => ({
        platformId: platform.platformId,
        status: 'PASS',
        evidenceSha256: platform.sourceEvidenceSha256,
        evidenceSizeBytes: platform.sourceEvidenceSizeBytes,
        modelLoad: 'PASS',
        taskCreation: 'PASS',
        validInferenceResult: 'PASS',
        eventOrAlarmOutput: 'PASS',
        taskRecoveryAfterServiceRestart: 'PASS',
      })),
    };
  }
  return canonical;
}

function stepStart(index) {
  return Date.UTC(2026, 7, 24, 0, index * 2, 0);
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
