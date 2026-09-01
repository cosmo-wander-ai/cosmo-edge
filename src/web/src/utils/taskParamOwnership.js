const FORCED_CHANNEL_PARAM_KEYS = new Set(['param.videoRepeatCount'])
const FORCED_CHANNEL_PARAM_TYPES = new Set(['retroDirect'])
const LEGACY_UNRENDERED_TEXT_KEYS = new Set(['minFaceWidth', 'quality'])
const LEGACY_UNRENDERED_TEXT_TYPES = new Set(['text', 'number'])
const ROOT_CHANNEL_PARAM_TYPES = new Set([
  'select',
  'switch',
  'check',
  'radio',
  'slider',
  'textarea',
  'number',
  'text',
  'confidenceConfig',
  'distanceRate',
  'commoditySet',
  'workClothesSet',
  'faceSet'
])
const CHILD_CHANNEL_PARAM_TYPES = new Set([
  'select',
  'switch',
  'check',
  'radio',
  'slider',
  'textarea',
  'text',
  'confidenceConfig',
  'distanceRate',
  'commoditySet',
  'workClothesSet',
  'faceSet'
])

const parseExplicitBoolean = (value) => {
  if (typeof value === 'boolean') return value
  if (value === 1 || value === '1' || value === 'true') return true
  if (value === 0 || value === '0' || value === 'false') return false
  return undefined
}

export const getExplicitChannelEditable = (param) =>
  parseExplicitBoolean(param?.channelEditable)

export const isForcedChannelParam = (param) => {
  if (!param) return false
  return (
    FORCED_CHANNEL_PARAM_KEYS.has(param.key) ||
    FORCED_CHANNEL_PARAM_TYPES.has(param.type)
  )
}

export const isChannelParamRenderableAtDepth = (param, depth) => {
  if (!param || depth < 0 || depth > 1) return false
  if (FORCED_CHANNEL_PARAM_KEYS.has(param.key)) return true
  if (FORCED_CHANNEL_PARAM_TYPES.has(param.type)) return depth === 0
  return (depth === 0
    ? ROOT_CHANNEL_PARAM_TYPES
    : CHILD_CHANNEL_PARAM_TYPES
  ).has(param.type)
}

export const isLegacyUnrenderedParam = (param) => {
  if (!param) return false
  if (param.key === 'FaceCheck' && param.type === 'switch') return true
  if (param.key === 'catchView' && param.type === 'radio') return true
  return (
    LEGACY_UNRENDERED_TEXT_KEYS.has(param.key) &&
    LEGACY_UNRENDERED_TEXT_TYPES.has(param.type)
  )
}

// A changed visibility selection is authoritative when metadata is saved.
export const deriveChannelEditableFromVisibility = (param) => {
  const senior = Number(param?.senior)
  return senior !== 1 && senior !== 2
}

export const isChannelEditableParam = (param) => {
  const explicit = getExplicitChannelEditable(param)
  if (explicit !== undefined) return explicit
  // Legacy edge metadata keeps these special controls on the channel even when
  // their historical visibility marker says otherwise.
  if (isForcedChannelParam(param)) return true
  if (isLegacyUnrenderedParam(param)) return false
  return deriveChannelEditableFromVisibility(param)
}

export const getParamDependencyKey = (param) => {
  if (param?.dependsOn === null || param?.dependsOn === undefined) {
    return undefined
  }
  if (
    typeof param.dependsOn !== 'object' ||
    Array.isArray(param.dependsOn) ||
    !Object.prototype.hasOwnProperty.call(param.dependsOn, 'key') ||
    param.dependsOn.key === null ||
    param.dependsOn.key === undefined ||
    typeof param.dependsOn.key !== 'string'
  ) {
    return null
  }
  return param.dependsOn.key === '' ? undefined : param.dependsOn.key
}

const TASK_PARAM_NON_SCHEMA_FIELDS = new Set(['position'])

const stableSchemaValue = (value) => {
  if (Array.isArray(value)) return value.map(stableSchemaValue)
  if (!value || typeof value !== 'object') return value
  return Object.fromEntries(
    Object.keys(value)
      .sort()
      .map((key) => [key, stableSchemaValue(value[key])])
  )
}

const cloneTaskParamWithoutPosition = (param) =>
  Object.fromEntries(
    Object.entries(param || {}).filter(([key]) => key !== 'position')
  )

