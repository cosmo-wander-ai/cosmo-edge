<template>
  <div class="action-node" :class="{ selected: props.data?.selected }">
    <button type="button" class="node-delete" @click.stop="handleDelete">×</button>
    <Handle type="target" :position="Position.Left" />

    <div class="node-card" @click.stop="handleClick">
      <div class="node-icon-wrapper" :class="iconColorClass">
        <!-- 视频解码 -->
        <svg v-if="iconKey === 'video'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <rect x="3" y="4" width="18" height="13" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <polygon points="10,7.5 10,13.5 15,10.5" fill="currentColor"/>
          <line x1="8" y1="20" x2="16" y2="20" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          <line x1="12" y1="17" x2="12" y2="20" stroke="currentColor" stroke-width="1.5"/>
        </svg>
        <!-- 目标检测 -->
        <svg v-else-if="iconKey === 'target'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <circle cx="12" cy="12" r="7.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <circle cx="12" cy="12" r="2.5" fill="currentColor"/>
          <line x1="12" y1="2" x2="12" y2="6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          <line x1="12" y1="18" x2="12" y2="22" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          <line x1="2" y1="12" x2="6" y2="12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          <line x1="18" y1="12" x2="22" y2="12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
        </svg>
        <!-- 追踪 -->
        <svg v-else-if="iconKey === 'tracking'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <circle cx="5" cy="18" r="2" fill="currentColor"/>
          <circle cx="19" cy="6" r="2" fill="currentColor"/>
          <path d="M7 17C8 13 10 10 13 9c2-.7 4-.5 5-1" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          <polyline points="16,3 19,6 16,9" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
        <!-- 大模型/AI -->
        <svg v-else-if="iconKey === 'ai'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <circle cx="12" cy="5" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <circle cx="5" cy="19" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <circle cx="19" cy="19" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <line x1="12" y1="7.5" x2="6.5" y2="16.5" stroke="currentColor" stroke-width="1.5"/>
          <line x1="12" y1="7.5" x2="17.5" y2="16.5" stroke="currentColor" stroke-width="1.5"/>
          <line x1="7.5" y1="19" x2="16.5" y2="19" stroke="currentColor" stroke-width="1.5"/>
        </svg>
        <!-- 类别过滤/目标判断 -->
        <svg v-else-if="iconKey === 'classify'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <rect x="3.5" y="8" width="6.5" height="6.5" rx="1" transform="rotate(45 6.75 11.25)" fill="currentColor"/>
          <rect x="14" y="8" width="6.5" height="6.5" rx="1" transform="rotate(45 17.25 11.25)" fill="currentColor"/>
        </svg>
        <!-- 目标分类 — 网格 -->
        <svg v-else-if="iconKey === 'grid'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <rect x="3" y="3" width="7" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <rect x="14" y="3" width="7" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <rect x="3" y="14" width="7" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <rect x="14" y="14" width="7" height="7" rx="1.5" fill="currentColor" stroke="currentColor" stroke-width="1.5"/>
        </svg>
        <!-- 类别过滤 — 漏斗 -->
        <svg v-else-if="iconKey === 'filter'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <polygon points="2,4 22,4 14,14 14,21 10,21 10,14" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
        </svg>
        <!-- 目标判断 — 盾牌勾选 -->
        <svg v-else-if="iconKey === 'judge'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <path d="M12 2L3 7v5c0 5.25 3.83 10.17 9 11.38C17.17 22.17 21 17.25 21 12V7l-9-5z" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
          <polyline points="9,12 11,14 15,10" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
        <!-- 计时 -->
        <svg v-else-if="iconKey === 'timer'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <circle cx="12" cy="13" r="8" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <polyline points="12,9 12,13 15,15" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
          <line x1="12" y1="2" x2="12" y2="5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          <line x1="9" y1="2" x2="15" y2="2" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
        </svg>
        <!-- 事件上报 -->
        <svg v-else-if="iconKey === 'send'" class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <path d="M22 2L11 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
          <path d="M22 2L15 22L11 13L2 9L22 2Z" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
        </svg>
        <!-- 默认-齿轮 -->
        <svg v-else class="node-icon-svg" style="width:30px;height:30px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="1.5"/>
          <path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 11-2.83 2.83l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 11-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 11-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 110-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 112.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 114 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 112.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 110 4h-.09a1.65 1.65 0 00-1.51 1z" fill="none" stroke="currentColor" stroke-width="1.5"/>
        </svg>
      </div>
      <div class="node-name">{{ resolveResourceActionName(nodeActionDetail) }}</div>
    </div>

    <Handle type="source" :position="Position.Right" />
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { Handle, Position, useVueFlow } from '@vue-flow/core'
import { ElMessageBox } from 'element-plus'
import EventBus from '@/components/eventBus.js'
import { getIconInfo } from './iconMapping.js'
import { t } from '@/i18n'
import { resolveResourceActionName } from '@/utils/i18nResource'

