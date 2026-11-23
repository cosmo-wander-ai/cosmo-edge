<template>
  <div class="page scene-flow" :style="{ '--scene-icon-tile': `${sceneLayout.tileSize}px`, '--scene-label-size': `${sceneLayout.fontSize}px` }">
    <div ref="toolbarRef" class="flow-toolbar">
      <slot name="toolbar" />
      <div class="flow-view-tools">
        <button type="button" class="flow-zoom-button" :title="t('glossary.flowZoomOut')" :aria-label="t('glossary.flowZoomOut')" :disabled="currentViewport.zoom <= minZoom" @click="zoomOut">
          <svg viewBox="0 0 24 24" aria-hidden="true" stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 12h14" />
          </svg>
        </button>
        <span class="flow-zoom-value" aria-live="polite">{{ Math.round(currentViewport.zoom * 100) }}%</span>
        <button type="button" class="flow-zoom-button" :title="t('glossary.flowZoomIn')" :aria-label="t('glossary.flowZoomIn')" :disabled="currentViewport.zoom >= maxZoom" @click="zoomIn">
          <svg viewBox="0 0 24 24" aria-hidden="true" stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 12h14" />
            <path d="M12 5v14" />
          </svg>
        </button>
        <span class="flow-tool-separator" aria-hidden="true" />
        <button type="button" class="flow-view-button" :title="t('glossary.flowActualSize')" @click="restoreReadingView">{{ t('glossary.flowActualSize') }}</button>
        <button type="button" class="flow-view-button" @click="centerView">
          <svg viewBox="0 0 24 24" aria-hidden="true" stroke-linecap="round" stroke-linejoin="round">
            <path d="M8 3H5a2 2 0 0 0-2 2v3" />
            <path d="M21 8V5a2 2 0 0 0-2-2h-3" />
            <path d="M3 16v3a2 2 0 0 0 2 2h3" />
            <path d="M16 21h3a2 2 0 0 0 2-2v-3" />
          </svg>
          <span>{{ t('glossary.flowFitOverview') }}</span>
        </button>
      </div>
    </div>
    <main class="page-main" :style="{ width: `${width}px`, height: `${editorViewport.height}px` }">
      <div class="flow-canvas" :style="{ width: `${canvasViewport.width}px` }">
        <VueFlow
          v-model:nodes="nodes"
          v-model:edges="edges"
          :node-types="nodeTypes"
          :edge-types="edgeTypes"
          :default-edge-options="defaultEdgeOptions"
          @node-click="handleNodeActive"
          @node-drag-start="handleNodeDragStart"
          @pane-click="handlePaneClick"
          @move="handleViewportMove"
        >
          <Background pattern-color="var(--flow-grid)" :gap="16" :size="1.55" />
        </VueFlow>
      </div>

      <!-- Docking reserves canvas space; floating remains available for dragging. -->
      <NodeDetailPanel
        v-if="detailPanelNodeId"
        ref="detailPanelRef"
        :node-id="detailPanelNodeId"
        :node-data="detailPanelNodeData"
        :atomic-list="atomicsWithBehaviorScores"
        :scene-params="algorithmMetadata.params || []"
        :position="screenPanelPosition"
        :viewport="editorViewport"
        :docked="panelDocked"
        :dock-size="dockedPanelSize"
        :floating-size="floatingPanelSize"
        show-dock-control
        @toggle-dock="togglePanelMode"
        @close="closeDetailPanel"
        @config-change="handlePanelConfigChange"
        @picture-match-param-change="emit('picture-match-param-change', $event)"
      />
    </main>

    <teleport to="body">
      <el-dialog v-model="addDialogVisible" :title="t('action.addComponent')" width="710px" :close-on-click-modal="true" center :z-index="6000" class="ui-admin-dialog flow-theme">
        <div class="component-dialog">
          <ActionView :actionList="actionList" @onAction="addComponentFromAction" />
        </div>
      </el-dialog>
    </teleport>
  </div>
</template>


