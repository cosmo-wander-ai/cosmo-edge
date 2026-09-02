import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import {
  collapseTaskParamSchemasByKey,
  combineTaskParamSources,
  filterActiveTaskParams,
  filterChannelEditableParams,
  filterTaskParamsForSubmission,
  flattenTaskParamTree,
  getParamDependencyCycleBreakIndexes,
  getTaskParamSchemaFingerprint,
  isChannelEditableInContext,
  isChannelParamRenderableAtDepth,
  mergeTaskParamSchemasByKey,
  normalizeChannelEditorVisibility,
  normalizeChannelVisibilitySelection,
  normalizeParamOwnershipList,
  resolveChannelEditableFlags,
  resolveFinalTaskParamDependencies,
  serializeTaskParamTree
} from '../src/utils/taskParamOwnership.js'

const parameterSettingSource = readFileSync(
  new URL(
    '../src/views/gam/countManagement/arrangeDetail/flow/ParameterSetting.vue',
    import.meta.url
  ),
  'utf8'
)
assert.doesNotMatch(parameterSettingSource, /glossary\.(?:allHidden|clientHidden)/)
assert.doesNotMatch(
  parameterSettingSource,
  /el-radio-group\s+v-model="item\.checkedClient"/
)
assert.match(
  parameterSettingSource,
  /el-checkbox\s+v-model="item\.checkedClient"\s+:true-value="0"\s+:false-value="2"/
)

assert.equal(normalizeChannelVisibilitySelection({ senior: 0 }), 0)
assert.equal(normalizeChannelVisibilitySelection({ senior: '0' }), 0)
assert.equal(normalizeChannelVisibilitySelection({ senior: 1 }), 2)
assert.equal(normalizeChannelVisibilitySelection({ senior: 2 }), 2)
assert.equal(
  normalizeChannelVisibilitySelection({ channelEditable: true }),
  0
)
assert.equal(
  normalizeChannelVisibilitySelection({ channelEditable: false }),
  2
)

const equivalentSchemaA = {
  key: 'threshold',
  type: 'text',
  level: '2',
  position: 'node-a',
  value: '11',
  defaultValue: '10',
  channelEditable: true,
  senior: 0,
  dependsOn: { key: 'mode', value: '1' }
}
const equivalentSchemaB = {
  key: 'threshold',
  type: 'text',
  level: '2',
  position: 'node-b',
  value: '11',
  defaultValue: '10',
  channelEditable: true,
  senior: 0,
  dependsOn: { key: 'mode', value: '1' }
}
assert.equal(
  getTaskParamSchemaFingerprint(equivalentSchemaA),
  getTaskParamSchemaFingerprint(equivalentSchemaB)
)
const collapsedEquivalentSchemas = collapseTaskParamSchemasByKey([
  equivalentSchemaA,
  equivalentSchemaB
])
assert.deepEqual(collapsedEquivalentSchemas.conflictKeys, [])
assert.equal(collapsedEquivalentSchemas.params.length, 1)
assert.equal(
  Object.prototype.hasOwnProperty.call(
    collapsedEquivalentSchemas.params[0],
    'position'
  ),
  false
)

const conflictingUserStateValues = {
  value: '12',
  defaultValue: '20',
  channelEditable: false,
  senior: 2
}
Object.entries(conflictingUserStateValues).forEach(([field, value]) => {
  assert.deepEqual(
    collapseTaskParamSchemasByKey([
      equivalentSchemaA,
      { ...equivalentSchemaB, [field]: value }
    ]).conflictKeys,
    ['threshold']
  )
})

const existingParam = {
  key: 'threshold',
  type: 'select',
  level: '2',
  name: 'user-edited name',
  description: 'user-edited description',
  failedTip: 'user-edited failure tip',
  regexpr: '/^edited$/',
  value: '11',
  defaultValue: '11',
  senior: 2,
  channelEditable: false,
  options: [{ name: 'edited option', value: '11' }],
  dependsOn: { key: 'editedMode', value: '11' }
}
const incomingParam = {
  key: 'threshold',
  type: 'slider',
  level: '2',
  name: 'new schema',
  value: '10',
  defaultValue: '10',
  senior: 0,
  channelEditable: true,
  range: '0,100',
  dependsOn: { key: 'mode', value: '1' }
}
const schemaMerge = mergeTaskParamSchemasByKey(
  [existingParam],
  [incomingParam]
)
assert.deepEqual(schemaMerge.conflictKeys, [])
assert.deepEqual(schemaMerge.params, [existingParam])
assert.deepEqual(incomingParam.dependsOn, { key: 'mode', value: '1' })
assert.deepEqual(
  mergeTaskParamSchemasByKey([], [incomingParam]).params,
  [incomingParam]
)