const props = defineProps({
  id: {
    type: String,
    required: true
  },
  data: {
    type: Object,
    default: () => ({})
  }
})

const nodeActionDetail = computed(() => props.data?.actionDetail)
const iconKey = computed(() => getIconInfo(nodeActionDetail.value?.actionId).key)
const iconColorClass = computed(() => getIconInfo(nodeActionDetail.value?.actionId).color)

const { getEdges, setEdges, getNodes } = useVueFlow()

// 点击卡片：通知 ArrangeFlow 打开浮动面板
const handleClick = () => {
  EventBus.$emit('flow:openDetailPanel', props.id)
}

const handleDelete = () => {
  ElMessageBox.confirm(t('validate.confirmDeleteNode'), t('common.notice'), {
    confirmButtonText: t('action.confirm'),
    cancelButtonText: t('action.cancel'),
    type: 'warning'
  }).then(() => {
    const edgesArr = getEdges && 'value' in getEdges ? getEdges.value : getEdges
    const nodesArr = getNodes && 'value' in getNodes ? getNodes.value : getNodes
    const incoming = edgesArr.filter((e) => e.target === props.id)
    const outgoing = edgesArr.filter((e) => e.source === props.id)

    setEdges((es) => {
      let next = es.filter((e) => e.source !== props.id && e.target !== props.id)

      // 建立前后端点的直连，保持流程连通
      const exists = (s, t) => next.some((e) => e.source === s && e.target === t)

      incoming.forEach((inE) => {
        outgoing.forEach((outE) => {
          const s = inE.source
          const t = outE.target

          if (s && t && s !== t && !exists(s, t)) {
            next = next.concat({
              id: `reconn-${Date.now()}-${s}-${t}`,
              type: inE.type || outE.type || 'action',
              source: s,
              target: t
            })
          }
        })
      })

      return next
    })

    // 检查是否需要删除孤立的结束节点（只在分支情况下）
    setTimeout(() => {
      // 重新获取当前状态
      const currentEdges =
        getEdges && 'value' in getEdges ? getEdges.value : getEdges
      const currentNodes =
        getNodes && 'value' in getNodes ? getNodes.value : getNodes

      const nodesToRemove = [props.id]

      // 找到所有结束节点
      const endNodes = Array.isArray(currentNodes)
        ? currentNodes.filter((n) => n.type === 'end')
        : []

      // 检查每个结束节点是否需要删除
      endNodes.forEach((endNode) => {
        // 找到指向这个结束节点的所有边
        const incomingToEnd = Array.isArray(currentEdges)
          ? currentEdges.filter((e) => e.target === endNode.id)
          : []

        // 如果没有入边，说明这个结束节点孤立了，需要删除
        if (incomingToEnd.length === 0) {
          nodesToRemove.push(endNode.id)
          return
        }

        // 检查这个结束节点是否是分支的结束节点
        const sourceIds = incomingToEnd.map((e) => e.source)
        if (sourceIds.length !== 1) return

        const directSourceId = sourceIds[0]
        const directSourceNode = Array.isArray(currentNodes)
          ? currentNodes.find((n) => n.id === directSourceId)
          : null

        if (!directSourceNode) return

        // 向上追溯，找到分支点
        let branchPointId = null
        let currentId = directSourceId
        const visited = new Set()

        while (currentId && !visited.has(currentId)) {
          visited.add(currentId)
          const outgoingEdges = Array.isArray(currentEdges)
            ? currentEdges.filter((e) => e.source === currentId)
            : []

          if (outgoingEdges.length > 1) {
            branchPointId = currentId
            break
          }

          const incomingEdges = Array.isArray(currentEdges)
            ? currentEdges.filter((e) => e.target === currentId)
            : []

          if (incomingEdges.length === 1) {
            currentId = incomingEdges[0].source
          } else {
            break
          }
        }

        if (branchPointId) {
          const pathNodes = new Set()
          const queue = [directSourceId]
          const pathVisited = new Set()

          while (queue.length > 0) {
            const nodeId = queue.shift()
            if (pathVisited.has(nodeId)) continue
            pathVisited.add(nodeId)
            if (nodeId === branchPointId) continue

            const node = Array.isArray(currentNodes)
              ? currentNodes.find((n) => n.id === nodeId)
              : null

            if (node && node.type !== 'start' && node.type !== 'end') {
              pathNodes.add(nodeId)
            }

            const incomingToNode = Array.isArray(currentEdges)
              ? currentEdges.filter((e) => e.target === nodeId)
              : []

            incomingToNode.forEach((edge) => {
              if (edge.source !== branchPointId) {
                queue.push(edge.source)
              }
            })
          }

          if (pathNodes.size === 0) {
            nodesToRemove.push(endNode.id)
          }
        }
      })

      // 通知关闭面板（如果删除的是当前打开的节点）
      EventBus.$emit('flow:closeDetailPanel', props.id)
      EventBus.$emit('flow:removeNodes', [...new Set(nodesToRemove)])
    }, 0)
  }).catch(() => {
    // 用户取消删除
  })
}
</script>