export const getTaskParamSchemaFingerprint = (param) => {
  const schema = Object.fromEntries(
    Object.entries(param || {}).filter(
      ([key]) => !TASK_PARAM_NON_SCHEMA_FIELDS.has(key)
    )
  )
  return JSON.stringify(stableSchemaValue(schema))
}

// Multiple flow nodes may emit the same global parameter descriptor. Fold
// structurally equivalent copies deterministically, but fail closed when one
// key describes conflicting schema or user state. Only flow-node `position`
// is ignored here; preserving existing user state happens in the later merge.
export const collapseTaskParamSchemasByKey = (params) => {
  const paramsByKey = new Map()
  const fingerprintsByKey = new Map()
  const conflictKeys = new Set()
  ;(Array.isArray(params) ? params : []).forEach((param) => {
    const key = String(param?.key ?? '')
    const fingerprint = getTaskParamSchemaFingerprint(param)
    if (!paramsByKey.has(key)) {
      paramsByKey.set(key, cloneTaskParamWithoutPosition(param))
      fingerprintsByKey.set(key, fingerprint)
    } else if (fingerprintsByKey.get(key) !== fingerprint) {
      conflictKeys.add(key)
    }
  })
  return { params: [...paramsByKey.values()], conflictKeys: [...conflictKeys] }
}

export const findTaskParamKeyOverlaps = (leftParams, rightParams) => {
  const leftKeys = new Set(
    (Array.isArray(leftParams) ? leftParams : []).map((param) =>
      String(param?.key ?? '')
    )
  )
  return [
    ...new Set(
      (Array.isArray(rightParams) ? rightParams : [])
        .map((param) => String(param?.key ?? ''))
        .filter((key) => leftKeys.has(key))
    )
  ]
}

export const combineTaskParamSources = (customParams, generatedParams) => {
  const customResult = collapseTaskParamSchemasByKey(customParams)
  const generatedResult = collapseTaskParamSchemasByKey(generatedParams)
  const combinedResult = collapseTaskParamSchemasByKey([
    ...customResult.params,
    ...generatedResult.params
  ])
  const conflictKeys = [
    ...new Set([
      ...customResult.conflictKeys,
      ...generatedResult.conflictKeys,
      ...combinedResult.conflictKeys,
      ...findTaskParamKeyOverlaps(customResult.params, generatedResult.params)
    ])
  ]
  return { params: combinedResult.params, conflictKeys }
}

// Flow-node forms resolve level-1 conditions before emitting their active
// level-2 metadata. Keep a dependency only when its parent is also part of the
// final metadata graph; otherwise clone the resolved child as a root control.
// Other orphan/malformed metadata remains unchanged and fails ownership closed.
export const resolveFinalTaskParamDependencies = (params) => {
  const list = Array.isArray(params) ? params : []
  const finalKeys = new Set(list.map((param) => String(param?.key ?? '')))
  return list.map((param) => {
    const dependencyKey = getParamDependencyKey(param)
    if (
      String(param?.level ?? '') === '2' &&
      typeof dependencyKey === 'string' &&
      !finalKeys.has(dependencyKey)
    ) {
      const resolvedRoot = { ...param }
      delete resolvedRoot.dependsOn
      return resolvedRoot
    }
    return { ...param }
  })
}

// ParameterSetting detail mode can edit every persisted field on an existing
// generated descriptor. There is no safe field-level provenance, so a matching
// existing descriptor remains authoritative; incoming schema only adds keys
// that do not yet exist.
export const mergeTaskParamSchemasByKey = (
  existingParams,
  incomingParams,
  { retainUnmatchedExisting = false } = {}
) => {
  const existingResult = collapseTaskParamSchemasByKey(existingParams)
  const incomingResult = collapseTaskParamSchemasByKey(incomingParams)
  const existing = existingResult.params
  const incoming = incomingResult.params
  const conflictKeys = [
    ...new Set([
      ...existingResult.conflictKeys,
      ...incomingResult.conflictKeys
    ])
  ]
  if (conflictKeys.length > 0) {
    return {
      params: existing.map((param) => ({ ...param })),
      conflictKeys
    }
  }

  const existingByKey = new Map(
    existing.map((param) => [String(param?.key ?? ''), param])
  )
  const incomingByKey = new Map(
    incoming.map((param) => [String(param?.key ?? ''), param])
  )
  const mergeDescriptor = (incomingParam) => {
    const current = existingByKey.get(String(incomingParam?.key ?? ''))
    return current ? { ...current } : { ...incomingParam }
  }

  const params = retainUnmatchedExisting
    ? existing.map((param) => {
      const incomingParam = incomingByKey.get(String(param?.key ?? ''))
      return incomingParam ? mergeDescriptor(incomingParam) : { ...param }
    })
    : incoming.map(mergeDescriptor)

  if (retainUnmatchedExisting) {
    incoming.forEach((param) => {
      if (!existingByKey.has(String(param?.key ?? ''))) {
        params.push({ ...param })
      }
    })
  }

  return { params, conflictKeys: [] }
}

