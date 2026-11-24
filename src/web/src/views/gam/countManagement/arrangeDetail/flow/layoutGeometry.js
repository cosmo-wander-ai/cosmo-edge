import { isAlarmDataAction } from './linkageFormCompatibility.js'

export const FLOW_NODE_SIZE = Object.freeze({ width: 136, height: 108 })
export const FLOW_TERMINAL_SIZE = Object.freeze({ width: 104, height: 44 })
export const DETAIL_PANEL_SIZE = Object.freeze({ width: 360, height: 350 })
export const ALARM_DETAIL_PANEL_SIZE = Object.freeze({ width: 760, height: 430 })
export const DETAIL_PANEL_GAP = 12

// Six action columns and two narrower terminals form the reference row.
// Shorter and branched workflows use the same card size; long rows can pan.
export const getSceneFlowLayout = (canvasWidth, viewportWidth) => {
  const inset = viewportWidth <= 730 ? 8 : 12
  const gap = Math.min(26, Math.max(16, viewportWidth * 0.0255))
  const rowWidth = Math.max(668, canvasWidth - 2 * inset)
  const width = (rowWidth - 7 * gap) / (6 + 2 * 0.76)
  const tileSize = Math.min(37, Math.max(29, viewportWidth * 0.035))
  const fontSize = Math.min(13, Math.max(11, viewportWidth * 0.0126))
  const contentHeight = tileSize + 5 + fontSize * 1.35 + 2 * (4 + 1)
  return {
    node: { width, height: Math.max(contentHeight, width * FLOW_NODE_SIZE.height / FLOW_NODE_SIZE.width) },
    terminal: { width: 0.76 * width, height: 29 },
    gap,
    inset,
    tileSize,
    fontSize
  }
}

// Reading is the default. Long flows start at the left and stay at 100%; an
// explicit overview action can still fit every branch into the available area.
export const getFlowReadingViewport = (bounds, viewport, padding = 56) => {
  const width = bounds.maxX - bounds.minX
  const height = bounds.maxY - bounds.minY
  if (![width, height, viewport?.width, viewport?.height].every(value => Number.isFinite(value) && value > 0)) {
    return { x: padding, y: padding, zoom: 1 }
  }
  return {
    x: Math.max(padding, (viewport.width - width) / 2) - bounds.minX,
    y: Math.max(padding, (viewport.height - height) / 2) - bounds.minY,
    zoom: 1
  }
}

export const getDockedPanelSize = (actionId, viewport) => {
  const availableWidth = Math.max(1, Number(viewport?.width) || 1)
  const availableHeight = Math.max(1, Number(viewport?.height) || 1)
  const preferredWidth = isAlarmDataAction(actionId) ? 640 : 460
  return {
    // Keep at least half the canvas for the graph; the panel's own content scrolls.
    width: Math.max(1, Math.min(preferredWidth, Math.floor(availableWidth * 0.48))),
    height: availableHeight
  }
}

export const getReadingFloatingPanelSize = (actionId, viewport, zoom = 1, nodeWidth = FLOW_NODE_SIZE.width) => {
  const size = isAlarmDataAction(actionId) ? ALARM_DETAIL_PANEL_SIZE : DETAIL_PANEL_SIZE
  const width = Math.max(1, Number(viewport?.width) || size.width + 240)
  const height = Math.max(1, Number(viewport?.height) || size.height + 32)
  return {
    // Reserve the rendered card width plus its reading margins.
    width: Math.max(1, Math.min(size.width, width - nodeWidth * Math.max(1, zoom) - 144)),
    height: Math.max(1, Math.min(size.height, height - 32))
  }
}

// Dagre writes x/y onto each label, so return a fresh object for every node.
export const getFlowNodeDimensions = (node, { node: card = FLOW_NODE_SIZE, terminal = FLOW_TERMINAL_SIZE } = {}) => ({
  ...(node?.type === 'start' || node?.type === 'end' ? terminal : card)
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
