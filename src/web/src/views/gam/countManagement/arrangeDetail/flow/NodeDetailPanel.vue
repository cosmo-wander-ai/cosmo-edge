<template>
  <div
    ref="panelRef"
    class="node-detail-panel"
    :style="panelStyle"
    @click.stop
    @mousedown.stop
    @pointerdown.stop
    @wheel.stop
  >
    <!-- Header：可拖拽区域 -->
    <div class="panel-header" @mousedown="startDrag">
      <div class="panel-header-left">
        <div class="panel-icon-wrapper" :class="iconColorClass">
          <!-- 视频解码 -->
          <svg v-if="iconKey === 'video'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <rect x="3" y="4" width="18" height="13" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <polygon points="10,7.5 10,13.5 15,10.5" fill="currentColor"/>
            <line x1="8" y1="20" x2="16" y2="20" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <line x1="12" y1="17" x2="12" y2="20" stroke="currentColor" stroke-width="1.5"/>
          </svg>
          <!-- 目标检测 -->
          <svg v-else-if="iconKey === 'target'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="7.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <circle cx="12" cy="12" r="2.5" fill="currentColor"/>
            <line x1="12" y1="2" x2="12" y2="6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <line x1="12" y1="18" x2="12" y2="22" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <line x1="2" y1="12" x2="6" y2="12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <line x1="18" y1="12" x2="22" y2="12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          </svg>
          <!-- 追踪 -->
          <svg v-else-if="iconKey === 'tracking'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <circle cx="5" cy="18" r="2" fill="currentColor"/>
            <circle cx="19" cy="6" r="2" fill="currentColor"/>
            <path d="M7 17C8 13 10 10 13 9c2-.7 4-.5 5-1" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <polyline points="16,3 19,6 16,9" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
          </svg>
          <!-- 大模型/AI -->
          <svg v-else-if="iconKey === 'ai'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="5" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <circle cx="5" cy="19" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <circle cx="19" cy="19" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <line x1="12" y1="7.5" x2="6.5" y2="16.5" stroke="currentColor" stroke-width="1.5"/>
            <line x1="12" y1="7.5" x2="17.5" y2="16.5" stroke="currentColor" stroke-width="1.5"/>
            <line x1="7.5" y1="19" x2="16.5" y2="19" stroke="currentColor" stroke-width="1.5"/>
          </svg>
          <!-- 类别过滤/目标判断 -->
          <svg v-else-if="iconKey === 'classify'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <rect x="3.5" y="8" width="6.5" height="6.5" rx="1" transform="rotate(45 6.75 11.25)" fill="currentColor"/>
            <rect x="14" y="8" width="6.5" height="6.5" rx="1" transform="rotate(45 17.25 11.25)" fill="currentColor"/>
          </svg>
          <!-- 目标分类 — 网格 -->
          <svg v-else-if="iconKey === 'grid'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <rect x="3" y="3" width="7" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <rect x="14" y="3" width="7" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <rect x="3" y="14" width="7" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <rect x="14" y="14" width="7" height="7" rx="1.5" fill="currentColor" stroke="currentColor" stroke-width="1.5"/>
          </svg>
          <!-- 类别过滤 — 漏斗 -->
          <svg v-else-if="iconKey === 'filter'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <polygon points="2,4 22,4 14,14 14,21 10,21 10,14" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
          </svg>
          <!-- 目标判断 — 盾牌勾选 -->
          <svg v-else-if="iconKey === 'judge'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 2L3 7v5c0 5.25 3.83 10.17 9 11.38C17.17 22.17 21 17.25 21 12V7l-9-5z" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
            <polyline points="9,12 11,14 15,10" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
          </svg>
          <!-- 计时 -->
          <svg v-else-if="iconKey === 'timer'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="13" r="8" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <polyline points="12,9 12,13 15,15" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
            <line x1="12" y1="2" x2="12" y2="5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <line x1="9" y1="2" x2="15" y2="2" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          </svg>
          <!-- 事件上报 -->
          <svg v-else-if="iconKey === 'send'" class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path d="M22 2L11 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M22 2L15 22L11 13L2 9L22 2Z" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
          </svg>
          <!-- 默认-齿轮 -->
          <svg v-else class="panel-icon-svg" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="1.5"/>
            <path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 11-2.83 2.83l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 11-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 11-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 110-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 112.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 114 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 112.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 110 4h-.09a1.65 1.65 0 00-1.51 1z" fill="none" stroke="currentColor" stroke-width="1.5"/>
          </svg>
        </div>
        <span class="panel-title">{{ actionDetail ? resolveResourceActionName(actionDetail) : t('glossary.nodeConfig') }}</span>
      </div>
      <button class="panel-close" @click.stop="$emit('close')">✕</button>
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
import { t } from '@/i18n'
import { resolveResourceActionName, resolveResourceActionRemark } from '@/utils/i18nResource'