// Return one deterministic incoming-edge break per dependency cycle. The
// dynamic form uses this to avoid a recursive render graph; ownership still
// fails the whole invalid dependency chain closed.
export const getParamDependencyCycleBreakIndexes = (params) => {
  const list = Array.isArray(params) ? params : []
  const keyToIndex = new Map()
  list.forEach((param, index) => {
    if (param?.key === null || param?.key === undefined) return
    const key = String(param.key)
    if (!keyToIndex.has(key)) keyToIndex.set(key, index)
  })

  const states = new Array(list.length).fill(0)
  const stack = []
  const stackIndexes = new Map()
  const cycleBreakIndexes = new Set()

  const visit = (index) => {
    if (states[index] === 2) return
    if (states[index] === 1) {
      const cycleStart = stackIndexes.get(index)
      const cycle = stack.slice(cycleStart)
      cycleBreakIndexes.add(Math.min(...cycle))
      return
    }

    states[index] = 1
    stackIndexes.set(index, stack.length)
    stack.push(index)

    const dependencyKey = getParamDependencyKey(list[index])
    if (typeof dependencyKey === 'string' && keyToIndex.has(dependencyKey)) {
      visit(keyToIndex.get(dependencyKey))
    }

    stack.pop()
    stackIndexes.delete(index)
    states[index] = 2
  }

  list.forEach((_, index) => visit(index))
  return cycleBreakIndexes
}

// DynamicForm stores dependent controls under `children` (and the paired
// endpoint under `correlation`). Convert that graph back to the flat metadata
// shape used by task payloads. Object identity, rather than key, is used for
// de-duplication because distinct conditional branches may intentionally share
// the same parameter key.
export const flattenTaskParamTree = (params) => {
  const result = []
  const visited = new WeakSet()

  const visit = (param) => {
    if (!param || typeof param !== 'object' || visited.has(param)) return
    visited.add(param)

    const { children, correlation, ...flatParam } = param
    result.push(flatParam)

    if (Array.isArray(children)) children.forEach(visit)
    if (
      param.type === 'initialPoint' &&
      correlation?.type === 'endPoint' &&
      correlation.key !== null &&
      correlation.key !== undefined &&
      correlation.key !== ''
    ) {
      visit(correlation)
    }
  }

  if (Array.isArray(params)) params.forEach(visit)
  return result
}

const normalizeConfidenceValue = (value) => {
  const number = Number(value)
  return Number.isFinite(number) ? number : 0
}

const serializeConfidenceValue = (param) => {
  const storedValues = String(
    param.value ?? param.defaultValue ?? '0,0'
  ).split(',')
  return `${normalizeConfidenceValue(param.confidenceConfigValue1 ?? storedValues[0])},${normalizeConfidenceValue(param.confidenceConfigValue2 ?? storedValues[1])}`
}

export const serializeTaskParamTree = (
  params,
  { faceSetIds = [] } = {}
) => {
  const faceSetValue = (Array.isArray(faceSetIds) ? faceSetIds : [])
    .map((value) => String(value))
    .join(',')

  return flattenTaskParamTree(params).map((param) => ({
    ...param,
    value:
      param.type === 'confidenceConfig'
        ? serializeConfidenceValue(param)
        : param.type === 'faceSet'
          ? faceSetValue
          : param.value ?? param.defaultValue ?? ''
  }))
}

const resolveParamDependencyDepths = (
  params,
  keyToIndex,
  cycleBreakIndexes
) => {
  const states = new Array(params.length).fill(0)
  const depths = new Array(params.length).fill(null)

  const resolveAt = (index) => {
    if (states[index] === 2) return depths[index]
    if (states[index] === 1) return null

    states[index] = 1
    const dependencyKey = getParamDependencyKey(params[index])
    let depth = null
    if (cycleBreakIndexes.has(index) || dependencyKey === undefined) {
      depth = 0
    } else if (
      typeof dependencyKey === 'string' &&
      keyToIndex.has(dependencyKey)
    ) {
      const parentDepth = resolveAt(keyToIndex.get(dependencyKey))
      if (Number.isInteger(parentDepth)) depth = parentDepth + 1
    }

    depths[index] = depth
    states[index] = 2
    return depth
  }

  params.forEach((_, index) => resolveAt(index))
  return depths
}

