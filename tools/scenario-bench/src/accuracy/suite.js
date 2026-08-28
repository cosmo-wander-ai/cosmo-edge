import fs from 'node:fs';
import path from 'node:path';
import { parse as parseYaml } from 'yaml';

import {
  assertFileContained,
  assertSha256,
  isPlainObject,
  requireNonNegativeInteger,
  requirePositiveNumber,
  resolveContainedPath,
  sha256Buffer,
  sha256File,
  stableStringify,
} from './utils.js';
import { selectAccuracyCases } from './selection.js';
import {
  ACCURACY_PROTOCOL_VERSION,
  ACCURACY_SUITE_SCHEMA_VERSION,
} from './protocol.js';

export { ACCURACY_PROTOCOL_VERSION, ACCURACY_SUITE_SCHEMA_VERSION } from './protocol.js';
const SOURCE_MODES = new Set(['local', 'rtsp-deterministic']);
const TASK_KINDS = new Set(['cv', 'vlm']);
const TARGET_PLATFORMS = new Set(['bm1688', 'cv186x', 'rk3576', 'rv1126b']);

export function loadAccuracySuite({
  suitePath,
  dataRoot,
  selection = null,
} = {}) {
  if (!suitePath) throw new Error('suitePath is required');
  if (!dataRoot) throw new Error('dataRoot is required');
  const absoluteSuitePath = path.resolve(suitePath);
  const suiteDir = path.dirname(absoluteSuitePath);
  const suiteBytes = fs.readFileSync(absoluteSuitePath);
  const raw = parseYaml(suiteBytes.toString('utf8'));
  if (!isPlainObject(raw)) throw new Error('suite.yml must contain an object');
  if (raw.schemaVersion === 2) {
    throw new Error('accuracy schemaVersion 2 is read-only; copy and migrate the suite to v3');
  }
  if (raw.schemaVersion !== ACCURACY_SUITE_SCHEMA_VERSION) {
    throw new Error(`unsupported accuracy schemaVersion ${raw.schemaVersion}`);
  }
  const id = requireIdentifier(raw.id, 'suite id');
  const sourceMode = String(raw.sourceMode ?? '');
  if (!SOURCE_MODES.has(sourceMode)) {
    throw new Error(`sourceMode must be one of: ${[...SOURCE_MODES].join(', ')}`);
  }
  const targetPlatforms = requireStringArray(raw.targetPlatforms, 'targetPlatforms');
  for (const platform of targetPlatforms) {
    if (!TARGET_PLATFORMS.has(platform)) throw new Error(`unsupported target platform: ${platform}`);
  }
  const defaults = normalizeDefaults(raw.defaults);
  const tasks = normalizeTasks(raw.tasks, suiteDir);
  if (sourceMode === 'local') validateLocalLoopConfigs(tasks);
  const taskById = new Map(tasks.map((task) => [task.id, task]));

  const manifestRelative = raw.dataset?.manifest;
  const manifestPath = resolveContainedPath(suiteDir, manifestRelative, 'dataset manifest');
  if (!fs.existsSync(manifestPath)) throw new Error(`dataset manifest not found: ${manifestRelative}`);
  const manifestBytes = fs.readFileSync(manifestPath);
  const cases = parseCases(manifestBytes.toString('utf8'), dataRoot, taskById);
  const selectedCases = selection ? selectAccuracyCases({ tasks, cases }, selection) : cases;
  verifyCaseHashes(selectedCases, dataRoot);

  const taskConfigSha256 = Object.fromEntries(
    tasks.map((task) => [task.id, task.taskConfigSha256 ?? null]),
  );
  const identity = {
    protocolVersion: ACCURACY_PROTOCOL_VERSION,
    schemaVersion: ACCURACY_SUITE_SCHEMA_VERSION,
    suiteId: id,
    suiteSha256: sha256Buffer(suiteBytes),
    caseManifestSha256: sha256Buffer(manifestBytes),
    caseSetSha256: sha256Buffer(Buffer.from(stableStringify(
      cases.map((item) => ({
        id: item.id,
        task: item.task,
        file: item.file,
        sha256: item.sha256,
        expectation: item.expectation,
        observeSec: item.observeSec,
      })),
    ))),
    taskConfigSha256,
    sourceMode,
  };

  return {
    schemaVersion: ACCURACY_SUITE_SCHEMA_VERSION,
    protocolVersion: ACCURACY_PROTOCOL_VERSION,
    id,
    displayName: String(raw.displayName ?? id),
    sourceMode,
    targetPlatforms,
    defaults,
    tasks,
    cases,
    rtsp: normalizeRtsp(raw.rtsp, sourceMode),
    identity,
  };
}