<script setup>
import { ref, markRaw, watch, nextTick, computed, provide, readonly, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import { VueFlow, useVueFlow } from '@vue-flow/core'
import { Background } from '@vue-flow/background'
import ActionView from './ActionView.vue'
import dagre from 'dagre'
import { flowEditorKey } from '@/views/gam/countManagement/arrangeDetail/flow/flowEditorContext.js'
import _ from 'lodash'
import { generateActionId } from './dataTools.js'
import { addFlowNode, createFlowGraph, deleteFlowNode, deleteFollowingFlow } from '@/utils/graphEdges.js'
import { attachBehaviorScores, collectFlowData, collectMetaDataParams, getAreaRuleSceneFallbacks, createNodeState, updateAtomicList, updateNodeConfig as replaceNodeConfig } from '@/views/gam/countManagement/arrangeDetail/flow/nodeState.js'
import { t } from '@/i18n'

import '@vue-flow/core/dist/style.css'
// theme-default.css 已移除 — 其默认阴影/边框样式由自定义CSS接管

import CustomFormNode from './CustomFormNode.vue'
import StartNode from './StartNode.vue'
import EndNode from './EndNode.vue'
import ActionEdge from './ActionEdge.vue'
import StageGroupNode from './StageGroupNode.vue'
import NodeDetailPanel from './NodeDetailPanel.vue'
import {
  getDockedPanelSize,
  getReadingFloatingPanelSize,
  getFlowBounds,
  getFlowFitZoom,
  getFlowReadingViewport,
  getFlowLayoutSpacing,
  getFlowNodeDimensions,
  getSceneFlowLayout
} from './layoutGeometry.js'

const props = defineProps({
  width: {
    type: Number,
    default: 0
  },
  height: {
    type: Number,
    default: 0
  },
  actionList: {
    type: Array,
    default: () => []
  },
  algorithmMetadata: {
    type: Object,
    default: () => ({})
  },
  algorithmData: {
    type: Object,
    default: () => ({})
  },
  algorithmMetadata: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['picture-match-param-change'])
const customMetadata = computed(() =>
  (props.algorithmMetadata?.params || []).filter(item => !item.level)
)
const nodes = ref([])
const edges = ref([])
const viewportWidth = ref(globalThis.innerWidth || props.width)
const sceneLayout = computed(() => getSceneFlowLayout(props.width, viewportWidth.value))
const activeEdgeId = ref(null)
const closeEdgeMenu = () => { activeEdgeId.value = null }
const nodeTypes = {
  start: markRaw(StartNode),
  customForm: markRaw(CustomFormNode),
  end: markRaw(EndNode),
  stageGroup: markRaw(StageGroupNode)
}
const edgeTypes = {
  action: markRaw(ActionEdge)
}
const defaultEdgeOptions = {
  markerEnd: { type: 'arrow', color: 'var(--flow-edge)', width: 12, height: 12 }
}

const { onPaneReady, minZoom, maxZoom } = useVueFlow()
const flowInstance = ref(null)
const templateVersion = ref(0)
const viewportInitialized = ref(false)
let viewMode = 'reading'
const initializeSceneViewport = () => {
  if (viewportInitialized.value || !flowInstance.value || !Object.keys(columnCenterX.value).length) return
  viewportInitialized.value = true
  if (viewMode === 'reading') restoreReadingView()
}
onPaneReady((instance) => {
  flowInstance.value = instance
  nextTick(initializeSceneViewport)
})

const addDialogVisible = ref(false)
const addDialogEdgeId = ref('')
const addDialogX = ref(0)
const addDialogY = ref(0)

const layoutDirection = 'LR'
const newFlowData = ref([])
const atomicList = ref([])
const behaviorModels = ref([])
const $API = getCurrentInstance().proxy.$API
const atomicsWithBehaviorScores = computed(() => attachBehaviorScores(atomicList.value, behaviorModels.value))
const columnCenterX = ref({})
const activeNodeId = ref('')
const lockedCenterY = ref({})

const parseArray = (val) => {
  if (Array.isArray(val)) return val
  if (typeof val === 'string' && val.trim()) {
    try {
      const r = JSON.parse(val)
      return Array.isArray(r) ? r : []
    } catch (e) {
      return []
    }
  }
  return []
}

const isStageGroupNode = (node) => {
  return Boolean(node && (node.type === 'stageGroup' || node.data?.isStageGroup))
}

const getNodeDimensions = (node) => {
  if (isStageGroupNode(node)) {
    return {
      width: Number(node.data?.width) || 360,
      height: Number(node.data?.height) || 180
    }
  }
  // Card and terminal dimensions match their rendered components.
  return getFlowNodeDimensions(node, sceneLayout.value)
}

const applyLayout = () => {
  const layoutNodes = nodes.value.filter((node) => !isStageGroupNode(node))
  const dagreGraph = new dagre.graphlib.Graph()
  dagreGraph.setDefaultEdgeLabel(() => ({}))

  dagreGraph.setGraph({
    rankdir: layoutDirection,
    nodesep: getFlowLayoutSpacing(sceneLayout.value.node).nodesep,
    ranksep: sceneLayout.value.gap
  })

  layoutNodes.forEach((node) => {
    dagreGraph.setNode(node.id, getNodeDimensions(node))
  })

  edges.value.forEach((edge) => {
    dagreGraph.setEdge(edge.source, edge.target)
  })

  dagre.layout(dagreGraph)

  const buildDepthMap = () => {
    const map = {}
    const adj = new Map()
    edges.value.forEach((e) => {
      const list = adj.get(e.source) || []
      list.push(e.target)
      adj.set(e.source, list)
    })
    const startId = '-1'
    map[startId] = 0
    const q = [startId]
    const seen = new Set([startId])
    while (q.length) {
      const cur = q.shift()
      const d = map[cur]
      const tgts = adj.get(cur) || []
      tgts.forEach((t) => {
        if (map[t] === undefined) {
          map[t] = d + 1
        } else {
          map[t] = Math.min(map[t], d + 1)
        }
        if (!seen.has(t)) {
          seen.add(t)
          q.push(t)
        }
      })
    }
    layoutNodes.forEach((n) => {
      if (map[n.id] === undefined) map[n.id] = 0
    })
    return map
  }
  const depthMap = buildDepthMap()
  const groups = {}
  layoutNodes.forEach((n) => {
    const pos = dagreGraph.node(n.id)
    if (!pos) return
    const d = depthMap[n.id] ?? 0
    if (!groups[d]) groups[d] = []
    groups[d].push(pos.x)
  })
  if (!columnCenterX.value || Object.keys(columnCenterX.value).length === 0) {
    const initCenters = {}
    Object.keys(groups).forEach((k) => {
      const xs = groups[k]
      const avg = xs.reduce((a, b) => a + b, 0) / xs.length
      initCenters[k] = avg
    })
    columnCenterX.value = initCenters
  } else {
    Object.keys(groups).forEach((k) => {
      if (columnCenterX.value[k] === undefined) {
        const xs = groups[k]
        const avg = xs.reduce((a, b) => a + b, 0) / xs.length
        columnCenterX.value[k] = avg
      }
    })
  }

  const positioned = layoutNodes.map((node) => {
    const pos = dagreGraph.node(node.id)
    if (!pos) return node

    const dimensions = getNodeDimensions(node)
    const d = depthMap[node.id] ?? 0
    const cx = columnCenterX.value[d] ?? pos.x
    const lockCy = lockedCenterY.value[node.id]
    const finalY =
      typeof lockCy === 'number' ? lockCy - dimensions.height / 2 : pos.y - dimensions.height / 2
    return {
      ...node,
      position: {
        x: cx - dimensions.width / 2,
        y: finalY
      }
    }
  })

  const stageNodes = buildStageGroupNodes(positioned)
  const nextNodes = [...stageNodes, ...positioned]
  nodes.value = nextNodes

  if (!viewportInitialized.value && positioned.length) nextTick(initializeSceneViewport)
}

// ---- 浮动配置面板状态 ----
const detailPanelRef = ref(null)
const detailPanelNodeId = ref(null)
const panelDocked = ref(true)
const detailPanelNodeData = computed(() =>
  nodes.value.find((node) => String(node.id) === String(detailPanelNodeId.value))?.data || null
)
const screenPanelPosition = ref({ x: 0, y: 0 })
const currentViewport = ref({ x: 0, y: 0, zoom: 1 })
const toolbarRef = ref(null)
const toolbarHeight = ref(49)
const editorViewport = computed(() => ({ width: props.width, height: Math.max(1, props.height - toolbarHeight.value) }))
const dockedPanelSize = computed(() => getDockedPanelSize(detailPanelNodeData.value?.actionId, editorViewport.value))
const floatingPanelSize = computed(() => getReadingFloatingPanelSize(
  detailPanelNodeData.value?.actionId, editorViewport.value, currentViewport.value.zoom, sceneLayout.value.node.width
))
const canvasViewport = computed(() => ({
  width: Math.max(1, props.width - (detailPanelNodeId.value && panelDocked.value ? dockedPanelSize.value.width : 0)),
  height: editorViewport.value.height
}))

let toolbarObserver = null
onMounted(() => {
  // Account for a wrapped toolbar at narrow widths and in longer translations.
  const measureToolbar = () => {
    viewportWidth.value = globalThis.innerWidth || props.width
    const measuredHeight = toolbarRef.value?.getBoundingClientRect?.().height
    if (measuredHeight > 0) toolbarHeight.value = measuredHeight
  }
  measureToolbar()
  if (toolbarRef.value && typeof ResizeObserver !== 'undefined') {
    toolbarObserver = new ResizeObserver(measureToolbar)
    toolbarObserver.observe(toolbarRef.value)
  }
})
onBeforeUnmount(() => toolbarObserver?.disconnect())

const zoomIn = () => {
  viewMode = 'manual'
  flowInstance.value?.zoomIn({ duration: 200 })
}
const zoomOut = () => {
  viewMode = 'manual'
  flowInstance.value?.zoomOut({ duration: 200 })
}

/** VueFlow 视口移动/缩放时更新坐标 */
const handleViewportMove = (payload) => {
  if (payload?.event?.sourceEvent || payload?.event?.type === 'wheel') viewMode = 'manual'
  const transform = payload?.flowTransform || payload
  if (transform && [transform.x, transform.y, transform.zoom].every(Number.isFinite)) {
    currentViewport.value = { x: transform.x, y: transform.y, zoom: transform.zoom }
  }
}

/** 收集当前面板配置并写回节点 data */
const updateNodeConfig = (nodeId, config) => {
  const nextNodes = replaceNodeConfig(nodes.value, nodeId, config)
  nodes.value = nextNodes
}

const handlePanelConfigChange = (config) => {
  updateNodeConfig(detailPanelNodeId.value, config)
}

const collectCurrentPanelConfig = () => {
  if (!detailPanelNodeId.value || !detailPanelRef.value) return
  const config = detailPanelRef.value.submitForm?.()
  updateNodeConfig(detailPanelNodeId.value, config)
}

/** 打开浮动面板 */
const openDetailPanel = (nodeId) => {
  const node = nodes.value.find((n) => n.id === nodeId)
  if (!node || isStageGroupNode(node) || node.type === 'start' || node.type === 'end') return

  // 如果已有面板打开，先保存当前配置
  if (detailPanelNodeId.value && detailPanelNodeId.value !== nodeId) {
    collectCurrentPanelConfig()
  }

  detailPanelNodeId.value = nodeId

  // 重置拖拽偏移（切换节点时复位）
  nextTick(() => {
    detailPanelRef.value?.resetDragOffset?.()
  })

  // 标记选中态
  markNodeSelected(nodeId)

  // After reserving the dock, keep the selected card readable in the remaining canvas.
  nextTick(() => {
    requestAnimationFrame(() => focusNode(nodeId))
  })
}

/** Closing saves the form and leaves the user's reading scale unchanged. */
const closeDetailPanel = () => {
  collectCurrentPanelConfig()
  detailPanelNodeId.value = null
  clearNodeSelected()
}

const togglePanelMode = () => {
  panelDocked.value = !panelDocked.value
  nextTick(() => {
    detailPanelRef.value?.resetDragOffset?.()
    requestAnimationFrame(() => focusNode(detailPanelNodeId.value))
  })
}

/** 标记节点选中态 */
const markNodeSelected = (id) => {
  const updated = nodes.value.map((n) => {
    if (isStageGroupNode(n)) return n
    const data = { ...(n.data || {}) }
    const style = { ...(n.style || {}) }
    data.selected = String(n.id) === String(id)
    if (data.selected) {
      style.zIndex = 50
    } else {
      if ('zIndex' in style) delete style.zIndex
    }
    return { ...n, data, style }
  })
  nodes.value = updated
}

/** 清除所有节点选中态 */
const clearNodeSelected = () => {
  const updated = nodes.value.map((n) => {
    if (isStageGroupNode(n)) return n
    const data = { ...(n.data || {}) }
    const style = { ...(n.style || {}) }
    data.selected = false
    if ('zIndex' in style) delete style.zIndex
    return { ...n, data, style }
  })
  nodes.value = updated
}

const handleNodeActive = (payload) => {
  if (isStageGroupNode(payload?.node || payload)) return
  const id =
    payload?.node?.id ??
    payload?.id ??
    (typeof payload === 'string' || typeof payload === 'number' ? payload : '')
  if (!id) return
  openDetailPanel(String(id))
  closeEdgeMenu()
}

const handleNodeDragStart = (payload) => {
  if (isStageGroupNode(payload?.node || payload)) return
  const id = payload?.node?.id ?? payload?.id ?? ''
  if (!id) return
  markNodeSelected(String(id))
  closeEdgeMenu()
}

const handlePaneClick = () => {
  closeDetailPanel()
  closeEdgeMenu()
}

const setFlowViewport = (viewport, duration = 240) => {
  if (!flowInstance.value) return
  flowInstance.value.setViewport(viewport, { duration })
  currentViewport.value = viewport
}

const focusNode = (nodeId) => {
  const node = nodes.value.find((n) => String(n.id) === String(nodeId))
  if (!node) return
  const dim = getNodeDimensions(node)
  const zoom = Math.max(1, currentViewport.value.zoom)
  const readingWidth = detailPanelNodeId.value && !panelDocked.value
    ? Math.max(dim.width * zoom + 32, props.width - floatingPanelSize.value.width - 32)
    : canvasViewport.value.width
  if (!panelDocked.value) {
    screenPanelPosition.value = {
      x: props.width - floatingPanelSize.value.width - 16,
      y: Math.max(16, (editorViewport.value.height - floatingPanelSize.value.height) / 2)
    }
  }
  setFlowViewport({
    x: readingWidth / 2 - (node.position.x + dim.width / 2) * zoom,
    y: canvasViewport.value.height / 2 - (node.position.y + dim.height / 2) * zoom,
    zoom
  })
}

const sceneBounds = () => {
  const bounds = getFlowBounds(nodes.value, getNodeDimensions)
  // Balance the 36px stage heading above the row with its 10px bottom inset.
  if (nodes.value.some(isStageGroupNode)) bounds.maxY += 10
  return bounds
}

const restoreReadingView = () => {
  if (!nodes.value.length) return
  viewMode = 'reading'
  if (detailPanelNodeId.value) {
    currentViewport.value = { ...currentViewport.value, zoom: 1 }
    focusNode(detailPanelNodeId.value)
    return
  }
  setFlowViewport(getFlowReadingViewport(sceneBounds(), canvasViewport.value, sceneLayout.value.inset))
}

const centerView = () => {
  if (!nodes.value.length) return
  viewMode = 'overview'
  const bounds = sceneBounds()
  const zoom = getFlowFitZoom(bounds, canvasViewport.value, 1, sceneLayout.value.inset)
  setFlowViewport({
    x: canvasViewport.value.width / 2 - (bounds.minX + bounds.maxX) / 2 * zoom,
    y: canvasViewport.value.height / 2 - (bounds.minY + bounds.maxY) / 2 * zoom,
    zoom
  })
}

watch([sceneLayout, () => props.height, toolbarHeight], ([layout], [previousLayout]) => {
  if (layout !== previousLayout) {
    columnCenterX.value = {}
    applyLayout()
  }
  if (detailPanelNodeId.value) {
    nextTick(() => requestAnimationFrame(() => focusNode(detailPanelNodeId.value)))
  } else if (viewportInitialized.value && viewMode !== 'manual') {
    if (viewMode === 'overview') centerView()
    else restoreReadingView()
  }
})

const handleEdgeMenuFocus = (nodeId) => {
  requestAnimationFrame(() => {
    markNodeSelected(nodeId)
    // focusNode(nodeId)
  })
}

const handleAddComponentDialogOpen = ({ edgeId, x, y } = {}) => {
  addDialogEdgeId.value = edgeId || ''
  addDialogX.value = Number(x || 0)
  addDialogY.value = Number(y || 0)
  addDialogVisible.value = true
  closeDetailPanel()
}

// 实时同步 atomicList：来自节点表单的更新
const handleAtomicUpdate = (payload) => {
  atomicList.value = updateAtomicList(atomicList.value, payload)
}

const commitGraph = ({ nodes: nextNodes, edges: nextEdges, removedNodeIds = [] }) => {
  if (removedNodeIds.some(id => String(id) === String(detailPanelNodeId.value))) {
    detailPanelNodeId.value = null
  }
  atomicList.value = atomicList.value.filter(atomic => !removedNodeIds.includes(String(atomic.position)))
  nodes.value = nextNodes
  edges.value = nextEdges
}

const deleteNode = (nodeId) => {
  commitGraph(deleteFlowNode(nodes.value, edges.value, nodeId, Date.now()))
  closeEdgeMenu()
}

const deleteFollowing = (operation) => {
  const result = deleteFollowingFlow(nodes.value, edges.value, { ...operation, timestamp: Date.now() })
  commitGraph(result)
  if (result.sourceId) handleEdgeMenuFocus(result.sourceId)
  closeEdgeMenu()
}

provide(flowEditorKey, {
  sceneLayout,
  customMetadata: readonly(customMetadata),
  nodes: readonly(nodes),
  edges: readonly(edges),
  activeEdgeId: readonly(activeEdgeId),
  closeEdgeMenu,
  toggleEdgeMenu(edgeId) {
    if (nodes.value.some(node => node.data?.expanded)) closeDetailPanel()
    activeEdgeId.value = activeEdgeId.value === edgeId ? null : edgeId
  },
  openDetailPanel,
  openAddDialog: handleAddComponentDialogOpen,
  deleteNode,
  deleteFollowing,
  updateAtomic: handleAtomicUpdate
})

const getNodeTypeForAction = (action) => {
  // const category = Number(action?.businessCategory)
  // if (category === 1) return 'detection'
  // if (category === 2) return 'customForm'
  return 'customForm'
}

const stageTitleKeys = {
  input: 'glossary.inputProcessing',
  detect: 'glossary.modelInference',
  rule: 'glossary.ruleJudgment',
  output: 'glossary.alertOutput'
}

const getActionStage = (actionId = '') => {
  if (actionId === 'BA_00001') return 'input'
  if (/^(AA|DA|PA|PDA)_/.test(actionId) || actionId === 'BA_00009') return 'detect'
  if (['BA_00004', 'BA_00006', 'BA_10004'].includes(actionId)) return 'output'
  return 'rule'
}

const getOrderedActionNodes = (positionedNodes) => {
  const nodeMap = new Map(positionedNodes.map((node) => [String(node.id), node]))
  const childrenMap = new Map()
  edges.value.forEach((edge) => {
    const source = String(edge.source)
    const target = String(edge.target)
    const list = childrenMap.get(source) || []
    list.push(target)
    childrenMap.set(source, list)
  })

  const ordered = []
  const visited = new Set()
  const visit = (nodeId) => {
    const children = (childrenMap.get(String(nodeId)) || []).slice().sort((a, b) => {
      const nodeA = nodeMap.get(String(a))
      const nodeB = nodeMap.get(String(b))
      if (!nodeA || !nodeB) return 0
      if (nodeA.position.x !== nodeB.position.x) return nodeA.position.x - nodeB.position.x
      return nodeA.position.y - nodeB.position.y
    })
    children.forEach((childId) => {
      if (visited.has(childId)) return
      visited.add(childId)
      const child = nodeMap.get(String(childId))
      if (child && child.type !== 'start' && child.type !== 'end') {
        ordered.push(child)
      }
      visit(childId)
    })
  }

  visit('-1')
  positionedNodes.forEach((node) => {
    if (node.type !== 'start' && node.type !== 'end' && !visited.has(String(node.id))) {
      ordered.push(node)
    }
  })
  return ordered
}

const buildStageGroupNodes = (positionedNodes) => {
  const segments = []
  getOrderedActionNodes(positionedNodes).forEach((node) => {
    const stage = node.data.stage
    const previous = segments[segments.length - 1]
    if (previous && previous.stage === stage) {
      previous.nodes.push(node)
    } else {
      segments.push({ stage, nodes: [node] })
    }
  })

  return segments
    .filter((segment) => segment.nodes.length > 0)
    .map((segment, index) => {
      const boxes = segment.nodes.map((node) => {
        const dim = getNodeDimensions(node)
        return {
          x: node.position.x,
          y: node.position.y,
          width: dim.width,
          height: dim.height
        }
      })
      const minX = Math.min(...boxes.map((box) => box.x))
      const minY = Math.min(...boxes.map((box) => box.y))
      const maxX = Math.max(...boxes.map((box) => box.x + box.width))
      // Stage labels track the card edges, leaving room for the connector controls.
      const padX = 0
      const bracketHeight = 36
      const width = maxX - minX + padX * 2
      const height = bracketHeight
      return {
        id: `stage-group-${index}-${segment.stage}`,
        type: 'stageGroup',
        position: {
          x: minX - padX,
          y: minY - bracketHeight
        },
        data: {
          isStageGroup: true,
          stage: segment.stage,
          titleKey: stageTitleKeys[segment.stage],
          width,
          height
        },
        draggable: false,
        selectable: false,
        connectable: false,
        deletable: false,
        zIndex: -10
      }
    })
}

const addComponentFromAction = (action) => {
  const normalized = {
    ...action,
    id: action?.id ?? action?.actionId ?? '',
    actionName: action?.actionName ?? action?.name ?? ''
  }
  const type = getNodeTypeForAction(normalized)
  const label = normalized.actionName || t('glossary.newNode')
  addComponentFromDialog(type, label, normalized)
}

const getEdgeEndpoints = (edgeId) => {
  const edge = edges.value.find(edge => edge.id === edgeId)
  return edge || { source: undefined, target: undefined }
}

const addComponentFromDialog = (type, label, action) => {
  const edgeId = addDialogEdgeId.value
  if (!edgeId) return
  const { source, target } = getEdgeEndpoints(edgeId)

  const newNodeId = generateActionId()
  let flowItem = Array.isArray(newFlowData.value)
    ? newFlowData.value.find(
        (f) => f.actionId === (action?.id ?? action?.actionId)
      )
    : undefined
  if (!flowItem) {
    const preId = source || '-1'
    flowItem = {
      actionId: action?.id ?? action?.actionId ?? '',
      actionName: action?.actionName ?? action?.name ?? '',
      remark: action?.description ?? action?.remark ?? '',
      flowActionId: String(newNodeId),
      preFlowActionId: String(preId),
      configObject: {
        webConfig: {
          labelList: [],
          labelFilterList: [],
          metaDataParams: [],
          atomic: {}
        },
        params: []
      },
      inputParamConfig: action?.inputParamConfig ?? ''
    }
  }

  // 方式一：直接通过 v-model 维护的 nodes/edges 数组更新（更直观、更稳定）
  const newNode = {
    id: newNodeId,
    type,
    position: { x: addDialogX.value, y: addDialogY.value },
    data: {
      label: label || t('glossary.newNode'),
      selectValue: '',
      checkedValues: [],
      inputValue: '',
      actionId: action?.id ?? action?.actionId,
      actionName: action?.actionName,
      actionType: action?.actionType,
      stage: getActionStage(action?.id ?? action?.actionId),
      // businessCategory: action?.businessCategory,
      description: action?.description,

      ...createNodeState(flowItem, {
        ...(action || {}),
        actionId: action?.id ?? action?.actionId ?? '',
        flowActionId: String(newNodeId),
        inputParamConfig: flowItem?.inputParamConfig ?? action?.inputParamConfig
      })
    }
  }
  commitGraph(addFlowNode(nodes.value, edges.value, newNode, {
    edgeId, source, target
  }, generateActionId))

  addDialogVisible.value = false

  nextTick(() => handleEdgeMenuFocus(newNodeId))
}

const rebuildFlowGraph = () => {
  viewportInitialized.value = false
  viewMode = 'reading'
  const items = Array.isArray(newFlowData.value) ? newFlowData.value : []
  columnCenterX.value = {}
  lockedCenterY.value = {}

  detailPanelNodeId.value = null
  closeEdgeMenu()
  addDialogVisible.value = false

  const actionMap = new Map(
    (Array.isArray(props.actionList) ? props.actionList : []).map((a) => [
      a.id,
      a
    ])
  )
  const makeType = (item) => {
    const meta = actionMap.get(item.actionId)
    return 'customForm'
  }

  const dataNodes = items.map((item) => {
    let newInputParamConfig = {}
    if (item.actionId === 'BA_90002') {
      newInputParamConfig = branchInputParamConfig.value
    } else {
      const action = _.find(props.actionList, { id: item.actionId })
      if (action) {
        newInputParamConfig = action.inputParamConfig
      }
    }
    // 用 actionList 中的标准名称覆盖 workflow 中可能过时的 actionName
    const canonicalName = actionMap.get(item.actionId)?.actionName || item.actionName
    return {
      id: String(item.flowActionId),
      type: makeType(item),
      position: { x: 0, y: 0 },
      data: {
        templateVersion: templateVersion.value,
        stage: getActionStage(item.actionId),
        label: canonicalName || t('glossary.node'),
        actionId: item.actionId,
        actionName: canonicalName,
        // businessCategory: actionMap.get(item.actionId)?.businessCategory,
        description: item.remark,

        ...createNodeState(item, {
          actionId: item.actionId,
          actionName: canonicalName,
          remark: item.remark,
          flowActionId: item.flowActionId,
          inputParamConfig: newInputParamConfig
        }, props.algorithmMetadata?.params)
      }
    }
  })
  commitGraph(createFlowGraph(items, dataNodes, generateActionId, Date.now()))

  // 渲染完成后居中显示
  requestAnimationFrame(() => {
    columnCenterX.value = {}
    applyLayout()
  })
}

const saveMetaDataParams = () => {
  collectCurrentPanelConfig()
  return collectMetaDataParams(nodes.value)
}

const saveFlowData = () => {
  // 先收集当前浮动面板的配置（如果有打开的面板）
  collectCurrentPanelConfig()
  const { items, atomicCollected } = collectFlowData(nodes.value, edges.value)
  // Algorithm templates keep canonical action names from the action catalog.
  items.forEach(item => {
    const action = (Array.isArray(props.actionList) ? props.actionList : []).find(action => action.id === item.actionId)
    item.actionName = action?.actionName || item.actionName
  })

  const params = {
    algorithmId: props.algorithmData?.algorithmCode || '',
    algorithmCategory: props.algorithmData?.algorithmCategory || '',
    algorithmUsage: props.algorithmData?.algorithmUsage || '',
    remark: props.algorithmData?.remark || '',
    atomicList: JSON.stringify(atomicCollected),
    algorithmProcessdata: JSON.stringify(items),
    algorithmMetadata:
      props.algorithmData?.algorithmMetadata ||
      JSON.stringify({ params: [], region: {}, regionType: 'quadrilateral' })
  }
  return params
}

// Expose functions to parent component
const clearFlow = () => {
  detailPanelNodeId.value = null
  closeEdgeMenu()
  addDialogVisible.value = false
  commitGraph({ nodes: [], edges: [] })
  newFlowData.value = []
  atomicList.value = []
  columnCenterX.value = {}
}

defineExpose({
  saveMetaDataParams,
  getAreaRuleSceneFallbacks: existing => getAreaRuleSceneFallbacks(nodes.value, existing),
  saveFlowData,
  clearFlow
})

const hasAlgorithmPayload = (val) => {
  if (!val || typeof val !== 'object') return false
  if ('algorithmProcessdata' in val) return true
  if (
    val.algorithmMetadata !== undefined ||
    val.atomicList !== undefined ||
    val.algorithmCode !== undefined ||
    val.algorithmName !== undefined
  ) {
    return true
  }
  return false
}

watch(
  () => edges.value.length,
  () => {
    requestAnimationFrame(() => {
      applyLayout()
    })
  },
  { immediate: true }
)

const branchInputParamConfig = computed(() =>
  JSON.stringify([{
    type: 'condition',
    defaultValue: '',
    description: t('glossary.conditionConfigDesc'),
    failedTip: t('validate.pleaseSelect', { name: '' }),
    key: 'condition',
    name: t('glossary.conditionConfig'),
    level: '1',
    regexpr: ''
  }])
)
let behaviorCatalogRequested = false
watch(
  () => detailPanelNodeData.value?.actionId,
  async (actionId) => {
    if (actionId !== 'BA_00002' || behaviorCatalogRequested || !atomicList.value.length) return
    behaviorCatalogRequested = true
    try {
      const result = await $API.atomicModelList({ modelType: 'yolo26_person_attributes', modelName: '', filePath: '' })
      behaviorModels.value = result?.resData?.list || []
    } catch {
      behaviorCatalogRequested = false
    }
  }
)
watch(
  () => props.algorithmData,
  (newVal) => {
    if (hasAlgorithmPayload(newVal)) {
      templateVersion.value += 1
      newFlowData.value = parseArray(newVal.algorithmProcessdata)
      atomicList.value = parseArray(newVal.atomicList)
      if (newFlowData.value) {
        newFlowData.value.forEach((item) => {
          if (item.actionId === 'BA_90002') {
            Object.assign(item, {
              inputParamConfig: branchInputParamConfig.value
            })
          } else {
            const action = _.find(props.actionList, { id: item.actionId })
            if (action) {
              Object.assign(item, { inputParamConfig: action.inputParamConfig })
            }
          }
        })
      } else {
        newFlowData.value = []
      }
      rebuildFlowGraph()
    }
  },
  { immediate: true, deep: true }
)
</script>

<style scoped>
.page {
  width: 100%;
  height: 100%;
  display: flex;
  flex-direction: column;
  background-color: var(--flow-scene-canvas);
}

.flow-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px 16px;
  flex-wrap: wrap;
  flex: none;
  min-height: 49px;
  box-sizing: border-box;
  padding: 8px 12px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-white);
}

