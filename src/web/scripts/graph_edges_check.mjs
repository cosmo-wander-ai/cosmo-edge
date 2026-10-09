import assert from 'node:assert/strict'
import { addFlowNode, createFlowGraph, deleteFlowNode, deleteFollowingFlow, insertNodeEdges } from '../src/utils/graphEdges.js'

function checkInsertion(edges, endpoints, expected, expectedCalls) {
  let calls = 0
  const result = insertNodeEdges(edges, endpoints, 'X', () => `edge-${++calls}`)
  assert.deepEqual(result, expected)
  assert.equal(calls, expectedCalls)
  return result
}

// Normal insertion inherits the type and creates the incoming edge first.
checkInsertion(
  [{ id: 'old', type: 'custom', source: 'A', target: 'B' }],
  { edgeId: 'old', source: 'A', target: 'B' },
  [
    { id: 'edge-1', type: 'custom', source: 'A', target: 'X' },
    { id: 'edge-2', type: 'custom', source: 'X', target: 'B' }
  ],
  2
)

// Frozen inputs remain intact; bypass edges retain their order and identity.
{
  const first = Object.freeze({ id: 'first', source: 'A', target: 'C' })
  const last = Object.freeze({ id: 'last', source: 'C', target: 'D' })
  const old = Object.freeze({
    id: 'old', type: 'custom', source: 'A', target: 'B',
    data: Object.freeze({ label: 'Old edge' }),
    style: Object.freeze({ color: 'red' }), sourceHandle: 'output'
  })
  const edges = Object.freeze([first, old, last])
  const result = checkInsertion(
    edges,
    { edgeId: 'old', source: 'A', target: 'B' },
    [first, last,
      { id: 'edge-1', type: 'custom', source: 'A', target: 'X' },
      { id: 'edge-2', type: 'custom', source: 'X', target: 'B' }],
    2
  )
  assert.notEqual(result, edges)
  assert.equal(result[0], first)
  assert.equal(result[1], last)
  assert.deepEqual(edges, [first, old, last])
}

// A missing source does not fall back to the old edge's source.
checkInsertion(
  [{ id: 'old', source: 'A', target: 'B' }],
  { edgeId: 'old' },
  [{ id: 'edge-1', type: 'action', source: 'X', target: 'B' }],
  1
)

// Explicit targets override; nullish targets fall back; an empty target stops.
for (const [target, expectedTarget] of [['C', 'C'], [null, 'B'], [undefined, 'B'], ['', null]]) {
  const expected = [{ id: 'edge-1', type: 'action', source: 'A', target: 'X' }]
  if (expectedTarget !== null) {
    expected.push({ id: 'edge-2', type: 'action', source: 'X', target: expectedTarget })
  }
  checkInsertion(
    [{ id: 'old', source: 'A', target: 'B' }],
    { edgeId: 'old', source: 'A', target },
    expected,
    expectedTarget === null ? 1 : 2
  )
}

// Missing old edges or types still permit explicit endpoints with action type.
{
  const other = { id: 'other', source: 'C', target: 'D' }
  for (const edges of [[other], [other, { id: 'old', source: 'A', target: 'B' }]]) {
    const result = checkInsertion(
      edges,
      { edgeId: 'old', source: 'A', target: 'B' },
      [other,
        { id: 'edge-1', type: 'action', source: 'A', target: 'X' },
        { id: 'edge-2', type: 'action', source: 'X', target: 'B' }],
      2
    )
    assert.equal(result[0], other)
  }
}

// With neither endpoint connectable, removal still returns a new array.
{
  const edges = [{ id: 'old', source: 'A', target: 'B' }]
  const result = checkInsertion(edges, { edgeId: 'old', target: '' }, [], 0)
  assert.notEqual(result, edges)
  assert.deepEqual(edges, [{ id: 'old', source: 'A', target: 'B' }])
}

const node = (id, type = 'customForm') => Object.freeze({ id, type, position: { x: 0, y: 0 }, data: {} })
const edge = (source, target, type = 'action') => Object.freeze({ id: `${source}-${target}`, source, target, type })
const endpoints = graph => graph.edges.map(({ source, target }) => [source, target])
const assertConnected = graph => {
  const ids = new Set(graph.nodes.map(node => node.id))
  for (const edge of graph.edges) assert.ok(ids.has(edge.source) && ids.has(edge.target), 'no dangling edges')
}

// Linear deletion reconnects in incoming/outgoing order and prefers the incoming type.
{
  const nodes = Object.freeze([node('S', 'start'), node('A'), node('B'), node('E', 'end')])
  const edges = Object.freeze([edge('S', 'A', 'incoming'), edge('A', 'B', 'outgoing'), edge('B', 'E')])
  const result = deleteFlowNode(nodes, edges, 'A', 123)
  assert.deepEqual(result.nodes.map(node => node.id), ['S', 'B', 'E'])
  assert.deepEqual(result.removedNodeIds, ['A'])
  assert.equal(result.nodes[1], nodes[2])
  assert.equal(result.edges[0], edges[2])
  assert.deepEqual(result.edges[1], { id: 'reconn-123-S-B', type: 'incoming', source: 'S', target: 'B' })
  assert.equal(deleteFlowNode(nodes, edges, 'missing', 123).nodes, nodes)
  assertConnected(result)
}

