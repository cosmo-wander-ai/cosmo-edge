<template>
  <div class="start-node" :style="{ width: `${terminalSize.width}px`, height: `${terminalSize.height}px` }">
    <Handle type="source" :position="Position.Right" />
    <div class="node-icon">
      <svg aria-hidden="true" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round">
        <path v-if="sceneLayout" stroke-linecap="round" d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z" />
        <polygon v-else points="7,4 20,12 7,20" />
      </svg>
    </div>
    <span class="start-label">{{ t('glossary.start') }}</span>
  </div>
</template>

<script setup>
import { computed, inject } from 'vue'
import { Handle, Position } from '@vue-flow/core'
import { t } from '@/i18n'
import { flowEditorKey } from './flowEditorContext.js'
import { FLOW_TERMINAL_SIZE } from './layoutGeometry.js'

const sceneLayout = inject(flowEditorKey, null)?.sceneLayout
const terminalSize = computed(() => sceneLayout?.value?.terminal ?? FLOW_TERMINAL_SIZE)

const props = defineProps({
  id: {
    type: String,
    required: true,
  },
})

</script>

<style scoped>
.start-node {
  position: relative;
  box-sizing: border-box;
  border: 1px solid var(--flow-action-border);
  border-radius: 24px;
  background: var(--flow-node);
  box-shadow: var(--flow-node-shadow);
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  color: var(--flow-node-text);
}

.node-icon { display: flex; align-items: center; justify-content: center; }
.start-label { font-size: 12px; font-weight: 500; }
</style>
