<template>
  <div
    class="action-node"
    :data-flow-stage="props.data?.stage"
    :class="{ selected: props.data?.selected }"
    :style="{ width: `${FLOW_NODE_SIZE.width}px`, height: `${FLOW_NODE_SIZE.height}px` }"
  >
    <button
      type="button"
      class="node-delete"
      :title="t('action.delete')"
      :aria-label="`${t('action.delete')} ${resolveResourceActionName(nodeActionDetail)}`"
      @click.stop="handleDelete"
    >
      <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
        <path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 10v7M14 10v7" />
      </svg>
    </button>
    <Handle type="target" :position="Position.Left" />

    <div class="node-card" @click.stop="handleClick">
      <div class="node-icon-wrapper">
        <FlowNodeIcon :kind="iconKey" />
      </div>
      <div class="node-name" :title="resolveResourceActionName(nodeActionDetail)">
        <span class="node-name-text">{{ resolveResourceActionName(nodeActionDetail) }}</span>
      </div>
    </div>

    <Handle type="source" :position="Position.Right" />
  </div>
</template>

<script setup>
import { computed, inject, onBeforeUnmount } from 'vue'
import { Handle, Position } from '@vue-flow/core'
import { ElMessageBox } from 'element-plus'
import { flowEditorKey } from './flowEditorContext.js'
import { getIconInfo } from './iconMapping.js'
import FlowNodeIcon from './FlowNodeIcon.vue'
import { FLOW_NODE_SIZE } from './layoutGeometry.js'
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

const editor = inject(flowEditorKey)
let mounted = true
onBeforeUnmount(() => { mounted = false })

const handleClick = () => {
  editor.openDetailPanel(props.id)
}

const handleDelete = () => {
  ElMessageBox.confirm(t('validate.confirmDeleteNode'), t('common.notice'), {
    confirmButtonText: t('action.confirm'),
    cancelButtonText: t('action.cancel'),
    type: 'warning'
  }).then(() => {
    if (mounted) editor.deleteNode(props.id)
  }).catch(() => {
    // A cancelled confirmation leaves the graph unchanged.
  })
}
</script>

<style scoped>
.action-node {
  background: var(--flow-node);
  border: 1px solid var(--flow-node-border);
  border-radius: 8px;
  box-sizing: border-box;
  box-shadow: var(--flow-node-shadow);
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  transition: border-color 0.2s, box-shadow 0.2s;
}

.action-node:hover {
  border-color: var(--flow-node-hover-border);
}

.action-node.selected {
  border-color: var(--primary-color);
  box-shadow: 0 0 0 2px var(--flow-selected-ring), var(--flow-node-shadow);
}

.action-node .node-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 8px 10px;
  cursor: pointer;
  width: 100%;
  height: 100%;
  box-sizing: border-box;
  gap: 10px;
  box-shadow: none;
}

.node-icon-wrapper {
  width: 44px;
  height: 44px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  border-radius: 8px;
  background: var(--flow-node-icon-bg);
  color: var(--flow-stage-accent, var(--flow-node-text));
  transition: color 0.2s;
}

.action-node.selected .node-icon-wrapper {
  color: var(--flow-stage-accent, var(--flow-node-icon-selected));
}

.node-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--flow-node-text);
  text-align: center;
  line-height: 18px;
  height: 36px;
  flex-shrink: 0;
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.node-name-text {
  word-break: normal;
  overflow-wrap: anywhere;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}

.node-delete {
  position: absolute;
  top: 2px;
  right: 2px;
  width: 24px;
  height: 24px;
  border-radius: 5px;
  border: none;
  background: transparent;
  color: var(--text-secondary);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  cursor: pointer;
  z-index: 10;
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.15s, background-color 0.15s, color 0.15s;
}

.node-delete svg {
  width: 14px;
  height: 14px;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.6;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.action-node:hover .node-delete,
.action-node.selected .node-delete,
.action-node:focus-within .node-delete {
  opacity: 1;
  pointer-events: auto;
}

.node-delete:hover {
  background-color: var(--bg-secondary);
  color: var(--flow-node-text);
}

.node-delete:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 1px;
}

@media (any-pointer: coarse) {
  .node-delete {
    opacity: 1;
    pointer-events: auto;
  }
}
</style>