// Existing connections are not duplicated; empty branch ends and isolated ends are removed.
{
  const nodes = [node('S', 'start'), node('A'), node('B'), node('E1', 'end'), node('E2', 'end'), node('orphan', 'end')]
  const edges = [edge('S', 'A'), edge('A', 'E1'), edge('S', 'B'), edge('B', 'E2')]
  const result = deleteFlowNode(nodes, edges, 'A', 10)
  assert.deepEqual(result.removedNodeIds, ['A', 'E1', 'orphan'])
  assert.deepEqual(endpoints(result), [['S', 'B'], ['B', 'E2']])
  assertConnected(result)
  const shared = deleteFlowNode(nodes.slice(0, 4), [edge('S', 'A'), edge('S', 'B'), edge('A', 'E1'), edge('B', 'E1'), edge('S', 'E1')], 'A', 10)
  assert.deepEqual(shared.removedNodeIds, ['A'])
  assert.equal(shared.edges.filter(edge => edge.source === 'S' && edge.target === 'E1').length, 1)
  assertConnected(shared)
}

// Deleting a following branch retains shared ends and reconnects other stranded sources.
{
  const nodes = Object.freeze([node('S', 'start'), node('A'), node('B'), node('C'), node('E', 'end')])
  const edges = Object.freeze([edge('S', 'A', 'custom'), edge('A', 'B'), edge('B', 'E'), edge('C', 'E')])
  const result = deleteFollowingFlow(nodes, edges, { edgeId: 'S-A', x: 40, y: 50, timestamp: 20 })
  assert.deepEqual(result.removedNodeIds, ['A', 'B'])
  assert.deepEqual(result.nodes.map(node => node.id), ['S', 'C', 'E', 'end-20'])
  assert.deepEqual(endpoints(result), [['C', 'E'], ['S', 'end-20']])
  assert.deepEqual(result.nodes.at(-1).position, { x: 260, y: 50 })
  assert.equal(result.edges.at(-1).type, 'custom')
  assert.equal(result.sourceId, 'S')
  assertConnected(result)
  const joined = deleteFollowingFlow(nodes, [edge('S', 'A'), edge('A', 'B'), edge('C', 'B'), edge('B', 'E'), edge('S', 'C')], { edgeId: 'S-A', x: 0, y: 0, timestamp: 21 })
  assert.deepEqual(joined.removedNodeIds, ['A', 'B', 'E'])
  assert.deepEqual(endpoints(joined), [['S', 'C'], ['C', 'end-21']])
  assertConnected(joined)
}

// A single deleted path gets a replacement end; untouched ends can be reused.
{
  const nodes = [node('S', 'start'), node('A'), node('E', 'end')]
  const edges = [edge('S', 'A'), edge('A', 'E')]
  const operation = { edgeId: 'S-A', x: 0, y: 0, timestamp: 30 }
  const result = deleteFollowingFlow(nodes, edges, operation)
  assert.deepEqual(result.removedNodeIds, ['A', 'E'])
  assert.deepEqual(endpoints(result), [['S', 'end-30']])
  assertConnected(result)
  const reused = deleteFollowingFlow([...nodes, node('other', 'end')], edges, operation)
  assert.deepEqual(endpoints(reused), [['S', 'other']])
  const branched = deleteFollowingFlow(nodes, [...edges, edge('S', 'E')], operation)
  assert.deepEqual(endpoints(branched), [['S', 'E']])
  assert.deepEqual(branched.removedNodeIds, ['A'])
  // Both original outgoing paths lead into the removed subtree.
  const converged = deleteFollowingFlow([...nodes, node('B')], [edge('S', 'A'), edge('S', 'B'), edge('A', 'B'), edge('B', 'E')], operation)
  assert.equal(converged.nodes.at(-1).id, 'end-S-30')
  assertConnected(converged)
  assert.equal(deleteFollowingFlow(nodes, edges, { edgeId: 'missing' }).nodes, nodes)
}

// Insertion and loading saved branches keep node/edge order and the ID sequence.
{
  let id = 0
  const nextId = () => `id-${++id}`
  const base = createFlowGraph([], [], nextId, 40)
  const inserted = addFlowNode(base.nodes, base.edges, node('B'), { edgeId: 'e-start-end', source: '-1', target: 'end-40' }, nextId)
  assert.deepEqual(inserted.nodes.map(node => node.id), ['-1', 'end-40', 'B'])
  assert.deepEqual(inserted.edges.map(edge => edge.id), ['id-1', 'id-2'])
  assert.deepEqual(endpoints(inserted), [['-1', 'B'], ['B', 'end-40']])
  const items = [{ flowActionId: 'A', preFlowActionId: -1 }, { flowActionId: 'B', preFlowActionId: '-1' }]
  const reloaded = createFlowGraph(items, [node('A'), node('B')], nextId, 50)
  assert.deepEqual(reloaded.nodes.map(node => node.id), ['-1', 'A', 'B', 'end-A', 'end-B'])
  assert.deepEqual(endpoints(reloaded), [['-1', 'A'], ['-1', 'B'], ['A', 'end-A'], ['B', 'end-B']])
  const branchEdge = reloaded.edges.find(edge => edge.source === 'A')
  const branchInserted = addFlowNode(reloaded.nodes, reloaded.edges, node('C'), {
    edgeId: branchEdge.id, source: branchEdge.source, target: branchEdge.target
  }, nextId)
  assert.deepEqual(branchInserted.nodes.map(node => node.id), ['-1', 'A', 'B', 'end-A', 'end-B', 'C'])
  assert.deepEqual(branchInserted.edges.map(edge => edge.id), ['id-3', 'id-4', 'id-6', 'id-7', 'id-8'])
  assert.deepEqual(endpoints(branchInserted), [['-1', 'A'], ['-1', 'B'], ['B', 'end-B'], ['A', 'C'], ['C', 'end-A']])
  assert.equal(branchInserted.edges[2], reloaded.edges[3], 'the other branch keeps its edge')
  assertConnected(inserted)
  assertConnected(reloaded)
  assertConnected(branchInserted)
}

console.log('Graph edge checks passed (insertion, node/branch deletion, shared/orphan ends, reconstruction and ordering).')
