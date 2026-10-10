import { isAlarmDataAction } from './linkageFormCompatibility.js'

export const FLOW_NODE_SIZE = Object.freeze({ width: 96, height: 96 })
export const FLOW_TERMINAL_SIZE = Object.freeze({ width: 96, height: 48 })
export const DETAIL_PANEL_SIZE = Object.freeze({ width: 360, height: 350 })
export const ALARM_DETAIL_PANEL_SIZE = Object.freeze({ width: 760, height: 430 })
export const DETAIL_PANEL_GAP = 12

export const getFlowNodeDimensions = (node) => ({
  ...(node?.type === 'start' || node?.type === 'end' ? FLOW_TERMINAL_SIZE : FLOW_NODE_SIZE)
})

export const getFlowBounds = (
  nodes,
  getDimensions = getFlowNodeDimensions,
  initial = { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity }
) => nodes.reduce((bounds, node) => {
  const { width, height } = getDimensions(node)
  return {
    minX: Math.min(bounds.minX, node.position.x),
    minY: Math.min(bounds.minY, node.position.y),
    maxX: Math.max(bounds.maxX, node.position.x + width),
    maxY: Math.max(bounds.maxY, node.position.y + height)
  }
}, initial)

// Centering should keep short flows at their natural size while fitting long
// or branched flows. The padding also leaves room for stage labels above cards.
export const getFlowFitZoom = (bounds, viewport, maxZoom = 1, padding = 56) => {
  const width = bounds.maxX - bounds.minX
  const height = bounds.maxY - bounds.minY
  if (![width, height, viewport?.width, viewport?.height].every(value => Number.isFinite(value) && value > 0)) {
    return maxZoom
  }
  return Math.min(
    maxZoom,
    Math.max(1, viewport.width - padding * 2) / width,
    Math.max(1, viewport.height - padding * 2) / height
  )
}

export const getFlowLayoutSpacing = (dimensions = FLOW_NODE_SIZE) => ({
  nodesep: Math.max(40, Math.min(240, Math.round(dimensions.height * 0.5))),
  ranksep: Math.max(50, Math.min(320, Math.round(dimensions.width * 0.4)))
})

export const getDetailPanelSize = (actionId, viewport) => {
  const size = isAlarmDataAction(actionId) ? ALARM_DETAIL_PANEL_SIZE : DETAIL_PANEL_SIZE
  if (![viewport?.width, viewport?.height].every(value => Number.isFinite(value) && value > 0)) return size
  return {
    width: Math.min(size.width, Math.max(1, viewport.width - 80)),
    // Keep a row of nodes and its stage label above the scrolling form. On an
    // unusually short canvas, preserve a usable header/body without overflowing.
    height: Math.min(size.height, Math.max(1, viewport.height - 16), Math.max(120, viewport.height - 160))
  }
}

export const getDetailPanelAnchor = (
  node,
  dimensions = FLOW_NODE_SIZE,
  panelSize = DETAIL_PANEL_SIZE
) => ({
  x: node.position.x + dimensions.width / 2 - panelSize.width / 2,
  y: node.position.y + dimensions.height + DETAIL_PANEL_GAP
})

// The anchor is in canvas coordinates, but the detached panel does not scale.
export const getDetailPanelCanvasBounds = (anchor, panelSize, zoom) => {
  const minX = anchor.x + panelSize.width / 2 - panelSize.width / zoom / 2
  return {
    minX,
    minY: anchor.y,
    maxX: minX + panelSize.width / zoom,
    maxY: anchor.y + panelSize.height / zoom
  }
}

export const getDetailPanelScreenPosition = (anchor, viewport, panelSize) => ({
  x: anchor.x * viewport.zoom + viewport.x - panelSize.width * (1 - viewport.zoom) / 2,
  y: anchor.y * viewport.zoom + viewport.y
})
