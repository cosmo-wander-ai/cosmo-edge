<template>
  <div
    ref="panelRef"
    class="node-detail-panel"
    :class="{ 'is-docked': docked }"
    :style="panelStyle"
    @click.stop
    @mousedown.stop
    @pointerdown.stop
    @wheel.stop
  >
    <!-- Header：可拖拽区域 -->
    <div class="panel-header" @mousedown="startDrag">
      <div class="panel-header-left">
        <div class="panel-icon-wrapper">
          <FlowNodeIcon :kind="iconKey" />
        </div>
        <span class="panel-title" :title="actionDetail ? resolveResourceActionName(actionDetail) : t('glossary.nodeConfig')">{{ actionDetail ? resolveResourceActionName(actionDetail) : t('glossary.nodeConfig') }}</span>
      </div>
      <div class="panel-header-actions">
        <button v-if="showDockControl" type="button" class="panel-mode" :title="t(docked ? 'glossary.flowFloatPanel' : 'glossary.flowDockPanel')" :aria-label="t(docked ? 'glossary.flowFloatPanel' : 'glossary.flowDockPanel')" @click.stop="$emit('toggle-dock')">
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <path v-if="docked" d="M9 3H3v18h18v-6M13 3h8v8M21 3 11 13" />
            <path v-else d="M3 4h18v16H3ZM14 4v16" />
          </svg>
        </button>
        <button type="button" class="panel-close" :title="t('action.close')" :aria-label="t('action.close')" @click.stop="$emit('close')">✕</button>
      </div>
    </div>

    <!-- Body：可滚动区域 -->
    <div class="panel-body">
      <!-- 描述提示（仅在有描述时显示） -->
      <div class="panel-hint" v-if="actionDetail?.remark">{{ resolveResourceActionRemark(actionDetail) }}</div>

      <!-- 动态表单 -->
      <div class="panel-form" v-if="actionDetail?.inputParamConfig">
        <dynamic-form
          :key="`panel-${nodeId}-${templateVersion}`"
          ref="dynamicFormRef"
          :flowData="flowData"
          :actionDetail="actionDetail"
          :configObject="nodeData.configObject"
          :atomicList="atomicList"
          @config-change="handleConfigChange"
        ></dynamic-form>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onBeforeUnmount } from 'vue'
import DynamicForm from './DynamicForm.vue'
import { getIconInfo } from './iconMapping.js'
import FlowNodeIcon from './FlowNodeIcon.vue'
import { t } from '@/i18n'
import { resolveResourceActionName, resolveResourceActionRemark } from '@/utils/i18nResource'
import { getDetailPanelSize } from './layoutGeometry.js'

const props = defineProps({
  nodeId: { type: String, required: true },
  nodeData: { type: Object, default: () => ({}) },
  atomicList: { type: Array, default: () => [] },
  viewport: { type: Object, default: () => ({}) },
  docked: { type: Boolean, default: false },
  dockSize: { type: Object, default: () => ({ width: 460, height: 500 }) },
  floatingSize: { type: Object, default: null },
  showDockControl: { type: Boolean, default: false },
  position: { type: Object, default: () => ({ x: 0, y: 0 }) }
})

const emit = defineEmits(['close', 'config-change', 'toggle-dock'])

// ---- 从 nodeData 提取子数据 ----
const actionDetail = computed(() => props.nodeData?.actionDetail)
const flowData = computed(() => props.nodeData?.flowData)
const templateVersion = computed(() => props.nodeData?.templateVersion || 0)

// ---- 图标 ----
const iconKey = computed(() => getIconInfo(actionDetail.value?.actionId).key)

// ---- DynamicForm ref ----
const dynamicFormRef = ref(null)

/** 供父组件调用：收集当前表单配置 */
const submitForm = () => {
  return dynamicFormRef.value?.submitForm?.()
}

const handleConfigChange = (config) => {
  emit('config-change', config)
}



// ---- 拖拽逻辑 ----
const panelRef = ref(null)
const dragOffset = ref({ x: 0, y: 0 })
const isDragging = ref(false)

const floatingPanelSize = computed(() => props.floatingSize || getDetailPanelSize(actionDetail.value?.actionId, props.viewport))

const panelStyle = computed(() => props.docked ? {
  width: `${props.dockSize.width}px`,
  height: `${props.dockSize.height}px`,
  maxHeight: `${props.dockSize.height}px`
} : ({
  '--panel-width': `${floatingPanelSize.value.width}px`,
  '--panel-height': `${floatingPanelSize.value.height}px`,
  '--panel-min-left': 'min(64px, max(8px, calc(100% - var(--panel-width) - 8px)))',
  // The panel uses screen pixels while its anchor follows the zoomable canvas.
  // Keep its controls reachable after zooming, resizing or dragging near an edge.
  left: `clamp(var(--panel-min-left), ${props.position.x + dragOffset.value.x}px, max(8px, calc(100% - var(--panel-width) - 8px)))`,
  top: `clamp(8px, ${props.position.y + dragOffset.value.y}px, max(8px, calc(100% - var(--panel-height) - 8px)))`,
  width: 'min(var(--panel-width), calc(100% - 16px))',
  maxHeight: 'min(var(--panel-height), calc(100% - 16px))'
}))

let dragStartMouse = { x: 0, y: 0 }
let dragStartOffset = { x: 0, y: 0 }