// Resolve legacy ownership as a collection because a dependent control can be
// edited only when its complete parent chain is also channel-editable. The
// current channel form renders roots plus one child level, so deeper controls
// fail closed even when metadata marks them explicit/forced. Invalid
// dependencies, scene-managed parents, and cycles fail closed.
export const resolveChannelEditableFlags = (params) => {
  const list = Array.isArray(params) ? params : []
  const keyToIndex = new Map()

  list.forEach((param, index) => {
    if (param?.key === null || param?.key === undefined) return
    const key = String(param.key)
    if (!keyToIndex.has(key)) {
      keyToIndex.set(key, index)
    }
  })
  const cycleBreakIndexes = getParamDependencyCycleBreakIndexes(list)
  const dependencyDepths = resolveParamDependencyDepths(
    list,
    keyToIndex,
    cycleBreakIndexes
  )

  // The point editor is a paired control. DynamicForm keeps only the last
  // root initial/end pair, so an orphan or an earlier duplicate is not a
  // channel-rendered override even if metadata marks it editable. Scene
  // metadata validation should reject a half-pair; this remains fail-closed
  // defense for legacy malformed data.
  let initialPointIndex
  let endPointIndex
  dependencyDepths.forEach((depth, index) => {
    if (depth !== 0) return
    if (list[index]?.type === 'initialPoint') initialPointIndex = index
    if (list[index]?.type === 'endPoint') endPointIndex = index
  })
  const pairedPointIndexes = new Set()
  if (
    Number.isInteger(initialPointIndex) &&
    Number.isInteger(endPointIndex)
  ) {
    pairedPointIndexes.add(initialPointIndex)
    pairedPointIndexes.add(endPointIndex)
  }

  const channelRenderable = list.map((param, index) => {
    const renderDepth = Number.isInteger(dependencyDepths[index])
      ? dependencyDepths[index]
      : 0
    if (pairedPointIndexes.has(index)) return true
    if (!isChannelParamRenderableAtDepth(param, renderDepth)) return false
    if (renderDepth !== 1) return true

    const dependencyKey = getParamDependencyKey(param)
    const parentIndex = typeof dependencyKey === 'string'
      ? keyToIndex.get(dependencyKey)
      : undefined
    const parentType = Number.isInteger(parentIndex)
      ? list[parentIndex]?.type
      : undefined
    return parentType === 'switch' || parentType === 'select'
  })

  const states = new Array(list.length).fill(0)
  const flags = new Array(list.length)

  const resolveAt = (index) => {
    if (states[index] === 2) return flags[index]

    const param = list[index]
    if (!channelRenderable[index]) {
      states[index] = 2
      flags[index] = false
      return false
    }
    const dependencyKey = getParamDependencyKey(param)
    if (
      dependencyKey === null ||
      (typeof dependencyKey === 'string' &&
        !keyToIndex.has(dependencyKey))
    ) {
      states[index] = 2
      flags[index] = false
      return false
    }
    const explicit = getExplicitChannelEditable(param)
    let ownEditable
    if (explicit !== undefined) {
      ownEditable = explicit
    } else if (isForcedChannelParam(param)) {
      ownEditable = true
    } else {
      ownEditable = !(
        (dependencyDepths[index] === 0 &&
          isLegacyUnrenderedParam(param)) ||
        !deriveChannelEditableFromVisibility(param)
      )
    }
    if (!ownEditable) {
      states[index] = 2
      flags[index] = false
      return false
    }

    if (dependencyKey === undefined) {
      states[index] = 2
      flags[index] = true
      return true
    }
    if (states[index] === 1) return false

    states[index] = 1
    const editable = resolveAt(keyToIndex.get(dependencyKey))
    states[index] = 2
    flags[index] = editable
    return editable
  }

  list.forEach((_, index) => resolveAt(index))
  if (
    pairedPointIndexes.size === 2 &&
    (!flags[initialPointIndex] || !flags[endPointIndex])
  ) {
    flags[initialPointIndex] = false
    flags[endPointIndex] = false
  }
  return flags
}