function normalizeDefaults(raw = {}) {
  const normalized = {
    observeSec: requirePositiveNumber(raw.observeSec ?? 45, 'defaults.observeSec'),
    eventFlushTimeoutSec: requirePositiveNumber(
      raw.eventFlushTimeoutSec ?? 15,
      'defaults.eventFlushTimeoutSec',
    ),
    eventPollIntervalSec: requirePositiveNumber(
      raw.eventPollIntervalSec ?? 1,
      'defaults.eventPollIntervalSec',
    ),
    eventSettleMinSec: requirePositiveNumber(
      raw.eventSettleMinSec ?? 5,
      'defaults.eventSettleMinSec',
    ),
    earlyStopPollIntervalSec: requirePositiveNumber(
      raw.earlyStopPollIntervalSec ?? 5,
      'defaults.earlyStopPollIntervalSec',
    ),
    readyTimeoutSec: requirePositiveNumber(raw.readyTimeoutSec ?? 120, 'defaults.readyTimeoutSec'),
    readyPollIntervalSec: requirePositiveNumber(
      raw.readyPollIntervalSec ?? 1,
      'defaults.readyPollIntervalSec',
    ),
    infrastructureRetriesPerTrial: requireNonNegativeInteger(
      raw.infrastructureRetriesPerTrial ?? 1,
      'defaults.infrastructureRetriesPerTrial',
    ),
  };
  if (normalized.eventSettleMinSec > normalized.eventFlushTimeoutSec) {
    throw new Error('defaults.eventSettleMinSec cannot exceed eventFlushTimeoutSec');
  }
  return normalized;
}

function normalizeTasks(rawTasks, suiteDir) {
  if (!Array.isArray(rawTasks) || rawTasks.length === 0) throw new Error('tasks must be non-empty');
  const seen = new Set();
  return rawTasks.map((raw, index) => {
    if (!isPlainObject(raw)) throw new Error(`tasks[${index}] must be an object`);
    const id = requireIdentifier(raw.id, `tasks[${index}].id`);
    if (seen.has(id)) throw new Error(`duplicate task id: ${id}`);
    seen.add(id);
    const kind = String(raw.kind ?? 'cv').toLowerCase();
    if (!TASK_KINDS.has(kind)) throw new Error(`task ${id} kind must be cv or vlm`);
    const algorithmId = requireNonEmptyString(raw.algorithmId, `task ${id} algorithmId`);
    const scheduleId = requireNonEmptyString(raw.scheduleId, `task ${id} scheduleId`);
    const configSource = raw.configSource ?? (raw.taskConfig ? 'frozen' : 'device-default');
    let taskConfig = null;
    let taskConfigFile = null;
    let taskConfigSha256 = null;
    if (configSource === 'frozen') {
      taskConfigFile = String(raw.taskConfig ?? '');
      const absolute = assertFileContained(suiteDir, taskConfigFile, `task ${id} taskConfig`);
      taskConfig = JSON.parse(fs.readFileSync(absolute, 'utf8'));
      if (!isPlainObject(taskConfig)) throw new Error(`task ${id} taskConfig must be an object`);
      taskConfigSha256 = sha256File(absolute);
    } else if (configSource !== 'device-default') {
      throw new Error(`task ${id} must use a frozen taskConfig or device-default`);
    }
    return {
      id,
      displayName: String(raw.displayName ?? id),
      kind,
      algorithmId,
      algorithmCode: String(raw.algorithmCode ?? algorithmId),
      scheduleId,
      configSource,
      taskConfigFile,
      taskConfig,
      taskConfigSha256,
      vlmCompletionActionId: raw.vlmCompletionActionId ?? null,
      thresholdDiagnostic: normalizeThresholdDiagnostic(raw.thresholdDiagnostic, id, kind),
    };
  });
}

function normalizeThresholdDiagnostic(raw, taskId, kind) {
  if (raw == null) return null;
  if (!isPlainObject(raw)) throw new Error(`task ${taskId} thresholdDiagnostic must be an object`);
  const parameterKeys = requireStringArray(raw.parameterKeys, `task ${taskId} threshold parameterKeys`);
  for (const key of parameterKeys) {
    if (key.includes('*')) throw new Error(`task ${taskId} threshold parameter keys cannot use wildcards`);
  }
  const values = Array.isArray(raw.values) ? raw.values.map(Number) : [];
  if (!values.length || values.some((value) => !Number.isFinite(value))) {
    throw new Error(`task ${taskId} threshold values must be finite numbers`);
  }
  if (new Set(values).size !== values.length) {
    throw new Error(`task ${taskId} threshold values contain duplicates`);
  }
  if (kind === 'vlm' && raw.allowVlm !== true) {
    throw new Error(`task ${taskId} VLM threshold diagnostics require allowVlm: true`);
  }
  return { parameterKeys, values, allowVlm: raw.allowVlm === true };
}

