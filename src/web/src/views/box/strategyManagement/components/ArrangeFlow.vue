<template>
  <div class="page">
    <main class="page-main" :style="{ width: `${width}px`, height: `${height}px` }">
      <VueFlow v-model:nodes="nodes" v-model:edges="edges" :node-types="nodeTypes" :edge-types="edgeTypes">
        <Background pattern-color="#e5e7eb" gap="16" />
        <Controls :show-interactive="false" />
        <!-- <MiniMap /> -->
      </VueFlow>
    </main>

    <teleport to="body">
      <el-dialog v-model="addDialogVisible" title="添加组件" width="710px" :close-on-click-modal="true" center :z-index="6000">
        <div class="component-dialog">
          <ActionView :actionList="actionList" @onAction="addComponentFromAction" />
        </div>
      </el-dialog>
    </teleport>
  </div>
</template>


<script setup>
import { ref, markRaw, watch, nextTick } from 'vue'
import { VueFlow, useVueFlow } from '@vue-flow/core'
import { Background } from '@vue-flow/background'
import { Controls } from '@vue-flow/controls'
import { MiniMap } from '@vue-flow/minimap'
import dagre from 'dagre'
import _ from 'lodash'

import '@vue-flow/core/dist/style.css'
import '@vue-flow/minimap/dist/style.css'
import '@vue-flow/controls/dist/style.css'

import EventBus from '@/components/eventBus.js'
import { generateActionId } from '@/views/gam/countManagement/arrangeDetail/flow/dataTools.js'
import ActionView from '@/views/gam/countManagement/arrangeDetail/flow/ActionView.vue'
import CustomFormNode from '@/views/gam/countManagement/arrangeDetail/flow/CustomFormNode.vue'
import StartNode from '@/views/gam/countManagement/arrangeDetail/flow/StartNode.vue'
import EndNode from '@/views/gam/countManagement/arrangeDetail/flow/EndNode.vue'
import ActionEdge from '@/views/gam/countManagement/arrangeDetail/flow/ActionEdge.vue'

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
  workFlow: {
    type: String,
    default: '[]'
  },
})

const nodes = ref([])
const edges = ref([])

const nodeTypes = {
  start: markRaw(StartNode),
  customForm: markRaw(CustomFormNode),
  end: markRaw(EndNode)
}

const edgeTypes = {
  action: markRaw(ActionEdge)
}

const { setNodes, setEdges, addNodes, onPaneReady, getEdges } = useVueFlow()
const flowInstance = ref(null)
onPaneReady((instance) => {
  flowInstance.value = instance
  // 设置默认视口，不使用自动适应
  instance.setViewport({ x: 100, y: 200, zoom: 0.6 })
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
  if (node.type === 'detection') {
    return { width: 550, height: 450 }
  }
  if (node.type === 'customForm') {
    return { width: 550, height: 450 }
  }
  return { width: nodeWidth, height: nodeHeight }
}

const applyLayout = () => {
  const dagreGraph = new dagre.graphlib.Graph()
  dagreGraph.setDefaultEdgeLabel(() => ({}))

  dagreGraph.setGraph({
    rankdir: layoutDirection,
    nodesep: 150,
    ranksep: 150
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

  setNodes(positioned)

  // 布局完成后居中显示
  // requestAnimationFrame(() => {
  //   centerView()
  // })
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
  const bounds = nodes.value.reduce(
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
    flowInstance.value.setCenter(cx, cy, { zoom: 0.8, duration: 300 })
  }
}

EventBus.$on('edgeMenu:focus', (nodeId) => {
  // requestAnimationFrame(() => {
  //   focusNode(nodeId)
  // })
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
      label: label || '新节点',
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
        atomicList: atomicList.value || [],
        flowData: item,
        actionDetail: {
          actionId: item.actionId,
          actionName: item.actionName,
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
  setNodes(nextNodes)
  setEdges(edgesArr)

  // 渲染完成后居中显示
  requestAnimationFrame(() => {
    applyLayout()
  })
}

const saveMetaDataParams = () => {
  const list = []
  nodes.value
    .filter((n) => n.type !== 'start' && n.type !== 'end')
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
  EventBus.$emit('flow:collectConfigs')
  const incomingMap = new Map()
  edges.value.forEach((e) => {
    if (e.target) incomingMap.set(e.target, e.source)
  })

  const items = []
  const atomicCollected = []

  nodes.value
    .filter((n) => n.type !== 'start' && n.type !== 'end')
    .forEach((n) => {
      const actionId = n.data?.actionId || ''
      const actionName = n.data?.actionName || ''
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

  return {
    workFlow: JSON.stringify(items),
    atomicList: JSON.stringify(atomicCollected)
  }
}

// Expose functions to parent component
const clearFlow = () => {
  setNodes([])
  setEdges([])
  nodes.value = []
  edges.value = []
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
  () => props.workFlow,
  (newVal) => {
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
