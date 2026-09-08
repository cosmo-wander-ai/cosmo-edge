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