function parseCases(text, dataRoot, taskById) {
  const seen = new Set();
  const rows = [];
  for (const [index, line] of text.split(/\r?\n/u).entries()) {
    if (!line.trim()) continue;
    let raw;
    try {
      raw = JSON.parse(line);
    } catch (error) {
      throw new Error(`cases.jsonl line ${index + 1} is invalid JSON: ${error.message}`);
    }
    const id = requireIdentifier(raw.id, `case line ${index + 1} id`);
    if (seen.has(id)) throw new Error(`duplicate case id: ${id}`);
    seen.add(id);
    const task = requireNonEmptyString(raw.task, `case ${id} task`);
    if (!taskById.has(task)) throw new Error(`case ${id} references unknown task ${task}`);
    const relativeFile = String(raw.file ?? '');
    const absoluteFile = resolveContainedPath(dataRoot, relativeFile, `case ${id} file`);
    const expectedSha256 = assertSha256(raw.sha256, `case ${id} sha256`);
    const expectation = normalizeExpectation(raw.expectation, id);
    const tags = raw.tags == null ? [] : requireStringArray(raw.tags, `case ${id} tags`, true);
    rows.push({
      id,
      task,
      file: relativeFile.replaceAll('\\', '/'),
      absoluteFile,
      sha256: expectedSha256,
      expectation,
      critical: raw.critical === true,
      tags,
      observeSec: raw.observeSec == null
        ? null
        : requirePositiveNumber(raw.observeSec, `case ${id} observeSec`),
    });
  }
  if (!rows.length) throw new Error('dataset manifest contains no cases');
  return rows;
}

function verifyCaseHashes(cases, dataRoot) {
  for (const item of cases) {
    item.absoluteFile = assertFileContained(dataRoot, item.file, `case ${item.id} file`);
    const actualSha256 = sha256File(item.absoluteFile);
    if (actualSha256 !== item.sha256) {
      throw new Error(`case ${item.id} SHA256 mismatch: expected ${item.sha256}, got ${actualSha256}`);
    }
  }
}

function normalizeExpectation(raw, caseId) {
  if (!isPlainObject(raw)) throw new Error(`case ${caseId} expectation must be an object`);
  if (raw.minEvents == null && raw.maxEvents == null) {
    throw new Error(`case ${caseId} expectation requires minEvents or maxEvents`);
  }
  const minEvents = raw.minEvents == null
    ? null
    : requireNonNegativeInteger(raw.minEvents, `case ${caseId} minEvents`);
  const maxEvents = raw.maxEvents == null
    ? null
    : requireNonNegativeInteger(raw.maxEvents, `case ${caseId} maxEvents`);
  if (minEvents != null && maxEvents != null && minEvents > maxEvents) {
    throw new Error(`case ${caseId} minEvents cannot exceed maxEvents`);
  }
  return { ...(minEvents == null ? {} : { minEvents }), ...(maxEvents == null ? {} : { maxEvents }) };
}

function normalizeRtsp(raw, sourceMode) {
  if (sourceMode !== 'rtsp-deterministic') return null;
  if (!isPlainObject(raw)) throw new Error('rtsp configuration is required for rtsp-deterministic');
  return {
    httpBase: requireNonEmptyString(raw.httpBase, 'rtsp.httpBase').replace(/\/+$/u, ''),
    mediaMtxBase: requireNonEmptyString(raw.mediaMtxBase, 'rtsp.mediaMtxBase').replace(/\/+$/u, ''),
    ffmpeg: String(raw.ffmpeg ?? 'ffmpeg'),
    ffprobe: String(raw.ffprobe ?? 'ffprobe'),
  };
}

function validateLocalLoopConfigs(tasks) {
  for (const task of tasks) {
    if (task.configSource !== 'frozen') continue;
    const repeat = (task.taskConfig?.params ?? []).find(
      (param) => String(param?.key ?? '') === 'param.videoRepeatCount',
    );
    if (!repeat || String(repeat.value) !== '0') {
      throw new Error(`task ${task.id} local measurement requires param.videoRepeatCount=0`);
    }
  }
}

function requireIdentifier(value, label) {
  const string = requireNonEmptyString(value, label);
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/u.test(string)) {
    throw new Error(`${label} contains unsupported characters`);
  }
  return string;
}

function requireNonEmptyString(value, label) {
  const string = String(value ?? '').trim();
  if (!string) throw new Error(`${label} is required`);
  return string;
}

function requireStringArray(value, label, allowEmpty = false) {
  if (!Array.isArray(value) || (!allowEmpty && value.length === 0)) {
    throw new Error(`${label} must be ${allowEmpty ? 'an' : 'a non-empty'} array`);
  }
  const items = value.map((item) => requireNonEmptyString(item, label));
  if (new Set(items).size !== items.length) throw new Error(`${label} contains duplicates`);
  return items;
}