const crossSourceParam = {
  key: 'crossSource',
  type: 'text',
  value: 'same',
  defaultValue: 'same'
}
assert.deepEqual(
  combineTaskParamSources(
    [crossSourceParam],
    [{ ...crossSourceParam, position: 'flow-node' }]
  ).conflictKeys,
  ['crossSource']
)
assert.deepEqual(
  combineTaskParamSources(
    [{ key: 'custom.only', type: 'text' }],
    [{ key: 'generated.only', type: 'text', level: '2' }]
  ).conflictKeys,
  []
)

const flowResolvedLibraryParams = [
  {
    key: 'faceLibrary',
    type: 'faceSet',
    level: '2',
    dependsOn: { key: 'featureInput', value: 'face' }
  },
  {
    key: 'workClothesLibrary',
    type: 'workClothesSet',
    level: 2,
    dependsOn: { key: 'featureInput', value: 'workClothes' }
  },
  {
    key: 'commodityLibrary',
    type: 'commoditySet',
    level: '2',
    dependsOn: { key: 'featureInput', value: 'commodity' }
  },
  { key: 'metadataParent', type: 'select', level: '2' },
  {
    key: 'metadataChild',
    type: 'text',
    level: '2',
    dependsOn: { key: 'metadataParent', value: '1' }
  },
  {
    key: 'trueOrphan',
    type: 'text',
    level: '1',
    dependsOn: { key: 'missingMetadataParent', value: '1' }
  },
  {
    key: 'malformedDependency',
    type: 'text',
    level: '2',
    dependsOn: {}
  }
]
const originalFlowResolvedLibraryParams = JSON.parse(
  JSON.stringify(flowResolvedLibraryParams)
)
const finalLibraryParams = resolveFinalTaskParamDependencies(
  flowResolvedLibraryParams
)
assert.deepEqual(
  finalLibraryParams.slice(0, 3).map((param) =>
    Object.prototype.hasOwnProperty.call(param, 'dependsOn')
  ),
  [false, false, false]
)
assert.deepEqual(finalLibraryParams[4].dependsOn, {
  key: 'metadataParent',
  value: '1'
})
assert.deepEqual(finalLibraryParams[5].dependsOn, {
  key: 'missingMetadataParent',
  value: '1'
})
assert.deepEqual(finalLibraryParams[6].dependsOn, {})
assert.deepEqual(flowResolvedLibraryParams, originalFlowResolvedLibraryParams)

const conflictingSchemas = collapseTaskParamSchemasByKey([
  equivalentSchemaA,
  { ...equivalentSchemaB, type: 'select' }
])
assert.deepEqual(conflictingSchemas.conflictKeys, ['threshold'])
assert.deepEqual(
  collapseTaskParamSchemasByKey([
    equivalentSchemaA,
    {
      ...equivalentSchemaB,
      dependsOn: { key: 'mode', value: '2' }
    }
  ]).conflictKeys,
  ['threshold']
)

const rootParam = {
  key: 'root',
  type: 'switch',
  value: '1',
  channelEditable: true
}
const childParam = {
  key: 'shared',
  type: 'text',
  value: 'child',
  channelEditable: true,
  dependsOn: { key: 'root', value: '1' }
}
const grandchildParam = {
  key: 'grandchild',
  type: 'text',
  value: 'nested',
  channelEditable: true,
  dependsOn: { key: 'shared', value: 'child' }
}
const siblingParam = {
  key: 'sibling',
  type: 'text',
  value: 'sibling',
  channelEditable: true,
  dependsOn: { key: 'root', value: '2' }
}
const hiddenChild = {
  key: 'sceneOnly',
  type: 'text',
  value: 'scene',
  channelEditable: false
}
rootParam.children = [childParam, siblingParam, hiddenChild]
childParam.children = [grandchildParam]
grandchildParam.children = [rootParam]

