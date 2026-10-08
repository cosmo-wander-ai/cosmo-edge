// Keep persisted fields and action templates separate from the editable config.
// Config is JSON data; detach it from imported templates and other node instances.
export const createEmptyNodeConfig = () => ({
  webConfig: { labelList: [], labelFilterList: [], metaDataParams: [], atomic: {} },
  params: []
})

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

const isActionNode = node =>
  node.type !== 'start' && node.type !== 'end' && node.type !== 'stageGroup' && !node.data?.isStageGroup

export const collectMetaDataParams = nodes => nodes.filter(isActionNode).flatMap(node => {
  const params = node.data?.configObject?.webConfig?.metaDataParams
  return Array.isArray(params) ? params.map(param => ({ ...param })) : []
})

export const collectFlowData = (nodes, edges) => {
  const incoming = new Map()
  edges.forEach(edge => { if (edge.target) incoming.set(edge.target, edge.source) })
  const atomicCollected = []
  const items = nodes.filter(isActionNode).map(node => {
    const flowActionId = String(node.id)
    const predecessor = incoming.get(node.id)
    const configObject = node.data?.configObject ?? createEmptyNodeConfig()
    const atomic = configObject.webConfig?.atomic
    if (atomic && (atomic.position || atomic.labelList)) {
      atomicCollected.push({
        ...atomic,
        position: atomic.position || flowActionId,
        atomicCode: atomic.atomicCode || '',
        atomicName: atomic.atomicName || '',
        labelList: atomic.labelList || []
      })
    }
    // Preserve extension and i18n sidecar fields; current edits take precedence.
    return {
      ...(node.data?.flowData || {}),
      actionId: node.data?.actionId || '',
      actionName: node.data?.actionName || '',
      remark: node.data?.description || '',
      flowActionId,
      preFlowActionId: !predecessor || predecessor === 'start' ? '-1' : String(predecessor),
      configObject
    }
  })
  return { items, atomicCollected }
}
