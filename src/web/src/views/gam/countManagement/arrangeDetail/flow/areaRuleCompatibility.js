export const AREA_RULE_UI_TYPES = Object.freeze({
  TARGET_LIMIT: 'target-limit',
  AREA_COUNT: 'area-count',
  PASS_FLOW: 'pass-flow',
  TRIPWIRE: 'tripwire',
  DIRECTION: 'direction',
  DURATION: 'duration',
  PRESENCE: 'presence',
  LEGACY_TARGET_LIMIT: 'legacy-target-limit',
  UNKNOWN: 'unknown'
})

const getParamValue = (params, key, fallback = '') => {
  const item = (Array.isArray(params) ? params : []).find((param) => param?.key === key)
  return item?.value ?? fallback
}

const setParamValue = (params, key, value) => {
  const item = params.find((param) => param?.key === key)
  if (item) {
    item.value = String(value)
  } else {
    params.push({ key, value: String(value) })
  }
}

export const inferAreaRuleUiType = (params) => {
  const alarmType = String(getParamValue(params, 'areaAlarmType', '0'))
  if (alarmType === '0') return AREA_RULE_UI_TYPES.TARGET_LIMIT
  if (alarmType === '2') return AREA_RULE_UI_TYPES.TRIPWIRE
  if (alarmType === '3') return AREA_RULE_UI_TYPES.DIRECTION
  if (alarmType === '4') return AREA_RULE_UI_TYPES.DURATION
  if (alarmType === '5') return AREA_RULE_UI_TYPES.PRESENCE
  if (alarmType === '6') return AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT
  if (alarmType !== '1') return AREA_RULE_UI_TYPES.UNKNOWN

  const breakType = String(getParamValue(params, 'countBreakAreaType', ''))
  if (breakType === '0') return AREA_RULE_UI_TYPES.AREA_COUNT
  if (breakType === '103') return AREA_RULE_UI_TYPES.PASS_FLOW
  return AREA_RULE_UI_TYPES.UNKNOWN
}

export const getAreaRulePurpose = (type) => {
  if ([AREA_RULE_UI_TYPES.AREA_COUNT, AREA_RULE_UI_TYPES.PASS_FLOW].includes(type)) {
    return 'statistics'
  }
  if (type === AREA_RULE_UI_TYPES.UNKNOWN) return 'compatibility'
  return 'alarm'
}

/**
 * Patch only the discriminator fields understood by the existing runtime.
 * All inactive, legacy and unknown fields are intentionally retained.
 */
export const applyAreaRuleUiType = (params, type) => {
  const next = (Array.isArray(params) ? params : []).map((param) => ({ ...param }))
  const alarmTypeByUiType = {
    [AREA_RULE_UI_TYPES.TARGET_LIMIT]: '0',
    [AREA_RULE_UI_TYPES.AREA_COUNT]: '1',
    [AREA_RULE_UI_TYPES.PASS_FLOW]: '1',
    [AREA_RULE_UI_TYPES.TRIPWIRE]: '2',
    [AREA_RULE_UI_TYPES.DIRECTION]: '3',
    [AREA_RULE_UI_TYPES.DURATION]: '4',
    [AREA_RULE_UI_TYPES.PRESENCE]: '5',
    [AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT]: '6'
  }
  const alarmType = alarmTypeByUiType[type]
  if (alarmType === undefined) return next

  setParamValue(next, 'areaAlarmType', alarmType)
  if (type === AREA_RULE_UI_TYPES.AREA_COUNT) {
    setParamValue(next, 'countBreakAreaType', '0')
  } else if (type === AREA_RULE_UI_TYPES.PASS_FLOW) {
    setParamValue(next, 'countBreakAreaType', '103')
  }
  return next
}

export const mergeParamsPreservingUnknown = (original, generated) => {
  const result = (Array.isArray(generated) ? generated : []).map((param) => ({ ...param }))
  const generatedKeys = new Set(result.map((param) => param?.key))
  ;(Array.isArray(original) ? original : []).forEach((param) => {
    if (!generatedKeys.has(param?.key)) result.push({ ...param })
  })
  return result
}