const flattenedParams = flattenTaskParamTree([rootParam, childParam])
assert.deepEqual(
  flattenedParams.map((param) => [param.key, param.value]),
  [
    ['root', '1'],
    ['shared', 'child'],
    ['grandchild', 'nested'],
    ['sibling', 'sibling'],
    ['sceneOnly', 'scene']
  ]
)
assert.equal(
  flattenedParams.every(
    (param) => !Object.prototype.hasOwnProperty.call(param, 'children')
  ),
  true
)
assert.deepEqual(
  filterChannelEditableParams(flattenedParams, '15').map(({ key, value }) => ({
    key,
    value
  })),
  [
    { key: 'root', value: '1' },
    { key: 'shared', value: 'child' },
    { key: 'sibling', value: 'sibling' }
  ]
)

const endPoint = { key: 'end', type: 'endPoint', value: '20,20' }
const initialPoint = {
  key: 'initial',
  type: 'initialPoint',
  value: '10,10',
  correlation: endPoint
}
assert.deepEqual(
  flattenTaskParamTree([initialPoint]).map((param) => param.key),
  ['initial', 'end']
)
assert.equal(
  Object.prototype.hasOwnProperty.call(
    flattenTaskParamTree([initialPoint])[0],
    'correlation'
  ),
  false
)
assert.deepEqual(
  flattenTaskParamTree([
    {
      key: 'initialOnly',
      type: 'initialPoint',
      value: '10,10',
      correlation: {}
    }
  ]).map((param) => param.key),
  ['initialOnly']
)

assert.deepEqual(
  serializeTaskParamTree(
    [
      {
        key: 'confidence',
        type: 'confidenceConfig',
        confidenceConfigValue1: '',
        confidenceConfigValue2: -100
      },
      {
        key: 'storedConfidence',
        type: 'confidenceConfig',
        value: '1,25'
      },
      { key: 'faces', type: 'faceSet', value: 'stale' },
      { key: 'zero', value: 0, defaultValue: 9 },
      { key: 'disabled', value: false, defaultValue: true }
    ],
    { faceSetIds: [7, '8'] }
  ).map(({ key, value }) => ({ key, value })),
  [
    { key: 'confidence', value: '0,-100' },
    { key: 'storedConfidence', value: '1,25' },
    { key: 'faces', value: '7,8' },
    { key: 'zero', value: 0 },
    { key: 'disabled', value: false }
  ]
)

assert.equal(isChannelParamRenderableAtDepth({ type: 'number' }, 0), true)
assert.equal(isChannelParamRenderableAtDepth({ type: 'number' }, 1), false)
assert.equal(isChannelParamRenderableAtDepth({ type: 'text' }, 1), true)
assert.equal(
  isChannelParamRenderableAtDepth({ type: 'customWidget' }, 0),
  false
)
assert.equal(
  isChannelParamRenderableAtDepth({ type: 'retroDirect' }, 0),
  true
)
assert.equal(
  isChannelParamRenderableAtDepth({ type: 'retroDirect' }, 1),
  false
)
assert.equal(
  isChannelParamRenderableAtDepth(
    { key: 'param.videoRepeatCount', type: 'customWidget' },
    1
  ),
  true
)

const conditionalParams = [
  { key: 'mode', type: 'select', value: 1 },
  {
    key: 'activeBranch',
    type: 'text',
    value: 'active',
    dependsOn: { key: 'mode', value: '1' }
  },
  {
    key: 'inactiveBranch',
    type: 'text',
    value: 'inactive',
    dependsOn: { key: 'mode', value: '2' }
  },
  {
    key: 'inactiveUnique',
    type: 'text',
    value: 'preserved',
    dependsOn: { key: 'mode', value: '2' }
  }
]
assert.deepEqual(
  filterActiveTaskParams(conditionalParams).map((param) => param.value),
  [1, 'active']
)
assert.deepEqual(
  filterTaskParamsForSubmission(conditionalParams).map((param) => param.value),
  [1, 'active', 'inactive', 'preserved']
)

