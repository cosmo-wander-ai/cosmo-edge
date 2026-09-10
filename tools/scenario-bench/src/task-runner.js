// Bind workload tasks and drive a slow-start load staircase.
//
// A workload can contain one or many tasks. The single-task case is represented
// as one task bound to all active channels, so the run path stays unified.

import { sleepWithSignal, throwIfAborted } from './shutdown-signal.js';

export class TaskRunner {
  /**
   * @param {import('./cosmo-client.js').CosmoClient} client
   * @param {object} ctx
   * @param {Array<object>} [ctx.tasks] normalized workload tasks
   * @param {Array<object>} [ctx.bindings] normalized binding matrix
   * @param {string} [ctx.algorithmId] legacy single-task algorithm id
   * @param {string|number} [ctx.algorithmCode] legacy single-task algorithm code
   * @param {string} [ctx.scheduleId] legacy single-task schedule id
   * @param {object} [ctx.taskConfig] legacy single-task config
   * @param {number} [ctx.rampBatchSize]
   * @param {number} [ctx.rampBatchDelaySec]
   * @param {AbortSignal} [ctx.signal]
   * @param {import('./logger.js').Logger} [logger]
   */
  constructor(client, ctx, logger) {
    this.client = client;
    this.tasks = normalizeTasks(ctx);
    this.taskById = new Map(this.tasks.map((task) => [task.id, task]));
    this.bindings = normalizeBindings(ctx.bindings, this.tasks);
    this.rampBatchSize = Math.max(1, Number(ctx.rampBatchSize ?? 1));
    this.rampBatchDelaySec = Math.max(0, Number(ctx.rampBatchDelaySec ?? 15));
    this.signal = ctx.signal;
    this.log = logger;
    /** @type {string[]} videoChannelIds in bind order */
    this.allChannelIds = [];
  }

  /**
   * Legacy signature taskIdFor(videoChannelId) still works for the primary task.
   * New signature is taskIdFor(taskId, videoChannelId).
   */
  taskIdFor(taskOrChannelId, maybeChannelId) {
    const task = maybeChannelId == null ? this.tasks[0] : this.taskById.get(String(taskOrChannelId));
    const channelId = maybeChannelId == null ? taskOrChannelId : maybeChannelId;
    if (!task) throw new Error(`unknown task: ${taskOrChannelId}`);
    return `${channelId}_${task.algorithmCode}`;
  }

  setChannels(videoChannelIds) {
    this.allChannelIds = [...videoChannelIds];
  }

  /**
   * Return expected task/channel records for the currently active channel set.
   * These records are passed to MetricsSampler and BatchSwitchTask.
   * @param {string[]} videoChannelIds
   */
  expectedTaskEntries(videoChannelIds) {
    const entries = [];
    for (const task of this.tasks) {
      const taskChannels = this._channelsForTask(task.id, videoChannelIds);
      for (const channelId of taskChannels) {
        entries.push({
          taskKey: task.id,
          taskDisplayName: task.displayName,
          taskType: task.type,
          vlmCompletionActionId: task.vlmCompletionActionId,
          algorithmId: task.algorithmId,
          algorithmCode: task.algorithmCode,
          targetFps: task.targetFps,
          channelId,
          taskId: this.taskIdFor(task.id, channelId),
        });
      }
    }
    return entries;
  }

  async _bind(videoChannelIds) {
    if (!videoChannelIds.length) return [];
    throwIfAborted(this.signal);
    const failures = [];
    for (const task of this.tasks) {
      throwIfAborted(this.signal);
      const targetChannelIds = this._channelsForTask(task.id, videoChannelIds);
      if (!targetChannelIds.length) continue;
      this.log?.info(
        `Binding task "${task.id}" (${task.algorithmId}) to ${targetChannelIds.length} channel(s) via ApplyParamsBatch...`,
      );
      const { failedList } = await this.client.taskApplyParamsBatch({
        algorithmId: task.algorithmId,
        scheduleId: task.scheduleId,
        taskConfig: task.taskConfig,
        targetChannelIds,
      });
      if (failedList?.length) {
        const failed = failedList.map((f) => f.id).join(', ');
        this.log?.warn(`ApplyParamsBatch partially failed for task "${task.id}" on: ${failed}`);
        failures.push(...failedList.map((f) => ({ ...f, taskId: task.id })));
      }
    }
    return failures;
  }

  async _switch(entries, enable) {
    if (!entries.length) return [];
    const tasks = entries.map((entry) => ({
      id: entry.taskId,
      channelId: entry.channelId,
      algorithmId: entry.algorithmId,
      enable,
    }));
    const { failedList } = await this.client.taskBatchSwitch(tasks);
    if (failedList?.length) {
      this.log?.warn(`BatchSwitchTask(enable=${enable}) failed on: ${failedList.map((f) => f.id).join(', ')}`);
    }
    return failedList ?? [];
  }

