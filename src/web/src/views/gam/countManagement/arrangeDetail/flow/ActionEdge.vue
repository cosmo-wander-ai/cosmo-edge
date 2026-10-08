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
import { computed, inject, onMounted, onBeforeUnmount } from 'vue'
import {
  BaseEdge,
  EdgeLabelRenderer,
  getBezierPath,
  getSmoothStepPath
} from '@vue-flow/core'
import { flowEditorKey } from './flowEditorContext.js'
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
  markerEnd: [String, Object],
  data: Object
})

const editor = inject(flowEditorKey)

const edgePath = computed(() =>
  props.data?.pathType === 'smoothstep'
    ? getSmoothStepPath(props)
    : getBezierPath(props)
)

const path = computed(() => edgePath.value[0])
const labelX = computed(() => edgePath.value[1])
const labelY = computed(() => edgePath.value[2])

const menuVisible = computed(() => editor.activeEdgeId.value === props.id)
const currentEdge = computed(() => editor.edges.value.find(edge => edge.id === props.id))
const isToEnd = computed(() =>
  editor.nodes.value.find(node => node.id === currentEdge.value?.target)?.type === 'end'
)

const handleClick = () => editor.toggleEdgeMenu(props.id)

const openAddDialog = () => {
  editor.closeEdgeMenu()
  editor.openAddDialog({ edgeId: props.id, x: labelX.value, y: labelY.value })
}

const closeMenu = (event) => {
  if (menuVisible.value && !event.target?.closest('.edge-action-wrapper')) {
    editor.closeEdgeMenu()
  }
}

onMounted(() => document.addEventListener('click', closeMenu))
onBeforeUnmount(() => {
  document.removeEventListener('click', closeMenu)
  if (menuVisible.value) editor.closeEdgeMenu()
})

const addBranch = () => {
  if (!currentEdge.value) return
  editor.closeEdgeMenu()
  editor.openAddDialog({
    mode: 'branch', sourceId: currentEdge.value.source, x: labelX.value, y: labelY.value
  })
}

const deleteFollowing = () => {
  editor.deleteFollowing({ edgeId: props.id, x: labelX.value, y: labelY.value })
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
