<template>
  <div class="page flow-theme">
    <main class="page-main" :style="{ width: `${width}px`, height: `${height}px` }">
      <div class="flow-canvas" :style="{ width: `${canvasViewport.width}px` }">
        <VueFlow
          v-model:nodes="nodes"
          v-model:edges="edges"
          :node-types="nodeTypes"
          :edge-types="edgeTypes"
          @node-click="handleNodeClick"
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
      <el-dialog v-model="addDialogVisible" title="添加组件" width="710px" :close-on-click-modal="true" center :z-index="6000" class="ui-admin-dialog flow-theme">
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
import { Controls } from '@vue-flow/controls'
import dagre from 'dagre'
import _ from 'lodash'
import { t } from '@/i18n'

import '@vue-flow/core/dist/style.css'
import '@vue-flow/controls/dist/style.css'

import { flowEditorKey } from '@/views/gam/countManagement/arrangeDetail/flow/flowEditorContext.js'
import { generateActionId } from '@/views/gam/countManagement/arrangeDetail/flow/dataTools.js'
import { addFlowNode, createFlowGraph, deleteFlowNode, deleteFollowingFlow } from '@/utils/graphEdges.js'
import { collectFlowData, collectMetaDataParams, createNodeState, updateAtomicList, updateNodeConfig as replaceNodeConfig } from '@/views/gam/countManagement/arrangeDetail/flow/nodeState.js'
import ActionView from '@/views/gam/countManagement/arrangeDetail/flow/ActionView.vue'
import CustomFormNode from '@/views/gam/countManagement/arrangeDetail/flow/CustomFormNode.vue'
import StartNode from '@/views/gam/countManagement/arrangeDetail/flow/StartNode.vue'
import EndNode from '@/views/gam/countManagement/arrangeDetail/flow/EndNode.vue'
import ActionEdge from '@/views/gam/countManagement/arrangeDetail/flow/ActionEdge.vue'
import NodeDetailPanel from '@/views/gam/countManagement/arrangeDetail/flow/NodeDetailPanel.vue'
import {
  getDockedPanelSize,
  getReadingFloatingPanelSize,
  getFlowBounds,
  getFlowFitZoom,
  getFlowReadingViewport,
  getFlowLayoutSpacing,
  getFlowNodeDimensions
} from '@/views/gam/countManagement/arrangeDetail/flow/layoutGeometry.js'

const props = defineProps({
  width: {
    type: Number,
    default: 0
  },
  height: {
    type: Number,
    default: 0
  },
  strategyId: {
    type: [String, Number],
    default: ''
  },
  actionList: {
    type: Array,
    default: () => []
  },
  workFlow: {
    type: String,
    default: '[]'
  },
})

const nodes = ref([])
const edges = ref([])
const activeEdgeId = ref(null)
const closeEdgeMenu = () => { activeEdgeId.value = null }

const nodeTypes = {
  start: markRaw(StartNode),
  customForm: markRaw(CustomFormNode),
  end: markRaw(EndNode)
}

const edgeTypes = {
  action: markRaw(ActionEdge)
}
const { onPaneReady } = useVueFlow()
const flowInstance = ref(null)
onPaneReady((instance) => {
  flowInstance.value = instance
  nextTick(() => {
    if (!nodes.value.length) return
    restoreReadingView()
    centeredStrategyId.value = String(props.strategyId || '')
  })
})

const addDialogVisible = ref(false)
const addDialogEdgeId = ref('')
const addDialogX = ref(0)
const addDialogY = ref(0)

const layoutDirection = 'LR'
const newFlowData = ref([])
const atomicList = ref([])
const centeredStrategyId = ref(null)
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

const getNodeDimensions = (node) => {
  // Keep the linkage graph on the same fixed card geometry as algorithm
  // orchestration. Configuration is rendered in the floating panel and must
  // not inflate Dagre's node bounds.
  return getFlowNodeDimensions(node)
}

const getLayoutSpacing = () => {
  let maxW = 0
  let maxH = 0
  nodes.value.forEach((node) => {
    const dimensions = getNodeDimensions(node)
    maxW = Math.max(maxW, dimensions.width)
    maxH = Math.max(maxH, dimensions.height)
  })
  return getFlowLayoutSpacing({ width: maxW, height: maxH })
}