  /**
   * @param {Array<{channels:number, holdSec:number}>} loadProfile
   * @param {object} hooks
   * @param {(step:object, active:string[], added:string[], entries:object[]) => Promise<{stop:boolean, reason?:string}|void>} [hooks.onRampBatch]
   * @param {(step:object, active:string[], entries:object[]) => Promise<void>} [hooks.onStepStart]
   * @param {() => Promise<{stop:boolean, reason?:string}|void>} [hooks.onSample]
   * @param {(step:object, active:string[], entries:object[]) => Promise<{stop:boolean, reason?:string}|void>} [hooks.onStepEnd]
   * @param {number} sampleIntervalSec
   * @returns {Promise<{bottleneckStep?:number, bottleneckReason?:string,
   *   bottleneckSource?:string, bottleneckGates?:object[]}>}
   */
  async runStaircase(loadProfile, hooks, sampleIntervalSec) {
    let active = [];
    let bottleneck = null;

    try {
      throwIfAborted(this.signal);
      for (let i = 0; i < loadProfile.length; i++) {
        throwIfAborted(this.signal);
        const step = { ...loadProfile[i], index: i };
        const target = this.allChannelIds.slice(0, step.channels);
        const toAdd = target.slice(active.length);

        this.log?.info(
          `[step ${i + 1}/${loadProfile.length}] ramp to ${step.channels} channels `
          + `(+${toAdd.length}), hold ${step.holdSec}s`,
        );

        for (let offset = 0; offset < toAdd.length; offset += this.rampBatchSize) {
          throwIfAborted(this.signal);
          const batch = toAdd.slice(offset, offset + this.rampBatchSize);
          // Treat attempted bindings as active for cleanup purposes. If the
          // request succeeds remotely but the connection or signal interrupts
          // the response, the final OFF remains safe and prevents task leaks.
          active = [...new Set([...active, ...batch])];
          const failedList = await this._bind(batch);
          throwIfAborted(this.signal);
          if (failedList.length > 0) {
            const failedIds = failedList.map((f) => f.id ?? f.channelId ?? '?').join(', ');
            this.log?.warn(`[step ${i + 1}] bottleneck detected - task bind failed on: ${failedIds}`);
            bottleneck = {
              bottleneckStep: i,
              bottleneckChannels: active.length,
              bottleneckPhase: 'ramp',
              bottleneckSource: 'task-binding',
              bottleneckReason: `任务绑定失败 (可能达到设备并发授权上限): ${failedIds}`,
            };
            return bottleneck;
          }
          const entries = this.expectedTaskEntries(active);

          if (hooks?.onRampBatch) {
            const decision = await hooks.onRampBatch(step, active, batch, entries);
            throwIfAborted(this.signal);
            if (decision?.stop) {
              this.log?.warn(`[step ${i + 1}] ramp fuse tripped: ${decision.reason ?? 'threshold breached'}`);
              bottleneck = {
                bottleneckStep: i,
                bottleneckChannels: active.length,
                bottleneckPhase: 'ramp',
                bottleneckSource: decision.source ?? 'quick-fuse',
                bottleneckGates: decision.gates ?? [],
                bottleneckReason: decision.reason ?? 'ramp fuse tripped',
              };
              return bottleneck;
            }
          }

          const hasMoreBatches = offset + this.rampBatchSize < toAdd.length;
          if (hasMoreBatches && this.rampBatchDelaySec > 0) {
            await sleepWithSignal(this.rampBatchDelaySec * 1000, this.signal);
          }
        }

        active = target;
        if (hooks?.onStepStart) {
          await hooks.onStepStart(step, active, this.expectedTaskEntries(active));
          throwIfAborted(this.signal);
        }

        const ticks = Math.max(1, Math.floor(step.holdSec / sampleIntervalSec));
        for (let t = 0; t < ticks; t++) {
          await sleepWithSignal(sampleIntervalSec * 1000, this.signal);
          try {
            if (hooks?.onSample) {
              const decision = await hooks.onSample();
              throwIfAborted(this.signal);
              if (decision?.stop) {
                this.log?.warn(
                  `[step ${i + 1}] hold fuse tripped: ${decision.reason ?? 'threshold breached'}`,
                );
                bottleneck = {
                  bottleneckStep: i,
                  bottleneckChannels: active.length,
                  bottleneckPhase: 'hold',
                  bottleneckSource: decision.source ?? 'quick-fuse',
                  bottleneckGates: decision.gates ?? [],
                  bottleneckReason: decision.reason ?? 'hold fuse tripped',
                };
                return bottleneck;
              }
            }
          } catch (err) {
            throwIfAborted(this.signal);
            // Missing the remainder of a hold window can otherwise turn an
            // outage into a false PASS based only on pre-failure samples.
            // Abort and let the caller write a clearly marked partial report.
            throw new Error(`sample tick failed: ${err.message}`, { cause: err });
          }
        }

        if (hooks?.onStepEnd) {
          const decision = await hooks.onStepEnd(step, active, this.expectedTaskEntries(active));
          throwIfAborted(this.signal);
          if (decision?.stop) {
            this.log?.warn(`[step ${i + 1}] bottleneck detected - stopping staircase: ${decision.reason ?? 'threshold breached'}`);
            bottleneck = {
              bottleneckStep: i,
              bottleneckChannels: step.channels,
              bottleneckPhase: 'hold',
              bottleneckSource: decision.source ?? 'runtime-threshold',
              bottleneckGates: decision.gates ?? [],
              bottleneckReason: decision.reason ?? 'threshold breached',
            };
            break;
          }
        }
      }

      return bottleneck ?? {};
    } finally {
      if (active.length) {
        this.client.beginCleanup?.();
        const entries = this.expectedTaskEntries(active);
        this.log?.info(`Switching ${entries.length} active task binding(s) OFF.`);
        try {
          await this._switch(entries, 0);
        } catch (err) {
          this.log?.warn(`Best-effort task OFF failed: ${err.message}`);
        }
      }
    }
  }

