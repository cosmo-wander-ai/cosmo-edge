<template>
  <div class="page">
    <main class="page-main" :style="{ width: `${width}px`, height: `${height}px` }">
      <div class="flow-canvas" :style="{ width: `${canvasViewport.width}px` }">
        <VueFlow
          v-model:nodes="nodes"
          v-model:edges="edges"
          :node-types="nodeTypes"
          :edge-types="edgeTypes"
          @node-click="handleNodeActive"
          @node-drag-start="handleNodeDragStart"
          @pane-click="handlePaneClick"
          @move="handleViewportMove"
        >
          <Background pattern-color="var(--flow-grid)" gap="16" />
          <Controls :show-interactive="false" :show-fit-view="false" />
        </VueFlow>
        <div class="flow-view-tools">
          <button type="button" class="flow-view-button" :title="t('glossary.flowActualSize')" @click="restoreReadingView">
            <span class="flow-zoom-value">{{ Math.round(currentViewport.zoom * 100) }}%</span>
            <span>{{ t('glossary.flowActualSize') }}</span>
          </button>
          <button type="button" class="flow-view-button" @click="centerView">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 3H3v5M16 3h5v5M21 16v5h-5M8 21H3v-5" /></svg>
            <span>{{ t('glossary.flowFitOverview') }}</span>
          </button>
        </div>
      </div>

      <!-- Docking reserves canvas space; floating remains available for dragging. -->
      <NodeDetailPanel
        v-if="detailPanelNodeId"
        ref="detailPanelRef"
        :node-id="detailPanelNodeId"
        :node-data="detailPanelNodeData"
        :atomic-list="atomicList"
        :position="screenPanelPosition"
        :viewport="{ width, height }"
        :docked="panelDocked"
        :dock-size="dockedPanelSize"
        :floating-size="floatingPanelSize"
        show-dock-control
        @toggle-dock="togglePanelMode"
        @close="closeDetailPanel"
        @config-change="handlePanelConfigChange"
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
import { ref, markRaw, watch, nextTick, computed, provide, readonly } from 'vue'
import { VueFlow, useVueFlow } from '@vue-flow/core'
import { Background } from '@vue-flow/background'
import ActionView from './ActionView.vue'
import { Controls } from '@vue-flow/controls'
import dagre from 'dagre'
import { flowEditorKey } from '@/views/gam/countManagement/arrangeDetail/flow/flowEditorContext.js'
import _ from 'lodash'
import { generateActionId } from './dataTools.js'
import { addFlowNode, createFlowGraph, deleteFlowNode, deleteFollowingFlow } from '@/utils/graphEdges.js'
import { collectFlowData, collectMetaDataParams, createNodeState, updateAtomicList, updateNodeConfig as replaceNodeConfig } from '@/views/gam/countManagement/arrangeDetail/flow/nodeState.js'
import { t } from '@/i18n'

import '@vue-flow/core/dist/style.css'
// theme-default.css 已移除 — 其默认阴影/边框样式由自定义CSS接管
import '@vue-flow/controls/dist/style.css'

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
  getFlowNodeDimensions
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
  algorithmData: {
    type: Object,
    default: () => ({})
  },
  algorithmMetadata: {
    type: Object,
    default: () => ({})
  }
})

const customMetadata = computed(() =>
  (props.algorithmMetadata?.params || []).filter(item => !item.level)
)
const nodes = ref([])
const edges = ref([])
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

const { onPaneReady } = useVueFlow()
const flowInstance = ref(null)
const templateVersion = ref(0)
const hasFitViewOnce = ref(false)
onPaneReady((instance) => {
  flowInstance.value = instance
  nextTick(() => {
    if (nodes.value.length) restoreReadingView()
  })
})

const addDialogVisible = ref(false)
const addDialogEdgeId = ref('')
const addDialogX = ref(0)
const addDialogY = ref(0)

const layoutDirection = 'LR'
const newFlowData = ref([])
const atomicList = ref([])
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
  return getFlowNodeDimensions(node)
}

