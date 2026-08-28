import {
  ACCURACY_PROTOCOL_VERSION,
  THRESHOLD_DIAGNOSTIC_SCHEMA_VERSION,
} from './protocol.js';

export function applyThresholdValue(taskConfig, parameterKeys, value) {
  if (!Number.isFinite(Number(value))) throw new Error('threshold value must be finite');
  if (!Array.isArray(parameterKeys) || parameterKeys.length === 0) {
    throw new Error('threshold parameter keys are required');
  }
  for (const key of parameterKeys) {
    if (String(key).includes('*')) throw new Error('threshold parameter wildcard is not allowed');
  }
  const clone = structuredClone(taskConfig);
  const params = Array.isArray(clone?.params) ? clone.params : [];
  for (const key of parameterKeys) {
    const matches = params.filter((param) => String(param?.key ?? '') === String(key));
    if (!matches.length) throw new Error(`threshold parameter not found: ${key}`);
    for (const param of matches) param.value = String(value);
  }
  return clone;
}

export function summarizeThresholdDiagnostic(values) {
  const points = (values ?? []).map((item) => {
    const valid = (item.trials ?? []).filter((trial) => ['PASS', 'FAIL'].includes(trial.status));
    const statuses = new Set(valid.map((trial) => trial.status));
    let status = 'ERROR';
    if (valid.length === 3 && statuses.size === 1) {
      status = valid[0].status === 'PASS' ? 'STABLE_PASS' : 'STABLE_FAIL';
    } else if (valid.length === 3) {
      status = 'FLAKY';
    }
    return { threshold: item.threshold, trials: item.trials ?? [], status };
  });
  return {
    evidenceKind: 'cosmo-accuracy-threshold-diagnostic',
    appliedAutomatically: false,
    points,
    stablePassValues: points.filter((item) => item.status === 'STABLE_PASS').map((item) => item.threshold),
    stableFailValues: points.filter((item) => item.status === 'STABLE_FAIL').map((item) => item.threshold),
    flakyValues: points.filter((item) => item.status === 'FLAKY').map((item) => item.threshold),
  };
}

export function assertThresholdDiagnosticDocument(value) {
  const errors = [];
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error('invalid threshold diagnostic: document must be an object');
  }
  const allowed = [
    'schemaVersion', 'protocolVersion', 'evidenceKind', 'evidenceStatus',
    'executionOutcome', 'executionReasons', 'appliedAutomatically', 'runId',
    'sourceRunSha256', 'caseId', 'parameterKeys', 'execution', 'admission',
    'suite', 'points', 'stablePassValues', 'stableFailValues', 'flakyValues',
    'device', 'toolIdentitySha256', 'repository',
  ];
  for (const key of Object.keys(value)) {
    if (!allowed.includes(key)) errors.push(`unsupported property ${key}`);
  }
  if (value.schemaVersion !== THRESHOLD_DIAGNOSTIC_SCHEMA_VERSION) {
    errors.push(`schemaVersion must equal ${THRESHOLD_DIAGNOSTIC_SCHEMA_VERSION}`);
  }
  if (value.protocolVersion !== ACCURACY_PROTOCOL_VERSION) {
    errors.push(`protocolVersion must equal ${ACCURACY_PROTOCOL_VERSION}`);
  }
  if (value.evidenceKind !== 'cosmo-accuracy-threshold-diagnostic') {
    errors.push('evidenceKind is invalid');
  }
  if (value.evidenceStatus !== 'DIAGNOSTIC') errors.push('evidenceStatus must equal DIAGNOSTIC');
  if (!['COMPLETED', 'ERROR', 'BLOCKED'].includes(value.executionOutcome)) {
    errors.push('executionOutcome is invalid');
  }
  if (value.appliedAutomatically !== false) errors.push('appliedAutomatically must be false');
  for (const field of ['runId', 'caseId']) {
    if (typeof value[field] !== 'string' || !value[field]) errors.push(`${field} is required`);
  }
  if (!/^[a-f0-9]{64}$/u.test(String(value.sourceRunSha256 ?? ''))) {
    errors.push('sourceRunSha256 must be a lowercase SHA-256');
  }
  validateStringArray(value.executionReasons, 'executionReasons', errors);
  validateStringArray(value.parameterKeys, 'parameterKeys', errors, { nonEmpty: true });
  for (const field of ['execution', 'admission', 'suite', 'device', 'repository']) {
    if (!value[field] || typeof value[field] !== 'object' || Array.isArray(value[field])) {
      errors.push(`${field} is required`);
    }
  }
  if (!/^[a-f0-9]{64}$/u.test(String(value.toolIdentitySha256 ?? ''))) {
    errors.push('toolIdentitySha256 must be a lowercase SHA-256');
  }
  if (!Array.isArray(value.points) || value.points.length === 0) {
    errors.push('points must be a non-empty array');
  } else {
    value.points.forEach((point, index) => {
      if (!point || typeof point !== 'object' || Array.isArray(point)) {
        errors.push(`points[${index}] must be an object`);
        return;
      }
      for (const key of Object.keys(point)) {
        if (!['threshold', 'status', 'trialStatuses'].includes(key)) {
          errors.push(`points[${index}] contains unsupported property ${key}`);
        }
      }
      if (!Number.isFinite(point.threshold)) errors.push(`points[${index}].threshold is invalid`);
      if (!['STABLE_PASS', 'STABLE_FAIL', 'FLAKY', 'ERROR'].includes(point.status)) {
        errors.push(`points[${index}].status is invalid`);
      }
      if (!Array.isArray(point.trialStatuses)
          || point.trialStatuses.some((status) => !['PASS', 'FAIL', 'ERROR'].includes(status))) {
        errors.push(`points[${index}].trialStatuses is invalid`);
      }
    });
  }
  for (const field of ['stablePassValues', 'stableFailValues', 'flakyValues']) {
    if (!Array.isArray(value[field]) || value[field].some((item) => !Number.isFinite(item))) {
      errors.push(`${field} must be an array of finite numbers`);
    } else if (new Set(value[field]).size !== value[field].length) {
      errors.push(`${field} must contain unique numbers`);
    }
  }
  if (errors.length) throw new Error(`invalid threshold diagnostic: ${errors.join('; ')}`);
  return value;
}

function validateStringArray(value, label, errors, { nonEmpty = false } = {}) {
  if (!Array.isArray(value)
      || (nonEmpty && value.length === 0)
      || value.some((item) => typeof item !== 'string' || !item)) {
    errors.push(`${label} must be ${nonEmpty ? 'a non-empty ' : 'an '}array of strings`);
  } else if (new Set(value).size !== value.length) {
    errors.push(`${label} must contain unique strings`);
  }
}
