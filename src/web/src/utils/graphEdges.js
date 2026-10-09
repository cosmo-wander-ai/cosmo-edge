export function insertNodeEdges(
  edges,
  { edgeId, source, target },
  nodeId,
  createEdgeId
) {
  const currentEdge = edges.find(edge => edge.id === edgeId)
  const nextEdges = edges.filter(edge => edge.id !== edgeId)
  const type = currentEdge?.type || 'action'
  const resolvedTarget = target ?? currentEdge?.target

  if (source) {
    nextEdges.push({
      id: createEdgeId(), type, source, target: nodeId
    })
  }
  if (resolvedTarget) {
    nextEdges.push({
      id: createEdgeId(), type, source: nodeId, target: resolvedTarget
    })
  }
  return nextEdges
}

export function addFlowNode(nodes, edges, node, { edgeId, source, target }, createId) {
  return {
    nodes: [...nodes, node],
    edges: insertNodeEdges(edges, { edgeId, source, target }, node.id, createId)
  }
}

// Reconnect predecessors before deciding which branch ends have become empty.
// The caller supplies the timestamp so the calculation has no clock dependency.
export function deleteFlowNode(nodes, edges, nodeId, timestamp) {
  if (!nodes.some(node => node.id === nodeId)) return { nodes, edges, removedNodeIds: [] }
  const incoming = edges.filter(edge => edge.target === nodeId)
  const outgoing = edges.filter(edge => edge.source === nodeId)
  const nextEdges = edges.filter(edge => edge.source !== nodeId && edge.target !== nodeId)
  for (const inEdge of incoming) {
    for (const outEdge of outgoing) {
      const source = inEdge.source
      const target = outEdge.target
      if (source && target && source !== target &&
          !nextEdges.some(edge => edge.source === source && edge.target === target)) {
        nextEdges.push({
          id: `reconn-${timestamp}-${source}-${target}`,
          type: inEdge.type || outEdge.type || 'action', source, target
        })
      }
    }
  }

  const removed = new Set([nodeId])
  for (const end of nodes.filter(node => node.type === 'end')) {
    const incomingToEnd = nextEdges.filter(edge => edge.target === end.id)
    if (!incomingToEnd.length) {
      removed.add(end.id)
      continue
    }
    if (incomingToEnd.length !== 1) continue
    const sourceId = incomingToEnd[0].source
    if (!nodes.some(node => node.id === sourceId)) continue

    let branchPointId = null
    let currentId = sourceId
    const visited = new Set()
    while (currentId && !visited.has(currentId)) {
      visited.add(currentId)
      if (nextEdges.filter(edge => edge.source === currentId).length > 1) {
        branchPointId = currentId
        break
      }
      const predecessors = nextEdges.filter(edge => edge.target === currentId)
      if (predecessors.length !== 1) break
      currentId = predecessors[0].source
    }
    if (!branchPointId) continue

    const pathNodes = new Set()
    const queue = [sourceId]
    const pathVisited = new Set()
    while (queue.length) {
      const id = queue.shift()
      if (pathVisited.has(id)) continue
      pathVisited.add(id)
      if (id === branchPointId) continue
      const node = nodes.find(item => item.id === id)
      if (node && node.type !== 'start' && node.type !== 'end') pathNodes.add(id)
      nextEdges.filter(edge => edge.target === id).forEach(edge => {
        if (edge.source !== branchPointId) queue.push(edge.source)
      })
    }
    if (!pathNodes.size) removed.add(end.id)
  }
  return {
    nodes: nodes.filter(node => !removed.has(node.id)),
    edges: nextEdges.filter(edge => !removed.has(edge.source) && !removed.has(edge.target)),
    removedNodeIds: [...removed]
  }
}

export function deleteFollowingFlow(nodes, edges, { edgeId, x, y, timestamp }) {
  const current = edges.find(edge => edge.id === edgeId)
  if (!current) return { nodes, edges, removedNodeIds: [] }
  const removed = new Set()
  const endsToCheck = new Set()
  const queue = [current.target]
  while (queue.length) {
    const id = queue.shift()
    if (removed.has(id)) continue
    if (nodes.find(node => node.id === id)?.type === 'end') {
      endsToCheck.add(id)
      continue
    }
    removed.add(id)
    edges.filter(edge => edge.source === id).forEach(edge => queue.push(edge.target))
  }

  let nextEdges = edges.filter(edge =>
    edge.id !== edgeId && !removed.has(edge.source) && !removed.has(edge.target))
  const sourcesToReconnect = new Set()
  if (!nextEdges.some(edge => edge.source === current.source)) sourcesToReconnect.add(current.source)
  for (const node of nodes) {
    if (removed.has(node.id) || node.type === 'end' || node.type === 'start') continue
    if (!nextEdges.some(edge => edge.source === node.id) &&
        edges.some(edge => edge.source === node.id && removed.has(edge.target))) {
      sourcesToReconnect.add(node.id)
    }
  }
  for (const endId of endsToCheck) {
    if (!nextEdges.some(edge => edge.target === endId)) {
      removed.add(endId)
      nextEdges = nextEdges.filter(edge => edge.target !== endId)
    }
  }

  const nextNodes = nodes.filter(node => !removed.has(node.id))
  for (const source of sourcesToReconnect) {
    const isBranch = edges.filter(edge => edge.source === source).length > 1
    const existingEnd = !isBranch && nodes.find(node =>
      node.type === 'end' && !removed.has(node.id) && !endsToCheck.has(node.id))
    const target = existingEnd ? existingEnd.id :
      isBranch ? `end-${source}-${timestamp}` : `end-${timestamp}`
    if (!existingEnd) {
      nextNodes.push({ id: target, type: 'end', position: { x: x + 220, y }, data: {} })
    }
    nextEdges.push({ id: `${source}-to-${target}`, type: current.type || 'action', source, target })
  }
  return { nodes: nextNodes, edges: nextEdges, removedNodeIds: [...removed], sourceId: current.source }
}

export function createFlowGraph(items, actionNodes, createEdgeId, timestamp) {
  const nodes = [{ id: '-1', type: 'start', position: { x: -220, y: 80 }, data: {} }, ...actionNodes]
  if (!items.length) {
    const endId = `end-${timestamp}`
    nodes.push({ id: endId, type: 'end', position: { x: 320, y: 80 }, data: {} })
    return { nodes, edges: [{ id: 'e-start-end', type: 'action', source: '-1', target: endId }] }
  }
  const edges = items.map(item => ({
    id: createEdgeId(), type: 'action', source: String(item.preFlowActionId), target: String(item.flowActionId)
  }))
  const hasOutgoing = new Set(items.map(item => String(item.preFlowActionId)).filter(id => id && id !== '-1'))
  const terminalIds = [...new Set(items.map(item => String(item.flowActionId)))].filter(id => !hasOutgoing.has(id))
  terminalIds.forEach(id => {
    const endId = terminalIds.length > 1 ? `end-${id}` : `end-${timestamp}`
    nodes.push({ id: endId, type: 'end', position: { x: 960, y: 120 }, data: {} })
    edges.push({ id: createEdgeId(), type: 'action', source: id, target: endId })
  })
  return { nodes, edges }
}
