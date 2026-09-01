import crypto from 'node:crypto';
import { performance } from 'node:perf_hooks';

import { ChannelCleanupError, ChannelManager } from '../channel-manager.js';
import { MetricsSampler } from '../metrics-sampler.js';
import { sleepWithSignal, throwIfAborted } from '../shutdown-signal.js';
import { waitForVlmReady } from '../vlm-readiness.js';
import { collectPersistedEvents, queryPersistedEventSnapshot } from './event-collector.js';
import { evaluateExpectation } from './evaluator.js';
import { ManagedRtspPusher } from './rtsp-pusher.js';
import { sha256Buffer, sha256File, stableStringify } from './utils.js';

export class DeviceTrialExecutor {
  constructor({
    client,
    suite,
    runId,
    evidenceWriter = null,
    logger = null,
    signal = null,
    now = () => Date.now(),
    monotonicNow = () => performance.now(),
    sleep = sleepWithSignal,
    eventCollector = collectPersistedEvents,
    eventSnapshot = queryPersistedEventSnapshot,
    channelManagerFactory = null,
    rtspPusherFactory = null,
    metricsSamplerFactory = null,
    vlmReadiness = waitForVlmReady,
  } = {}) {
    if (!client || !suite || !runId) throw new Error('client, suite, and runId are required');
    this.client = client;
    this.suite = suite;
    this.runId = runId;
    this.evidenceWriter = evidenceWriter;
    this.logger = logger;
    this.signal = signal;
    this.now = now;
    this.monotonicNow = monotonicNow;
    this.sleep = sleep;
    this.eventCollector = eventCollector;
    this.eventSnapshot = eventSnapshot;
    this.channelManagerFactory = channelManagerFactory ?? ((opts) => new ChannelManager(client, opts));
    this.rtspPusherFactory = rtspPusherFactory ?? (() => new ManagedRtspPusher({ logger, sleep }));
    this.metricsSamplerFactory = metricsSamplerFactory ?? (() => new MetricsSampler(client, logger));
    this.vlmReadiness = vlmReadiness;
  }

