<template>
  <BaseEdge :id="id" :style="style" :path="path" :marker-end="markerEnd" />
  <EdgeLabelRenderer>
    <div class="edge-action-wrapper" :style="{
        transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)`,
      }">
      <button type="button" class="edge-action-button" :aria-label="t('action.addComponent')" :aria-expanded="menuVisible" @click.stop="handleClick">
        +
      </button>
      <div v-if="menuVisible" class="edge-menu">
        <span class="menu-item" @click.stop="openAddDialog">{{ t('action.addComponent') }}</span>
        <span class="divider"></span>
        <span class="menu-item menu-item-danger" :class="{ 'is-disabled': isToEnd }" @click.stop="!isToEnd && deleteFollowing()">
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
  font-size: 14px;
  font-weight: 500;
  line-height: 22px;
  border-radius: 50%;
  border: 1px solid var(--flow-action-border);
  background-color: var(--flow-node);
  color: var(--text-secondary);
  cursor: pointer;
  white-space: nowrap;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: border-color 0.15s, color 0.15s, background-color 0.15s;
}
.edge-action-button.is-disabled,
.edge-action-button:disabled {
  border-color: var(--flow-action-disabled-border);
  background-color: var(--flow-disabled-bg);
  color: var(--flow-disabled-text);
  cursor: not-allowed;
}

.edge-menu .menu-item.is-disabled {
  color: var(--flow-disabled-text);
  cursor: not-allowed;
  pointer-events: none;
}

.edge-action-button:hover:not(.is-disabled):not(:disabled) {
  border-color: var(--primary-color);
  color: var(--primary-color);
  background-color: var(--el-color-primary-light-9);
  box-shadow: none;
}

.edge-menu {
  position: absolute;
  top: 34px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 9px 12px;
  border-radius: 6px;
  color: var(--text-primary);
  background: var(--bg-secondary);
  box-shadow: var(--shadow-md);
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

.edge-menu .menu-item:not(.is-disabled):hover {
  color: var(--primary-color);
}

.edge-menu .menu-item-danger:not(.is-disabled) {
  color: var(--danger-color);
}

.edge-action-button:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 2px;
}

.edge-menu .divider {
  width: 1px;
  height: 16px;
  background: var(--border-color);
}
</style>