.page-main {
  display: flex;
  flex: none;
  min-height: 0;
  overflow: hidden;
  position: relative;
}

.flow-canvas {
  position: relative;
  flex: none;
  height: 100%;
  min-width: 0;
}

.scene-flow :deep(.action-node) {
  border-radius: 6px;
  container: scene-card / inline-size;
}

.scene-flow :deep(.action-node .node-card) {
  gap: 5px;
  padding: 4px 1px;
}

.scene-flow :deep(.node-icon-wrapper) {
  width: var(--scene-icon-tile);
  height: var(--scene-icon-tile);
  border-radius: 7px;
}

.scene-flow :deep(.node-icon-wrapper svg) {
  width: 16px;
  height: 16px;
  stroke-width: 1.8;
}

.scene-flow :deep(.node-name) {
  height: auto;
  font-size: var(--scene-label-size);
  line-height: 1.35;
}

@container scene-card (max-width: 110px) {
  .scene-flow :deep(.node-name-text) {
    -webkit-line-clamp: 1;
  }
}

.scene-flow :deep(.start-node),
.scene-flow :deep(.end-node) {
  gap: 3px;
  padding: 0 5px;
  box-shadow: none;
}

.scene-flow :deep(.node-icon svg) {
  width: 16px;
  height: 16px;
}

