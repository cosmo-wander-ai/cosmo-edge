import { isPlainObject } from './utils.js';
import {
  ACCURACY_PROTOCOL_VERSION,
  ACCURACY_SUMMARY_SCHEMA_VERSION,
} from './protocol.js';

const HASH = /^[a-f0-9]{64}$/u;
const CASE_STATUSES = new Set(['PASS', 'FAIL', 'ERROR']);
const EXECUTION_OUTCOMES = new Set(['COMPLETED', 'ERROR', 'BLOCKED']);
const CHECK_STATUSES = new Set(['PASS', 'FAIL', 'UNVERIFIED']);

// This validator checks only fields consumed by render/compare. Extra fields are
// intentionally tolerated so a completed measurement is not rejected by a
// newer producer adding optional metadata.
export function validateAccuracySummary(value) {
  const errors = [];
  if (!object(value, 'summary', errors)) return { valid: false, errors };
  equal(value.schemaVersion, ACCURACY_SUMMARY_SCHEMA_VERSION, 'schemaVersion', errors);
  equal(value.protocolVersion, ACCURACY_PROTOCOL_VERSION, 'protocolVersion', errors);
  oneOf(value.executionOutcome, EXECUTION_OUTCOMES, 'executionOutcome', errors);
  strings(value.executionReasons, 'executionReasons', errors);
  validateExecution(value.execution, errors);
  validateSuite(value.suite, errors);
  validateTasks(value.tasks, errors);
  validateCases(value.cases, errors);
  validateMetrics(value.metrics, errors);
  validateAdmission(value.admission, errors);
  object(value.performance, 'performance', errors);
  validateRelationships(value, errors);
  return { valid: errors.length === 0, errors };
}

export function assertAccuracySummary(value) {
  const result = validateAccuracySummary(value);
  if (!result.valid) throw new Error(`invalid accuracy summary: ${result.errors.join('; ')}`);
  return value;
}

function validateExecution(value, errors) {
  if (!object(value, 'execution', errors)) return;
  oneOf(value.profile, new Set(['full', 'quick']), 'execution.profile', errors);
  oneOf(value.concurrency, new Set([1, 2, 4]), 'execution.concurrency', errors);
  validateSelection(value.selection, 'execution.selection', errors);
  hash(value.measurementConfigSha256, 'execution.measurementConfigSha256', errors);
}

function validateSelection(value, label, errors) {
  if (!object(value, label, errors)) return;
  integer(value.count, `${label}.count`, errors, 1);
  hash(value.sha256, `${label}.sha256`, errors);
}

function validateSuite(value, errors) {
  if (!object(value, 'suite', errors)) return;
  nonEmpty(value.id, 'suite.id', errors);
  nonEmpty(value.displayName, 'suite.displayName', errors);
  hash(value.suiteSha256, 'suite.suiteSha256', errors);
  hash(value.caseManifestSha256, 'suite.caseManifestSha256', errors);
  hash(value.caseSetSha256, 'suite.caseSetSha256', errors);
  oneOf(value.sourceMode, new Set(['local', 'rtsp-deterministic']), 'suite.sourceMode', errors);
  nonEmpty(value.targetChip, 'suite.targetChip', errors);
}

