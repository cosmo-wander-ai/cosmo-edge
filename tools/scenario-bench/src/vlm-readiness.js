import { sleepWithSignal, throwIfAborted } from './shutdown-signal.js';
import { latencyMetricsForNodes, normalizeTaskType } from './task-strategies.js';

export const DEFAULT_VLM_READY_TIMEOUT_SEC = 180;
export const VLM_READY_POLL_INTERVAL_SEC = 3;

export class VlmReadinessTimeoutError extends Error {
  constructor(message, readiness) {
    super(message);
    this.name = 'VlmReadinessTimeoutError';
    this.readiness = readiness;
  }
}

export function resolveVlmReadyTimeoutSec(value) {
  if (value == null) return DEFAULT_VLM_READY_TIMEOUT_SEC;
  const seconds = Number(value);
  if (!Number.isFinite(seconds) || seconds <= 0) {
    throw new TypeError('--vlm-ready-timeout-sec must be a positive number');
  }
  return seconds;
}

/**
 * Wait until every newly-added direct VLM binding has completed work locally.
 *
 * Readiness is deliberately separate from benchmark sampling and FPS gates. A
 * binding is ready only after its selected task-local completion counter grows
 * beyond the first observed value and direct-Qwen latency telemetry is visible.
 * The caller supplies a probe function so these observations never enter the
 * formal hold sample set.
 *
 * @param {object} options
 * @param {Array<object>} options.entries newly-added task/channel bindings
 * @param {(entries:Array<object>) => Promise<object>} options.probe sampler probe
 * @param {number} [options.timeoutSec]
 * @param {number} [options.pollIntervalSec]
 * @param {AbortSignal} [options.signal]
 * @param {object} [options.logger]
 * @param {() => number} [options.now]
 * @param {(ms:number, signal?:AbortSignal) => Promise<void>} [options.sleep]
 */
export async function waitForVlmReady({
  entries = [],
  probe,
  timeoutSec = DEFAULT_VLM_READY_TIMEOUT_SEC,
  pollIntervalSec = VLM_READY_POLL_INTERVAL_SEC,
  signal,
  logger,
  now = () => Date.now(),
  sleep = sleepWithSignal,
} = {}) {
  const vlmEntries = entries.filter((entry) => normalizeTaskType(entry?.taskType) === 'vlm');
  if (!vlmEntries.length) {
    return {
      ready: true,
      status: 'ready',
      probes: 0,
      timeoutSec: Number(timeoutSec),
      pollIntervalSec: Number(pollIntervalSec),
      startedAt: null,
      endedAt: null,
      elapsedMs: 0,
      bindings: [],
    };
  }
  if (typeof probe !== 'function') throw new TypeError('waitForVlmReady requires a probe function');

  const timeoutMs = positiveMilliseconds(timeoutSec, 'timeoutSec');
  const pollIntervalMs = positiveMilliseconds(pollIntervalSec, 'pollIntervalSec');
  const startedAt = now();
  const deadline = startedAt + timeoutMs;
  const states = new Map(vlmEntries.map((entry) => [bindingKey(entry), {
    entry,
    completionActionId: normalizeActionId(entry.vlmCompletionActionId) ?? 'BA_00004',
    baselineTotal: null,
    currentTotal: null,
    completionAdvanced: false,
    qwenLatencyMs: null,
  }]));
  let probes = 0;

  while (true) {
    throwIfAborted(signal);
    const sample = await probe(vlmEntries);
    throwIfAborted(signal);
    probes++;

    for (const entry of vlmEntries) {
      const state = states.get(bindingKey(entry));
      const channel = findBindingChannel(sample?.channels, entry);
      if (!channel) continue;

      const completionActionId = String(channel.completionActionId ?? '').trim().toUpperCase();
      const completionTotal = finiteNumber(channel.primaryProcessTotal);
      if (completionActionId === state.completionActionId && completionTotal != null) {
        if (state.baselineTotal == null) state.baselineTotal = completionTotal;
        state.currentTotal = completionTotal;
        if (completionTotal > state.baselineTotal) state.completionAdvanced = true;
      }

      const { primaryLatencyMs } = latencyMetricsForNodes(channel.nodeDurationInfos ?? [], 'vlm');
      if (primaryLatencyMs != null) state.qwenLatencyMs = primaryLatencyMs;
    }

    const pending = [...states.values()].filter(
      (state) => !state.completionAdvanced || state.qwenLatencyMs == null,
    );
    if (!pending.length) {
      const readiness = readinessSnapshot({
        states,
        ready: true,
        status: 'ready',
        probes,
        timeoutSec,
        pollIntervalSec,
        startedAt,
        endedAt: now(),
      });
      logger?.info?.(
        `[ready] ${readiness.bindings.length} newly-added VLM binding(s) completed task-local work `
        + `with Qwen latency telemetry after ${Math.max(0, Math.round((now() - startedAt) / 1000))}s.`,
      );
      return readiness;
    }

    const remainingMs = deadline - now();
    if (remainingMs <= 0) {
      const detail = pending.map((state) => pendingReason(state)).join('; ');
      const readiness = readinessSnapshot({
        states,
        ready: false,
        status: 'timed-out',
        probes,
        timeoutSec,
        pollIntervalSec,
        startedAt,
        endedAt: now(),
      });
      throw new VlmReadinessTimeoutError(
        `VLM readiness timed out after ${timeoutSec}s: ${detail}`,
        readiness,
      );
    }
    await sleep(Math.min(pollIntervalMs, remainingMs), signal);
  }
}

