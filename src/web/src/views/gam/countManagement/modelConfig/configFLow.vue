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
import FlowMainNode from './FlowMainNode.vue'
import FlowVerticalNode from './FlowVerticalNode.vue'

const props = defineProps({
  flowData: {
    type: Object,
    default: () => ({})
  },
  modelComponents: {
    type: Array,
    default: () => []
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

const toText = (value) => {
  if (Array.isArray(value) || (value && typeof value === 'object')) {
    return JSON.stringify(value)
  }
  if (value === undefined || value === null) return ''
  return String(value)
}

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

const getFormValueByNode = (kind, config) => {
  if (kind === 'input') {
    return {
      name: toText(config.name),
      shape: toText(config.shape),
      data_type: toText(config.data_type ?? '0')
    }
  }
  if (kind === 'output') {
    return {
      name: toText(config.name),
      shape: toText(config.shape),
      data_type: toText(config.data_type ?? '0')
    }
  }
  return {}
}

const getSchemaByNode = (kind, config) => {
  if (!config) return []

  const parseOptions = (item) => {
    if (Array.isArray(item?.options) && item.options.length) {
      return item.options
    }
    const enumeration = item?.enumeration || item?.enums
    if (typeof enumeration !== 'string' || !enumeration.trim()) {
      return []
    }
    return enumeration
      .split(',')
      .map((segment) => segment.split(':'))
      .filter((parts) => parts.length >= 2)
      .map((parts) => ({
        name: String(parts[0]).trim(),
        value: String(parts[1]).trim()
      }))
  }

  const normalizeSchemaItem = (item = {}) => ({
    type: String(item.type || item.moduleType || 'text'),
    key: item.key || item.keyValue || '',
    name: item.name || item.nameValue || item.label || item.key || '',
    defaultValue:
      item.defaultValue !== undefined && item.defaultValue !== null
        ? item.defaultValue
        : '',
    description: item.description || item.describe || '',
    options: parseOptions(item)
  })

  const getComponentSchema = (componentType) => {
    const parseConfigArray = (value) => {
      if (Array.isArray(value)) return value
      if (typeof value !== 'string' || !value.trim()) return []
      try {
        const parsed = JSON.parse(value)
        return Array.isArray(parsed) ? parsed : []
      } catch {
        return []
      }
    }

    const renameInputNodeKey = (configArray) => {
      return configArray.map((item) => {
        if (item.key === 'input_node' || item.key === 'output_node') {
          // 只修改匹配项，其余保持不变
          return { ...item, key: 'name' }
        }
        return item
      })
    }

    const component = (
      Array.isArray(props.modelComponents) ? props.modelComponents : []
    ).find(
      (item) =>
        String(item?.componentType || item?.type || '')
          .toLowerCase()
          .trim() === componentType
    )
    if (!component) return []

    const componentConfigKeyMap = {
      input: 'inputParamConfig',
      output: 'inputParamConfig'
    }
    const componentConfigKey = componentConfigKeyMap[componentType]
    const componentConfigList = parseConfigArray(
      component?.[componentConfigKey]
    )
    if (componentConfigList.length) {
      const newComponentConfigList = renameInputNodeKey(componentConfigList)
      return newComponentConfigList
        .map((item) => normalizeSchemaItem(item))
        .filter((item) => item.key && ['text', 'select'].includes(item.type))
    }

    const candidateKeys = [
      'params',
      'config',
      'componentConfig',
      'formItems',
      'items',
      'metaDataParams'
    ]
    let source = []
    for (const key of candidateKeys) {
      if (Array.isArray(component?.[key])) {
        source = component[key]
        break
      }
    }

    return source
      .map((item) => normalizeSchemaItem(item))
      .filter((item) => item.key && ['text', 'select'].includes(item.type))
  }

  const baseSchema = getComponentSchema(kind) || []
  const sourceSchema = baseSchema

  return sourceSchema.map((item) => {
    const hasConfigValue =
      config[item.key] !== undefined && config[item.key] !== null
    const options =
      item.type === 'select' &&
      (!Array.isArray(item.options) || !item.options.length)
        ? []
        : item.options
    return {
      ...item,
      options,
      defaultValue: hasConfigValue
        ? toText(config[item.key])
        : toText(item.defaultValue)
    }
  })
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
  box: markRaw(FlowBoxNode),
  main: markRaw(FlowMainNode),
  vertical: markRaw(FlowVerticalNode)
}
</script>

<style scoped lang="scss">
.flow-container {
  width: 100%;
}

.flow-wrap {
  width: 100%;
  height: 400px;
  border: 1px solid var(--el-border-color, #dcdfe6);
  border-radius: 6px;
  background: #fff;
}
</style>
