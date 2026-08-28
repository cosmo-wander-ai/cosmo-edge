#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';

import { CosmoClient } from './cosmo-client.js';
import { Logger } from './logger.js';
import { installShutdownSignalHandlers } from './shutdown-signal.js';
import { readSecretLine } from './stdin-secret.js';
import { DeviceTrialExecutor } from './accuracy/device-trial.js';
import {
  runAccuracyDoctor,
  summarizeAccuracyAdmission,
} from './accuracy/doctor.js';
import { AccuracyEvidenceWriter, sanitizeSummary } from './accuracy/evidence.js';
import { summarizeAccuracyRun } from './accuracy/evaluator.js';
import {
  buildRunIdentity,
  buildToolIdentity,
  newRunId,
  sanitizedDeviceIdentity,
} from './accuracy/identity.js';
import { writePrivateFile } from './accuracy/private-files.js';
import {
  ACCURACY_PROTOCOL_VERSION,
  THRESHOLD_DIAGNOSTIC_SCHEMA_VERSION,
} from './accuracy/protocol.js';
import { renderAccuracyHtml } from './accuracy/report.js';
import { AccuracyRunner, resolveAccuracyExecution } from './accuracy/runner.js';
import { loadAccuracySuite } from './accuracy/suite.js';
import { initializeAccuracySuite } from './accuracy/suite-initializer.js';
import { applyThresholdValue, summarizeThresholdDiagnostic } from './accuracy/threshold.js';
import { sha256Buffer, sha256File, stableStringify } from './accuracy/utils.js';
import {
  accuracyExitCode,
  hasMeasuredFailures,
} from './accuracy/outcome.js';
import { assertAccuracySummary } from './accuracy/summary.js';
import { compareAccuracySummaries, writeAccuracyComparison } from './accuracy/comparison.js';

const BOOLEAN_OPTIONS = new Set([
  'help', 'password-stdin', 'resume', 'verbose',
]);
const REPEATABLE_OPTIONS = new Set(['candidate']);

export async function main(argv = process.argv.slice(2)) {
  try {
    const args = parseArgs(argv);
    if (!args.command || args.help || args.command === 'help') {
      printHelp();
      return args.help || args.command ? 0 : 2;
    }
    switch (args.command) {
      case 'doctor': return await commandDoctor(args);
      case 'init-suite': return commandInitSuite(args);
      case 'run': return await commandRun(args);
      case 'compare': return commandCompare(args);
      case 'render': return commandRender(args);
      case 'diagnose-threshold': return await commandDiagnoseThreshold(args);
      default:
        throw new Error(`unknown command: ${args.command}`);
    }
  } catch (error) {
    console.error(`[accuracy] ${error.message}`);
    return error.exitCode ?? 2;
  }
}

export function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const value = argv[index];
    if (value === '-h' || value === '--help') {
      args.help = true;
    } else if (value === '--password' || value.startsWith('--password=')) {
      throw new Error('cosmo-accuracy does not accept --password; use --password-stdin or --token-env');
    } else if (value.startsWith('--')) {
      const [inlineKey, inlineValue] = value.slice(2).split('=', 2);
      if (BOOLEAN_OPTIONS.has(inlineKey)) {
        if (inlineValue != null) throw new Error(`--${inlineKey} does not accept a value`);
        args[inlineKey] = true;
      } else {
        const next = inlineValue ?? argv[++index];
        if (next == null || next.startsWith('--')) throw new Error(`--${inlineKey} requires a value`);
        if (REPEATABLE_OPTIONS.has(inlineKey)) {
          args[inlineKey] = [...(args[inlineKey] ?? []), next];
        } else {
          args[inlineKey] = next;
        }
      }
    } else if (!args.command) {
      args.command = value;
    } else {
      throw new Error(`unexpected positional argument: ${value}`);
    }
  }
  return args;
}

async function commandDoctor(args) {
  requireArgs(args, ['suite', 'data-root', 'target-chip']);
  const options = accuracyExecutionOptions(args);
  const auth = args.device ? authFromArgs(args) : null;
  const result = await runAccuracyDoctor({
    suitePath: args.suite,
    dataRoot: args['data-root'],
    targetChip: args['target-chip'],
    device: args.device,
    auth,
    lang: args.lang ?? 'zh-CN',
    selection: options.selection,
    concurrency: options.concurrency,
  });
  for (const check of result.checks) {
    const label = check.status === 'PASS'
      ? 'OK '
      : check.blocking === false ? 'WARN' : check.status === 'UNVERIFIED' ? 'UNV' : 'ERR';
    console.log(`[${label}] ${check.name}: ${check.detail}`);
  }
  for (const warning of result.warnings) console.warn(`[WARN] ${warning}`);
  return result.status === 'PASS' ? 0 : 2;
}