  _channelsForTask(taskId, candidateChannelIds) {
    const matchingBindings = this.bindings.filter((binding) => binding.taskId === taskId);
    if (!matchingBindings.length) return [...candidateChannelIds];

    const selected = new Set();
    for (const binding of matchingBindings) {
      for (const channelId of candidateChannelIds) {
        const oneBasedIndex = this.allChannelIds.indexOf(channelId) + 1;
        if (matchesChannelSelector(binding.channels, channelId, oneBasedIndex)) {
          selected.add(channelId);
        }
      }
    }
    return [...selected];
  }
}

function normalizeTasks(ctx) {
  if (Array.isArray(ctx.tasks) && ctx.tasks.length) {
    return ctx.tasks.map((task, index) => ({
      id: String(task.id ?? `task-${index + 1}`),
      displayName: task.displayName ?? task.id ?? `task-${index + 1}`,
      type: task.type ?? 'cv',
      vlmCompletionActionId: task.vlmCompletionActionId ?? null,
      algorithmId: String(task.algorithmId),
      algorithmCode: String(task.algorithmCode ?? task.algorithmId),
      scheduleId: task.scheduleId,
      taskConfig: task.taskConfig ?? { params: [], areas: [] },
      targetFps: task.targetFps ?? null,
    }));
  }

  return [{
    id: 'default',
    displayName: 'default',
    type: 'cv',
    vlmCompletionActionId: ctx.vlmCompletionActionId ?? null,
    algorithmId: String(ctx.algorithmId),
    algorithmCode: String(ctx.algorithmCode ?? ctx.algorithmId),
    scheduleId: ctx.scheduleId,
    taskConfig: ctx.taskConfig ?? { params: [], areas: [] },
    targetFps: ctx.targetFps ?? null,
  }];
}

function normalizeBindings(bindings, tasks) {
  if (Array.isArray(bindings) && bindings.length) {
    return bindings.map((binding) => ({
      taskId: String(binding.taskId ?? binding.task),
      channels: binding.channels ?? 'all',
    }));
  }
  return tasks.map((task) => ({ taskId: task.id, channels: 'all' }));
}

function matchesChannelSelector(selector, channelId, oneBasedIndex) {
  if (selector == null || selector === 'all') return true;

  if (Array.isArray(selector)) {
    return selector.some((item) => matchesChannelSelector(item, channelId, oneBasedIndex));
  }

  if (typeof selector === 'number') {
    return oneBasedIndex === selector;
  }

  if (typeof selector === 'string') {
    const numeric = Number(selector);
    if (Number.isInteger(numeric) && numeric > 0) return oneBasedIndex === numeric;
    return selector === channelId;
  }

  if (typeof selector === 'object') {
    const from = Number(selector.from ?? selector.start ?? 1);
    const to = Number(selector.to ?? selector.end ?? from);
    if (Number.isInteger(from) && Number.isInteger(to)) {
      return oneBasedIndex >= from && oneBasedIndex <= to;
    }
  }

  return false;
}
