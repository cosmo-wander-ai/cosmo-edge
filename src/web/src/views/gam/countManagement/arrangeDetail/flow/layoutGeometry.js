import { isAlarmDataAction } from './linkageFormCompatibility.js'

export const FLOW_NODE_SIZE = Object.freeze({ width: 76, height: 96 })
export const DETAIL_PANEL_SIZE = Object.freeze({ width: 360, height: 350 })
export const ALARM_DETAIL_PANEL_SIZE = Object.freeze({ width: 760, height: 430 })
export const DETAIL_PANEL_GAP = 12

export const getFlowNodeDimensions = () => ({ ...FLOW_NODE_SIZE })

export const getFlowLayoutSpacing = (dimensions = FLOW_NODE_SIZE) => ({
  nodesep: Math.max(40, Math.min(240, Math.round(dimensions.height * 0.5))),
  ranksep: Math.max(50, Math.min(320, Math.round(dimensions.width * 0.4)))
})

export const getDetailPanelSize = (actionId) =>
  actionId === 'BA_20003' ? { width: 760, height: 560 } :
    isAlarmDataAction(actionId) ? ALARM_DETAIL_PANEL_SIZE : DETAIL_PANEL_SIZE

export const getDetailPanelAnchor = (
  node,
  dimensions = FLOW_NODE_SIZE,
  panelSize = DETAIL_PANEL_SIZE
) => ({
  x: node.position.x + dimensions.width / 2 - panelSize.width / 2,
  y: node.position.y + dimensions.height + DETAIL_PANEL_GAP
})