.scene-flow :deep(.start-label),
.scene-flow :deep(.end-label) {
  font-size: 11px;
  font-weight: 400;
  line-height: 1.45;
}

.scene-flow :deep(.edge-action-button) {
  position: relative;
  width: 14px;
  height: 14px;
  font: 400 10px/10px Arial, sans-serif;
  box-shadow: none;
}

/* Keep the insertion control easy to hit at the smaller visible diameter. */
.scene-flow :deep(.edge-action-button::before) {
  content: '';
  position: absolute;
  inset: -5px;
  border-radius: 50%;
}

/* The first connector starts at the inner edge of the outlined terminal. */
.scene-flow :deep(.edge-action-button[data-source-type='start']) {
  left: -1px;
}

.scene-flow :deep(.vue-flow__handle:not(:hover):not(.connecting):not(.valid)) {
  opacity: 0;
}

.scene-flow :deep(.vue-flow__edge-path) {
  stroke-width: 1;
}

.flow-view-tools {
  display: flex;
  align-items: center;
  gap: 4px;
  flex-wrap: wrap;
  margin-left: auto;
}

.flow-view-button,
.flow-zoom-button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 5px 8px;
  height: 32px;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  background: transparent;
  color: var(--text-secondary);
  font: inherit;
  font-size: 12px;
  cursor: pointer;
}

