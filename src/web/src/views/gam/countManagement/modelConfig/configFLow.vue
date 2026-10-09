<template>
  <div class="flow-container">
    <div class="flow-wrap">
      <VueFlow :nodes="nodes" :edges="edges" :node-types="nodeTypes" :default-zoom="0.4">
        <Background pattern-color="#e5e7eb" gap="16" />
        <Controls show-interactive />
      </VueFlow>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, markRaw } from 'vue'
import { VueFlow, useVueFlow } from '@vue-flow/core'
import { Background } from '@vue-flow/background'
import { Controls } from '@vue-flow/controls'
import '@vue-flow/core/dist/style.css'
import '@vue-flow/controls/dist/style.css'
import { t } from '@/i18n'
import FlowBoxNode from './FlowBoxNode.vue'

const props = defineProps({
  flowData: {
    type: Object,
    default: () => ({})
  }
})
const emit = defineEmits(['update:flowData', 'node-config-change'])

const nodes = ref([])

const edges = ref([])
const expandedNodeId = ref('')

const { onPaneReady } = useVueFlow()
onPaneReady((instance) => {
  instance.fitView()
})

const toValue = (key, value) => {
  if (key === 'shape') {
    try {
      const parsed = JSON.parse(value)
      if (Array.isArray(parsed)) return parsed
      return value
    } catch {
      return value
    }
  }
  return value
}

const handleToggleNode = (nodeId) => {
  if (!nodeId) return
  expandedNodeId.value = expandedNodeId.value === nodeId ? '' : nodeId
  buildGraph(props.flowData || {})
}

const handleInlineFormChange = (kind, index, nodeId, changed) => {
  if (!kind || index < 0) return
  const nextData = JSON.parse(JSON.stringify(props.flowData || {}))
  const listKey = kind === 'input' ? 'inputs' : 'outputs'
  const list = Array.isArray(nextData[listKey]) ? nextData[listKey] : []
  if (!list[index]) return
  Object.keys(changed || {}).forEach((key) => {
    list[index][key] = toValue(key, changed[key])
  })
  emit('update:flowData', nextData)
  emit('node-config-change', {
    kind,
    index,
    config: list[index]
  })
  expandedNodeId.value = nodeId
  buildGraph(nextData)
}

const buildGraph = (data = {}) => {
  const inputs = Array.isArray(data.inputs) ? data.inputs : []
  const outputs = Array.isArray(data.outputs) ? data.outputs : []

  const inputRoot = {
    id: 'input-root',
    label: `${t('glossary.input')}\n(${inputs.length})`,
    position: { x: 100, y: 140 },
    type: 'box',
    data: {
      label: `${t('glossary.input')}\n(${inputs.length})`,
      kind: 'input',
      list: inputs,
      expanded: expandedNodeId.value === 'input-root',
      onToggle: () => handleToggleNode('input-root'),
      onUpdate: (index, changed) => handleInlineFormChange('input', index, 'input-root', changed),
    },
    style: {
      background: 'transparent',
      border: 'none',
      padding: 0
    }
  }

  const outputRoot = {
    id: 'output-root',
    label: `${t('glossary.output')}\n(${outputs.length})`,
    position: { x: 800, y: 140 },
    type: 'box',
    data: {
      label: `${t('glossary.output')}\n(${outputs.length})`,
      kind: 'output',
      list: outputs,
      expanded: expandedNodeId.value === 'output-root',
      onToggle: () => handleToggleNode('output-root'),
      onUpdate: (index, changed) => handleInlineFormChange('output', index, 'output-root', changed),
    },
    style: {
      background: 'transparent',
      border: 'none',
      padding: 0
    }
  }

  const mainEdge = {
    id: 'e-main',
    source: 'input-root',
    target: 'output-root',
    sourceHandle: 'right-out',
    targetHandle: 'left-in',
  }

  nodes.value = [inputRoot, outputRoot]
  edges.value = [mainEdge]
}

watch(
  () => props.flowData,
  (val) => {
    buildGraph(val)
  },
  { immediate: true, deep: true }
)

const nodeTypes = {
  box: markRaw(FlowBoxNode)
}
</script>

<style scoped lang="scss">
.flow-container {
  width: 100%;
}

.flow-wrap {
  width: 100%;
  height: 400px;
  border: 1px solid var(--el-border-color, var(--border-color));
  border-radius: 6px;
  background: #fff;
}
</style>