function validateTasks(value, errors) {
  if (!array(value, 'tasks', errors)) return;
  value.forEach((task, index) => {
    const label = `tasks[${index}]`;
    if (!object(task, label, errors)) return;
    nonEmpty(task.id, `${label}.id`, errors);
    nonEmpty(task.displayName, `${label}.displayName`, errors);
    oneOf(task.kind, new Set(['cv', 'vlm']), `${label}.kind`, errors);
    nonEmpty(task.algorithmId, `${label}.algorithmId`, errors);
    nonEmpty(task.algorithmCode, `${label}.algorithmCode`, errors);
    oneOf(
      task.configSource,
      new Set(['frozen', 'device-default', 'unknown']),
      `${label}.configSource`,
      errors,
    );
    if (array(task.taskConfigHashes, `${label}.taskConfigHashes`, errors)) {
      task.taskConfigHashes.forEach((item, hashIndex) =>
        hash(item, `${label}.taskConfigHashes[${hashIndex}]`, errors));
    }
    for (const name of ['positive', 'negative', 'custom']) {
      validateCountMetric(task[name], `${label}.${name}`, errors);
    }
    rate(task.falsePositiveRate, `${label}.falsePositiveRate`, errors);
    integer(task.errors, `${label}.errors`, errors);
    if (object(task.coverage, `${label}.coverage`, errors)) {
      if (typeof task.coverage.positive !== 'boolean') {
        errors.push(`${label}.coverage.positive must be boolean`);
      }
      if (typeof task.coverage.negative !== 'boolean') {
        errors.push(`${label}.coverage.negative must be boolean`);
      }
      integer(task.coverage.customCases, `${label}.coverage.customCases`, errors);
      integer(task.coverage.totalCases, `${label}.coverage.totalCases`, errors);
    }
  });
}

function validateCountMetric(value, label, errors) {
  if (!object(value, label, errors)) return;
  integer(value.passed, `${label}.passed`, errors);
  integer(value.total, `${label}.total`, errors);
  rate(value.rate, `${label}.rate`, errors);
  if (Number.isInteger(value.passed) && Number.isInteger(value.total)
      && value.passed > value.total) {
    errors.push(`${label}.passed cannot exceed total`);
  }
}

function validateCases(value, errors) {
  if (!array(value, 'cases', errors)) return;
  value.forEach((item, index) => {
    const label = `cases[${index}]`;
    if (!object(item, label, errors)) return;
    nonEmpty(item.id, `${label}.id`, errors);
    nonEmpty(item.task, `${label}.task`, errors);
    validateExpectation(item.expectation, `${label}.expectation`, errors);
    oneOf(item.status, CASE_STATUSES, `${label}.status`, errors);
    if (typeof item.cleanupBlocked !== 'boolean') {
      errors.push(`${label}.cleanupBlocked must be boolean`);
    }
    nullableInteger(item.eventCount, `${label}.eventCount`, errors);
    nullableInteger(item.trialCount, `${label}.trialCount`, errors);
    strings(item.reasons, `${label}.reasons`, errors);
    array(item.alertArtifacts, `${label}.alertArtifacts`, errors);
    object(item.eventConvergence, `${label}.eventConvergence`, errors);
    object(item.cleanup, `${label}.cleanup`, errors);
  });
}

function validateExpectation(value, label, errors) {
  if (!object(value, label, errors)) return;
  const hasMin = Object.hasOwn(value, 'minEvents');
  const hasMax = Object.hasOwn(value, 'maxEvents');
  if (!hasMin && !hasMax) errors.push(`${label} requires minEvents or maxEvents`);
  if (hasMin) integer(value.minEvents, `${label}.minEvents`, errors);
  if (hasMax) integer(value.maxEvents, `${label}.maxEvents`, errors);
  if (hasMin && hasMax && value.minEvents > value.maxEvents) {
    errors.push(`${label}.minEvents cannot exceed maxEvents`);
  }
}

function validateMetrics(value, errors) {
  if (!object(value, 'metrics', errors)) return;
  for (const name of ['micro', 'macro']) {
    const metric = value[name];
    if (!object(metric, `metrics.${name}`, errors)) continue;
    for (const field of ['positiveHitRate', 'negativeCleanRate', 'negativeFalsePositiveRate']) {
      rate(metric[field], `metrics.${name}.${field}`, errors);
    }
  }
  if (!object(value.coverage, 'metrics.coverage', errors)) return;
  integer(value.coverage.taskCount, 'metrics.coverage.taskCount', errors);
  integer(value.coverage.tasksWithPositive, 'metrics.coverage.tasksWithPositive', errors);
  integer(value.coverage.tasksWithNegative, 'metrics.coverage.tasksWithNegative', errors);
  strings(value.coverage.missingPositiveTasks, 'metrics.coverage.missingPositiveTasks', errors);
  strings(value.coverage.missingNegativeTasks, 'metrics.coverage.missingNegativeTasks', errors);
}