const applyLayout = () => {
  const dagreGraph = new dagre.graphlib.Graph()
  dagreGraph.setDefaultEdgeLabel(() => ({}))

  const spacing = getLayoutSpacing()
  dagreGraph.setGraph({
    rankdir: layoutDirection,
    nodesep: spacing.nodesep,
    ranksep: spacing.ranksep
  })

  nodes.value.forEach((node) => {
    const dimensions = getNodeDimensions(node)
    dagreGraph.setNode(node.id, dimensions)
  })

  edges.value.forEach((edge) => {
    dagreGraph.setEdge(edge.source, edge.target)
  })

  dagre.layout(dagreGraph)

  const positioned = nodes.value.map((node) => {
    const pos = dagreGraph.node(node.id)
    if (!pos) return node

    const dimensions = getNodeDimensions(node)
    return {
      ...node,
      position: {
        x: pos.x - dimensions.width / 2,
        y: pos.y - dimensions.height / 2
      }
    }
  })

  nodes.value = positioned

  const strategyId = String(props.strategyId || '')
  if (centeredStrategyId.value !== strategyId) {
    requestAnimationFrame(() => {
      if (!flowInstance.value) return
      restoreReadingView()
      centeredStrategyId.value = strategyId
    })
  }
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
  const visibleNodes = nodes.value
  if (!visibleNodes.length) return
  if (detailPanelNodeId.value) {
    currentViewport.value = { ...currentViewport.value, zoom: 1 }
    focusNode(detailPanelNodeId.value)
    return
  }
  setFlowViewport(getFlowReadingViewport(getFlowBounds(visibleNodes, getNodeDimensions), canvasViewport.value))
}