  async executeTrial({
    task,
    case: item,
    attemptNumber,
    validTrialNumber,
    taskConfigOverride = null,
  } = {}) {
    throwIfAborted(this.signal);
    const prefix = channelPrefix(this.runId, item.id, attemptNumber);
    const totalStartedAt = this.monotonicNow();
    const timingMs = {};
    const measure = async (name, action) => {
      const startedAt = this.monotonicNow();
      try { return await action(); }
      finally { timingMs[name] = (timingMs[name] ?? 0) + this.monotonicNow() - startedAt; }
    };
    const measureSync = (name, action) => {
      const startedAt = this.monotonicNow();
      try { return action(); }
      finally { timingMs[name] = (timingMs[name] ?? 0) + this.monotonicNow() - startedAt; }
    };
    const manager = this.channelManagerFactory({
      channelPrefix: prefix,
      reuse: false,
      cleanup: true,
      strictCleanup: true,
      recoverAmbiguousCreates: true,
      sleep: (ms) => this.sleep(ms),
      monotonicNow: this.monotonicNow,
      logger: this.logger,
    });
    let pusher = null;
    let channelId = null;
    let channelName = null;
    let taskConfigSha256 = null;
    let taskApplied = false;
    let taskDeleteError = null;
    let outcome = null;
    const cleanup = {
      task: { attempted: false, deleted: false, verified: false, warnings: [], errors: [] },
      channel: null,
      rtsp: null,
    };

    try {
      const currentSha256 = measureSync('sampleHash', () => sha256File(item.absoluteFile));
      if (currentSha256 !== item.sha256) {
        throw new Error(`case ${item.id} SHA256 changed after suite admission`);
      }
      const source = await measure('sourcePrepare', () => this._sourceForTrial(item, prefix));
      pusher = source.pusher;
      const channelProvisionStartedAt = this.monotonicNow();
      let channelIds;
      try {
        channelIds = await manager.ensureChannels(source.videos, 1);
      } finally {
        const channelProvisionTotal = this.monotonicNow() - channelProvisionStartedAt;
        const channelTiming = manager.timingSnapshot?.() ?? {};
        timingMs.videoUpload = Number(channelTiming.videoUpload ?? 0);
        timingMs.channelCreate = Number(channelTiming.cameraCreate ?? channelProvisionTotal);
        timingMs.channelProvisionOverhead = Math.max(
          0,
          channelProvisionTotal - timingMs.videoUpload - timingMs.channelCreate,
        );
      }
      channelId = channelIds[0];
      channelName = manager.created.get(channelId)?.channelName;
      if (!channelName) throw new Error('created channel metadata is missing channelName');
      const taskConfig = taskConfigOverride ?? await this._taskConfig(task, channelId);
      taskConfigSha256 = taskConfigOverride || task.configSource === 'device-default'
        ? sha256Buffer(Buffer.from(stableStringify(taskConfig)))
        : task.taskConfigSha256;
      taskApplied = true;
      await measure('taskApply', () => this.client.taskSaveOrUpdate({
        channelId,
        channelName,
        algorithmId: task.algorithmId,
        custId: '',
        category: 0,
        taskConfig,
        scheduleId: task.scheduleId,
        pollingId: '',
      }));
      const ready = await measure('readiness', () => waitForAccuracyTaskReady({
        client: this.client,
        channelId,
        algorithmId: task.algorithmId,
        algorithmCode: task.algorithmCode,
        taskKind: task.kind,
        timeoutSec: this.suite.defaults.readyTimeoutSec,
        pollIntervalSec: this.suite.defaults.readyPollIntervalSec,
        signal: this.signal,
        now: this.now,
        sleep: this.sleep,
      }));
      let vlmReady = null;
      if (task.kind === 'vlm') {
        const taskId = `${channelId}_${task.algorithmCode}`;
        const entry = {
          taskKey: task.id,
          taskDisplayName: task.displayName ?? task.id,
          taskType: 'vlm',
          taskId,
          channelId,
          algorithmId: task.algorithmId,
          algorithmCode: task.algorithmCode,
          targetFps: task.targetFps ?? null,
          vlmCompletionActionId: task.vlmCompletionActionId,
        };
        const sampler = this.metricsSamplerFactory();
        vlmReady = await measure('vlmReadiness', () => this.vlmReadiness({
          entries: [entry],
          probe: (entries) => sampler.sample(entries),
          timeoutSec: this.suite.defaults.readyTimeoutSec,
          pollIntervalSec: this.suite.defaults.readyPollIntervalSec,
          signal: this.signal,
          logger: this.logger,
          now: this.now,
          sleep: this.sleep,
        }));
      }
      const timeBegin = this.now();
      const observeSec = item.observeSec ?? this.suite.defaults.observeSec;
      const observation = await measure('observation', () => observeUntilDecisiveEvent({
        client: this.client,
        channelName,
        channelId,
        algorithmCode: task.algorithmCode ?? task.algorithmId,
        expectation: item.expectation,
        timeBegin,
        observeSec,
        taskConfigSha256,
        pollIntervalSec: this.suite.defaults.earlyStopPollIntervalSec,
        signal: this.signal,
        now: this.now,
        monotonicNow: this.monotonicNow,
        sleep: this.sleep,
        querySnapshot: this.eventSnapshot,
      }));
      const observationEnd = observation.endedAt;
      const taskId = `${channelId}_${task.algorithmCode}`;
      const switchResult = await measure('taskStop', () => this.client.taskBatchSwitch([{
        id: taskId,
        channelId,
        algorithmId: task.algorithmId,
        enable: 0,
      }]));
      if (switchResult?.failedList?.length) {
        throw new Error(`task stop failed for ${taskId}`);
      }
      const timeEnd = observationEnd + this.suite.defaults.eventFlushTimeoutSec * 1000;
      const collected = await measure('eventCollection', () => this.eventCollector({
        client: this.client,
        channelName,
        channelId,
        algorithmCode: task.algorithmCode ?? task.algorithmId,
        timeBegin,
        timeEnd,
        observationEnd,
        flushTimeoutSec: this.suite.defaults.eventFlushTimeoutSec,
        pollIntervalSec: this.suite.defaults.eventPollIntervalSec,
        settleMinSec: this.suite.defaults.eventSettleMinSec,
        signal: this.signal,
        now: this.now,
        sleep: this.sleep,
      }));
      const events = this.evidenceWriter
        ? await measure('artifactArchive', () => this.evidenceWriter.archiveAlertImages(
            this.client,
            collected.events,
          ))
        : collected.events;
      const verdict = evaluateExpectation(item.expectation, events.length);
      if (observation.earlyStopped && verdict.status !== observation.triggerStatus) {
        throw new Error(
          `Event/Page early-stop verdict ${observation.triggerStatus} did not survive final settlement`,
        );
      }
      outcome = {
        ...verdict,
        attemptNumber,
        validTrialNumber,
        channelId,
        channelName,
        timeBegin,
        timeEnd,
        observeSec,
        observation,
        ready,
        vlmReady,
        events,
        eventCollection: { settled: collected.settled, queryCount: collected.queryCount },
      };
    } catch (error) {
      outcome = {
        status: 'ERROR',
        attemptNumber,
        validTrialNumber,
        error: error.message,
        events: [],
      };
    } finally {
      const cleanupStartedAt = this.monotonicNow();
      const cleanupClient = this.client.detachedCleanupClient?.() ?? this.client;
      if (cleanupClient === this.client) this.client.beginCleanup?.();
      if (taskApplied && channelId) {
        cleanup.task.attempted = true;
        try {
          await cleanupClient.taskDelete({ channelId, algorithmId: task.algorithmId });
          cleanup.task.deleted = true;
        } catch (error) {
          taskDeleteError = error.message;
          cleanup.task.warnings.push(`delete:${error.message}`);
        }
      } else {
        cleanup.task.verified = true;
      }
      try {
        cleanup.channel = await manager.finish({ client: cleanupClient });
      } catch (error) {
        cleanup.channel = error instanceof ChannelCleanupError
          ? error.result
          : { attempted: channelId ? [channelId] : [], verified: false, remaining: [], errors: [error.message] };
      }
      if (taskApplied && channelId) {
        cleanup.task.verified = await verifyTaskRemoved(
          cleanupClient,
          channelId,
          task.algorithmCode,
          this.sleep,
        );
        if (!cleanup.task.verified) {
          cleanup.task.errors.push(taskDeleteError ?? 'task remained visible after cleanup');
        }
      }
      if (pusher) {
        try { cleanup.rtsp = await pusher.stop(); }
        catch (error) { cleanup.rtsp = { stopped: false, error: error.message }; }
      }
      timingMs.cleanup = this.monotonicNow() - cleanupStartedAt;
    }

    const cleanupFailed = cleanup.task.errors.length > 0
      || cleanup.task.verified !== true
      || cleanup.channel?.verified !== true
      || cleanup.rtsp?.stopped === false;
    if (cleanupFailed) {
      outcome = {
        ...outcome,
        status: 'ERROR',
        cleanupBlocked: true,
        error: [outcome?.error, 'strict cleanup failed'].filter(Boolean).join('; '),
      };
    }
    timingMs.total = this.monotonicNow() - totalStartedAt;
    return {
      ...outcome,
      channelId: outcome?.channelId ?? channelId,
      channelName: outcome?.channelName ?? channelName,
      taskConfigSha256: outcome?.taskConfigSha256 ?? taskConfigSha256,
      timingMs: roundedTimings(timingMs),
      cleanup,
    };
  }

