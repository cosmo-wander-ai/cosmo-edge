import { throwIfAborted } from '../shutdown-signal.js';
import {
  ACCURACY_EXECUTION_PROFILES,
  selectAccuracyCases,
  selectedCaseIdentity,
} from './selection.js';
import { sha256Buffer, stableStringify } from './utils.js';

const VALID_TRIAL_STATUSES = new Set(['PASS', 'FAIL']);
const TERMINAL_CASE_STATUSES = new Set(['PASS', 'FAIL', 'ERROR']);

export class AccuracyRunner {
  constructor({
    suite,
    targetChip,
    executeTrial,
    evidenceWriter = null,
    logger = null,
    signal = null,
    resumedCases = [],
    selectedCases,
    execution,
    runStartedAt = null,
    invocations = [],
    invocationElapsedMs = null,
  } = {}) {
    if (!suite) throw new Error('AccuracyRunner requires a suite');
    if (!targetChip) throw new Error('AccuracyRunner requires targetChip');
    if (typeof executeTrial !== 'function') throw new Error('AccuracyRunner requires executeTrial');
    if (!suite.targetPlatforms?.includes(targetChip)) {
      throw new Error(`suite does not allow target chip ${targetChip}`);
    }
    if (!Array.isArray(selectedCases) || selectedCases.length === 0) {
      throw new Error('AccuracyRunner requires selectedCases');
    }
    if (!execution) throw new Error('AccuracyRunner requires execution');
    const selectedIdentity = selectedCaseIdentity(selectedCases);
    if (selectedIdentity.count !== execution.selection?.count
        || selectedIdentity.sha256 !== execution.selection?.sha256) {
      throw new Error('runner selected cases do not match the frozen execution identity');
    }
    this.suite = suite;
    this.executeTrial = executeTrial;
    this.evidenceWriter = evidenceWriter;
    this.logger = logger;
    this.signal = signal;
    this.selectedCases = [...selectedCases];
    this.execution = structuredClone(execution);
    this.runStartedAt = runStartedAt;
    this.invocations = structuredClone(invocations);
    this.invocationElapsedMs = invocationElapsedMs;
    const terminalResumedCases = resumedCases.filter((item) => TERMINAL_CASE_STATUSES.has(item?.status));
    if (new Set(terminalResumedCases.map((item) => item.id)).size !== terminalResumedCases.length) {
      throw new Error('resume partial contains duplicate terminal case IDs');
    }
    this.resumedCases = new Map(terminalResumedCases.map((item) => [item.id, item]));
  }

  async run() {
    const selected = this.selectedCases;
    const selectedIds = new Set(selected.map((item) => item.id));
    const unexpectedResume = [...this.resumedCases.keys()].find((id) => !selectedIds.has(id));
    if (unexpectedResume) {
      throw new Error(`resume partial contains a case outside the frozen selection: ${unexpectedResume}`);
    }
    const resultById = new Map();
    for (const item of selected) {
      const resumed = this.resumedCases.get(item.id);
      if (resumed) resultById.set(item.id, resumed);
    }
    let checkpoint = Promise.resolve();
    const checkpointPayload = () => ({
      status: 'running',
      startedAt: this.runStartedAt,
      invocations: this.invocationEvidence(),
      cases: selected.filter((item) => resultById.has(item.id)).map((item) => resultById.get(item.id)),
    });
    const writeCheckpoint = (force = false) => {
      checkpoint = checkpoint.then(() => this.evidenceWriter?.writePartial?.(
        checkpointPayload(),
        { force },
      ));
      return checkpoint;
    };
    const record = async (result) => {
      resultById.set(result.id, result);
      await writeCheckpoint();
    };
    const cvCases = selected.filter((item) => this._taskKind(item.task) !== 'vlm');
    const vlmCases = selected.filter((item) => this._taskKind(item.task) === 'vlm');
    const runGroup = async (group, width) => {
      const pending = group.filter((item) => !resultById.has(item.id));
      let nextIndex = 0;
      let firstError = null;
      const workers = Array.from({ length: Math.min(width, pending.length) }, async () => {
        while (nextIndex < pending.length && !firstError) {
          const item = pending[nextIndex];
          nextIndex += 1;
          try {
            throwIfAborted(this.signal);
            await record(await this._runCase(item));
          } catch (error) {
            firstError ??= error;
          }
        }
      });
      await Promise.all(workers);
      if (firstError) throw firstError;
    };
    try {
      await runGroup(cvCases, this.execution.concurrency);
      await runGroup(vlmCases, 1);
    } finally {
      await writeCheckpoint(true);
    }
    const cases = selected.map((item) => resultById.get(item.id));
    if (cases.some((item) => !item)) {
      throw new Error('accuracy runner completed with a missing case result');
    }
    return { cases, execution: structuredClone(this.execution) };
  }

