<template>
  <div class="page">
    <main class="page-main" :style="{ width: `${width}px`, height: `${height}px` }">
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
        <Background pattern-color="#e5e7eb" gap="16" />
        <Controls  :show-interactive="false" />
        <!-- <MiniMap /> -->
      </VueFlow>

      <!-- 浮动配置面板（在 VueFlow 外部，使用屏幕坐标定位） -->
      <NodeDetailPanel
        v-if="detailPanelNodeId"
        ref="detailPanelRef"
        :node-id="detailPanelNodeId"
        :node-data="detailPanelNodeData"
        :position="screenPanelPosition"
        @close="closeDetailPanel"
        @config-change="handlePanelConfigChange"
      />
    </main>

    <teleport to="body">
      <el-dialog v-model="addDialogVisible" :title="t('action.addComponent')" width="710px" :close-on-click-modal="true" center :z-index="6000">
        <div class="component-dialog">
          <ActionView :actionList="actionList" @onAction="addComponentFromAction" />
        </div>
      </el-dialog>
    </teleport>
  </div>
</template>


<script setup>
import { ref, markRaw, watch, nextTick, computed, onMounted, onBeforeUnmount } from 'vue'
import { VueFlow, useVueFlow } from '@vue-flow/core'
import { Background } from '@vue-flow/background'
import ActionView from './ActionView.vue'
import { Controls } from '@vue-flow/controls'
import { MiniMap } from '@vue-flow/minimap'
import dagre from 'dagre'
import EventBus from '@/components/eventBus.js'
import _ from 'lodash'
import { generateActionId } from './dataTools.js'
import { t } from '@/i18n'

import '@vue-flow/core/dist/style.css'
// theme-default.css 已移除 — 其默认阴影/边框样式由自定义CSS接管
import '@vue-flow/minimap/dist/style.css'
import '@vue-flow/controls/dist/style.css'

import CustomFormNode from './CustomFormNode.vue'
import StartNode from './StartNode.vue'
import EndNode from './EndNode.vue'
import ActionEdge from './ActionEdge.vue'
import StageGroupNode from './StageGroupNode.vue'
import NodeDetailPanel from './NodeDetailPanel.vue'

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
  }
})

const nodes = ref([])
const edges = ref([])
const nodeTypes = {
  start: markRaw(StartNode),
  customForm: markRaw(CustomFormNode),
  end: markRaw(EndNode),
  stageGroup: markRaw(StageGroupNode)
}
const edgeTypes = {
  action: markRaw(ActionEdge)
}

const { setNodes, setEdges, addNodes, onPaneReady, getEdges } = useVueFlow()
const flowInstance = ref(null)
const templateVersion = ref(0)
const hasFitViewOnce = ref(false)
onPaneReady((instance) => {
  flowInstance.value = instance
  // 设置默认视口，不使用自动适应
  // instance.setViewport({ x: 100, y: 200, zoom: 0.6 })
})

const addDialogVisible = ref(false)
const addDialogEdgeId = ref('')
const addDialogX = ref(0)
const addDialogY = ref(0)
const addDialogMode = ref('insert') // insert | branch
const addDialogSourceId = ref('')

const nodeWidth = 260
const nodeHeight = 160
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
  // 所有业务节点均为固定尺寸卡片（不再有展开态）
  return { width: 76, height: 96 }
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
  const nodesep = Math.max(40, Math.min(240, Math.round(maxH * 0.5)))
  const ranksep = Math.max(50, Math.min(320, Math.round(maxW * 0.4)))
  return { nodesep, ranksep }
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
  setNodes(nextNodes)

  if (!hasFitViewOnce.value && flowInstance.value) {
    requestAnimationFrame(() => {
      if (!flowInstance.value) return
      const totalCount = Array.isArray(positioned) ? positioned.length : 0
      if (totalCount < 5) {
        centerView()
      } else {
        flowInstance.value.fitView()
      }
      hasFitViewOnce.value = true
    })
  }
}

// ---- 浮动配置面板状态 ----
const detailPanelRef = ref(null)
const detailPanelNodeId = ref(null)
const detailPanelNodeData = ref(null)
const detailPanelPosition = ref({ x: 0, y: 0 })  // 画布坐标
const currentViewport = ref({ x: 0, y: 0, zoom: 1 })

