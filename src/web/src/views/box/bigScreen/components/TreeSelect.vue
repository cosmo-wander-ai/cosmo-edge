<template>
  <div>
    <el-select ref="treeSelect" filterable popper-class="custom-select-popper" :filter-method="filterTree" style="width: 100%" v-model="valueLabel" size="small" collapse-tags :clearable="clearable" :placeholder="placeholder" :multiple="multiple" @clear="handleClear" @remove-tag="handleRemoveTag">
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
.custom-select-popper {
  .el-select-dropdown__item {
    height: auto;
    max-height: 350px;
    padding: 0;
    overflow-y: auto;

    &::-webkit-scrollbar-track {
      background: rgba(19, 31, 58, 0.3);
      border-radius: 3px;
    }

    &::-webkit-scrollbar {
      -webkit-appearance: none;
      width: 6px;
      height: 6px;
    }

    &::-webkit-scrollbar-thumb {
      cursor: pointer;
      border-radius: 3px;
      background: rgba(95, 200, 223, 0.4);
      transition: all 0.2s ease;

      &:hover {
        background: rgba(95, 200, 223, 0.6);
      }
    }
  }
}

ul li {
  .el-tree {
    .el-tree-node__content {
      height: auto;
      padding: 0 20px;
      transition: all 0.2s;
    }
    .el-tree-node__label {
      font-weight: normal;
    }
    .is-current > .el-tree-node__label {
      color: #5fc8df;
      font-weight: 600;
    }
  }
}

.tree_label {
  line-height: 28px;

  .label_index {
    background: linear-gradient(135deg, #5fc8df 0%, #409eff 100%);
    width: 22px;
    height: 22px;
    display: inline-flex;
    border-radius: 4px;

    .label_index_font {
      color: #ffffff;
      width: 100%;
      text-align: center;
    }
  }
}
</style>

<style lang="scss">
.el-select {
  .el-input__wrapper {
    background: linear-gradient(180deg, rgba(19, 31, 58, 0.6) 0%, rgba(21, 35, 69, 0.8) 100%);
    border: 1px solid rgba(95, 200, 223, 0.3);
    box-shadow: none;
    transition: all 0.3s;

    &:hover {
      border-color: rgba(95, 200, 223, 0.5);
      background: linear-gradient(180deg, rgba(19, 31, 58, 0.7) 0%, rgba(21, 35, 69, 0.9) 100%);
    }

    &.is-focus {
      border-color: #5fc8df;
      box-shadow: 0 0 8px rgba(95, 200, 223, 0.3);
    }
  }

  .el-input__inner {
    color: #94d0ff;
    
    &::placeholder {
      color: rgba(148, 208, 255, 0.5);
    }
  }

  .el-select__caret {
    color: #94d0ff;
  }

  .el-select__tags {
    .el-tag {
      background: rgba(95, 200, 223, 0.2);
      border-color: rgba(95, 200, 223, 0.4);
      color: #94d0ff;

      .el-tag__close {
        color: #94d0ff;
        
        &:hover {
          background: rgba(95, 200, 223, 0.3);
          color: #5fc8df;
        }
      }
    }
  }
}

.el-select .el-input .el-select__caret {
  color: #94d0ff;
}

.el-icon-search:before {
  color: #94d0ff;
}

.custom-select-popper {
  border: 1px solid rgba(95, 200, 223, 0.4);
  background: linear-gradient(180deg, rgba(19, 31, 58, 0.98) 0%, rgba(21, 35, 69, 0.98) 100%);
  backdrop-filter: blur(8px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.6);

  .popper__arrow {
    display: none;
  }

  .el-select-dropdown__item {
    background: transparent;
    
    &.hover {
      background-color: transparent;
    }
  }

  .el-scrollbar {
    background: transparent;
    border-radius: 3px;
    padding: 8px;
  }

  .el-tree {
    background: transparent;
    padding: 4px 0;
  }

  .el-tree-node__content {
    background: transparent;
    color: #94d0ff;
    height: 34px;
    transition: all 0.2s;
    border-radius: 4px;
    margin: 2px 0;

    &:hover {
      background: rgba(95, 200, 223, 0.2);
    }
  }

  .el-tree-node.is-current > .el-tree-node__content {
    background: rgba(95, 200, 223, 0.3);
    color: #5fc8df;
    font-weight: 500;
  }

  .el-tree-node__expand-icon {
    color: #94d0ff;
    
    &.is-leaf {
      color: transparent;
    }
  }

  .el-checkbox {
    .el-checkbox__inner {
      background: rgba(19, 31, 58, 0.8);
      border: 1px solid rgba(95, 200, 223, 0.5);
      width: 16px;
      height: 16px;

      &:hover {
        border-color: #5fc8df;
      }

      &::after {
        border-color: #fff;
        border-width: 2px;
      }
    }

    .el-checkbox__input.is-checked .el-checkbox__inner {
      background: linear-gradient(135deg, #5fc8df 0%, #409eff 100%);
      border-color: #5fc8df;
    }

    .el-checkbox__input.is-indeterminate .el-checkbox__inner {
      background: linear-gradient(135deg, rgba(95, 200, 223, 0.6) 0%, rgba(64, 158, 255, 0.6) 100%);
      border-color: #5fc8df;
    }

    .el-checkbox__label {
      color: #94d0ff;
      padding-left: 8px;
    }
  }
}
</style>