<style scoped>
.action-node {
  background: #ffffff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  width: 96px;
  min-height: 96px;
  box-sizing: border-box;
  box-shadow: none;
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  transition: border-color 0.2s, box-shadow 0.2s;
}

.action-node:hover {
  border-color: #90cdf4;
}

/* 选中态：indigo 发光边框 */
.action-node.selected {
  border-color: #3182ce;
  box-shadow: 0 0 0 3px rgba(49, 130, 206, 0.2);
}

.node-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 14px 6px 10px;
  cursor: pointer;
  width: 100%;
  box-sizing: border-box;
  gap: 6px;
}

.node-icon-wrapper {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.node-icon-wrapper.icon-blue   { background: #eff6ff; color: #3b82f6; }
.node-icon-wrapper.icon-purple { background: #ebf8ff; color: #2b6cb0; }
.node-icon-wrapper.icon-orange { background: #fff7ed; color: #ea580c; }
.node-icon-wrapper.icon-green  { background: #f0fdf4; color: #16a34a; }
.node-icon-wrapper.icon-gray   { background: #f3f4f6; color: #6b7280; }

.node-icon-svg {
  width: 30px;
  height: 30px;
}

.node-name {
  font-size: 10px;
  font-weight: 500;
  color: #374151;
  text-align: center;
  line-height: 1.25;
  word-break: normal;
  overflow-wrap: break-word;
  max-width: 88px;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}

.node-delete {
  position: absolute;
  top: -6px;
  right: -6px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  border: 1px solid #f56c6c;
  background-color: #ffffff;
  color: #f56c6c;
  font-size: 12px;
  line-height: 1;
  padding: 0;
  cursor: pointer;
  z-index: 10;
}

.node-delete:hover {
  background-color: #fef0f0;
}
</style>
