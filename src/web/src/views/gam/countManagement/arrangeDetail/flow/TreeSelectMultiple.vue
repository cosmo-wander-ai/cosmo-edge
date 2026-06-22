<template>
  <el-select ref="select" :model-value="checkedValues" :placeholder="placeholder || t('validate.pleaseSelect', { name: '' })" @visible-change="visibleChange" size="small" filterable :filter-method="filterTree" :disabled="disabled" collapse-tags :multiple="multiple" clearable @clear="clear" @remove-tag="handleRemoveTag">
    <el-option ref="option" class="option" :value="optionData.id" :label="optionData.name">
      <el-tree ref="tree" class="tree" :node-key="nodeKey" :data="data" :props="props" default-expand-all highlight-current :show-checkbox="multiple" :expand-on-click-node="false" @node-click="handleNodeClick" @check="handleNodeCheckbox" :filter-node-method="filterLeafNode"></el-tree>
    </el-option>
  </el-select>
</template>
  
<script setup>
import { ref, watch, onMounted, nextTick } from 'vue'
import { t } from '@/i18n'

const props = defineProps({
  modelValue: {
    type: String,
    default: () => []
  },
  data: {
    type: Array,
    default: () => []
  },
  nodeKey: {
    type: [String, Number],
    default: 'id'
  },
  props: {
    type: Object,
    default: () => ({
      label: 'label',
      children: 'children'
    })
  },
  placeholder: {
    type: String,
    default: ''
  },
  onlyLeaf: {
    type: Boolean,
    default: true
  },
  multiple: {
    type: Boolean,
    default: false
  },
  disabled: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['update:modelValue'])

const select = ref(null)
const option = ref(null)
const tree = ref(null)
const filterText = ref('')
const optionData = ref({
  id: '',
  name: ''
})
const checkedValues = ref([])

watch(() => props.modelValue, (val) => {
  if (!isEmpty(props.data)) {
    init(val)
  }
})

watch(() => props.data, (val) => {
  if (!isEmpty(val)) {
    init(props.modelValue)
  }
})

watch(filterText, (val) => {
  tree.value.filter(val)
})

onMounted(() => {
  if (!isEmpty(props.data)) {
    init(props.modelValue)
  }
})

const isEmpty = (val) => {
  for (let key in val) {
    return false
  }
  return true
}

const handleNodeClick = (data, node) => {
  // checkedValues.value = tree.value.getCheckedKeys(true)
  // emit('update:modelValue', JSON.stringify(checkedValues.value))
}

const handleNodeCheckbox = (data, checked, indeterminate) => {
  checkedValues.value = tree.value.getCheckedKeys(true)
  emit('update:modelValue', JSON.stringify(checkedValues.value))
}

const init = (val) => {
  if (val) {
    nextTick(() => {
      checkedValues.value = JSON.parse(val)
      tree.value.setCheckedKeys(JSON.parse(val))
    })
  } else {
    tree.value.setCheckedKeys([])
  }
}

const visibleChange = (e) => {
  if (e) {
    let selectDom = document.querySelector('.is-current')
    setTimeout(() => {
      select.value.scrollToOption({ $el: selectDom })
    }, 0)
  }
}

const filterTree = (value) => {
  filterText.value = value
}

const filterLeafNode = (value, data) => {
  if (!value) return true
  return data.label.indexOf(value) !== -1
}

const clear = () => {
  checkedValues.value = []
  emit('update:modelValue', JSON.stringify(checkedValues.value))
}

const handleRemoveTag = (tag) => {
  checkedValues.value.pop()
  tree.value.setCheckedKeys(checkedValues.value)
  emit('update:modelValue', JSON.stringify(checkedValues.value))
}
</script>
  
<style lang="scss" scoped>
.option {
  height: auto;
  line-height: 1;
  padding: 0;
  background-color: #fff;
}

.tree {
  padding: 4px 20px;
  font-weight: 400;
}

// 限制文字长度并不换行的样式
.el-select {
  :deep(.el-select__tags-text) {
    white-space: nowrap; // 不换行
    overflow: hidden; // 超出部分隐藏
    text-overflow: ellipsis; // 显示省略号
    max-width: 65px; // 限制最大宽度，可以根据需要调整
  }
}
</style>