.flow-view-button:hover,
.flow-zoom-button:hover:not(:disabled) {
  background: var(--bg-secondary);
  color: var(--text-primary);
}

.flow-view-button:focus-visible,
.flow-zoom-button:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 1px;
}

.flow-view-button svg,
.flow-zoom-button svg {
  width: 16px;
  height: 16px;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.7;
}

.flow-zoom-value {
  min-width: 48px;
  text-align: center;
  font-size: 13px;
  font-variant-numeric: tabular-nums;
  color: var(--text-primary);
}

.flow-zoom-button {
  width: 32px;
  padding: 6px;
}

.flow-zoom-button:disabled {
  cursor: not-allowed;
  color: var(--text-muted);
  background: var(--bg-secondary);
}

.flow-tool-separator {
  height: 18px;
  width: 1px;
  margin: 0 4px;
  background: var(--border-color);
}

.page-main :deep(.vue-flow) {
  width: 100%;
  height: 100%;
  overflow: hidden;
}

.page-main :deep(.edge-menu) {
  color: var(--text-primary);
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  box-shadow: var(--shadow-sm);
}



.component-dialog {
  padding: 0;
}

.group {
  margin-bottom: 16px;
}

.group-title {
  font-size: 14px;
  color: var(--text-primary);
  font-weight: 600;
  margin-bottom: 8px;
  display: flex;
  align-items: center;
}

