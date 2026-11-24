<template>
  <div class="end-node" :style="{ width: `${terminalSize.width}px`, height: `${terminalSize.height}px` }">
    <Handle type="target" :position="Position.Left" />
    <div class="node-icon">
      <svg aria-hidden="true" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.7">
        <rect v-if="sceneLayout" width="18" height="18" x="3" y="3" rx="2" stroke-linecap="round" stroke-linejoin="round" />
        <rect v-else x="5" y="5" width="14" height="14" rx="2" />
      </svg>
    </div>
    <span class="end-label">{{ t('glossary.end') }}</span>
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
.end-node {
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
.end-label { font-size: 12px; font-weight: 500; }
</style>
