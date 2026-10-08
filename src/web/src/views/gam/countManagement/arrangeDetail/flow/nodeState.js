// Keep persisted fields and action templates separate from the editable config.
// Config is JSON data; detach it from imported templates and other node instances.
export const createEmptyNodeConfig = () => ({
  webConfig: { labelList: [], labelFilterList: [], metaDataParams: [], atomic: {} },
  params: []
})

const pictureMatchKeys = new Set(['param.faceSet', 'param.workClothesSet', 'param.limitScore'])

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
    return metadata.filter(param => !(ownedKeys.has(param.key) && actionId(node) === 'PA_00005' && pictureMatchKeys.has(param.key)))
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