// The runtime reads config.params and the channel reads webConfig.metaDataParams.
// Keep known, inactive branch inputs in editor storage for a later switch back.
export const separateInactiveAreaRuleFields = (config, definitions, sceneParams = []) => {
  const rule = getParamValue(config.params, 'areaAlarmType')
  if (rule === '' || inferAreaRuleUiType(config.params) === AREA_RULE_UI_TYPES.UNKNOWN) return config
  const branches = new Map(definitions
    .filter(field => field?.dependsOn?.key === 'areaAlarmType')
    .map(field => [field.key, String(field.dependsOn.value)]))
  if (!branches.size) return config

  const web = config.webConfig || {}
  const history = web.inactiveAreaRule || {}
  const split = (fields, previous) => {
    const active = []
    const inactive = []
    const liveKeys = new Set((Array.isArray(fields) ? fields : []).map(field => field.key))
    for (const field of mergeParamsPreservingUnknown(previous, fields)) {
      const owner = branches.get(field.key)
      if (owner === String(rule) || (owner === undefined && liveKeys.has(field.key))) {
        active.push(field)
      } else {
        inactive.push(field)
      }
    }
    return { active, inactive }
  }
  const params = split(config.params, history.params)
  const metadata = split(web.metaDataParams, history.metaDataParams)
  if (!params.inactive.length && !metadata.inactive.length && !web.inactiveAreaRule) return config
  const nextWeb = { ...web, metaDataParams: metadata.active }
  // Scene-level overrides are separate from generated node descriptors. Mixing
  // them makes two nodes that share one channel field report conflicting schemas.
  const inactiveKeys = new Set(metadata.inactive.map(field => field.key))
  const sceneHistory = mergeParamsPreservingUnknown(history.sceneParams,
    (Array.isArray(sceneParams) ? sceneParams : []).filter(field => inactiveKeys.has(field.key)))
  if (params.inactive.length || metadata.inactive.length || sceneHistory.length) {
    nextWeb.inactiveAreaRule = { params: params.inactive, metaDataParams: metadata.inactive, sceneParams: sceneHistory }
  } else {
    delete nextWeb.inactiveAreaRule
  }
  return { ...config, params: params.active, webConfig: nextWeb }
}

const AREA_RULE_FIELD_KEYS = Object.freeze({
  inputAreaType: 'decisionRegion',
  'param.areaLimitTargetCount': 'targetCountThreshold',
  'param.areaLimitTargetType': 'triggerCondition',
  'param.areaLimitDuration': 'conditionDuration',
  'param.areaLimitDurationTimeType': 'durationUnit',
  'param.areaCalcDuration': 'reportingPeriod',
  'param.areaCalcDurationTimeType': 'reportingPeriodUnit',
  breakAreaType: 'crossingDirection',
  'param.trippingWireType': 'requiredLineCount',
  durationBreakAreaType: 'stayStartCondition',
  'param.areaDuration': 'minimumStayDuration',
  'param.areaDurationTimeType': 'stayDurationUnit',
  'param.retroDirect': 'prohibitedDirection',
  'param.retroDistance': 'minimumAbnormalDisplacement',
  areaLimitTargetCount: 'legacyTargetCountThreshold',
  areaLimitTargetType: 'legacyTriggerCondition',
  areaLimitDuration: 'legacyConditionDuration',
  areaDurationTimeType: 'legacyDurationUnit',
  targetCountChange: 'periodicDataChangeMarker'
})

const translate = (translator, key, params = {}) =>
  typeof translator === 'function' ? translator(key, params) : key

export const getAreaRuleFieldText = (key, translator) => {
  const fieldKey = AREA_RULE_FIELD_KEYS[key]
  if (!fieldKey) return null
  const prefix = `flow.areaRule.fields.${fieldKey}`
  return {
    name: translate(translator, `${prefix}.name`),
    description: translate(translator, `${prefix}.description`)
  }
}

const COMPARE_OPTION_KEYS = {
  '0': 'lessThan',
  '1': 'greaterThan',
  '2': 'notGreaterThan',
  '3': 'notLessThan',
  '4': 'equalTo'
}

const COMPARE_SYMBOL = {
  '0': '<',
  '1': '>',
  '2': '≤',
  '3': '≥',
  '4': '='
}

const UNIT_OPTION_KEYS = {
  '1': 'milliseconds',
  '1000': 'seconds',
  '60000': 'minutes',
  '3600000': 'hours'
}

