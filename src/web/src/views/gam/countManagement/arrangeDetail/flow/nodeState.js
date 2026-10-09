// Keep persisted fields and action templates separate from the editable config.
// Config is JSON data; detach it from imported templates and other node instances.
export const createEmptyNodeConfig = () => ({
  webConfig: { labelList: [], labelFilterList: [], metaDataParams: [], atomic: {} },
  params: []
})

const pictureMatchKeys = new Set(['param.faceSet', 'param.workClothesSet', 'param.limitScore', 'pair.threshold'])

// Deliberate node edits must also update scene defaults, which take precedence
// over node parameters during image execution. Preserve ownership and labels.
export const updatePictureMatchDefaults = (params, changed) => {
  const source = Array.isArray(params) ? params : []
  if (!pictureMatchKeys.has(changed?.key)) return source
  const value = String(changed.value ?? changed.defaultValue ?? '')
  if (!source.some(param => param.key === changed.key)) {
    const { position, ...descriptor } = changed
    return [...source, { ...descriptor, value, defaultValue: value }]
  }
  return source.map(param => param.key === changed.key ? { ...param, value, defaultValue: value } : param)
}

export const isPairPictureWorkflow = process => {
  try {
    const nodes = typeof process === 'string' ? JSON.parse(process) : process
    return Array.isArray(nodes) && nodes.some(node => node.actionId === 'PB_00007')
  } catch { return false }
}

// Read the actual graph, including old persisted metadata with both library fields.
// Keep both bindings when separate match nodes genuinely use both library kinds.
export const filterPictureLibraryParams = (params, process) => {
  let graph = process
  try { if (typeof graph === 'string') graph = JSON.parse(graph) } catch { return params }
  if (!Array.isArray(graph)) return params
  const matches = graph.filter(node => node.actionId === 'PB_00006')
  if (!matches.length && graph.some(node => node.actionId === 'PB_00007')) return params.filter(p => !['param.faceSet', 'param.workClothesSet', 'param.limitScore'].includes(p.key))
  if (!matches.length) return params
  const active = new Set(matches.map(node =>
    node.configObject?.params?.find(p => p.key === 'match.libraryType')?.value === 'body'
      ? 'param.workClothesSet' : 'param.faceSet'))
  return params.filter(p => !['param.faceSet', 'param.workClothesSet'].includes(p.key) || active.has(p.key))
}

export const collectNodeMetadata = nodes => {
  const actionId = node => node.data?.actionId || node.data?.actionDetail?.actionId || node.data?.actionDetail?.id
  const ownedKeys = new Set(nodes.filter(node => actionId(node) === 'PB_00006')
    .flatMap(node => node.data?.configObject?.webConfig?.metaDataParams || [])
    .map(param => param.key))
  return nodes.flatMap(node => {
    const metadata = node.data?.configObject?.webConfig?.metaDataParams
    if (!Array.isArray(metadata)) return []
    // Older converted templates left comparison descriptors on the extraction
    // node. The dedicated match node now owns these descriptors.
    const applicable = actionId(node) === 'PB_00006' && Array.isArray(node.data?.configObject?.params)
      ? filterPictureLibraryParams(metadata, [{ actionId: 'PB_00006', configObject: node.data.configObject }]) : metadata
    return applicable.filter(param => !(ownedKeys.has(param.key) && actionId(node) === 'PA_00005' && pictureMatchKeys.has(param.key)))
      .map(param => ({ ...param }))
  })
}

export const createNodeState = (flowItem = {}, action = {}) => {
  const { configObject, ...flowData } = flowItem
  const { configObject: unusedConfig, ...actionDetail } = action
  return {
    flowData,
    actionDetail,
    configObject: JSON.parse(JSON.stringify(configObject ?? createEmptyNodeConfig()))
  }
}

export const updateNodeConfig = (nodes, nodeId, configObject) => {
  if (!nodeId || !configObject) return nodes
  return nodes.map((node) => String(node.id) === String(nodeId)
    ? { ...node, data: { ...node.data, configObject } }
    : node)
}

export const updateAtomicList = (list, payload = {}) => {
  const { position, atomicCode, atomicName, labelList } = payload
  if (!position) return list
  const source = list || []
  const index = source.findIndex((atomic) => String(atomic.position) === String(position))
  const entry = {
    position: String(position),
    atomicCode: atomicCode || '',
    atomicName: atomicName || '',
    labelList: Array.isArray(labelList) ? labelList : []
  }
  return index < 0
    ? [...source, entry]
    : source.map((atomic, current) => current === index ? entry : atomic)
}