const props = defineProps({
  nodeId: { type: String, required: true },
  nodeData: { type: Object, default: () => ({}) },
  position: { type: Object, default: () => ({ x: 0, y: 0 }) }
})

const emit = defineEmits(['close', 'config-change'])

// ---- 从 nodeData 提取子数据 ----
const actionDetail = computed(() => props.nodeData?.actionDetail)
const flowData = computed(() => props.nodeData?.flowData)
const atomicList = computed(() => props.nodeData?.atomicList)
const templateVersion = computed(() => props.nodeData?.templateVersion || 0)

// ---- 图标 ----
const iconKey = computed(() => getIconInfo(actionDetail.value?.actionId).key)
const iconColorClass = computed(() => getIconInfo(actionDetail.value?.actionId).color)

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

const panelStyle = computed(() => ({
  left: `${props.position.x + dragOffset.value.x}px`,
  top: `${props.position.y + dragOffset.value.y}px`
}))

let dragStartMouse = { x: 0, y: 0 }
let dragStartOffset = { x: 0, y: 0 }

const startDrag = (e) => {
  // 忽略关闭按钮上的拖拽
  if (e.target.closest('.panel-close')) return
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
  width: 360px;
  max-height: 350px;
  display: flex;
  flex-direction: column;
  background: #ffffff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.12), 0 4px 12px rgba(0, 0, 0, 0.06);
  z-index: 100;
  animation: panel-in 0.2s ease-out;
  overflow: visible;
}

@keyframes panel-in {
  from { opacity: 0; transform: translateY(-8px) scale(0.97); }
  to   { opacity: 1; transform: translateY(0) scale(1); }
}

/* ---- Header ---- */
.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px solid #f3f4f6;
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
  width: 32px;
  height: 32px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.panel-icon-wrapper.icon-blue   { background: #eff6ff; color: #3b82f6; }
.panel-icon-wrapper.icon-purple { background: #ebf8ff; color: #2b6cb0; }
.panel-icon-wrapper.icon-orange { background: #fff7ed; color: #ea580c; }
.panel-icon-wrapper.icon-green  { background: #f0fdf4; color: #16a34a; }
.panel-icon-wrapper.icon-gray   { background: #f3f4f6; color: #6b7280; }

.panel-icon-svg {
  width: 20px;
  height: 20px;
}

.panel-title {
  font-size: 15px;
  font-weight: 600;
  color: #1f2937;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.panel-close {
  width: 28px;
  height: 28px;
  border: none;
  background: transparent;
  color: #9ca3af;
  font-size: 14px;
  border-radius: 6px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.15s;
}

.panel-close:hover {
  background: #f3f4f6;
  color: #374151;
}

/* ---- Body ---- */
.panel-body {
  flex: 1;
  overflow-y: auto;
  overflow-x: visible;
  padding: 10px 16px 14px;
}

/* 滚动条美化 */
.panel-body::-webkit-scrollbar {
  width: 5px;
}

.panel-body::-webkit-scrollbar-track {
  background: transparent;
}

.panel-body::-webkit-scrollbar-thumb {
  background: #d1d5db;
  border-radius: 3px;
}

.panel-body::-webkit-scrollbar-thumb:hover {
  background: #9ca3af;
}

/* ---- 描述提示 ---- */
.panel-hint {
  font-size: 12px;
  color: #9ca3af;
  line-height: 1.4;
  margin-bottom: 10px;
  padding: 0 2px;
}
</style>