  invocationEvidence() {
    return this.invocations.map((item, index) => index === this.invocations.length - 1
      && typeof this.invocationElapsedMs === 'function'
      ? { ...item, activeDurationMs: this.invocationElapsedMs() }
      : { ...item });
  }

  _taskKind(taskId) {
    return this.suite.tasks.find((task) => task.id === taskId)?.kind ?? 'cv';
  }

  async _runCase(item) {
    const task = this.suite.tasks.find((candidate) => candidate.id === item.task);
    const attempts = [];
    let valid = null;
    for (let retry = 0; retry <= this.suite.defaults.infrastructureRetriesPerTrial; retry += 1) {
      throwIfAborted(this.signal);
      let trial;
      try {
        trial = await this.executeTrial({
          suite: this.suite,
          task,
          case: item,
          attemptNumber: attempts.length + 1,
          validTrialNumber: 1,
          infrastructureRetry: retry,
          signal: this.signal,
        });
      } catch (error) {
        trial = { status: 'ERROR', error: error.message };
      }
      if (!trial || !['PASS', 'FAIL', 'ERROR'].includes(trial.status)) {
        trial = { status: 'ERROR', error: 'trial executor returned an invalid status' };
      }
      attempts.push({
        attemptNumber: attempts.length + 1,
        validTrialNumber: 1,
        infrastructureRetry: retry,
        ...trial,
      });
      if (VALID_TRIAL_STATUSES.has(trial.status)) {
        valid = trial;
        break;
      }
    }
    const result = {
      id: item.id,
      task: item.task,
      file: item.file,
      sha256: item.sha256,
      expectation: item.expectation,
      tags: item.tags ?? [],
      status: valid?.status ?? 'ERROR',
      cleanupBlocked: attempts.some((trial) => trial.cleanupBlocked === true),
      reasons: valid ? [] : ['no valid trial result'],
      eventCount: valid?.eventCount ?? null,
      trials: attempts,
    };
    this.logger?.info?.(
      `[accuracy] ${item.id}: ${result.status} (${valid ? 1 : 0} valid/${attempts.length} attempted)`,
    );
    return result;
  }
}

export function resolveAccuracyExecution(suite, {
  profile = 'full',
  concurrency = 1,
  selectedCases = null,
} = {}) {
  if (!ACCURACY_EXECUTION_PROFILES.has(profile)) {
    throw new Error(`accuracy profile must be one of: ${[...ACCURACY_EXECUTION_PROFILES].join(', ')}`);
  }
  if (![1, 2, 4].includes(concurrency)) {
    throw new Error('accuracy concurrency must be 1, 2, or 4');
  }
  const selected = selectedCases ?? selectAccuracyCases(suite, { profile });
  return {
    profile,
    concurrency,
    selection: selectedCaseIdentity(selected),
    measurementConfigSha256: sha256Buffer(Buffer.from(stableStringify({
      defaults: suite.defaults,
      cases: selected.map((item) => ({
        id: item.id,
        expectation: item.expectation,
        observeSec: item.observeSec ?? suite.defaults.observeSec,
      })),
    }))),
  };
}
