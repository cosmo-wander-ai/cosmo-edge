export const ALARM_PRIVACY_PARAM_KEYS = new Set([
  'param.privacyEnabled',
  'param.privacyLabels',
  'param.privacyStrength'
])

export const createAlarmPrivacyDefaults = () => ({
  enabled: '0',
  labels: '*',
  strength: '2'
})

export const readAlarmPrivacyParams = (params) => {
  const defaults = createAlarmPrivacyDefaults()
  const values = new Map((Array.isArray(params) ? params : [])
    .map((param) => [param.key, param.value]))
  return {
    enabled: String(values.get('param.privacyEnabled') ?? defaults.enabled),
    labels: String(values.get('param.privacyLabels') ?? defaults.labels),
    strength: String(values.get('param.privacyStrength') ?? defaults.strength)
  }
}

export const mergeAlarmPrivacyParams = (params, privacy) => [
  ...(Array.isArray(params) ? params : [])
    .filter((param) => !ALARM_PRIVACY_PARAM_KEYS.has(param.key)),
  { key: 'param.privacyEnabled', value: privacy.enabled },
  { key: 'param.privacyLabels', value: privacy.labels },
  { key: 'param.privacyStrength', value: privacy.strength }
]

// A detection-position descriptor identifies a detector's configured class.
// Confidence descriptors alone can also belong to classifiers, so do not use
// them to advertise full-frame detection capabilities.
export const getAlarmPrivacyLabels = (metadata) => {
  const labels = new Set()
  for (const param of Array.isArray(metadata?.params) ? metadata.params : []) {
    const match = /^aiParam\.(.+)\.detPostion$/.exec(String(param.key || ''))
    if (match && !match[1].includes(',') && match[1] !== '*') {
      labels.add(match[1])
    }
  }
  return [...labels]
}

export const isAlarmPrivacyConfigValid = (privacy, availableLabels) => {
  if (!['0', '1'].includes(privacy.enabled) ||
      !['1', '2', '3'].includes(privacy.strength)) return false
  if (privacy.labels === '*') return true
  // Match the backend label syntax even when privacy is disabled: no empty
  // tokens, ASCII control/space characters, or wildcards mixed with classes.
  if (typeof privacy.labels !== 'string' ||
      !/^[^,\x00-\x20\x7f*]+(?:,[^,\x00-\x20\x7f*]+)*$/.test(privacy.labels)) return false
  if (privacy.enabled === '0') return true
  return privacy.labels.split(',').every((label) => availableLabels.includes(label))
}