function positiveMilliseconds(value, label) {
  const seconds = Number(value);
  if (!Number.isFinite(seconds) || seconds <= 0) {
    throw new TypeError(`${label} must be a positive number`);
  }
  return seconds * 1000;
}

function bindingKey(entry) {
  return String(entry?.taskId ?? `${entry?.taskKey ?? ''}:${entry?.channelId ?? ''}`);
}

function findBindingChannel(channels, entry) {
  const list = Array.isArray(channels) ? channels : [];
  return list.find((channel) => String(channel?.taskId ?? '') === String(entry?.taskId ?? ''))
    ?? list.find((channel) => String(channel?.channelId ?? '') === String(entry?.channelId ?? '')
      && String(channel?.taskKey ?? '') === String(entry?.taskKey ?? ''));
}

function readinessRecord(state) {
  const pendingReasons = pendingReasonsForState(state);
  return {
    taskId: state.entry.taskId,
    taskKey: state.entry.taskKey,
    channelId: state.entry.channelId,
    completionActionId: state.completionActionId,
    baselineTotal: state.baselineTotal,
    currentTotal: state.currentTotal,
    completionAdvanced: state.completionAdvanced,
    qwenLatencyMs: state.qwenLatencyMs,
    ready: pendingReasons.length === 0,
    pendingReasons,
  };
}

function readinessSnapshot({
  states,
  ready,
  status,
  probes,
  timeoutSec,
  pollIntervalSec,
  startedAt,
  endedAt,
}) {
  return {
    ready,
    status,
    probes,
    timeoutSec: Number(timeoutSec),
    pollIntervalSec: Number(pollIntervalSec),
    startedAt: new Date(startedAt).toISOString(),
    endedAt: new Date(endedAt).toISOString(),
    elapsedMs: Math.max(0, endedAt - startedAt),
    bindings: [...states.values()].map((state) => readinessRecord(state)),
  };
}

function pendingReason(state) {
  const label = state.entry.taskId ?? `${state.entry.taskKey ?? 'vlm'}@${state.entry.channelId ?? '?'}`;
  const reasons = pendingReasonsForState(state);
  return `${label} (${reasons.join(', ')})`;
}

function pendingReasonsForState(state) {
  const reasons = [];
  if (state.baselineTotal == null) reasons.push(`${state.completionActionId} completion counter missing`);
  else if (!state.completionAdvanced) reasons.push(`${state.completionActionId} did not advance from ${state.baselineTotal}`);
  if (state.qwenLatencyMs == null) reasons.push('direct Qwen latency missing');
  return reasons;
}

function finiteNumber(value) {
  if (value === null || value === undefined || value === '') return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function normalizeActionId(value) {
  const normalized = String(value ?? '').trim().toUpperCase();
  return normalized || null;
}