function commandInitSuite(args) {
  requireArgs(args, ['input-root', 'output']);
  const result = initializeAccuracySuite({
    inputRoot: args['input-root'],
    outputDir: args.output,
    targetChip: args['target-chip'] ?? 'bm1688',
  });
  console.log(`Accuracy suite draft: ${result.outputDir}`);
  console.log(`Tasks: ${result.taskCount}, cases: ${result.caseCount}`);
  return 0;
}

async function commandRun(args) {
  const invocationStartedAt = new Date().toISOString();
  const invocationStartedMonotonic = performance.now();
  requireArgs(args, ['suite', 'data-root', 'target-chip', 'device', 'output']);
  const { profile, concurrency, selection } = accuracyExecutionOptions(args);
  const auth = authFromArgs(args);
  const admissionStartedAt = performance.now();
  const doctor = await runAccuracyDoctor({
    suitePath: args.suite,
    dataRoot: args['data-root'],
    targetChip: args['target-chip'],
    device: args.device,
    auth,
    lang: args.lang ?? 'zh-CN',
    selection,
    concurrency,
  });
  if (doctor.status !== 'PASS') {
    const failed = doctor.checks
      .filter((check) => check.status !== 'PASS')
      .map((check) => `${check.name}:${check.detail}`);
    throw new Error(`doctor failed before device mutation: ${failed.join('; ')}`);
  }
  const admissionMs = performance.now() - admissionStartedAt;
  const suite = doctor.suite;
  const selectedCases = doctor.admittedCases;
  if (!Array.isArray(selectedCases) || selectedCases.length === 0) {
    throw new Error('doctor returned no admitted cases');
  }
  const execution = resolveAccuracyExecution(suite, {
    profile,
    concurrency,
    selectedCases,
  });
  const preflightTool = buildToolIdentity();
  const abortController = new AbortController();
  const logger = new Logger({ verbose: args.verbose === true });
  const disposeSignals = installShutdownSignalHandlers(abortController, {
    onSignal: (error) => {
      process.exitCode = error.exitCode;
      logger.warn(`${error.message}; strict cleanup will run`);
    },
  });
  const client = new CosmoClient({
    base: args.device,
    ...auth,
    lang: args.lang ?? 'zh-CN',
    signal: abortController.signal,
  });
  try {
    await client.login();
    const device = sanitizedDeviceIdentity(await client.queryDeviceInfo());
    let runId = newRunId();
    let resumedCases = [];
    let runStartedAt = invocationStartedAt;
    let rawPartial = null;
    let invocations = [];
    if (args.resume) {
      rawPartial = readJson(path.join(args.output, 'run.partial.json'));
      if (rawPartial.status === 'completed') throw new Error('completed accuracy runs cannot be resumed');
      assertV4ResumePartial(rawPartial);
      runId = rawPartial.identity?.runId;
      if (!runId) throw new Error('resume partial evidence has no runId');
      runStartedAt = rawPartial.startedAt ?? invocationStartedAt;
      invocations = rawPartial.invocations ?? [];
    }
    invocations = [...invocations, {
      startedAt: invocationStartedAt,
      resumed: args.resume === true,
      admissionMs,
      activeDurationMs: 0,
    }];
    const identity = buildRunIdentity({
      suite,
      targetChip: args['target-chip'],
      device,
      runId,
      execution,
      tool: preflightTool,
    });
    const writer = new AccuracyEvidenceWriter(args.output, { resume: args.resume === true });
    writer.initialize(identity);
    if (args.resume) resumedCases = writer.loadPartial(identity).cases ?? [];
    const executor = new DeviceTrialExecutor({
      client,
      suite,
      runId,
      evidenceWriter: writer,
      logger,
      signal: abortController.signal,
    });
    const runner = new AccuracyRunner({
      suite,
      targetChip: args['target-chip'],
      executeTrial: (context) => executor.executeTrial(context),
      evidenceWriter: writer,
      logger,
      signal: abortController.signal,
      resumedCases,
      selectedCases,
      execution,
      runStartedAt,
      invocations,
      invocationElapsedMs: () => performance.now() - invocationStartedMonotonic,
    });
    const result = await runner.run();
    const completedAt = new Date().toISOString();
    const completedInvocations = invocations.map((item, index) => index === invocations.length - 1
      ? { ...item, activeDurationMs: performance.now() - invocationStartedMonotonic, endedAt: completedAt }
      : item);
    const activeDurationMs = completedInvocations.reduce(
      (sum, item) => sum + Number(item.activeDurationMs ?? 0),
      0,
    );
    const admission = sanitizeSummary(summarizeAccuracyAdmission(doctor));
    const summary = {
      ...summarizeAccuracyRun({
        suite,
        cases: result.cases,
        targetChip: args['target-chip'],
        execution: result.execution,
        admission,
      }),
      runId,
      startedAt: runStartedAt,
      endedAt: completedAt,
      invocations: completedInvocations,
      activeDurationMs,
      admissionMs: completedInvocations.reduce((sum, item) => sum + Number(item.admissionMs ?? 0), 0),
      wallDurationMs: Math.max(0, Date.parse(completedAt) - Date.parse(runStartedAt)),
      device: {
        model: device.model,
        softwareVersion: device.softwareVersion,
        hardwareVersion: device.hardwareVersion,
        fingerprint: device.deviceFingerprint,
      },
      toolIdentitySha256: sha256Buffer(Buffer.from(stableStringify(identity.tool))),
      repository: structuredClone(identity.tool.repository),
    };
    const paths = await writer.finalize({
      privateRun: {
        evidenceKind: 'cosmo-accuracy-run',
        executionOutcome: summary.executionOutcome,
        startedAt: runStartedAt,
        endedAt: summary.endedAt,
        invocations: completedInvocations,
        activeDurationMs,
        admissionMs: summary.admissionMs,
        wallDurationMs: summary.wallDurationMs,
        execution,
        admission: summary.admission,
        device,
        suite: {
          id: suite.id,
          displayName: suite.displayName,
          sourceMode: suite.sourceMode,
          targetChip: args['target-chip'],
        },
        tasks: suite.tasks.map((task) => ({
          id: task.id,
          kind: task.kind,
          algorithmId: task.algorithmId,
          algorithmCode: task.algorithmCode,
          scheduleId: task.scheduleId,
          taskConfigSha256: task.taskConfigSha256,
          taskConfig: task.taskConfig,
        })),
        cases: result.cases,
      },
      summary,
    });
    console.log(`Accuracy execution: ${summary.executionOutcome}`);
    if (hasMeasuredFailures(summary)) {
      console.warn('[accuracy] measurement completed with FAIL cases; inspect the report');
    }
    for (const warning of paths.warnings ?? []) console.warn(`[accuracy] ${warning}`);
    console.log(`Summary: ${paths.summaryPath}`);
    if (fs.existsSync(paths.htmlPath)) console.log(`Report: ${paths.htmlPath}`);
    return accuracyExitCode(summary);
  } finally {
    disposeSignals();
  }
}

