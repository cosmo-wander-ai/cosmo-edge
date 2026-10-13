<template>
  <div class="realtime-tree-select">
    <el-select ref="treeSelect" filterable popper-class="realtime-tree-popper" :filter-method="filterTree" style="width: 100%" v-model="valueLabel" size="small" collapse-tags :clearable="clearable" :placeholder="placeholder" :multiple="multiple" @clear="handleClear" @remove-tag="handleRemoveTag">
      <el-option :value="valueLabel" :label="option.name" class="select-options">
        <el-tree id="tree-option" ref="treeSelectTree" :accordion="accordion" :data="treeData" :props="props" :node-key="props.value" :highlight-current="!multiple" :show-checkbox="multiple" :check-strictly="checkStrictly" :default-expand-all="expandAll" :expand-on-click-node="multiple" :filter-node-method="filterNode" @node-click="handleNodeClick" @check="handleNodeCheckbox">
          <template #default="{ node }">
            <span class="tree_label">
              {{ resolveNodeLabel(node.data) }}
            </span>
          </template>
        </el-tree>
      </el-option>
    </el-select>
  </div>
</template>
<script setup>
import { ref, watch, onMounted, nextTick } from 'vue'
import { t } from '@/i18n'

const props = defineProps({
  modelValue: {
    type: [String, Number, Object, Array],
    default: () => []
  },
  clearable: {
    type: Boolean,
    default: true
  },
  placeholder: {
    type: String,
    default: () => t('event.pleaseSelect')
  },
  multipleLimit: {
    type: Number,
    default: 2
  },
  filter: {
    type: Boolean,
    default: true
  },
  filterPlaceholder: {
    type: String,
    default: () => t('event.searchKeyword')
  },
  accordion: {
    type: Boolean,
    default: false
  },
  treeData: {
    type: Array,
    default: () => []
  },
  props: {
    type: Object,
    default: () => ({
      value: 'id',
      label: 'label',
      children: 'children'
    })
  },
  expandAll: {
    type: Boolean,
    default: true
  },
  checkStrictly: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['update:modelValue', 'change'])

const treeSelect = ref(null)
const treeSelectTree = ref(null)

const tp = ref({
  value: 'id',
  label: 'label',
  children: 'children',
  prentId: 'parentId'
})
const multiple = ref(false)
const valueLabel = ref([])
const option = ref({
  id: '',
  name: ''
})
const filterText = ref(undefined)
const valueId = ref([])
const treeIds = ref([])

watch(valueId, () => {
  if (multiple.value) {
    let valueStr = ''
    if (props.modelValue instanceof Array) {
      valueStr = props.modelValue.join()
    } else {
      valueStr = '' + props.modelValue
    }
    if (valueStr !== valueId.value.join()) {
      emit('update:modelValue', valueId.value)
      emit('change', valueId.value)
    }
  } else {
    let id = valueId.value.length > 0 ? valueId.value[0] : undefined
    if (id !== props.modelValue) {
      emit('update:modelValue', id)
      emit('change', id)
    }
  }
})

watch(() => props.modelValue, (newVal, oldVal) => {
  if (newVal !== oldVal) {
    init()
  }
})

watch(filterText, (newVal, oldVal) => {
  if (newVal !== oldVal) {
    treeSelectTree.value.filter(newVal)
  }
})

const init = () => {
  if (props.modelValue instanceof Array) {
    valueId.value = props.modelValue
  } else if (props.modelValue === undefined) {
    valueId.value = []
  } else {
    valueId.value = [props.modelValue]
  }
  if (multiple.value) {
    for (let id of valueId.value) {
      treeSelectTree.value.setChecked(id, true, false)
    }
  } else {
    treeSelectTree.value.setCurrentKey(
      valueId.value.length > 0 ? valueId.value[0] : undefined
    )
  }
  initValueLabel()
  initTreeIds()
  initScroll()
}

const initScroll = () => {
  nextTick(() => {
    let scrollWrap = document.querySelectorAll(
      '.el-scrollbar .el-select-dropdown__wrap'
    )[0]
    if (scrollWrap) {
      scrollWrap.style.cssText =
        'margin: 0px; max-height: none; overflow: hidden;'
    }
    let scrollBar = document.querySelectorAll(
      '.el-scrollbar .el-scrollbar__bar'
    )
    scrollBar.forEach((ele) => (ele.style.width = 0))
  })
}

const initTreeIds = () => {
  let ids = []
  
  function traverse(nodes) {
    for (let node of nodes) {
      ids.push(node[tp.value.value])
      if (node[tp.value.children]) {
        traverse(node[tp.value.children])
      }
    }
  }
  
  traverse(props.treeData)
  treeIds.value = ids
}

const initValueLabel = () => {
  let labels = []
  for (let id of valueId.value) {
    let node = traverse(
      props.treeData,
      (node) => node[tp.value.value] === id
    )
    if (node) {
      labels.push(node[tp.value.label])
    }
  }
  if (multiple.value) {
    valueLabel.value = labels
    option.value.name = labels.join()
  } else {
    valueLabel.value = labels.length > 0 ? labels[0] : undefined
    option.value.name = valueLabel.value
  }
}

const traverse = (tree, func) => {
  for (let node of tree) {
    if (func(node)) {
      return node
    }
    if (node[tp.value.children]) {
      let result = traverse(node[tp.value.children], func)
      if (result !== undefined) {
        return result
      }
    }
  }
  return undefined
}

const handleClear = () => {
  valueLabel.value = []
  valueId.value = []
  if (multiple.value) {
    for (let id of treeIds.value) {
      treeSelectTree.value.setChecked(id, false, false)
    }
  } else {
    treeSelectTree.value.setCurrentKey(null)
  }
}

const filterTree = (val) => {
  filterText.value = val
}

const filterNode = (value, data) => {
  if (!value) return true
  return resolveNodeLabel(data).indexOf(value) !== -1
}

const resolveNodeLabel = (node) => {
  return node?.labelI18nKey ? t(node.labelI18nKey) : node?.[props.props.label]
}

const handleNodeClick = (data, node) => {
  if (!multiple.value) {
    filterText.value = ''
    valueId.value = [data[tp.value.value]]
  }
  if (node.childNodes) {
    node.expanded = true
  }
}

const handleNodeCheckbox = (data, node) => {
  valueId.value = node.checkedKeys
}

const handleRemoveTag = (tag) => {
  let n = traverse(
    props.treeData,
    (node) => node[tp.value.label] === tag
  )
  if (n) {
    treeSelectTree.value.setChecked(
      n[tp.value.value],
      false,
      !props.checkStrictly
    )
  }
  valueId.value = treeSelectTree.value.getCheckedKeys()
}

onMounted(() => {
  for (let key in tp.value) {
    if (props.props[key] !== undefined) {
      tp.value[key] = props.props[key]
    }
  }
  multiple.value = props.multipleLimit > 1
  init()
  nextTick(() => {
    if (multiple.value) {
      const tagsEl = document.getElementsByClassName('el-select__tags')[0]
      const selectEl = document.getElementsByClassName('el-select')[0]
      if (tagsEl && selectEl) {
        tagsEl.style.maxHeight = selectEl.offsetHeight * 2 - 4 + 'px'
      }
    }
  })
})
</script>
 
<style scoped lang="scss">
.realtime-tree-select {
  :deep(.el-input__wrapper),
  :deep(.el-select__wrapper) { background: var(--bg-white); }
  :deep(.el-input__inner) { color: var(--text-primary); }
  :deep(.el-select__tags .el-tag) { color: var(--primary-color); border-color: var(--theme-selected-border, #e3e0f8); background: var(--theme-selected-bg, #f3f1ff); }
}
.tree_label { line-height: 28px; }
</style>

<style lang="scss">
.realtime-tree-popper {
  border: 1px solid var(--border-color);
  background: var(--bg-white);
  border-radius: 6px;
  box-shadow: 0 8px 24px #20222d1a;

  .el-select-dropdown__item {
    height: auto;
    max-height: 350px;
    padding: 0;
    overflow-y: auto;
    background: transparent;
    &.hover, &.is-hovering { background: transparent; }
  }
  .el-scrollbar { padding: 6px; }
  .el-tree { padding: 4px 0; background: transparent; }
  .el-tree-node__content {
    height: 34px;
    padding-right: 12px;
    color: var(--text-primary);
    border-radius: 4px;
    &:hover { background: var(--bg-primary); }
  }
  .el-tree-node.is-current > .el-tree-node__content { color: var(--primary-color); background: var(--theme-selected-bg, #f3f1ff); }
  .el-tree-node__expand-icon { color: var(--text-secondary); &.is-leaf { color: transparent; } }
  .el-checkbox__label { color: var(--text-primary); }
}
</style>