const startDrag = (e) => {
  // Docking is the default; floating keeps the existing movable header.
  if (props.docked || e.target.closest('button')) return
  // Resume from the clamped position, not an invisible offset outside the canvas.
  if (panelRef.value) {
    dragOffset.value = {
      x: panelRef.value.offsetLeft - props.position.x,
      y: panelRef.value.offsetTop - props.position.y
    }
  }
  isDragging.value = true
  dragStartMouse = { x: e.clientX, y: e.clientY }
  dragStartOffset = { ...dragOffset.value }
  document.addEventListener('mousemove', onDrag)
  document.addEventListener('mouseup', endDrag)
}

const onDrag = (e) => {
  if (!isDragging.value) return
  dragOffset.value = {
    x: dragStartOffset.x + (e.clientX - dragStartMouse.x),
    y: dragStartOffset.y + (e.clientY - dragStartMouse.y)
  }
}

const endDrag = () => {
  isDragging.value = false
  document.removeEventListener('mousemove', onDrag)
  document.removeEventListener('mouseup', endDrag)
}

/** 重置拖拽偏移（切换节点时由父组件调用） */
const resetDragOffset = () => {
  dragOffset.value = { x: 0, y: 0 }
}

defineExpose({ submitForm, resetDragOffset })

onBeforeUnmount(() => {
  document.removeEventListener('mousemove', onDrag)
  document.removeEventListener('mouseup', endDrag)
})
</script>

<style scoped>
.node-detail-panel {
  position: absolute;
  box-sizing: border-box;
  width: 360px;
  max-height: 350px;
  display: flex;
  flex-direction: column;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 12px;
  box-shadow: var(--flow-panel-shadow);
  z-index: 100;
  animation: panel-in 0.2s ease-out;
  overflow: visible;
}

@keyframes panel-in {
  from { opacity: 0; transform: translateY(-8px) scale(0.97); }
  to   { opacity: 1; transform: translateY(0) scale(1); }
}

.node-detail-panel.is-docked {
  position: relative;
  flex: none;
  border-radius: 0;
  border-top: 0;
  border-right: 0;
  border-bottom: 0;
  box-shadow: none;
  animation: none;
}

.is-docked .panel-header {
  cursor: default;
}

/* ---- Header ---- */
.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px solid var(--bg-secondary);
  cursor: grab;
  user-select: none;
  flex-shrink: 0;
}

.panel-header:active {
  cursor: grabbing;
}

.panel-header-left {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.panel-icon-wrapper {
  width: 24px;
  height: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  color: var(--flow-node-text);
}

.panel-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.panel-header-actions {
  display: flex;
  gap: 4px;
  padding-left: 8px;
  flex-shrink: 0;
}

.panel-mode,
.panel-close {
  width: 28px;
  height: 28px;
  border: none;
  background: transparent;
  color: var(--text-secondary);
  font-size: 14px;
  border-radius: 6px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.15s;
}

.panel-mode svg {
  width: 17px;
  height: 17px;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.5;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.panel-mode:focus-visible,
.panel-close:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 1px;
}

.panel-mode:hover,
.panel-close:hover {
  background: var(--bg-secondary);
  color: var(--flow-node-text);
}

/* ---- Body ---- */
.panel-body {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  overflow-x: visible;
  padding: 10px 16px 14px;
  container: flow-parameters / inline-size;
}

/* The task picker is a third-party two-column tree. Size it to the panel, not
   its package's fixed 560px minimum, and use the same theme as the form. */
.node-detail-panel .panel-body :deep(.tree-transfer-vue3) {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 44px minmax(0, 1fr);
  width: 100%;
  min-width: 0;
  min-height: 240px;
  background: transparent;
}

.node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-left),
.node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-right) {
  width: auto;
  min-width: 0;
  border-color: var(--border-color);
  border-radius: 6px;
  background: var(--bg-white);
  overflow: hidden;
}

.node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-title) {
  min-height: 40px;
  box-sizing: border-box;
  background: var(--bg-secondary);
  color: var(--text-primary);
  border-bottom: 1px solid var(--border-light);
}

.node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-center) {
  min-width: 0;
}

.node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-main) {
  min-width: 0;
  max-height: 260px;
  overflow: auto;
}

@container flow-parameters (max-width: 540px) {
  .node-detail-panel .panel-body :deep(.tree-transfer-vue3) {
    grid-template-columns: minmax(0, 1fr);
    gap: 10px;
  }

  .node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-left),
  .node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-right) {
    min-height: 180px;
  }

  .node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-center) {
    flex-direction: row;
    gap: 12px;
    min-height: 32px;
  }

  .node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-center .el-button + .el-button) {
    margin: 0;
  }

  .node-detail-panel .panel-body :deep(.tree-transfer-vue3 .transfer-center .el-icon) {
    transform: rotate(90deg);
  }
}

/* 滚动条美化 */
.panel-body::-webkit-scrollbar {
  width: 5px;
}

.panel-body::-webkit-scrollbar-track {
  background: transparent;
}

.panel-body::-webkit-scrollbar-thumb {
  background: var(--flow-scrollbar);
  border-radius: 3px;
}

.panel-body::-webkit-scrollbar-thumb:hover {
  background: var(--text-secondary);
}

/* ---- 描述提示 ---- */
.panel-hint {
  font-size: 13px;
  color: var(--text-secondary);
  line-height: 1.5;
  margin-bottom: 10px;
  padding: 0 2px;
}
</style>