function commandCompare(args) {
  requireArgs(args, ['reference', 'candidate', 'output']);
  if (!Array.isArray(args.candidate) || args.candidate.length === 0) {
    throw new Error('compare requires at least one --candidate');
  }
  const comparison = compareAccuracySummaries(
    readJson(args.reference),
    args.candidate.map(readJson),
  );
  const sources = [
    { role: 'reference', sha256: sha256File(path.resolve(args.reference)) },
    ...args.candidate.map((file, index) => ({
      role: `candidate-${index + 1}`,
      sha256: sha256File(path.resolve(file)),
    })),
  ];
  const paths = writeAccuracyComparison(args.output, comparison, sources);
  console.log(`Accuracy comparison: ${paths.outputDir}`);
  console.log(`Report: ${paths.htmlPath}`);
  return 0;
}

function commandRender(args) {
  requireArgs(args, ['input', 'output']);
  const summary = sanitizeSummary(readJson(args.input));
  assertAccuracySummary(summary);
  writePrivateFile(args.output, renderAccuracyHtml(summary));
  console.log(`Accuracy report: ${path.resolve(args.output)}`);
  return 0;
}

async function commandDiagnoseThreshold(args) {
  requireArgs(args, ['suite', 'data-root', 'target-chip', 'device', 'output', 'from', 'case']);
  const prior = readJson(args.from);
  const priorCase = (prior.cases ?? []).find((item) => item.id === args.case);
  if (!priorCase || priorCase.status !== 'FAIL') {
    throw new Error('threshold diagnostics require a measured FAIL case from run.private.json');
  }
  const selection = { profile: 'full', caseIds: [args.case] };
  const suite = loadAccuracySuite({
    suitePath: args.suite,
    dataRoot: args['data-root'],
    selection,
  });
  const item = suite.cases.find((candidate) => candidate.id === args.case);
  if (!item) throw new Error(`case not found in suite: ${args.case}`);
  const task = suite.tasks.find((candidate) => candidate.id === item.task);
  if (!task?.thresholdDiagnostic) throw new Error(`task ${item.task} has no thresholdDiagnostic`);
  const priorProtocolVersion = prior.identity?.protocolVersion;
  if (priorProtocolVersion !== suite.protocolVersion) {
    throw new Error('threshold diagnostic source protocol does not match the suite protocol');
  }
  const auth = authFromArgs(args);
  const doctor = await runAccuracyDoctor({
    suitePath: args.suite,
    dataRoot: args['data-root'],
    targetChip: args['target-chip'],
    device: args.device,
    auth,
    selection,
  });
  if (doctor.status !== 'PASS') throw new Error('doctor failed before threshold diagnostic');
  const abortController = new AbortController();
  const logger = new Logger({ verbose: args.verbose === true });
  const disposeSignals = installShutdownSignalHandlers(abortController, {
    onSignal: (error) => {
      process.exitCode = error.exitCode;
      logger.warn(`${error.message}; strict cleanup will run`);
    },
  });
  const client = new CosmoClient({ base: args.device, ...auth, signal: abortController.signal });
  try {
    await client.login();
    const device = sanitizedDeviceIdentity(await client.queryDeviceInfo());
    const runId = newRunId();
    const execution = resolveAccuracyExecution(suite, { selectedCases: [item] });
    const identity = {
      ...buildRunIdentity({
        suite,
        targetChip: args['target-chip'],
        device,
        runId,
        execution,
      }),
      evidenceKind: 'threshold-diagnostic',
      sourceRunSha256: sha256File(path.resolve(args.from)),
      caseId: item.id,
    };
    const writer = new AccuracyEvidenceWriter(args.output);
    writer.initialize(identity);
    const executor = new DeviceTrialExecutor({
      client,
      suite,
      runId,
      evidenceWriter: writer,
      logger,
      signal: abortController.signal,
    });
    const points = [];
    let globalAttemptNumber = 0;
    for (const threshold of task.thresholdDiagnostic.values) {
      const taskConfigOverride = applyThresholdValue(
        task.taskConfig,
        task.thresholdDiagnostic.parameterKeys,
        threshold,
      );
      const trials = [];
      for (let validTrialNumber = 1; validTrialNumber <= 3; validTrialNumber += 1) {
        let valid = null;
        for (let retry = 0; retry <= suite.defaults.infrastructureRetriesPerTrial; retry += 1) {
          globalAttemptNumber += 1;
          const trial = await executor.executeTrial({
            task,
            case: item,
            attemptNumber: globalAttemptNumber,
            validTrialNumber,
            taskConfigOverride,
          });
          trials.push(trial);
          if (['PASS', 'FAIL'].includes(trial.status)) { valid = trial; break; }
        }
        if (!valid) break;
      }
      points.push({ threshold, trials });
      await writer.writePartial({ caseId: item.id, points });
    }
    const diagnostic = summarizeThresholdDiagnostic(points);
    const compactDiagnostic = {
      ...diagnostic,
      points: diagnostic.points.map((point) => ({
        threshold: point.threshold,
        status: point.status,
        trialStatuses: point.trials.map((trial) => trial.status),
      })),
    };
    const diagnosticError = diagnostic.points.some((point) => point.status === 'ERROR');
    const admission = sanitizeSummary(summarizeAccuracyAdmission(doctor));
    const executionOutcome = diagnosticError ? 'ERROR' : 'COMPLETED';
    const diagnosticDocument = {
      schemaVersion: THRESHOLD_DIAGNOSTIC_SCHEMA_VERSION,
      protocolVersion: ACCURACY_PROTOCOL_VERSION,
      evidenceKind: 'cosmo-accuracy-threshold-diagnostic',
      evidenceStatus: 'DIAGNOSTIC',
      executionOutcome,
      executionReasons: [
        ...(diagnosticError ? ['threshold diagnostic has incomplete points'] : []),
      ],
      appliedAutomatically: false,
      runId,
      sourceRunSha256: identity.sourceRunSha256,
      caseId: item.id,
      parameterKeys: [...task.thresholdDiagnostic.parameterKeys],
      execution,
      admission,
      suite: {
        id: suite.id,
        displayName: suite.displayName,
        suiteSha256: suite.identity.suiteSha256,
        caseManifestSha256: suite.identity.caseManifestSha256,
        caseSetSha256: suite.identity.caseSetSha256,
        sourceMode: suite.sourceMode,
        targetChip: args['target-chip'],
        taskConfigSha256: structuredClone(suite.identity.taskConfigSha256),
      },
      points: compactDiagnostic.points,
      stablePassValues: compactDiagnostic.stablePassValues,
      stableFailValues: compactDiagnostic.stableFailValues,
      flakyValues: compactDiagnostic.flakyValues,
      device: {
        model: device.model,
        softwareVersion: device.softwareVersion,
        hardwareVersion: device.hardwareVersion,
        fingerprint: device.deviceFingerprint,
      },
      toolIdentitySha256: sha256Buffer(Buffer.from(stableStringify(identity.tool))),
      repository: structuredClone(identity.tool.repository),
    };
    const paths = await writer.finalizeDiagnostic({
      privateRun: {
        evidenceKind: 'cosmo-accuracy-threshold-diagnostic',
        sourceRunSha256: identity.sourceRunSha256,
        caseId: item.id,
        execution,
        admission,
        cases: [{ id: item.id, sha256: item.sha256 }],
        points,
      },
      diagnostic: diagnosticDocument,
    });
    console.log(`Threshold diagnostic: ${path.resolve(args.output)}`);
    for (const warning of paths.warnings ?? []) console.warn(`[accuracy] ${warning}`);
    if (fs.existsSync(paths.htmlPath)) console.log(`Report: ${paths.htmlPath}`);
    return executionOutcome === 'COMPLETED' ? 0 : 2;
  } finally {
    disposeSignals();
  }
}

