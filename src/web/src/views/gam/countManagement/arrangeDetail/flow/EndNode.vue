<template>
  <div class="end-node">
    <Handle type="target" :position="Position.Left" />
    <div class="node-icon">
      <svg viewBox="0 0 24 24" width="30" height="30" fill="#fff">
        <rect x="4" y="4" width="16" height="16" rx="2" />
      </svg>
    </div>
    <span class="end-label">{{ t('glossary.end') }}</span>
  </div>
</template>

<script setup>
import { Handle, Position } from '@vue-flow/core'
import { t } from '@/i18n'

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
  width: 76px;
  height: 96px;
  box-sizing: border-box;
  border-radius: 12px;
  background: #1f2937;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  color: #ffffff;
  box-shadow: none;
}

.node-icon {
  display: flex;
  align-items: center;
  justify-content: center;
}

.end-label {
  font-size: 11px;
  margin-top: 2px;
  letter-spacing: 1px;
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
}

.node-delete:hover {
  background-color: #fef0f0;
}
</style>
