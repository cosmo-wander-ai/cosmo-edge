import assert from 'node:assert/strict'
import dagre from 'dagre'
import {
  DETAIL_PANEL_GAP,
  DETAIL_PANEL_SIZE,
  ALARM_DETAIL_PANEL_SIZE,
  FLOW_NODE_SIZE,
  FLOW_TERMINAL_SIZE,
  getDetailPanelAnchor,
  getDetailPanelSize,
  getFlowLayoutSpacing,
  getFlowNodeDimensions,
  getFlowBounds
} from '../src/views/gam/countManagement/arrangeDetail/flow/layoutGeometry.js'

const spacing = getFlowLayoutSpacing(FLOW_NODE_SIZE)
assert.ok(FLOW_NODE_SIZE.width > 0 && FLOW_NODE_SIZE.height > 0)
assert.ok(spacing.nodesep >= 0 && spacing.ranksep >= 0)

// Start + one linkage action + end should remain a compact horizontal flow.
const compactWidth = FLOW_TERMINAL_SIZE.width * 2 + FLOW_NODE_SIZE.width + spacing.ranksep * 2
assert.ok(compactWidth < 400, `three-node flow is too wide: ${compactWidth}px`)

// Dagre receives both compact terminals and full business-node dimensions.
// Verify visible rectangles and connector space, rather than exact coordinates
// that can change when Dagre chooses a different equally valid layout.
const layout = (nodes, edges) => {
  const graph = new dagre.graphlib.Graph()
  graph.setDefaultEdgeLabel(() => ({}))
  graph.setGraph({ rankdir: 'LR', ...spacing })
  nodes.forEach(node => graph.setNode(node.id, getFlowNodeDimensions(node)))
  edges.forEach(([source, target]) => graph.setEdge(source, target))
  dagre.layout(graph)
  return nodes.map(node => {
    const { x, y } = graph.node(node.id)
    const size = getFlowNodeDimensions(node)
    return { ...node, ...size, position: { x: x - size.width / 2, y: y - size.height / 2 } }
  })
}
const boxesOverlap = (a, b) =>
  a.position.x < b.position.x + b.width && a.position.x + a.width > b.position.x &&
  a.position.y < b.position.y + b.height && a.position.y + a.height > b.position.y
const start = { id: '-1', type: 'start' }
const end = { id: 'end', type: 'end' }
const business = Array.from({ length: 8 }, (_, index) => ({ id: `node-${index + 1}`, type: 'customForm' }))
const linearNodes = [start, ...business, end]
const linearEdges = linearNodes.slice(1).map((node, index) => [linearNodes[index].id, node.id])
const linear = layout(linearNodes, linearEdges)
assert.equal(linear.length, 10, 'long-flow fixture includes ten visible nodes')
assert.deepEqual(getFlowNodeDimensions(start), FLOW_TERMINAL_SIZE)
assert.deepEqual(getFlowNodeDimensions(end), FLOW_TERMINAL_SIZE)
assert.deepEqual(getFlowNodeDimensions(business[0]), FLOW_NODE_SIZE)
for (let index = 1; index < linear.length; index++) {
  const previous = linear[index - 1]
  const current = linear[index]
  assert.ok(current.position.x - previous.position.x - previous.width >= spacing.ranksep - 0.01,
    'long flows retain clear edge space between cards')
  assert.equal(current.position.y + current.height / 2, previous.position.y + previous.height / 2,
    'short terminal handles share the business-node center line')
}

const branchNodes = [start, ...business.slice(0, 4), { id: 'end-a', type: 'end' }, { id: 'end-b', type: 'end' }]
const branchEdges = [['-1', 'node-1'], ['node-1', 'node-2'], ['node-1', 'node-3'],
  ['node-2', 'node-4'], ['node-4', 'end-a'], ['node-3', 'end-b']]
const branched = layout(branchNodes, branchEdges)
for (const [index, a] of branched.entries()) {
  for (const b of branched.slice(index + 1)) {
    assert.ok(!boxesOverlap(a, b), `branch rectangles overlap: ${a.id}, ${b.id}`)
  }
}
const siblings = branched.filter(node => ['node-2', 'node-3'].includes(node.id)).sort((a, b) => a.position.y - b.position.y)
assert.ok(siblings[1].position.y - siblings[0].position.y - siblings[0].height >= spacing.nodesep - 0.01,
  'branch siblings retain space for labels, selection rings and edge controls')
const branchBounds = getFlowBounds(branched)
assert.ok(Object.values(branchBounds).every(Number.isFinite))
assert.ok(branched.every(node => node.position.x >= branchBounds.minX &&
  node.position.y >= branchBounds.minY && node.position.x + node.width <= branchBounds.maxX &&
  node.position.y + node.height <= branchBounds.maxY), 'mixed node bounds include every visible rectangle')

const node = { position: { x: 500, y: 200 } }
const panel = getDetailPanelAnchor(node)
assert.equal(panel.x, 500 + FLOW_NODE_SIZE.width / 2 - DETAIL_PANEL_SIZE.width / 2)
assert.equal(panel.y, 200 + FLOW_NODE_SIZE.height + DETAIL_PANEL_GAP)

const alarmPanelSize = getDetailPanelSize('LA_AlarmData_Code')
assert.deepEqual(alarmPanelSize, ALARM_DETAIL_PANEL_SIZE)
assert.ok(alarmPanelSize.width >= DETAIL_PANEL_SIZE.width * 2)
const alarmPanel = getDetailPanelAnchor(node, FLOW_NODE_SIZE, alarmPanelSize)
assert.equal(alarmPanel.x, 500 + FLOW_NODE_SIZE.width / 2 - alarmPanelSize.width / 2)
for (const selected of [linear[1], linear.at(-2), ...siblings]) {
  for (const size of [DETAIL_PANEL_SIZE, ALARM_DETAIL_PANEL_SIZE]) {
    const anchor = getDetailPanelAnchor(selected, getFlowNodeDimensions(selected), size)
    assert.equal(anchor.x + size.width / 2, selected.position.x + selected.width / 2,
      'normal and alarm panels center on the selected business node')
    assert.ok(anchor.y >= selected.position.y + selected.height + DETAIL_PANEL_GAP,
      'panel anchors stay below their selected cards')
  }
}
assert.deepEqual(getFlowBounds([{ ...start, position: { x: 0, y: 0 } }]), {
  minX: 0, minY: 0, maxX: FLOW_TERMINAL_SIZE.width, maxY: FLOW_TERMINAL_SIZE.height
}, 'terminal bounds use their actual compact height')

assert.deepEqual(getFlowBounds([node, { position: { x: -40, y: -20 } }]), {
  minX: -40, minY: -20, maxX: 500 + FLOW_NODE_SIZE.width, maxY: 200 + FLOW_NODE_SIZE.height
})
assert.deepEqual(getFlowBounds([node], () => ({ width: 20, height: 30 }), {
  minX: 0, minY: 0, maxX: 1000, maxY: 800
}), { minX: 0, minY: 0, maxX: 1000, maxY: 800 })

console.log('linkage flow layout checks passed')