function authFromArgs(args) {
  const tokenName = args['token-env'];
  const token = tokenName ? process.env[tokenName] : null;
  if (tokenName && !token) throw new Error(`token environment variable is empty or unset: ${tokenName}`);
  if (token) return { token };
  if (!args.user || !args['password-stdin']) {
    throw new Error('use --token-env, or provide --user with --password-stdin');
  }
  const password = readSecretLine();
  return { user: args.user, password };
}

function requireArgs(args, names) {
  const missing = names.filter((name) => !args[name]);
  if (missing.length) throw new Error(`missing required option(s): ${missing.map((name) => `--${name}`).join(', ')}`);
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(path.resolve(file), 'utf8'));
}

function csv(value) {
  if (!value) return null;
  return String(value).split(',').map((item) => item.trim()).filter(Boolean);
}

function executionProfile(value = 'full') {
  const profile = String(value).toLowerCase();
  if (!['full', 'quick'].includes(profile)) {
    throw new Error('--profile must be full or quick');
  }
  return profile;
}

function executionConcurrency(value = '1') {
  const concurrency = Number(value);
  if (![1, 2, 4].includes(concurrency)) {
    throw new Error('--concurrency must be 1, 2, or 4');
  }
  return concurrency;
}

export function accuracyExecutionOptions(args = {}) {
  const profile = executionProfile(args.profile);
  const concurrency = executionConcurrency(args.concurrency);
  return {
    profile,
    concurrency,
    selection: {
      profile,
      caseIds: csv(args.case),
      taskIds: csv(args.task),
      tags: csv(args.tag),
    },
  };
}