const leadsRadioParams = [
  { key: 'unrelated', type: 'switch', value: '1' },
  { key: 'isEnabled', type: 'switch', value: '0' },
  { key: 'LeadsRadio', type: 'text', value: 'legacy' }
]
assert.deepEqual(
  filterActiveTaskParams(leadsRadioParams).map((param) => param.key),
  ['unrelated', 'isEnabled']
)
assert.deepEqual(
  filterTaskParamsForSubmission(leadsRadioParams).map((param) => param.key),
  ['unrelated', 'isEnabled']
)
leadsRadioParams[1].value = '1'
assert.deepEqual(
  filterActiveTaskParams(leadsRadioParams).map((param) => param.key),
  ['unrelated', 'isEnabled', 'LeadsRadio']
)
assert.deepEqual(
  filterActiveTaskParams([
    { key: 'booleanParent', type: 'switch', value: false },
    {
      key: 'looselyMatchedChild',
      type: 'text',
      dependsOn: { key: 'booleanParent', value: 0 }
    }
  ]).map((param) => param.key),
  ['booleanParent', 'looselyMatchedChild']
)

assert.deepEqual(
  normalizeChannelEditorVisibility(
    { key: 'explicitVisible', senior: 2, channelEditable: true },
    '15'
  ),
  { key: 'explicitVisible', senior: 0, channelEditable: true }
)
assert.deepEqual(
  normalizeChannelEditorVisibility(
    { key: 'sceneOnly', senior: 2, channelEditable: false },
    '15'
  ),
  { key: 'sceneOnly', senior: 2, channelEditable: false }
)
assert.deepEqual(
  normalizeChannelEditorVisibility(
    { key: 'platformAdvanced', senior: 2, channelEditable: true },
    '1'
  ),
  { key: 'platformAdvanced', senior: 2, channelEditable: true }
)

assert.deepEqual(
  [...getParamDependencyCycleBreakIndexes([
    { key: 'root' },
    { key: 'child', dependsOn: { key: 'root', value: '1' } }
  ])],
  []
)
assert.deepEqual(
  [...getParamDependencyCycleBreakIndexes([
    { key: 'cycleA', dependsOn: { key: 'cycleB', value: '1' } },
    { key: 'cycleB', dependsOn: { key: 'cycleA', value: '1' } },
    { key: 'selfCycle', dependsOn: { key: 'selfCycle', value: '1' } }
  ])],
  [0, 2]
)

const ownershipByKey = (params) =>
  Object.fromEntries(
    normalizeParamOwnershipList(params).map((param) => [
      param.key,
      param.channelEditable
    ])
  )

// For a structurally valid root, explicit ownership is authoritative over
// forced-channel and legacy-unrendered compatibility rules.
assert.deepEqual(
  ownershipByKey([
    {
      key: 'FaceCheck',
      type: 'switch',
      senior: 2,
      channelEditable: true
    },
    {
      key: 'param.videoRepeatCount',
      senior: 0,
      channelEditable: false
    },
    {
      key: 'retro',
      type: 'retroDirect',
      channelEditable: 'false'
    }
  ]),
  {
    FaceCheck: true,
    'param.videoRepeatCount': false,
    retro: false
  }
)

// Forced channel controls retain their legacy visibility behavior, but a real
// orphan dependency remains invalid and must fail closed.
assert.deepEqual(
  ownershipByKey([
    {
      key: 'param.videoRepeatCount',
      senior: 2,
      dependsOn: { key: 'missing', value: '1' }
    },
    { key: 'retro', type: 'retroDirect', senior: 1 }
  ]),
  { 'param.videoRepeatCount': false, retro: true }
)

assert.deepEqual(
  ownershipByKey([
    { key: 'FaceCheck', type: 'switch' },
    { key: 'catchView', type: 'radio' },
    { key: 'minFaceWidth', type: 'text' },
    { key: 'quality', type: 'number' }
  ]),
  {
    FaceCheck: false,
    catchView: false,
    minFaceWidth: false,
    quality: false
  }
)
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'FaceCheck', type: 'select' },
    { key: 'catchView', type: 'switch' },
    { key: 'minFaceWidth', type: 'slider' },
    { key: 'quality', type: 'select' }
  ]),
  [true, true, true, true]
)

assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'basic', type: 'text' },
    { key: 'scene.senior1', type: 'text', senior: 1 },
    { key: 'scene.senior2', type: 'text', senior: '2' }
  ]),
  [true, false, false]
)

// A legacy child is editable only when its complete dependency chain exists
// and resolves editable.
assert.deepEqual(
  ownershipByKey([
    { key: 'root', type: 'switch' },
    {
      key: 'child',
      type: 'text',
      dependsOn: { key: 'root', value: '1' }
    },
    {
      key: 'grandchild',
      type: 'text',
      dependsOn: { key: 'child', value: '1' }
    }
  ]),
  { root: true, child: true, grandchild: false }
)
assert.deepEqual(
  ownershipByKey([
    { key: 'orphan', type: 'text', dependsOn: { key: 'missing', value: '1' } },
    { key: 'malformed', type: 'text', dependsOn: {} },
    { key: 'nullDependency', type: 'text', dependsOn: null },
    { key: 'emptyRoot', type: 'text', dependsOn: { key: '' } },
    { key: 'nullKey', type: 'text', dependsOn: { key: null } },
    { key: 'numericKey', type: 'text', dependsOn: { key: 7 } },
    { key: 'stringDependency', type: 'text', dependsOn: 'root' },
    { key: 'arrayDependency', type: 'text', dependsOn: [] }
  ]),
  {
    orphan: false,
    malformed: false,
    nullDependency: true,
    emptyRoot: true,
    nullKey: false,
    numericKey: false,
    stringDependency: false,
    arrayDependency: false
  }
)
assert.deepEqual(
  ownershipByKey([
    {
      key: 'filter.pedestrian.side.min',
      type: 'text',
      dependsOn: { key: 'custom.detection', value: '0' }
    },
    {
      key: 'aiParam.pedestrian.detPostion',
      type: 'text',
      dependsOn: { key: 'custom.detection', value: '0' }
    }
  ]),
  {
    'filter.pedestrian.side.min': false,
    'aiParam.pedestrian.detPostion': false
  }
)
assert.deepEqual(
  ownershipByKey([
    { key: 'hiddenParent', type: 'switch', senior: 2 },
    {
      key: 'legacyChild',
      type: 'text',
      dependsOn: { key: 'hiddenParent', value: '1' }
    },
    {
      key: 'explicitChild',
      type: 'text',
      channelEditable: true,
      dependsOn: { key: 'hiddenParent', value: '1' }
    }
  ]),
  { hiddenParent: false, legacyChild: false, explicitChild: false }
)
assert.deepEqual(
  ownershipByKey([
    {
      key: 'explicitOrphan',
      type: 'text',
      channelEditable: true,
      dependsOn: { key: 'missing', value: '1' }
    }
  ]),
  { explicitOrphan: false }
)
assert.deepEqual(
  ownershipByKey([
    {
      key: 'explicitMalformed',
      type: 'text',
      channelEditable: true,
      dependsOn: {}
    }
  ]),
  { explicitMalformed: false }
)

// Legacy hidden-key exceptions match only root rendering. The child template
// does render these types, so a valid child is not hidden by the root quirk.
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'visibleParent', type: 'switch', value: '1' },
    {
      key: 'FaceCheck',
      type: 'switch',
      dependsOn: { key: 'visibleParent', value: '1' }
    },
    {
      key: 'minFaceWidth',
      type: 'text',
      dependsOn: { key: 'visibleParent', value: '1' }
    }
  ]),
  [true, true, true]
)

