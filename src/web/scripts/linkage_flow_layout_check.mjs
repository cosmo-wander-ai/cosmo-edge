import assert from 'node:assert/strict'
import {
  DETAIL_PANEL_GAP,
  DETAIL_PANEL_SIZE,
  ALARM_DETAIL_PANEL_SIZE,
  FLOW_NODE_SIZE,
  getDetailPanelAnchor,
  getDetailPanelSize,
  getFlowLayoutSpacing
} from '../src/views/gam/countManagement/arrangeDetail/flow/layoutGeometry.js'

const spacing = getFlowLayoutSpacing(FLOW_NODE_SIZE)
assert.ok(FLOW_NODE_SIZE.width > 0 && FLOW_NODE_SIZE.height > 0)
assert.ok(spacing.nodesep >= 0 && spacing.ranksep >= 0)

// Start + one linkage action + end should remain a compact horizontal flow.
const compactWidth = FLOW_NODE_SIZE.width * 3 + spacing.ranksep * 2
assert.ok(compactWidth < 400, `three-node flow is too wide: ${compactWidth}px`)

const node = { position: { x: 500, y: 200 } }
const panel = getDetailPanelAnchor(node)
assert.equal(panel.x, 500 + FLOW_NODE_SIZE.width / 2 - DETAIL_PANEL_SIZE.width / 2)
assert.equal(panel.y, 200 + FLOW_NODE_SIZE.height + DETAIL_PANEL_GAP)

const alarmPanelSize = getDetailPanelSize('LA_AlarmData_Code')
assert.deepEqual(alarmPanelSize, ALARM_DETAIL_PANEL_SIZE)
assert.ok(alarmPanelSize.width >= DETAIL_PANEL_SIZE.width * 2)
const alarmPanel = getDetailPanelAnchor(node, FLOW_NODE_SIZE, alarmPanelSize)
assert.equal(alarmPanel.x, 500 + FLOW_NODE_SIZE.width / 2 - alarmPanelSize.width / 2)

console.log('linkage flow layout checks passed')