export function assertV4ResumePartial(partial) {
  if (partial?.identity?.protocolVersion !== ACCURACY_PROTOCOL_VERSION) {
    throw new Error('pre-v4 partial evidence cannot be resumed by the v4 runner');
  }
  return partial;
}

function printHelp() {
  console.log(`cosmo-accuracy - CosmoEdge event-level video evaluation (protocol v4)

Usage:
  cosmo-accuracy doctor --suite <suite.yml> --data-root <dir> --target-chip <chip> [device auth]
  cosmo-accuracy init-suite --input-root <legacy-data> --output <draft-dir> [--target-chip bm1688]
  cosmo-accuracy run --suite <suite.yml> --data-root <dir> --target-chip <chip> --device <url> --output <new-dir> <auth>
  cosmo-accuracy compare --reference <summary.json> --candidate <summary.json> [--candidate ...] --output <new-dir>
  cosmo-accuracy render --input <summary.json> --output <report.html>
  cosmo-accuracy diagnose-threshold --from <run.private.json> --case <id> --suite <suite.yml> --data-root <dir> --target-chip <chip> --device <url> --output <new-dir> <auth>

Authentication:
  --token-env <name>                  Read a short-lived token from an environment variable
  --user <account> --password-stdin  Read one password line from stdin

Run options:
  --profile <full|quick>  Full selection, or representative cases tagged "quick" (default: full)
  --concurrency <1|2|4>  Run up to one, two, or four CV samples in parallel
  --resume       Resume a protocol v4 partial when inputs, device, and tool still match
  --case <ids>   Comma-separated case IDs
  --task <ids>   Comma-separated task IDs
  --tag <tags>   Comma-separated tags
  --verbose
`);
}

const isEntryPoint = process.argv[1]
  && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isEntryPoint) {
  process.exitCode = await main();
}