const centerView = () => {
  const visibleNodes = nodes.value
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

const updateNodeConfig = (nodeId, config) => {
  const nextNodes = replaceNodeConfig(nodes.value, nodeId, config)
  nodes.value = nextNodes
}

const collectCurrentPanelConfig = () => {
  if (!detailPanelNodeId.value || !detailPanelRef.value) return
  updateNodeConfig(
    detailPanelNodeId.value,
    detailPanelRef.value.submitForm?.()
  )
}

const handlePanelConfigChange = (config) => {
  updateNodeConfig(detailPanelNodeId.value, config)
}

const handleViewportMove = (payload) => {
  const transform = payload?.flowTransform || payload
  if (transform && [transform.x, transform.y, transform.zoom].every(Number.isFinite)) {
    currentViewport.value = { x: transform.x, y: transform.y, zoom: transform.zoom }
  }
}

const markNodeSelected = (nodeId) => {
  const nextNodes = nodes.value.map((node) => ({
    ...node,
    data: {
      ...(node.data || {}),
      selected: String(node.id) === String(nodeId)
    }
  }))
  nodes.value = nextNodes
}

const clearNodeSelected = () => {
  const nextNodes = nodes.value.map((node) => ({
    ...node,
    data: {
      ...(node.data || {}),
      selected: false
    }
  }))
  nodes.value = nextNodes
}

const handleNodeClick = ({ node } = {}) => {
  if (!node || node.type === 'start' || node.type === 'end') return
  if (detailPanelNodeId.value && detailPanelNodeId.value !== String(node.id)) {
    collectCurrentPanelConfig()
  }
  detailPanelNodeId.value = String(node.id)
  markNodeSelected(node.id)
  closeEdgeMenu()

  nextTick(() => {
    detailPanelRef.value?.resetDragOffset?.()
    requestAnimationFrame(() => focusNode(node.id))
  })
}

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

const handlePaneClick = () => {
  closeDetailPanel()
  closeEdgeMenu()
}

const handleOpenDetailPanel = (nodeId) => {
  const node = nodes.value.find((item) => String(item.id) === String(nodeId))
  if (node) handleNodeClick({ node })
}

const handleAddComponentDialogOpen = ({ edgeId, x, y } = {}) => {
  addDialogEdgeId.value = edgeId || ''
  addDialogX.value = Number(x || 0)
  addDialogY.value = Number(y || 0)
  addDialogVisible.value = true
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
  closeEdgeMenu()
}

provide(flowEditorKey, {
  customMetadata: readonly(ref([])),
  nodes: readonly(nodes),
  edges: readonly(edges),
  activeEdgeId: readonly(activeEdgeId),
  closeEdgeMenu,
  toggleEdgeMenu(edgeId) {
    if (nodes.value.some(node => node.data?.expanded)) closeDetailPanel()
    activeEdgeId.value = activeEdgeId.value === edgeId ? null : edgeId
  },
  openDetailPanel: handleOpenDetailPanel,
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

const addComponentFromAction = (action) => {
  const normalized = {
    ...action,
    id: action?.id ?? action?.actionId ?? '',
    actionName: action?.actionName ?? action?.name ?? ''
  }
  const type = getNodeTypeForAction(normalized)
  const label = normalized.actionName || '新节点'
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
      label: label || '新节点',
      selectValue: '',
      checkedValues: [],
      inputValue: '',
      actionId: action?.id ?? action?.actionId,
      actionName: action?.actionName,
      actionType: action?.actionType,
      // businessCategory: action?.businessCategory,
      description: action?.description ?? action?.remark,

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
}

const rebuildFlowGraph = () => {
  const items = Array.isArray(newFlowData.value) ? newFlowData.value : []

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
    return {
      id: String(item.flowActionId),
      type: makeType(item),
      position: { x: 0, y: 0 },
      data: {
        label: item.actionName || '节点',
        actionId: item.actionId,
        actionName: item.actionName,
        // businessCategory: actionMap.get(item.actionId)?.businessCategory,
        description: item.remark,

        ...createNodeState(item, {
          actionId: item.actionId,
          actionName: item.actionName,
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
    applyLayout()
  })
}

const saveMetaDataParams = () => {
  collectCurrentPanelConfig()
  return collectMetaDataParams(nodes.value)
}

const saveFlowData = () => {
  collectCurrentPanelConfig()
  const { items, atomicCollected } = collectFlowData(nodes.value, edges.value)

  return {
    workFlow: JSON.stringify(items),
    atomicList: JSON.stringify(atomicCollected)
  }
}

// Expose functions to parent component
const clearFlow = () => {
  detailPanelNodeId.value = null
  closeEdgeMenu()
  addDialogVisible.value = false
  commitGraph({ nodes: [], edges: [] })
  newFlowData.value = []
  atomicList.value = []
}

defineExpose({
  saveMetaDataParams,
  saveFlowData,
  clearFlow
})

watch(
  () => edges.value.length,
  () => {
    requestAnimationFrame(() => {
      applyLayout()
    })
  },
  { immediate: true }
)

const branchInputParamConfig = ref(
  '[{"type":"condition","defaultValue":"","description":"配置条件使其结果为真，并运行下面的动作","failedTip":"请选择","key":"condition","name":"条件配置","level":"1","regexpr":""}]'
)
watch(
  [() => props.strategyId, () => props.workFlow],
  ([, newVal]) => {
    detailPanelNodeId.value = null
    newFlowData.value = parseArray(newVal)
    atomicList.value = []
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
  },
  { immediate: true, deep: true }
)
</script>

<style lang="scss" src="../../../gam/countManagement/arrangeDetail/flow/flow-palette.scss"></style>

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
  color: var(--text-secondary);
  background: var(--bg-white);
  border-bottom-color: var(--border-light);
}

.page-main :deep(.vue-flow__controls-button:hover:not(:disabled)) {
  color: var(--primary-color);
  background: var(--bg-secondary);
}

.page-main :deep(.vue-flow__controls-button:disabled) {
  color: var(--text-muted);
  background: var(--bg-secondary);
}

.page-main :deep(.vue-flow__controls-button svg) {
  fill: currentColor;
}

.page-main :deep(.vue-flow__handle) {
  width: 6px;
  height: 6px;
  border: 2px solid var(--flow-handle-ring);
  border-radius: 50%;
  background: var(--flow-edge);
}

.page-main :deep(.vue-flow__handle:hover) {
  background: var(--primary-color);
}

.page-main :deep(.vue-flow__edge-path) {
  stroke: var(--flow-edge);
  stroke-width: 1.25;
}

.page-main :deep(.vue-flow__edge.selected .vue-flow__edge-path),
.page-main :deep(.vue-flow__edge:hover .vue-flow__edge-path) {
  stroke: var(--primary-color);
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
  box-shadow: var(--shadow-lg);
}

:deep(.el-dialog__header) {
  margin: 0;
  padding: 14px 16px;
  background: var(--bg-secondary);
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
