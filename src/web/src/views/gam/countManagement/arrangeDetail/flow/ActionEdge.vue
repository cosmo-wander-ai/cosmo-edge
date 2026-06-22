<template>
  <BaseEdge :id="id" :style="style" :path="path" :marker-end="markerEnd" />
  <EdgeLabelRenderer>
    <div class="edge-action-wrapper" :style="{
        transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)`,
      }">
      <button type="button" class="edge-action-button" @click.stop="handleClick">
        +
      </button>
      <div v-if="menuVisible" class="edge-menu">
        <span class="menu-item" @click.stop="openAddDialog">{{ t('action.addComponent') }}</span>
        <span class="divider"></span>
        <span class="menu-item" :class="{ 'is-disabled': isToEnd }" @click.stop="!isToEnd && deleteFollowing()">
          {{ t('action.deleteFollowingFlow') }}
        </span>
      </div>
    </div>
  </EdgeLabelRenderer>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import {
  BaseEdge,
  EdgeLabelRenderer,
  getBezierPath,
  useVueFlow
} from '@vue-flow/core'
import EventBus from '@/components/eventBus.js'
import { t } from '@/i18n'

const props = defineProps({
  id: {
    type: String,
    required: true
  },
  sourceX: {
    type: Number,
    required: true
  },
  sourceY: {
    type: Number,
    required: true
  },
  targetX: {
    type: Number,
    required: true
  },
  targetY: {
    type: Number,
    required: true
  },
  sourcePosition: {
    type: String,
    required: true
  },
  targetPosition: {
    type: String,
    required: true
  },
  style: Object,
  markerEnd: [String, Object]
})

const { addNodes, setEdges, getEdges, getNodes } = useVueFlow()

const edgePath = computed(() => getBezierPath(props))

const path = computed(() => edgePath.value[0])
const labelX = computed(() => edgePath.value[1])
const labelY = computed(() => edgePath.value[2])

const menuVisible = ref(false)
const isToEnd = computed(() => {
  const edges = getEdges && 'value' in getEdges ? getEdges.value : getEdges
  const nodes = getNodes && 'value' in getNodes ? getNodes.value : getNodes
  const current = edges.find((e) => e.id === props.id)
  if (!current) return false
  const targetId = current.target
  const targetNode = Array.isArray(nodes)
    ? nodes.find((n) => n.id === targetId)
    : undefined
  return targetNode?.type === 'end'
})

const handleClick = () => {
  const nodes = getNodes && 'value' in getNodes ? getNodes.value : getNodes
  const hasExpanded =
    Array.isArray(nodes) && nodes.some((n) => !!(n.data && n.data.expanded))
  if (hasExpanded) {
    EventBus.$emit('flow:collapseAll', { keepMenu: true })
  }
  menuVisible.value = !menuVisible.value
  if (menuVisible.value) {
    EventBus.$emit('edgeMenu:open', props.id)
  }
}

const openAddDialog = () => {
  menuVisible.value = false
  EventBus.$emit('flow:addComponentDialog:open', {
    edgeId: props.id,
    x: labelX.value,
    y: labelY.value
  })
}

const closeMenu = (e) => {
  if (!e) {
    menuVisible.value = false
    return
  }
  const target = e.target
  if (!(target && target.closest('.edge-action-wrapper'))) {
    menuVisible.value = false
  }
}

onMounted(() => {
  document.addEventListener('click', closeMenu)
  EventBus.$on('edgeMenu:open', (edgeId) => {
    if (edgeId !== props.id) {
      menuVisible.value = false
    }
  })
})

onBeforeUnmount(() => {
  document.removeEventListener('click', closeMenu)
  EventBus.$off('edgeMenu:open')
})

// 添加分支：从当前边的 source 增加一个并行分支到新节点
const addBranch = () => {
  const edges = getEdges && 'value' in getEdges ? getEdges.value : getEdges
  const current = edges.find((e) => e.id === props.id)
  if (!current) return
  menuVisible.value = false
  // 打开"添加组件"弹窗，进入 branch 模式，由主画布在选择组件后创建新节点和一个结束节点
  EventBus.$emit('flow:addComponentDialog:open', {
    mode: 'branch',
    sourceId: current.source,
    x: labelX.value,
    y: labelY.value
  })
}

// 删除以下流程：删除从当前边 target 开始的所有后续节点与边
const deleteFollowing = () => {
  const edges = getEdges && 'value' in getEdges ? getEdges.value : getEdges
  const nodes = getNodes && 'value' in getNodes ? getNodes.value : getNodes
  const current = edges.find((e) => e.id === props.id)
  if (!current) return

  const start = current.target
  const toRemove = new Set()
  const queue = [start]
  const endNodesToCheck = new Set() // 记录可能需要删除的结束节点

  // 收集所有需要删除的节点（从目标节点开始的所有后续节点）
  while (queue.length) {
    const nid = queue.shift()
    if (toRemove.has(nid)) continue

    const node = Array.isArray(nodes) ? nodes.find((n) => n.id === nid) : null

    // 如果是结束节点，先记录下来，稍后判断是否需要删除
    if (node && node.type === 'end') {
      endNodesToCheck.add(nid)
      continue
    }

    toRemove.add(nid)

    // 找到所有从当前节点出发的边
    edges
      .filter((e) => e.source === nid)
      .forEach((e) => {
        queue.push(e.target)
      })
  }

  // 过滤掉将被删除的节点相关的边，以及当前这条边
  let filteredEdges = edges.filter(
    (e) =>
      e.id !== props.id && !toRemove.has(e.source) && !toRemove.has(e.target)
  )

  // 找到所有需要重新连接到结束节点的源节点
  const sourcesToReconnect = new Set()

  // 检查当前源节点是否还有其他出边
  const currentSourceHasOtherOutgoing = filteredEdges.some(
    (e) => e.source === current.source
  )
  if (!currentSourceHasOtherOutgoing) {
    sourcesToReconnect.add(current.source)
  }

  // 检查是否有其他节点因为删除操作而失去了出边
  const nodeArr = Array.isArray(nodes) ? nodes : []
  nodeArr.forEach((node) => {
    if (toRemove.has(node.id) || node.type === 'end' || node.type === 'start')
      return

    // 检查这个节点是否还有出边
    const hasOutgoing = filteredEdges.some((e) => e.source === node.id)
    if (!hasOutgoing) {
      // 检查这个节点是否原本有出边指向被删除的节点
      const hadOutgoingToDeleted = edges.some(
        (e) => e.source === node.id && toRemove.has(e.target)
      )
      if (hadOutgoingToDeleted) {
        sourcesToReconnect.add(node.id)
      }
    }
  })

  // 检查结束节点是否需要删除
  endNodesToCheck.forEach((endNodeId) => {
    // 检查这个结束节点是否还有入边（除了被删除的边）
    const hasIncomingEdges = filteredEdges.some((e) => e.target === endNodeId)

    // 如果没有入边，说明这个分支已经完全被删除，结束节点也应该删除
    if (!hasIncomingEdges) {
      toRemove.add(endNodeId)
      // 同时删除指向这个结束节点的边
      filteredEdges = filteredEdges.filter((e) => e.target !== endNodeId)
    }
  })

  // 为每个需要重新连接的源节点创建到结束节点的连接
  sourcesToReconnect.forEach((sourceId) => {
    // 检查是否是分支情况（原本有多个出边）
    const originalOutgoingCount = edges.filter(
      (e) => e.source === sourceId
    ).length
    const isBranch = originalOutgoingCount > 1

    if (isBranch) {
      // 分支情况：创建独立的结束节点
      const newEndId = `end-${sourceId}-${Date.now()}`
      addNodes([
        {
          id: newEndId,
          type: 'end',
          position: { x: labelX.value + 220, y: labelY.value },
          data: {}
        }
      ])
      filteredEdges.push({
        id: `${sourceId}-to-${newEndId}`,
        type: current.type || 'action',
        source: sourceId,
        target: newEndId
      })
    } else {
      // 非分支情况：查找现有的结束节点或创建新的
      // 查找是否有现有的结束节点可以重用（排除被删除的结束节点）
      const existingEnd = nodeArr.find(
        (n) =>
          n.type === 'end' && !toRemove.has(n.id) && !endNodesToCheck.has(n.id)
      )

      let targetEndId
      if (existingEnd) {
        targetEndId = existingEnd.id
      } else {
        // 如果没有可用的结束节点，创建一个
        const newEndId = `end-${Date.now()}`
        addNodes([
          {
            id: newEndId,
            type: 'end',
            position: { x: labelX.value + 220, y: labelY.value },
            data: {}
          }
        ])
        targetEndId = newEndId
      }

      filteredEdges.push({
        id: `${sourceId}-to-${targetEndId}`,
        type: current.type || 'action',
        source: sourceId,
        target: targetEndId
      })
    }
  })

  // 应用更改
  setEdges(filteredEdges)

  // 发送删除节点事件（包括孤立的结束节点）
  if (toRemove.size > 0) {
    EventBus.$emit('flow:removeNodes', Array.from(toRemove))
  }

  // 聚焦到源节点
  EventBus.$emit('edgeMenu:focus', current.source)

  menuVisible.value = false
}
</script>

<script>
export default {
  inheritAttrs: false
}
</script>

<style scoped>
.edge-action-wrapper {
  position: absolute;
  pointer-events: all;
  z-index: 3000;
}

.edge-action-button {
  width: 24px;
  height: 24px;
  padding: 0;
  font-size: 16px;
  font-weight: 500;
  line-height: 22px;
  border-radius: 50%;
  border: 1.5px solid #d1d5db;
  background-color: #ffffff;
  color: #9ca3af;
  cursor: pointer;
  white-space: nowrap;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}
.edge-action-button.is-disabled {
  border-color: #cfd4dc;
  background-color: #e5e7eb;
  color: #9ca3af;
  cursor: not-allowed;
}

.edge-menu .menu-item.is-disabled {
  color: #9ca3af;
  cursor: not-allowed;
  pointer-events: none;
}

.edge-action-button:hover {
  border-color: #3182ce;
  color: #3182ce;
  background-color: #ebf8ff;
  box-shadow: 0 2px 8px rgba(49, 130, 206, 0.2);
}

.edge-menu {
  position: absolute;
  top: 48px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 10px 16px;
  border-radius: 18px;
  color: #fff;
  background: linear-gradient(90deg, #5fc8df 0%, #3182ce 100%);
  box-shadow: 0 6px 18px rgba(49, 130, 206, 0.25);
  z-index: 4000;
}

.edge-menu .menu-item {
  display: inline-block;
  cursor: pointer;
  user-select: none;
  font-size: 13px;
  writing-mode: horizontal-tb;
  white-space: nowrap;
}

.edge-menu .divider {
  width: 1px;
  height: 16px;
  background: rgba(255, 255, 255, 0.6);
}
</style>