  async _taskConfig(task, channelId) {
    if (task.configSource === 'frozen') return structuredClone(task.taskConfig);
    if (task.configSource === 'device-default') {
      const selected = await this.client.taskSelectConfig({
        channelId,
        algorithmId: task.algorithmId,
      });
      if (!selected?.taskConfig) throw new Error(`device returned no default taskConfig for ${task.id}`);
      return structuredClone(selected.taskConfig);
    }
    throw new Error(`unsupported task config source for ${task.id}`);
  }

  async _sourceForTrial(item, prefix) {
    if (this.suite.sourceMode === 'local') {
      return {
        pusher: null,
        videos: { mode: 'local', local: [{ file: item.absoluteFile, name: 'src' }] },
      };
    }
    if (this.suite.sourceMode !== 'rtsp-deterministic') {
      throw new Error(`unsupported accuracy source mode ${this.suite.sourceMode}`);
    }
    const encodedPath = item.file.split('/').map((segment) => encodeURIComponent(segment)).join('/');
    const sourceUrl = `${this.suite.rtsp.httpBase}/${encodedPath}`;
    const targetUrl = `${this.suite.rtsp.mediaMtxBase}/${prefix}`;
    const pusher = this.rtspPusherFactory();
    await pusher.start({
      ffmpeg: this.suite.rtsp.ffmpeg,
      sourceUrl,
      targetUrl,
      signal: this.signal,
    });
    return {
      pusher,
      videos: { mode: 'rtsp-deterministic', rtsp: [{ url: targetUrl, name: `${prefix}-src` }] },
    };
  }
}