export const getAreaRuleOptionLabel = (key, value, fallback, translator) => {
  const stringValue = String(value)
  if (['param.areaLimitTargetType', 'areaLimitTargetType'].includes(key)) {
    const optionKey = COMPARE_OPTION_KEYS[stringValue]
    return optionKey
      ? translate(translator, `flow.areaRule.options.compare.${optionKey}`)
      : fallback
  }
  if (key === 'param.trippingWireType') {
    if (!['1', '2'].includes(stringValue)) return fallback
    return translate(translator, `flow.areaRule.options.tripwire.count${stringValue}`)
  }
  if (key === 'durationBreakAreaType') {
    const optionKey = stringValue === '0' ? 'inside' : stringValue === '2' ? 'entered' : ''
    return optionKey
      ? translate(translator, `flow.areaRule.options.stayStart.${optionKey}`)
      : fallback
  }
  if (key === 'param.retroDirect') {
    const optionKey = stringValue === '0' ? 'bottomToTop' : stringValue === '1' ? 'topToBottom' : ''
    return optionKey
      ? translate(translator, `flow.areaRule.options.direction.${optionKey}`)
      : fallback
  }
  return fallback
}

const summaryParamValue = (params, primaryKey, legacyKey, fallback = '') =>
  getParamValue(params, primaryKey, getParamValue(params, legacyKey, fallback))

export const buildAreaRuleSummary = (params, type, translator) => {
  const duration = (valueKey, unitKey) => {
    const value = getParamValue(params, valueKey, '0')
    const unitKeyName = UNIT_OPTION_KEYS[String(getParamValue(params, unitKey, '1000'))] || 'seconds'
    const unit = translate(translator, `flow.areaRule.options.unit.${unitKeyName}`)
    return value === '0'
      ? translate(translator, 'flow.areaRule.summary.currentFrame')
      : translate(translator, 'flow.areaRule.summary.satisfiedFor', { value, unit })
  }
  if ([AREA_RULE_UI_TYPES.TARGET_LIMIT, AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT].includes(type)) {
    const legacy = type === AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT
    const count = summaryParamValue(
      params,
      legacy ? 'areaLimitTargetCount' : 'param.areaLimitTargetCount',
      legacy ? 'param.areaLimitTargetCount' : 'areaLimitTargetCount',
      '0'
    )
    const compare = summaryParamValue(
      params,
      legacy ? 'areaLimitTargetType' : 'param.areaLimitTargetType',
      legacy ? 'param.areaLimitTargetType' : 'areaLimitTargetType',
      '0'
    )
    const durationKey = legacy ? 'areaLimitDuration' : 'param.areaLimitDuration'
    const unitKey = legacy ? 'areaDurationTimeType' : 'param.areaLimitDurationTimeType'
    const expression = COMPARE_SYMBOL[String(compare)]
      ? translate(translator, 'flow.areaRule.summary.targetExpression', {
          symbol: COMPARE_SYMBOL[String(compare)],
          count
        })
      : translate(translator, 'flow.areaRule.summary.targetExpressionFallback')
    return translate(translator, 'flow.areaRule.summary.targetLimit', {
      expression,
      duration: duration(durationKey, unitKey)
    })
  }
  if (type === AREA_RULE_UI_TYPES.AREA_COUNT) {
    return translate(translator, 'flow.areaRule.summary.areaCount')
  }
  if (type === AREA_RULE_UI_TYPES.PASS_FLOW) {
    return translate(translator, 'flow.areaRule.summary.passFlow')
  }
  if (type === AREA_RULE_UI_TYPES.TRIPWIRE) {
    const count = getParamValue(params, 'param.trippingWireType', '1')
    return translate(translator, 'flow.areaRule.summary.tripwire', { count })
  }
  if (type === AREA_RULE_UI_TYPES.DIRECTION) {
    const direction = getAreaRuleOptionLabel(
      'param.retroDirect',
      getParamValue(params, 'param.retroDirect', '0'),
      translate(translator, 'flow.areaRule.options.direction.specified'),
      translator
    )
    const distance = Number(getParamValue(params, 'param.retroDistance', '0.05')) * 100
    return translate(translator, 'flow.areaRule.summary.direction', {
      direction,
      distance: Number.isFinite(distance) ? distance : 5
    })
  }
  if (type === AREA_RULE_UI_TYPES.DURATION) {
    const condition = getAreaRuleOptionLabel(
      'durationBreakAreaType',
      getParamValue(params, 'durationBreakAreaType', '0'),
      translate(translator, 'flow.areaRule.options.stayStart.insideFallback'),
      translator
    )
    return translate(translator, 'flow.areaRule.summary.duration', {
      condition,
      duration: duration('param.areaDuration', 'param.areaDurationTimeType')
    })
  }
  if (type === AREA_RULE_UI_TYPES.PRESENCE) {
    return translate(translator, 'flow.areaRule.summary.presence')
  }
  return translate(translator, 'flow.areaRule.summary.unknown')
}
