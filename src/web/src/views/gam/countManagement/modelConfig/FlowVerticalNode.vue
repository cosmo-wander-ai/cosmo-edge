<template>
  <div class="v-node" :class="{ expanded: data?.expanded }" :style="style" @click.stop="handleToggle">
    <Handle type="target" position="top" />
    <div class="label toggle-label">{{ currentLabel }}</div>
    <div v-if="data?.expanded" class="node-form-area nodrag nopan" @click.stop @mousedown.stop>
      <DynamicForm
        v-model="innerFormValue"
        :schema="data?.schema || []"
        label-width="120px"
        size="small"
        @update:model-value="handleRealtimeChange"
        @change="handleRealtimeChange"
      />
    </div>
    <Handle type="source" position="bottom" />
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { Handle } from '@vue-flow/core'
import { t } from '@/i18n'
import DynamicForm from './DynamicForm.vue'
const props = defineProps({
  label: {
    type: String,
    default: ''
  },
  style: {
    type: Object,
    default: () => ({})
  },
  data: {
    type: Object,
    default: () => ({})
  }
})

const innerFormValue = ref({})

const currentLabel = computed(() => { 
  if (!props.data?.config) return props.label
  if (props.data?.kind === 'input') {
    return `${t('glossary.input')}: ${props.data.config.name || ''}`
  }
  if (props.data?.kind === 'output') {
    return `${t('glossary.output')}: ${props.data.config.name || ''}`
  }
  return props.label
})

watch(
  () => props.data?.formValue,
  (val) => {
    console.log(props.data.formValue,'=====');
    innerFormValue.value = { ...(val || {}) }
  },
  { deep: true, immediate: true }
)

const lastSyncedValue = ref('')

const syncToParent = (changed) => {
  const payload = JSON.stringify(changed || {})
  if (payload === lastSyncedValue.value) return
  lastSyncedValue.value = payload
  if (typeof props.data?.onFormChange === 'function') {
    props.data.onFormChange(changed)
  }
}

const handleRealtimeChange = (changed) => {
  syncToParent(changed)
}

const handleToggle = () => {
  if (typeof props.data?.onToggle === 'function') {
    props.data.onToggle()
  }
}
</script>

<style scoped lang="scss">
.v-node {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  justify-content: flex-start;
  width: 160px;
  height: 60px;
  border-radius: 14px;
  background: #fff;
  border: 1px solid #dcdfe6;
  color: #111;
  font-weight: 600;
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.06);
  padding: 10px 12px;
  box-sizing: border-box;
}
.label {
  font-size: 14px;
  text-align: center;
  line-height: 30px;
}
.toggle-label {
  cursor: pointer;
}
.node-form-area {
  padding: 20px 40px 20px 15px;
}
.expanded {
  width: 400px;
  min-height: 200px;
}
</style>
