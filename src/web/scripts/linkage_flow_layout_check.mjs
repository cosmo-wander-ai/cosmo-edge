import assert from 'node:assert/strict'
import dagre from 'dagre'
import {
  DETAIL_PANEL_GAP,
  DETAIL_PANEL_SIZE,
  ALARM_DETAIL_PANEL_SIZE,
  FLOW_NODE_SIZE,
  FLOW_TERMINAL_SIZE,
  getDetailPanelAnchor,
  getDetailPanelCanvasBounds,
  getDetailPanelScreenPosition,
  getDetailPanelSize,
  getFlowLayoutSpacing,
  getFlowFitZoom,
  getFlowReadingViewport,
  getDockedPanelSize,
  getReadingFloatingPanelSize,
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

// At realistic canvas sizes, centering must not crop the first/last card or
// the stage label area when the user explicitly requests an overview.
for (const positioned of [linear, branched]) {
  const bounds = getFlowBounds(positioned)
  for (const viewport of [{ width: 1040, height: 510 }, { width: 1486, height: 695 }]) {
    const zoom = getFlowFitZoom(bounds, viewport)
    const offsetX = viewport.width / 2 - (bounds.minX + bounds.maxX) / 2 * zoom
    const offsetY = viewport.height / 2 - (bounds.minY + bounds.maxY) / 2 * zoom
    assert.ok(zoom > 0 && zoom <= 1, 'fit zoom is positive and does not enlarge short flows')
    for (const node of positioned) {
      assert.ok(node.position.x * zoom + offsetX >= 55.99)
      assert.ok((node.position.x + node.width) * zoom + offsetX <= viewport.width - 55.99)
      assert.ok(node.position.y * zoom + offsetY >= 55.99)
      assert.ok((node.position.y + node.height) * zoom + offsetY <= viewport.height - 55.99)
    }
    assert.ok((bounds.minY - 48) * zoom + offsetY >= 7.99, 'stage labels stay above the cards without clipping')
    assert.equal(getFlowFitZoom({
      minX: bounds.minX - 500, maxX: bounds.maxX - 500,
      minY: bounds.minY + 200, maxY: bounds.maxY + 200
    }, viewport), zoom, 'panning the graph does not change its required fit scale')
  }
}
// Reading mode must keep 14px labels at 14 screen pixels even when a long
// workflow extends off-screen. Opening a dock reserves actual graph width.
for (const viewport of [{ width: 1040, height: 440 }, { width: 1272, height: 493 }]) {
  for (const actionId of ['BA_00001', 'LA_AlarmData_Code']) {
    const dock = getDockedPanelSize(actionId, viewport)
    const graphViewport = { width: viewport.width - dock.width - 12, height: viewport.height }
    assert.ok(dock.width >= 440, 'desktop forms are wider than the old 360px panel')
    assert.equal(dock.height, viewport.height, 'the form can use the full available height')
    assert.ok(graphViewport.width >= viewport.width / 2 - 12, 'the graph keeps its own usable region')
    const bounds = getFlowBounds(linear)
    const reading = getFlowReadingViewport(bounds, graphViewport)
    assert.equal(reading.zoom * 14, 14, 'default label size is 14 screen pixels')
    assert.ok(linear[0].position.x + reading.x >= 56, 'long workflows start with their input visible')
    assert.ok(linear.at(-1).position.x + reading.x > graphViewport.width, 'long workflows remain pannable instead of shrinking')
    const overview = getFlowFitZoom(bounds, graphViewport)
    assert.ok(overview < 1, 'explicit overview still fits the whole workflow')
  }
}
assert.deepEqual(getFlowReadingViewport(getFlowBounds([]), { width: 1000, height: 500 }), { x: 56, y: 56, zoom: 1 })
for (const viewport of [{ width: 1040, height: 440 }, { width: 820, height: 420 }]) {
  for (const actionId of ['BA_00001', 'LA_AlarmData_Code']) {
    for (const zoom of [1, 1.5, 2]) {
      const panel = getReadingFloatingPanelSize(actionId, viewport, zoom)
      const panelX = viewport.width - panel.width - 16
      const readingWidth = viewport.width - panel.width - 32
      const nodeRight = readingWidth / 2 + FLOW_NODE_SIZE.width * zoom / 2
      assert.ok(nodeRight + 16 <= panelX, 'floating configuration leaves the selected card unobstructed')
      assert.ok(readingWidth >= FLOW_NODE_SIZE.width * zoom + 112, 'floating mode retains unscaled reading margins')
      assert.ok(panel.height <= viewport.height - 32, 'floating controls stay within the canvas height')
    }
  }
}

assert.equal(getFlowFitZoom({ minX: 0, minY: 0, maxX: compactWidth, maxY: 96 }, { width: 1040, height: 510 }), 1,
  'short flows retain the natural 100% scale')
assert.equal(getFlowFitZoom({ minX: 0, minY: 0, maxX: compactWidth, maxY: 96 }, { width: 1040, height: 510 }, 0.75), 0.75,
  'an explicit zoom ceiling is honored')
assert.equal(getFlowFitZoom(getFlowBounds([]), { width: 1040, height: 510 }), 1,
  'an empty canvas does not produce NaN or infinite viewport transforms')

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

// Fixed-size floating panels remain centered and included in canvas bounds at
// every zoom. Screen-space dimensions must not shrink with the graph.
for (const zoom of [0.4, 0.75, 1, 1.5]) {
  for (const size of [DETAIL_PANEL_SIZE, ALARM_DETAIL_PANEL_SIZE]) {
    const anchor = getDetailPanelAnchor(node, FLOW_NODE_SIZE, size)
    const viewport = { x: 60, y: 40, zoom }
    const screen = getDetailPanelScreenPosition(anchor, viewport, size)
    const bounds = getDetailPanelCanvasBounds(anchor, size, zoom)
    assert.ok(Math.abs((bounds.maxX - bounds.minX) * zoom - size.width) < 0.001)
    assert.ok(Math.abs((bounds.maxY - bounds.minY) * zoom - size.height) < 0.001)
    assert.ok(Math.abs(screen.x + size.width / 2 - ((node.position.x + FLOW_NODE_SIZE.width / 2) * zoom + viewport.x)) < 0.001)
    assert.ok(screen.y > (node.position.y + FLOW_NODE_SIZE.height) * zoom + viewport.y)
  }
}

// A short canvas must leave room for the selected node as well as the detached
// scrolling panel. These are the two browser fixtures that previously clipped
// the graph when a 430px alarm panel was centered into a smaller canvas.
for (const viewport of [{ width: 1040, height: 510 }, { width: 820, height: 420 }]) {
  for (const actionId of ['LA_AlarmData_Code', 'EVT_00001', 'BA_00001']) {
    const size = getDetailPanelSize(actionId, viewport)
    assert.ok(size.width + 64 <= viewport.width - 8, 'left toolbar and right edge both retain space')
    assert.ok(size.height <= viewport.height - 160, 'panel reserves a visible graph row')
    assert.ok(size.height >= 120, 'panel retains room for a header and scrolling controls')
    const selected = { type: 'customForm', position: { x: 300, y: 0 } }
    const anchor = getDetailPanelAnchor(selected, FLOW_NODE_SIZE, size)
    const zoom = 0.75
    const bounds = getFlowBounds([selected], getFlowNodeDimensions, getDetailPanelCanvasBounds(anchor, size, zoom))
    const transform = {
      x: viewport.width / 2 - (bounds.minX + bounds.maxX) / 2 * zoom,
      y: viewport.height / 2 - (bounds.minY + bounds.maxY) / 2 * zoom,
      zoom
    }
    const screen = getDetailPanelScreenPosition(anchor, transform, size)
    const nodeBottom = (selected.position.y + FLOW_NODE_SIZE.height) * zoom + transform.y
    assert.ok(transform.y >= 8, 'selected node stays inside the canvas')
    assert.ok(screen.y >= nodeBottom + DETAIL_PANEL_GAP * zoom - 0.01, 'panel does not cover the selected node')
    assert.ok(screen.x >= 8 && screen.x + size.width <= viewport.width - 8)
    assert.ok(screen.y + size.height <= viewport.height - 8, 'last panel controls remain within the canvas')
  }
}
for (const viewport of [{ width: 240, height: 180 }, { width: 12, height: 12 }]) {
  const size = getDetailPanelSize('LA_AlarmData_Code', viewport)
  assert.ok(size.width > 0 && size.width <= viewport.width)
  assert.ok(size.height > 0 && size.height <= viewport.height)
}
assert.deepEqual(getDetailPanelSize('LA_AlarmData_Code', { width: 0, height: 0 }), ALARM_DETAIL_PANEL_SIZE,
  'an unmeasured viewport keeps the default dimensions')

console.log('linkage flow layout checks passed')