/** 画布坐标 → 屏幕坐标（相对于 .page-main 容器） */
const screenPanelPosition = computed(() => {
  const vp = currentViewport.value
  return {
    x: detailPanelPosition.value.x * vp.zoom + vp.x,
    y: detailPanelPosition.value.y * vp.zoom + vp.y
  }
})

/** VueFlow 视口移动/缩放时更新坐标 */
const handleViewportMove = (transform) => {
  if (transform) {
    currentViewport.value = { x: transform.x, y: transform.y, zoom: transform.zoom }
  }
}

/** 收集当前面板配置并写回节点 data */
const updateNodeConfig = (nodeId, config) => {
  if (!nodeId || !config) return
  const nextNodes = nodes.value.map((node) => {
    if (String(node.id) !== String(nodeId) || !node.data) return node
    const flowData = {
      ...(node.data.flowData || {}),
      configObject: config
    }
    const actionDetail = {
      ...(node.data.actionDetail || {}),
      configObject: config
    }
    return {
      ...node,
      data: {
        ...node.data,
        flowData,
        actionDetail
      }
    }
  })
  nodes.value = nextNodes
  setNodes(nextNodes)

  if (String(detailPanelNodeId.value) === String(nodeId)) {
    const currentNode = nextNodes.find((node) => String(node.id) === String(nodeId))
    detailPanelNodeData.value = currentNode?.data || null
  }
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

  const dim = getNodeDimensions(node)
  const panelGap = 12     // 节点底部与面板顶部之间的间距
  const panelW = 360      // 面板宽度（与 NodeDetailPanel.vue 保持一致）
  const panelH = 350      // 面板最大高度

  // 面板顶部对齐选中节点的底部，X 居中对齐节点
  const panelX = node.position.x + dim.width / 2 - panelW / 2
  const panelY = node.position.y + dim.height + panelGap

  detailPanelNodeId.value = nodeId
  detailPanelNodeData.value = node.data
  detailPanelPosition.value = {
    x: panelX,
    y: panelY
  }

  // 重置拖拽偏移（切换节点时复位）
  nextTick(() => {
    detailPanelRef.value?.resetDragOffset?.()
  })

  // 标记选中态
  markNodeSelected(nodeId)

  // ---- 视口自适应：让所有节点+面板组合都可见 ----
  nextTick(() => {
    if (!flowInstance.value || typeof flowInstance.value.setCenter !== 'function') return

    // 先同步当前视口状态
    const vpNow = flowInstance.value.getViewport?.()
    if (vpNow) currentViewport.value = { x: vpNow.x, y: vpNow.y, zoom: vpNow.zoom }

    // 计算所有节点的总边界
    const allVisible = nodes.value.filter((n) => !isStageGroupNode(n))
    const bounds = allVisible.reduce(
      (acc, n) => {
        const d = getNodeDimensions(n)
        return {
          minX: Math.min(acc.minX, n.position.x),
          minY: Math.min(acc.minY, n.position.y),
          maxX: Math.max(acc.maxX, n.position.x + d.width),
          maxY: Math.max(acc.maxY, n.position.y + d.height)
        }
      },
      { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity }
    )

    // 将面板区域也纳入边界
    bounds.minX = Math.min(bounds.minX, panelX)
    bounds.maxX = Math.max(bounds.maxX, panelX + panelW)
    bounds.maxY = Math.max(bounds.maxY, panelY + panelH)

    const cx = (bounds.minX + bounds.maxX) / 2
    const cy = (bounds.minY + bounds.maxY) / 2

    // 获取当前缩放级别，适度缩小以确保全部可见
    const zoom = Math.min(currentViewport.value.zoom, 0.75)

    flowInstance.value.setCenter(cx, cy, { zoom, duration: 280 })

    // 动画结束后同步视口状态
    setTimeout(() => {
      const vpAfter = flowInstance.value?.getViewport?.()
      if (vpAfter) currentViewport.value = { x: vpAfter.x, y: vpAfter.y, zoom: vpAfter.zoom }
    }, 300)
  })
}