export async function observeUntilDecisiveEvent({
  client,
  channelName,
  channelId,
  algorithmCode,
  expectation,
  timeBegin,
  observeSec,
  pollIntervalSec = 5,
  signal,
  now = () => Date.now(),
  monotonicNow = () => performance.now(),
  sleep = sleepWithSignal,
  querySnapshot = queryPersistedEventSnapshot,
} = {}) {
  const requestedMs = Number(observeSec) * 1000;
  const pollMs = Number(pollIntervalSec) * 1000;
  const observationStartedAt = monotonicNow();
  const elapsedMs = () => Math.max(0, monotonicNow() - observationStartedAt);
  let queryCount = 0;
  let queryError = null;
  let queryErrorCount = 0;
  while (elapsedMs() < requestedMs) {
    throwIfAborted(signal);
    await sleep(Math.min(pollMs, Math.max(0, requestedMs - elapsedMs())), signal);
    if (elapsedMs() >= requestedMs) break;
    let events;
    const queryAbort = boundedSignal(signal, requestedMs - elapsedMs());
    try {
      events = await querySnapshot({
        client,
        channelName,
        channelId,
        algorithmCode,
        timeBegin,
        timeEnd: now(),
        pageSize: 200,
        signal: queryAbort.signal,
      });
      queryCount += 1;
    } catch (error) {
      throwIfAborted(signal);
      queryError = error.message;
      queryErrorCount += 1;
      continue;
    } finally {
      queryAbort.dispose();
    }
    if (elapsedMs() >= requestedMs) break;
    const decisive = decisiveExpectation(expectation, events.length);
    if (decisive) {
      const endedAt = now();
      return {
        requestedSec: Number(observeSec),
        actualSec: elapsedMs() / 1000,
        endedAt,
        earlyStopped: true,
        triggerStatus: decisive,
        triggerEventCount: events.length,
        queryCount,
        queryError,
        queryErrorCount,
      };
    }
  }
  const endedAt = now();
  return {
    requestedSec: Number(observeSec),
    actualSec: elapsedMs() / 1000,
    endedAt,
    earlyStopped: false,
    triggerStatus: null,
    triggerEventCount: null,
    queryCount,
    queryError,
    queryErrorCount,
  };
}

function boundedSignal(signal, remainingMs) {
  const controller = new AbortController();
  const onAbort = () => controller.abort(signal?.reason);
  if (signal) signal.addEventListener('abort', onAbort, { once: true });
  const timer = setTimeout(
    () => controller.abort(new Error('observation deadline exceeded')),
    Math.max(1, Math.ceil(remainingMs)),
  );
  return {
    signal: controller.signal,
    dispose() {
      clearTimeout(timer);
      signal?.removeEventListener('abort', onAbort);
    },
  };
}

