import assert from 'node:assert/strict'
import {
  AREA_RULE_UI_TYPES,
  applyAreaRuleUiType,
  buildAreaRuleSummary,
  getAreaRuleFieldText,
  getAreaRuleOptionLabel,
  inferAreaRuleUiType,
  mergeParamsPreservingUnknown
} from '../src/views/gam/countManagement/arrangeDetail/flow/areaRuleCompatibility.js'
import zhCN from '../src/i18n/locales/zh-CN.js'
import enUS from '../src/i18n/locales/en-US.js'

const createTranslator = (messages) => (key, params = {}) => {
  const value = key.split('.').reduce((current, segment) => current?.[segment], messages)
  assert.equal(typeof value, 'string', `missing translation: ${key}`)
  return Object.entries(params).reduce(
    (text, [name, replacement]) => text.replaceAll(`{${name}}`, String(replacement)),
    value
  )
}

const zhT = createTranslator(zhCN)
const enT = createTranslator(enUS)

const legacy = [
  { key: 'areaAlarmType', value: '6' },
  { key: 'areaLimitTargetCount', value: '2' },
  { key: 'vendor.extension', value: 'keep-me' }
]
assert.equal(inferAreaRuleUiType(legacy), AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT)
assert.match(
  buildAreaRuleSummary(legacy, AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT, (key, params) => key + JSON.stringify(params)),
  /flow\.areaRule\.summary\.targetLimit.*<.*2/
)

const compareCases = [
  ['0', '<'],
  ['1', '>'],
  ['2', '≤'],
  ['3', '≥'],
  ['4', '=']
]
compareCases.forEach(([compare, symbol]) => {
  const summary = buildAreaRuleSummary(
    [
      { key: 'param.areaLimitTargetCount', value: '3' },
      { key: 'param.areaLimitTargetType', value: compare }
    ],
    AREA_RULE_UI_TYPES.TARGET_LIMIT,
    (key, params) => key + JSON.stringify(params)
  )
  assert.ok(summary.startsWith('flow.areaRule.summary.targetLimit'))
  assert.ok(summary.includes(symbol))
  assert.ok(summary.includes('3'))
})

const areaCount = applyAreaRuleUiType(
  [
    { key: 'areaAlarmType', value: '1' },
    { key: 'countBreakAreaType', value: '103' },
    { key: 'vendor.extension', value: 'keep-me' }
  ],
  AREA_RULE_UI_TYPES.AREA_COUNT
)
assert.equal(inferAreaRuleUiType(areaCount), AREA_RULE_UI_TYPES.AREA_COUNT)
assert.equal(areaCount.find((item) => item.key === 'vendor.extension').value, 'keep-me')

const passFlow = applyAreaRuleUiType(areaCount, AREA_RULE_UI_TYPES.PASS_FLOW)
assert.equal(inferAreaRuleUiType(passFlow), AREA_RULE_UI_TYPES.PASS_FLOW)

const merged = mergeParamsPreservingUnknown(
  [
    { key: 'known', value: 'old' },
    { key: 'unknown', value: 'preserved' }
  ],
  [{ key: 'known', value: 'new' }]
)
assert.deepEqual(merged, [
  { key: 'known', value: 'new' },
  { key: 'unknown', value: 'preserved' }
])

const unknown = [{ key: 'areaAlarmType', value: '99' }]
assert.equal(inferAreaRuleUiType(unknown), AREA_RULE_UI_TYPES.UNKNOWN)
assert.deepEqual(applyAreaRuleUiType(unknown, AREA_RULE_UI_TYPES.UNKNOWN), unknown)

assert.equal(
  getAreaRuleFieldText('inputAreaType', enT).name,
  enT('flow.areaRule.fields.decisionRegion.name')
)
assert.equal(
  getAreaRuleOptionLabel('param.areaLimitTargetType', '0', '', enT),
  enT('flow.areaRule.options.compare.lessThan')
)

const englishSummaryCases = [
  [
    AREA_RULE_UI_TYPES.TARGET_LIMIT,
    [
      { key: 'param.areaLimitTargetCount', value: '3' },
      { key: 'param.areaLimitTargetType', value: '0' }
    ]
  ],
  [AREA_RULE_UI_TYPES.AREA_COUNT, []],
  [AREA_RULE_UI_TYPES.PASS_FLOW, []],
  [AREA_RULE_UI_TYPES.TRIPWIRE, [{ key: 'param.trippingWireType', value: '1' }]],
  [
    AREA_RULE_UI_TYPES.DIRECTION,
    [
      { key: 'param.retroDirect', value: '0' },
      { key: 'param.retroDistance', value: '0.05' }
    ]
  ],
  [
    AREA_RULE_UI_TYPES.DURATION,
    [
      { key: 'durationBreakAreaType', value: '0' },
      { key: 'param.areaDuration', value: '10' },
      { key: 'param.areaDurationTimeType', value: '1000' }
    ]
  ],
  [AREA_RULE_UI_TYPES.PRESENCE, []],
  [AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT, legacy],
  [AREA_RULE_UI_TYPES.UNKNOWN, unknown]
]

englishSummaryCases.forEach(([type, params]) => {
  const summary = buildAreaRuleSummary(params, type, enT)
  assert.ok(summary.length > 0, `empty English summary for ${type}`)
  assert.doesNotMatch(summary, /\p{Script=Han}/u, `Chinese leaked into ${type}: ${summary}`)
})

console.log('area rule compatibility checks passed')