/** 关闭浮动面板 */
const closeDetailPanel = () => {
  collectCurrentPanelConfig()
  detailPanelNodeId.value = null
  detailPanelNodeData.value = null
  clearNodeSelected()

  // 恢复视口：让所有节点居中可见
  nextTick(() => {
    if (!flowInstance.value) return
    const visibleNodes = nodes.value.filter((n) => !isStageGroupNode(n))
    if (!visibleNodes.length) return

    const bounds = visibleNodes.reduce(
      (acc, n) => {
        const d = getNodeDimensions(n)
        return {
          minX: Math.min(acc.minX, n.position.x),
          minY: Math.min(acc.minY, n.position.y),
          maxX: Math.max(acc.maxX, n.position.x + d.width),
          maxY: Math.max(acc.maxY, n.position.y + d.height)
        }
      },
      { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity }
    )

    const cx = (bounds.minX + bounds.maxX) / 2
    const cy = (bounds.minY + bounds.maxY) / 2

    if (typeof flowInstance.value.setCenter === 'function') {
      flowInstance.value.setCenter(cx, cy, { zoom: 1.0, duration: 280 })
    }
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
  setNodes(updated)
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
  setNodes(updated)
}

const handleNodeActive = (payload) => {
  if (isStageGroupNode(payload?.node || payload)) return
  const id =
    payload?.node?.id ??
    payload?.id ??
    (typeof payload === 'string' || typeof payload === 'number' ? payload : '')
  if (!id) return
  openDetailPanel(String(id))
  EventBus.$emit('edgeMenu:open', null)
}

const handleNodeDragStart = (payload) => {
  if (isStageGroupNode(payload?.node || payload)) return
  const id = payload?.node?.id ?? payload?.id ?? ''
  if (!id) return
  markNodeSelected(String(id))
  EventBus.$emit('edgeMenu:open', null)
}

const handlePaneClick = () => {
  closeDetailPanel()
  EventBus.$emit('edgeMenu:open', null)
}

const focusNode = (nodeId) => {
  if (!nodeId) return
  const node = nodes.value.find((n) => n.id === nodeId)
  if (!node) return

  const dim = getNodeDimensions(node)
  const cx = node.position.x + dim.width / 2
  const cy = node.position.y + dim.height / 2

  if (
    flowInstance.value &&
    typeof flowInstance.value.setCenter === 'function'
  ) {
    flowInstance.value.setCenter(cx, cy, { zoom: 0.8, duration: 300 })
  }
}

const centerView = () => {
  if (!nodes.value.length) return

  // 计算所有节点的边界
  const visibleNodes = nodes.value.filter((node) => !isStageGroupNode(node))
  if (!visibleNodes.length) return

  const bounds = visibleNodes.reduce(
    (acc, node) => {
      const x = node.position.x
      const y = node.position.y
      const dimensions = getNodeDimensions(node)
      return {
        minX: Math.min(acc.minX, x),
        minY: Math.min(acc.minY, y),
        maxX: Math.max(acc.maxX, x + dimensions.width),
        maxY: Math.max(acc.maxY, y + dimensions.height)
      }
    },
    { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity }
  )

  const cx = (bounds.minX + bounds.maxX) / 2
  const cy = (bounds.minY + bounds.maxY) / 2

  if (
    flowInstance.value &&
    typeof flowInstance.value.setCenter === 'function'
  ) {
    flowInstance.value.setCenter(cx, cy, { zoom: 1.0, duration: 300 })
  }
}

EventBus.$on('edgeMenu:focus', (nodeId) => {
  requestAnimationFrame(() => {
    markNodeSelected(nodeId)
    // focusNode(nodeId)
  })
})

EventBus.$on('flow:layout:request', () => {
  requestAnimationFrame(() => {
    applyLayout()
  })
})

EventBus.$on('flow:collapseAll', (payload = {}) => {
  closeDetailPanel()
})

EventBus.$on('flow:openDetailPanel', (nodeId) => {
  openDetailPanel(nodeId)
})

EventBus.$on('flow:closeDetailPanel', (deletedNodeId) => {
  // 如果删除的正是当前面板展示的节点，关闭面板（不保存）
  if (detailPanelNodeId.value === deletedNodeId) {
    detailPanelNodeId.value = null
    detailPanelNodeData.value = null
    clearNodeSelected()
  }
})

EventBus.$on('flow:removeNodes', (ids) => {
  if (!Array.isArray(ids) || ids.length === 0) return
  setNodes((ns) => ns.filter((n) => !ids.includes(n.id)))
  nodes.value = nodes.value.filter((n) => !ids.includes(n.id))
  // 同步清理 atomicList 中对应 position
  atomicList.value = (atomicList.value || []).filter(
    (a) => !ids.includes(String(a.position))
  )
  // 将最新 atomicList 透传到节点 data
  nodes.value.forEach((n) => {
    if (n.data) n.data.atomicList = atomicList.value
  })
})

EventBus.$on(
  'flow:addComponentDialog:open',
  ({ edgeId, x, y, mode, sourceId } = {}) => {
    addDialogMode.value = mode || 'insert'
    addDialogSourceId.value = sourceId || ''
    addDialogEdgeId.value = edgeId || ''
    addDialogX.value = Number(x || 0)
    addDialogY.value = Number(y || 0)
    addDialogVisible.value = true
    closeDetailPanel()
  }
)

// 实时同步 atomicList：来自节点表单的更新
EventBus.$on('flow:atomic:update', (payload = {}) => {
  const { position, atomicCode, atomicName, labelList } = payload
  if (!position) return
  const list = atomicList.value || []
  const idx = list.findIndex((a) => String(a.position) === String(position))
  const entry = {
    position: String(position),
    atomicCode: atomicCode || '',
    atomicName: atomicName || '',
    labelList: Array.isArray(labelList) ? labelList : []
  }
  if (idx > -1) {
    list.splice(idx, 1, entry)
  } else {
    list.push(entry)
  }
  atomicList.value = [...list]
  console.log(atomicList.value,'=======atomicList==========')
  // 将最新 atomicList 透传到节点 data，供后续节点依赖
  nodes.value.forEach((n) => {
    if (n.data) n.data.atomicList = atomicList.value
  })
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
      stroke: '#f97316',
      color: '#9a3412'
    }
  }
  // 模型推理 (AA/DA/PA/PDA 前缀 + BA_00009混合目标关联)
  if (/^(AA|DA|PA|PDA)_/.test(actionId) || actionId === 'BA_00009') {
    return {
      key: 'detect',
      title: t('glossary.modelInference'),
      titleKey: 'glossary.modelInference',
      fill: 'transparent',
      stroke: '#3b82f6',
      color: '#1d4ed8'
    }
  }
  // 告警输出 (事件上报/抓拍/特征上报)
  if (['BA_00004', 'BA_00006', 'BA_10004'].includes(actionId)) {
    return {
      key: 'output',
      title: t('glossary.alertOutput'),
      titleKey: 'glossary.alertOutput',
      fill: 'transparent',
      stroke: '#22c55e',
      color: '#15803d'
    }
  }
  // 规则判断 (默认 — 筛选/灵敏度/判断类)
  return {
    key: 'rule',
    title: t('glossary.ruleJudgment'),
    titleKey: 'glossary.ruleJudgment',
    fill: 'transparent',
    stroke: '#f97316',
    color: '#ea580c'
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
  const edgesArr = 'value' in getEdges ? getEdges.value : getEdges
  const edge = Array.isArray(edgesArr)
    ? edgesArr.find((e) => e.id === edgeId)
    : undefined
  return edge
    ? { source: edge.source, target: edge.target }
    : { source: undefined, target: undefined }
}

const addComponentFromDialog = (type, label, action) => {
  const edgeId = addDialogEdgeId.value
  const mode = addDialogMode.value
  const { source, target } =
    mode === 'insert'
      ? getEdgeEndpoints(edgeId)
      : { source: addDialogSourceId.value, target: undefined }
  if (mode === 'insert' && !edgeId) return

  const newNodeId = generateActionId()
  let flowItem = Array.isArray(newFlowData.value)
    ? newFlowData.value.find(
        (f) => f.actionId === (action?.id ?? action?.actionId)
      )
    : undefined
  if (!flowItem) {
    const preId =
      mode === 'insert' ? source || '-1' : addDialogSourceId.value || '-1'
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
      atomicList: atomicList.value || [],
      flowData: flowItem || {},
      actionDetail: {
        ...(action || {}),
        actionId: action?.id ?? action?.actionId ?? '',
        flowActionId: String(newNodeId),
        inputParamConfig: flowItem?.inputParamConfig ?? action?.inputParamConfig
      }
    }
  }
  nodes.value = [...nodes.value, newNode]

  if (mode === 'insert') {
    // 找到当前被点击的边，按源→新节点→目标的方式重连
    const currentEdge = edges.value.find((e) => e.id === edgeId)
    const rest = edges.value.filter((e) => e.id !== edgeId)
    const nextEdges = [...rest]
    const edgeType = currentEdge?.type || 'action'

    if (source) {
      nextEdges.push({
        id: generateActionId(),
        type: edgeType,
        source,
        target: newNodeId
      })
    }
    // 如果存在目标，再连 新节点→目标
    if (target ?? currentEdge?.target) {
      nextEdges.push({
        id: generateActionId(),
        type: edgeType,
        source: newNodeId,
        target: target ?? currentEdge?.target
      })
    }
    edges.value = nextEdges
  } else {
    // 分支模式：从 sourceId 发出一条新分支到新节点，并默认再添加一个结束节点
    const newEndId = generateActionId()
    const endNode = {
      id: newEndId,
      type: 'end',
      position: { x: addDialogX.value + 300, y: addDialogY.value + 40 },
      data: {}
    }
    nodes.value = [...nodes.value, endNode]
    const nextEdges = [...edges.value]
    if (source) {
      nextEdges.push({
        id: generateActionId(),
        type: 'action',
        source,
        target: newNodeId
      })
    }
    nextEdges.push({
      id: generateActionId(),
      type: 'action',
      source: newNodeId,
      target: newEndId
    })
    edges.value = nextEdges
  }

  addDialogVisible.value = false

  nextTick(() => {
    EventBus.$emit('edgeMenu:focus', newNodeId)
  })
}

const rebuildFlowGraph = () => {
  const items = Array.isArray(newFlowData.value) ? newFlowData.value : []
  columnCenterX.value = {}
  lockedCenterY.value = {}

  // 基础开始节点
  const baseNodesStart = [
    {
      id: '-1',
      type: 'start',
      position: { x: -220, y: 80 },
      data: {}
    }
  ]

  // 如果没有数据，只渲染开始和结束节点
  if (items.length === 0) {
    const endNodeId = `end-${Date.now()}`
    const emptyNodes = [
      ...baseNodesStart,
      {
        id: endNodeId,
        type: 'end',
        position: { x: 320, y: 80 },
        data: {}
      }
    ]

    const emptyEdges = [
      {
        id: 'e-start-end',
        type: 'action',
        source: '-1',
        target: endNodeId
      }
    ]

    nodes.value = emptyNodes
    edges.value = emptyEdges
    setNodes(emptyNodes)
    setEdges(emptyEdges)

    // 应用布局并居中显示
    requestAnimationFrame(() => {
      applyLayout()
    })

    return
  }

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
        atomicList: atomicList.value || [],
        flowData: item,
        actionDetail: {
          actionId: item.actionId,
          actionName: canonicalName,
          remark: item.remark,
          flowActionId: item.flowActionId,
          configObject: item.configObject || {},
          inputParamConfig: newInputParamConfig
        }
      }
    }
  })
  const nextNodes = [...baseNodesStart, ...dataNodes]
  const edgesArr = []
  items.forEach((item) => {
    const src =
      item.preFlowActionId === '-1' || item.preFlowActionId === -1
        ? '-1'
        : String(item.preFlowActionId)
    const tgt = String(item.flowActionId)
    edgesArr.push({
      id: generateActionId(),
      type: 'action',
      source: src,
      target: tgt
    })
  })
  const hasOutgoing = new Set(
    items.map((i) => String(i.preFlowActionId)).filter((v) => v && v !== '-1')
  )
  const allIds = new Set(items.map((i) => String(i.flowActionId)))
  const terminalIds = Array.from(allIds).filter((id) => !hasOutgoing.has(id))
  if (terminalIds.length > 1) {
    // 多分支：为每个末尾节点拼接一个独立结束节点
    terminalIds.forEach((id) => {
      const endId = `end-${id}`
      nextNodes.push({
        id: endId,
        type: 'end',
        position: { x: 960, y: 120 },
        data: {}
      })
      edgesArr.push({
        id: generateActionId(),
        type: 'action',
        source: id,
        target: endId
      })
    })
  } else if (terminalIds.length === 1) {
    // 单分支：创建结束节点
    const endNodeId = `end-${Date.now()}`
    nextNodes.push({
      id: endNodeId,
      type: 'end',
      position: { x: 960, y: 120 },
      data: {}
    })
    edgesArr.push({
      id: generateActionId(),
      type: 'action',
      source: terminalIds[0],
      target: endNodeId
    })
  }
  nodes.value = nextNodes
  edges.value = edgesArr
  setNodes(nextNodes)
  setEdges(edgesArr)

  // 渲染完成后居中显示
  requestAnimationFrame(() => {
    columnCenterX.value = {}
    applyLayout()
  })
}