// Cycles fail closed, including when a descriptor is explicitly editable.
assert.deepEqual(
  ownershipByKey([
    { key: 'cycleA', type: 'switch', dependsOn: { key: 'cycleB', value: '1' } },
    { key: 'cycleB', type: 'text', dependsOn: { key: 'cycleA', value: '1' } },
    { key: 'selfCycle', type: 'switch', dependsOn: { key: 'selfCycle', value: '1' } }
  ]),
  { cycleA: false, cycleB: false, selfCycle: false }
)
assert.deepEqual(
  ownershipByKey([
    {
      key: 'explicitCycleA',
      type: 'switch',
      channelEditable: true,
      dependsOn: { key: 'explicitCycleB', value: '1' }
    },
    {
      key: 'explicitCycleB',
      type: 'text',
      dependsOn: { key: 'explicitCycleA', value: '1' }
    },
    {
      key: 'explicitCycleDescendant',
      type: 'text',
      channelEditable: true,
      dependsOn: { key: 'explicitCycleB', value: '1' }
    }
  ]),
  {
    explicitCycleA: false,
    explicitCycleB: false,
    explicitCycleDescendant: false
  }
)
assert.deepEqual(
  ownershipByKey([
    {
      key: 'disabledCycleA',
      type: 'switch',
      channelEditable: false,
      dependsOn: { key: 'disabledCycleB', value: '1' }
    },
    {
      key: 'disabledCycleB',
      type: 'text',
      dependsOn: { key: 'disabledCycleA', value: '1' }
    }
  ]),
  { disabledCycleA: false, disabledCycleB: false }
)
assert.deepEqual(
  ownershipByKey([
    {
      key: 'forcedCycle',
      type: 'retroDirect',
      dependsOn: { key: 'forcedCycle', value: '1' }
    }
  ]),
  { forcedCycle: false }
)

// The channel form renders only roots and one child level. A deeper control
// must follow the scene value even when explicit ownership or a legacy special
// case would otherwise make it channel-editable.
assert.deepEqual(
  ownershipByKey([
    { key: 'depthRoot', type: 'switch' },
    {
      key: 'depthChild',
      type: 'text',
      dependsOn: { key: 'depthRoot', value: '1' }
    },
    {
      key: 'explicitGrandchild',
      type: 'text',
      channelEditable: true,
      dependsOn: { key: 'depthChild', value: '1' }
    },
    {
      key: 'forcedGrandchild',
      type: 'retroDirect',
      dependsOn: { key: 'depthChild', value: '1' }
    }
  ]),
  {
    depthRoot: true,
    depthChild: true,
    explicitGrandchild: false,
    forcedGrandchild: false
  }
)
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'textParent', type: 'text' },
    {
      key: 'invisibleTextChild',
      type: 'text',
      channelEditable: true,
      dependsOn: { key: 'textParent', value: '1' }
    }
  ]),
  [true, false]
)
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'initial', type: 'initialPoint' },
    { key: 'end', type: 'endPoint' }
  ]),
  [true, true]
)
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'orphanInitial', type: 'initialPoint', channelEditable: true },
    { key: 'unknown', type: 'customWidget', channelEditable: true }
  ]),
  [false, false]
)
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'orphanEnd', type: 'endPoint', channelEditable: true },
    { key: 'unknown', type: 'customWidget', channelEditable: true }
  ]),
  [false, false]
)
assert.deepEqual(
  resolveChannelEditableFlags([
    { key: 'firstInitial', type: 'initialPoint' },
    { key: 'firstEnd', type: 'endPoint' },
    { key: 'lastInitial', type: 'initialPoint' },
    { key: 'lastEnd', type: 'endPoint' }
  ]),
  [false, false, true, true]
)

const edgeOnlyParams = [
  { key: 'hidden', type: 'text', senior: 2 },
  { key: 'orphan', type: 'text', dependsOn: { key: 'missing', value: '1' } },
  { key: 'FaceCheck', type: 'switch' },
  { key: 'explicitFalse', type: 'text', channelEditable: false },
  { key: 'cycle', type: 'switch', dependsOn: { key: 'cycle', value: '1' } }
]
assert.deepEqual(filterChannelEditableParams(edgeOnlyParams, '15'), [])
assert.deepEqual(filterChannelEditableParams(edgeOnlyParams, '1'), edgeOnlyParams)
assert.deepEqual(filterChannelEditableParams(edgeOnlyParams, 1), edgeOnlyParams)
assert.deepEqual(filterChannelEditableParams(edgeOnlyParams, '01'), [])
assert.deepEqual(filterChannelEditableParams(edgeOnlyParams), [])
assert.equal(
  isChannelEditableInContext({ channelEditable: false }, '1'),
  true
)

console.log('task parameter ownership checks passed')