function validateAdmission(value, errors) {
  if (!object(value, 'admission', errors)) return;
  oneOf(value.status, CHECK_STATUSES, 'admission.status', errors);
  nonEmpty(value.checkedAt, 'admission.checkedAt', errors);
  oneOf(value.profile, new Set(['full', 'quick']), 'admission.profile', errors);
  oneOf(value.concurrency, new Set([1, 2, 4]), 'admission.concurrency', errors);
  validateSelection(value.selection, 'admission.selection', errors);
  if (!object(value.media, 'admission.media', errors)) return;
  for (const field of ['checked', 'valid', 'invalid']) {
    integer(value.media[field], `admission.media.${field}`, errors);
  }
  if (array(value.checks, 'admission.checks', errors)) {
    value.checks.forEach((check, index) => {
      const label = `admission.checks[${index}]`;
      if (!object(check, label, errors)) return;
      nonEmpty(check.name, `${label}.name`, errors);
      oneOf(check.status, CHECK_STATUSES, `${label}.status`, errors);
      if (typeof check.blocking !== 'boolean') errors.push(`${label}.blocking must be boolean`);
    });
  }
  strings(value.warnings, 'admission.warnings', errors);
}

function validateRelationships(value, errors) {
  if (value.admission && value.execution) {
    if (value.admission.profile !== value.execution.profile) {
      errors.push('admission.profile must match execution.profile');
    }
    if (value.admission.concurrency !== value.execution.concurrency) {
      errors.push('admission.concurrency must match execution.concurrency');
    }
    if (value.admission.selection?.sha256 !== value.execution.selection?.sha256
        || value.admission.selection?.count !== value.execution.selection?.count) {
      errors.push('admission.selection must match execution.selection');
    }
  }
  const taskIds = new Set();
  for (const [index, task] of (value.tasks ?? []).entries()) {
    if (taskIds.has(task.id)) errors.push(`tasks[${index}].id is duplicated`);
    taskIds.add(task.id);
  }
  const caseIds = new Set();
  for (const [index, item] of (value.cases ?? []).entries()) {
    if (caseIds.has(item.id)) errors.push(`cases[${index}].id is duplicated`);
    caseIds.add(item.id);
    if (!taskIds.has(item.task)) errors.push(`cases[${index}].task is unknown`);
  }
  if (Array.isArray(value.cases) && value.execution?.selection?.count !== value.cases.length) {
    errors.push('execution.selection.count must match cases.length');
  }
}

function object(value, label, errors) {
  if (isPlainObject(value)) return true;
  errors.push(`${label} must be an object`);
  return false;
}

function array(value, label, errors) {
  if (Array.isArray(value)) return true;
  errors.push(`${label} must be an array`);
  return false;
}

function strings(value, label, errors) {
  if (!array(value, label, errors)) return;
  value.forEach((item, index) => nonEmpty(item, `${label}[${index}]`, errors));
}

function nonEmpty(value, label, errors) {
  if (typeof value !== 'string' || !value.trim()) errors.push(`${label} must be a non-empty string`);
}

function integer(value, label, errors, minimum = 0) {
  if (!Number.isInteger(value) || value < minimum) errors.push(`${label} must be an integer >= ${minimum}`);
}

function nullableInteger(value, label, errors) {
  if (value !== null) integer(value, label, errors);
}

function rate(value, label, errors) {
  if (value !== null && (!Number.isFinite(value) || value < 0 || value > 1)) {
    errors.push(`${label} must be null or a number between 0 and 1`);
  }
}

function hash(value, label, errors) {
  if (typeof value !== 'string' || !HASH.test(value)) errors.push(`${label} must be a lowercase SHA-256`);
}

function equal(value, expected, label, errors) {
  if (value !== expected) errors.push(`${label} must equal ${expected}`);
}

function oneOf(value, allowed, label, errors) {
  if (!allowed.has(value)) errors.push(`${label} is invalid`);
}