const saveMetaDataParams = () => {
  const list = []
  nodes.value
    .filter((n) => n.type !== 'start' && n.type !== 'end' && !isStageGroupNode(n))
    .forEach((n) => {
      const meta =
        n.data?.flowData?.configObject?.webConfig?.metaDataParams ?? []
      if (Array.isArray(meta)) {
        meta.forEach((m) => list.push({ ...m }))
      }
    })
  return list
}

const saveFlowData = () => {
  // 先收集当前浮动面板的配置（如果有打开的面板）
  collectCurrentPanelConfig()
  const incomingMap = new Map()
  edges.value.forEach((e) => {
    if (e.target) incomingMap.set(e.target, e.source)
  })

  const items = []
  const atomicCollected = []

  nodes.value
    .filter((n) => n.type !== 'start' && n.type !== 'end' && !isStageGroupNode(n))
    .forEach((n) => {
      const actionId = n.data?.actionId || ''
      // 保存时同样用 actionList 标准名称归一化，修正历史脏数据
      const actionMeta = (Array.isArray(props.actionList) ? props.actionList : []).find(a => a.id === actionId)
      const actionName = actionMeta?.actionName || n.data?.actionName || ''
      const remark = n.data?.description || ''
      const flowActionId = String(n.id)
      let preFlowActionId = incomingMap.get(n.id)
      if (
        !preFlowActionId ||
        preFlowActionId === 'start' ||
        preFlowActionId === '-1'
      ) {
        preFlowActionId = '-1'
      } else {
        preFlowActionId = String(preFlowActionId)
      }

      const configObject = n.data?.flowData?.configObject ?? {
        webConfig: {
          labelList: [],
          labelFilterList: [],
          metaDataParams: [],
          atomic: {}
        },
        params: []
      }

      const atomic = configObject?.webConfig?.atomic
      if (atomic && (atomic.position || atomic.labelList)) {
        // Spread original atomic first to preserve sidecar fields (*I18nKey),
        // then override with current values so edits take precedence.
        atomicCollected.push({
          ...atomic,
          position: atomic.position || flowActionId,
          atomicCode: atomic.atomicCode || '',
          atomicName: atomic.atomicName || '',
          labelList: atomic.labelList || []
        })
      }

      // Spread original flow item first to preserve sidecar fields (*I18nKey),
      // then override with current values so edits take precedence.
      const originalFlowItem = n.data?.flowData || {}
      items.push({
        ...originalFlowItem,
        actionId,
        actionName,
        remark,
        flowActionId,
        preFlowActionId,
        configObject
      })
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
  setNodes([])
  setEdges([])
  nodes.value = []
  edges.value = []
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
  background-color: #f5f7fa;
}

.page-header {
  padding: 12px 20px;
  border-bottom: 1px solid #e4e7ed;
  background: #ffffff;
}

.page-header h1 {
  margin: 0;
  font-size: 18px;
}

.page-header p {
  margin: 4px 0 0;
  font-size: 13px;
  color: #909399;
}

.page-main {
  flex: none;
  min-height: 0;
  overflow: hidden;
  position: relative;
}

.page-main :deep(.vue-flow) {
  width: 100%;
  height: 100%;
  overflow: hidden;
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
  background: linear-gradient(
    135deg,
    var(--bg-primary) 0%,
    var(--bg-white) 100%
  );
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
  background: linear-gradient(
    135deg,
    var(--primary-color) 0%,
    var(--primary-light) 100%
  );
}

:deep(.el-dialog__title) {
  color: #ffffff;
  font-weight: 600;
}

:deep(.el-dialog__headerbtn .el-dialog__close) {
  color: #ffffff;
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
  width: 8px;
  height: 8px;
  background: #b1b1b7;
  border: 2px solid #fff;
  border-radius: 50%;
}

.vue-flow__handle:hover {
  background: #3182ce;
}

/* 连线基本样式 */
.vue-flow__edge-path {
  stroke: #b1b1b7;
  stroke-width: 1.5;
}

.vue-flow__edge.selected .vue-flow__edge-path,
.vue-flow__edge:hover .vue-flow__edge-path {
  stroke: #3182ce;
}
</style>