const getLayoutSpacing = () => {
  // 依据当前节点最大尺寸动态计算间距，避免展开后遮挡
  let maxW = 0
  let maxH = 0
  nodes.value.filter((node) => !isStageGroupNode(node)).forEach((n) => {
    const d = getNodeDimensions(n)
    if (d.width > maxW) maxW = d.width
    if (d.height > maxH) maxH = d.height
  })
  // LR 布局下：nodesep 控制纵向间距，ranksep 控制横向间距
  return getFlowLayoutSpacing({ width: maxW, height: maxH })
}

const applyLayout = () => {
  const layoutNodes = nodes.value.filter((node) => !isStageGroupNode(node))
  const dagreGraph = new dagre.graphlib.Graph()
  dagreGraph.setDefaultEdgeLabel(() => ({}))

  const spacing = getLayoutSpacing()
  dagreGraph.setGraph({
    rankdir: layoutDirection,
    nodesep: spacing.nodesep,
    ranksep: spacing.ranksep
  })

  layoutNodes.forEach((node) => {
    const dimensions = getNodeDimensions(node)
    dagreGraph.setNode(node.id, dimensions)
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

  if (!hasFitViewOnce.value && positioned.length && flowInstance.value) {
    requestAnimationFrame(() => {
      if (!flowInstance.value) return
      restoreReadingView()
      hasFitViewOnce.value = true
    })
  }
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
const dockedPanelSize = computed(() => getDockedPanelSize(detailPanelNodeData.value?.actionId, props))
const floatingPanelSize = computed(() => getReadingFloatingPanelSize(detailPanelNodeData.value?.actionId, props, currentViewport.value.zoom))
const canvasViewport = computed(() => ({
  width: Math.max(1, props.width - (detailPanelNodeId.value && panelDocked.value ? dockedPanelSize.value.width + 12 : 0)),
  height: props.height
}))

/** VueFlow 视口移动/缩放时更新坐标 */
const handleViewportMove = (payload) => {
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
      y: Math.max(16, (props.height - floatingPanelSize.value.height) / 2)
    }
  }
  setFlowViewport({
    x: readingWidth / 2 - (node.position.x + dim.width / 2) * zoom,
    y: canvasViewport.value.height / 2 - (node.position.y + dim.height / 2) * zoom,
    zoom
  })
}

const restoreReadingView = () => {
  const visibleNodes = nodes.value.filter((node) => !isStageGroupNode(node))
  if (!visibleNodes.length) return
  if (detailPanelNodeId.value) {
    currentViewport.value = { ...currentViewport.value, zoom: 1 }
    focusNode(detailPanelNodeId.value)
    return
  }
  setFlowViewport(getFlowReadingViewport(getFlowBounds(visibleNodes, getNodeDimensions), canvasViewport.value))
}

const centerView = () => {
  const visibleNodes = nodes.value.filter((node) => !isStageGroupNode(node))
  if (!visibleNodes.length) return
  const bounds = getFlowBounds(visibleNodes, getNodeDimensions)
  const zoom = getFlowFitZoom(bounds, canvasViewport.value)
  setFlowViewport({
    x: canvasViewport.value.width / 2 - (bounds.minX + bounds.maxX) / 2 * zoom,
    y: canvasViewport.value.height / 2 - (bounds.minY + bounds.maxY) / 2 * zoom,
    zoom
  })
}

watch(
  () => [props.width, props.height],
  () => {
    if (detailPanelNodeId.value) {
      nextTick(() => requestAnimationFrame(() => focusNode(detailPanelNodeId.value)))
    }
  }
)

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

const getActionStage = (actionId = '') => {
  // 输入处理
  if (actionId === 'BA_00001') {
    return {
      key: 'input',
      title: t('glossary.inputProcessing'),
      titleKey: 'glossary.inputProcessing',
      fill: 'transparent',
      stroke: 'var(--flow-stage-input-line)',
      color: 'var(--flow-stage-input)'
    }
  }
  // 模型推理 (AA/DA/PA/PDA 前缀 + BA_00009混合目标关联)
  if (/^(AA|DA|PA|PDA)_/.test(actionId) || actionId === 'BA_00009') {
    return {
      key: 'detect',
      title: t('glossary.modelInference'),
      titleKey: 'glossary.modelInference',
      fill: 'transparent',
      stroke: 'var(--flow-stage-model-line)',
      color: 'var(--flow-stage-model)'
    }
  }
  // 告警输出 (事件上报/抓拍/特征上报)
  if (['BA_00004', 'BA_00006', 'BA_10004'].includes(actionId)) {
    return {
      key: 'output',
      title: t('glossary.alertOutput'),
      titleKey: 'glossary.alertOutput',
      fill: 'transparent',
      stroke: 'var(--flow-stage-output-line)',
      color: 'var(--flow-stage-output)'
    }
  }
  // 规则判断 (默认 — 筛选/灵敏度/判断类)
  return {
    key: 'rule',
    title: t('glossary.ruleJudgment'),
    titleKey: 'glossary.ruleJudgment',
    fill: 'transparent',
    stroke: 'var(--flow-stage-rule-line)',
    color: 'var(--flow-stage-rule)'
  }
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
    const stage = getActionStage(node.data?.actionId || '')
    const previous = segments[segments.length - 1]
    if (previous && previous.stage.key === stage.key) {
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
      // Bracket: only covers the area above the nodes (label + bracket line)
      const padX = 20
      const bracketHeight = 40  // label(18px) + gap(6px) + bracket-line top area
      const width = maxX - minX + padX * 2
      const height = bracketHeight
      return {
        id: `stage-group-${index}-${segment.stage.key}`,
        type: 'stageGroup',
        position: {
          x: minX - padX,
          y: minY - bracketHeight - 8
        },
        data: {
          isStageGroup: true,
          title: segment.stage.title,
          titleKey: segment.stage.titleKey,
          fill: segment.stage.fill,
          stroke: segment.stage.stroke,
          color: segment.stage.color,
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
  hasFitViewOnce.value = false
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
        })
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
  background-color: var(--flow-canvas);
}

.page-header {
  padding: 12px 20px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-white);
}

.page-header h1 {
  margin: 0;
  font-size: 18px;
}

.page-header p {
  margin: 4px 0 0;
  font-size: 13px;
  color: var(--text-secondary);
}

.page-main {
  display: flex;
  gap: 12px;
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

.flow-view-tools {
  position: absolute;
  left: 56px;
  bottom: 14px;
  display: flex;
  gap: 4px;
  max-width: calc(100% - 70px);
  flex-wrap: wrap;
  padding: 4px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 6px;
  z-index: 5;
}

.flow-view-button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 6px 8px;
  min-height: 30px;
  border: 0;
  border-radius: 4px;
  background: transparent;
  color: var(--text-secondary);
  font: inherit;
  font-size: 12px;
  cursor: pointer;
}

.flow-view-button:hover {
  background: var(--bg-secondary);
  color: var(--text-primary);
}

.flow-view-button:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 1px;
}

.flow-view-button svg {
  width: 16px;
  height: 16px;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.6;
}

.flow-zoom-value {
  min-width: 38px;
  font-variant-numeric: tabular-nums;
  color: var(--text-primary);
}

.page-main :deep(.vue-flow) {
  width: 100%;
  height: 100%;
  overflow: hidden;
}

.page-main :deep(.vue-flow__controls) {
  border: 1px solid var(--border-color);
  border-radius: 6px;
  box-shadow: none;
  overflow: hidden;
}

.page-main :deep(.vue-flow__controls-button) {
  color: var(--secondary-color);
  background: var(--bg-white);
  border-bottom-color: var(--border-light);
}

.page-main :deep(.vue-flow__controls-button:hover:not(:disabled)) {
  color: var(--primary-color);
  background: var(--bg-secondary);
}

.page-main :deep(.vue-flow__controls-button svg) {
  fill: currentColor;
}

.page-main :deep(.vue-flow__controls-button:disabled) {
  color: var(--text-muted);
  background: var(--bg-secondary);
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