const dependencyValuesMatch = (expected, actual) => {
  // Match the existing DynamicForm template (`dependsOn.value == value`).
  // eslint-disable-next-line eqeqeq
  return expected == actual
}

// Filter a flat, already ownership-filtered edge parameter list down to the
// controls that DynamicForm currently displays. Missing parents and the one
// deterministic cycle break are lifted to roots by the editor; ownership
// filtering removes those malformed edge-device controls first.
const resolveActiveTaskParamFlags = (params) => {
  const list = Array.isArray(params) ? params : []
  const editableKeys = new Set(
    list
      .filter((param) => param?.key !== null && param?.key !== undefined)
      .map((param) => String(param.key))
  )
  const cycleBreakIndexes = getParamDependencyCycleBreakIndexes(list)
  const active = new Array(list.length).fill(false)
  const rootIndexes = []

  list.forEach((param, index) => {
    const dependencyKey = getParamDependencyKey(list[index])
    if (
      cycleBreakIndexes.has(index) ||
      dependencyKey === undefined ||
      dependencyKey === null ||
      !editableKeys.has(dependencyKey)
    ) {
      active[index] = true
      rootIndexes.push(index)
    }
  })

  rootIndexes.forEach((parentIndex) => {
    const parent = list[parentIndex]
    if (parent?.type !== 'switch' && parent?.type !== 'select') return
    const parentKey = String(parent.key)
    const parentValue = parent.value ?? parent.defaultValue ?? ''
    list.forEach((child, childIndex) => {
      if (cycleBreakIndexes.has(childIndex)) return
      if (getParamDependencyKey(child) !== parentKey) return
      if (dependencyValuesMatch(child?.dependsOn?.value, parentValue)) {
        active[childIndex] = true
      }
    })
  })

  // LeadsRadio is a root-only legacy control whose actual template is gated
  // specifically by isEnabled. Unrelated switches must not make it active.
  const leadsRadioEnabled = list.some(
    (param) =>
      String(param?.key ?? '') === 'isEnabled' &&
      dependencyValuesMatch(1, param.value ?? param.defaultValue ?? '')
  )
  list.forEach((param, index) => {
    if (
      String(param?.key ?? '') === 'LeadsRadio' &&
      getParamDependencyKey(param) === undefined
    ) {
      active[index] = leadsRadioEnabled
    }
  })

  return active
}

export const filterActiveTaskParams = (params) => {
  const list = Array.isArray(params) ? params : []
  const active = resolveActiveTaskParamFlags(list)
  return list.filter((_, index) => active[index])
}

// Metadata keys are globally unique. Preserve inactive conditional values so
// switching a selector does not erase user input; only the root-only legacy
// LeadsRadio follows its template's isEnabled gate.
export const filterTaskParamsForSubmission = (params) => {
  const list = Array.isArray(params) ? params : []
  const active = resolveActiveTaskParamFlags(list)
  return list.filter((param, index) => {
    const key = String(param?.key ?? '')
    return key !== 'LeadsRadio' || active[index]
  })
}

// Parameter ownership is an edge-device concept. The platform task editor
// keeps its legacy behavior and continues to expose/submit every metadata
// parameter, with `senior` only controlling normal/advanced visibility.
export const isChannelEditableInContext = (param, platformType) =>
  String(platformType ?? '') === '1' || isChannelEditableParam(param)

export const filterChannelEditableParams = (params, platformType) => {
  const list = Array.isArray(params) ? params : []
  if (String(platformType ?? '') === '1') return [...list]
  const flags = resolveChannelEditableFlags(list)
  return list.filter((_, index) => flags[index])
}

export const normalizeParamOwnershipList = (params) => {
  const list = Array.isArray(params) ? params : []
  const flags = resolveChannelEditableFlags(list)
  return list.map((param, index) => ({
    ...param,
    channelEditable: flags[index]
  }))
}

// The edge channel editor must display every parameter it is allowed to
// override. Work on the editor copy only so legacy `senior` visibility does not
// hide an explicitly editable control or mutate the algorithm metadata.
export const normalizeChannelEditorVisibility = (param, platformType) => {
  const normalized = { ...param }
  if (
    String(platformType ?? '') !== '1' &&
    getExplicitChannelEditable(normalized) === true
  ) {
    normalized.senior = 0
  }
  return normalized
}

export const resolveSceneParamValue = (param) => {
  if (param?.value !== null && param?.value !== undefined) return param.value
  return param?.defaultValue ?? ''
}