.group-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
}

.comp-btn {
  width: 100%;
  text-align: left;
  border-radius: var(--radius-sm);
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  color: var(--text-primary);
}

.comp-btn:hover {
  border-color: var(--primary-color);
  background: var(--bg-secondary);
  color: var(--primary-color);
}

:deep(.el-dialog) {
  border-radius: var(--radius-sm);
  overflow: hidden;
  box-shadow: var(--shadow-md);
}

:deep(.el-dialog__header) {
  margin: 0;
  padding: 14px 16px;
  background: var(--bg-white);
}

:deep(.el-dialog__title) {
  color: var(--text-primary);
  font-weight: 600;
}

:deep(.el-dialog__headerbtn .el-dialog__close) {
  color: var(--text-secondary);
}

:deep(.el-dialog__body) {
  padding: 0;
  background: var(--bg-white);
}
</style>

<!-- 非 scoped：移除 theme-default 后的节点/连接点基本样式 -->
<style>
/* 节点 wrapper 清除所有装饰 */
.vue-flow__node {
  box-shadow: none !important;
  border-radius: 0 !important;
  border: none !important;
  background: transparent !important;
  padding: 0 !important;
}

.vue-flow__node.selected,
.vue-flow__node:focus,
.vue-flow__node:focus-visible {
  box-shadow: none !important;
  outline: none !important;
}

/* 连接锚点 handle 基本样式（原 theme-default 提供） */
.vue-flow__handle {
  width: 6px;
  height: 6px;
  background: var(--flow-edge, #b1b1b7);
  border: 2px solid var(--flow-handle-ring, #fff);
  border-radius: 50%;
}

.vue-flow__handle:hover {
  background: var(--primary-color);
}

/* 连线基本样式 */
.vue-flow__edge-path {
  stroke: var(--flow-edge, #b1b1b7);
  stroke-width: 1.25;
}

.vue-flow__edge.selected .vue-flow__edge-path,
.vue-flow__edge:hover .vue-flow__edge-path {
  stroke: var(--primary-color);
}
</style>