function decisiveExpectation(expectation, eventCount) {
  if (expectation?.maxEvents != null && eventCount > expectation.maxEvents) return 'FAIL';
  if (expectation?.minEvents != null
      && expectation.maxEvents == null
      && eventCount >= expectation.minEvents) return 'PASS';
  return null;
}

export async function waitForAccuracyTaskReady({
  client,
  channelId,
  algorithmId,
  algorithmCode = algorithmId,
  taskKind = 'cv',
  timeoutSec = 120,
  pollIntervalSec = 3,
  signal,
  now = () => Date.now(),
  sleep = sleepWithSignal,
} = {}) {
  const deadline = now() + Number(timeoutSec) * 1000;
  const taskId = `${channelId}_${algorithmCode}`;
  let probes = 0;
  while (now() < deadline) {
    throwIfAborted(signal);
    probes += 1;
    const cameras = await client.cameraPage({
      channelName: '', channelStatus: -1, pageNum: 1, pageSize: 500,
    });
    const camera = (cameras?.rows ?? []).find(
      (item) => String(item.videoChannelId ?? item.id) === String(channelId),
    );
    const status = String(camera?.channelStatus ?? '');
    if (['2', '3', '4'].includes(status)) {
      throw new Error(`channel entered failure status ${status}`);
    }
    if (status === '1') {
      const detail = await client.taskRunningDetail([taskId]);
      const tasks = Array.isArray(detail) ? detail : detail?.status ?? [];
      const task = tasks.find((item) => String(item.taskId ?? '') === taskId)
        ?? tasks.find((item) => String(item.channelId ?? '') === String(channelId));
      const progressed = (task?.actionStatus ?? []).filter((item) =>
        Number(item.processCount ?? 0) > 0 || Number(item.processCountPeriod ?? 0) > 0);
      const isMediaAction = (item) => {
        const label = `${item.name ?? ''} ${item.actionId ?? ''}`;
        return /(Decode|Demux)/iu.test(label)
          || /^BA_00001(?:\s|$)/u.test(String(item.actionId ?? ''));
      };
      const action = taskKind === 'vlm'
        ? progressed.find(isMediaAction)
        : progressed.find((item) => !isMediaAction(item));
      if (action) return { ready: true, probes, taskId, actionId: action.actionId ?? null };
    }
    await sleep(Math.min(Number(pollIntervalSec) * 1000, Math.max(0, deadline - now())), signal);
  }
  const expectedProgress = taskKind === 'vlm' ? 'decoded frames' : 'algorithm processing';
  throw new Error(
    `accuracy task readiness timed out after ${timeoutSec}s (${expectedProgress} did not advance)`,
  );
}

async function verifyTaskRemoved(client, channelId, algorithmCode, sleep) {
  const taskId = `${channelId}_${algorithmCode}`;
  for (let attempt = 1; attempt <= 3; attempt += 1) {
    try {
      const detail = await client.taskRunningDetail([taskId]);
      const tasks = Array.isArray(detail) ? detail : detail?.status ?? [];
      if (!tasks.some((item) => String(item.taskId ?? '') === taskId)) return true;
    } catch {
      return false;
    }
    if (attempt < 3) await sleep(250);
  }
  return false;
}

function channelPrefix(runId, caseId, attemptNumber) {
  const digest = crypto.createHash('sha256').update(`${runId}\0${caseId}`).digest('hex');
  return `acc-${digest.slice(0, 6)}-${String(attemptNumber).padStart(2, '0')}`;
}

function roundedTimings(value) {
  return Object.fromEntries(Object.entries(value).map(([key, duration]) => [
    key,
    Math.max(0, Math.round(Number(duration) * 1000) / 1000),
  ]));
}
