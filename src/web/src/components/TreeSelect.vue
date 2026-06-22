<template>
  <el-select 
    :key="renderKey"
    ref="selectRef" 
    :model-value="modelValue" 
    :placeholder="placeholder || t('event.pleaseSelect')" 
    @visible-change="visibleChange" 
    size="small" 
    filterable 
    :filter-method="filterTree" 
    :clearable="clearable" 
    :disabled="disabled" 
    @clear="clear"
  >
    <el-option 
      ref="optionRef" 
      class="option" 
      :value="optionData.id" 
      :label="optionData.name"
    >
      <el-tree 
        ref="treeRef" 
        class="tree" 
        :node-key="nodeKey" 
        :data="data" 
        :props="props" 
        default-expand-all 
        highlight-current 
        :expand-on-click-node="false" 
        @node-click="handleNodeClick" 
        :filter-node-method="filterLeafNode"
      />
    </el-option>
  </el-select>
</template>
  
<script setup>
import { ref, watch, nextTick, onMounted } from 'vue'
import { t } from '@/i18n'

const props = defineProps({
  // v-model绑定
  modelValue: {
    type: [String, Number],
    default: ''
  },
  // 树形的数据
  data: {
    type: Array,
    default: () => []
  },
  // 每个树节点用来作为唯一标识的属性
  nodeKey: {
    type: [String, Number],
    default: 'id'
  },
  // tree的props配置
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
  // 是否显示全路径
  showFullPath: {
    type: Boolean,
    default: false
  },
  // 是否只能选择最后一级
  onlyLeaf: {
    type: Boolean,
    default: true
  },
  disabled: {
    type: Boolean,
    default: false
  },
  clearable: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['update:modelValue'])

const selectRef = ref(null)
const treeRef = ref(null)
const optionRef = ref(null)
const filterText = ref('')
const optionData = ref({
  id: '',
  name: ''
})
const renderKey = ref(0)

// 是否为空
const isEmpty = (val) => {
  if (!val) return true
  if (Array.isArray(val)) return val.length === 0
  if (typeof val === 'object') {
    for (let key in val) {
      return false
    }
    return true
  }
  return false
}

// 处理节点点击
const handleNodeClick = (data, node) => {
  if (
    props.onlyLeaf &&
    data[props.props.children] &&
    data[props.props.children].length > 0
  ) {
    node.expanded = !node.expanded
    return
  }
  
  const label = props.props.label || 'name'
  emit('update:modelValue', data[props.nodeKey])
  
  if (props.showFullPath) {
    const fullPath = getFullPath(node, label)
    optionData.value.name = fullPath
  } else {
    optionData.value.name = data[label]
  }

  optionData.value.id = data[props.nodeKey]
  filterText.value = '' // 清除过滤文本
  treeRef.value.filter('') // 清除树的过滤状态
  // 关闭下拉
  nextTick(() => {
    if (selectRef.value && typeof selectRef.value.blur === 'function') {
      selectRef.value.blur()
    }
    if (selectRef.value && typeof selectRef.value.toggleMenu === 'function') {
      selectRef.value.toggleMenu()
    }
    if (selectRef.value && 'visible' in selectRef.value) {
      selectRef.value.visible = false
    }
    renderKey.value += 1
  })
}

// 初始化
const init = (val) => {
  if (val) {
    nextTick(() => {
      const label = props.props.label || 'name'
      treeRef.value.setCurrentKey(val)
      const node = treeRef.value.getNode(val)
      if (node) {
        optionData.value.id = val
        if (props.showFullPath) {
          const fullPath = getFullPath(node, label)
          optionData.value.name = fullPath
        } else {
          optionData.value.name = node.label
        }
      }
    })
  } else {
    treeRef.value?.setCurrentKey(null)
  }
}

// 下拉框显示/隐藏
const visibleChange = (e) => {
  if (e) {
    setTimeout(() => {
      const dropdowns = document.querySelectorAll('.el-select-dropdown__wrap')
      dropdowns.forEach((dropdown) => {
        if (dropdown.offsetParent !== null) {
          dropdown.scrollTop = 0

          // 如果有选中值，滚动到选中节点位置
          if (props.modelValue) {
            const selectedNode = dropdown.querySelector('.is-current')
            if (selectedNode) {
              selectedNode.scrollIntoView({
                block: 'center',
                behavior: 'auto'
              })
            }
          }
        }
      })
    }, 50)
  } else {
    filterText.value = ''
    treeRef.value?.filter('')
  }
}

// 获取完整路径
const getFullPath = (node, label) => {
  let path = node.data[label]
  let parent = node.parent
  while (parent && parent.data && parent.level > 0) {
    path = parent.data[label] + '/' + path
    parent = parent.parent
  }
  return path
}

// 过滤树
const filterTree = (value) => {
  filterText.value = value
}

// 过滤叶子节点
const filterLeafNode = (value, data) => {
  if (!value) return true
  return data.label.indexOf(value) !== -1
}

// 清除
const clear = () => {
  emit('update:modelValue', '')
}

// 监听 modelValue 变化
watch(() => props.modelValue, (val) => {
  if (!isEmpty(props.data)) {
    init(val)
  }
})

// 监听 data 变化
watch(() => props.data, (val) => {
  if (!isEmpty(val)) {
    init(props.modelValue)
  }
})

// 监听 filterText 变化
watch(filterText, (val) => {
  treeRef.value?.filter(val)
})

// 组件挂载
onMounted(() => {
  if (!isEmpty(props.data)) {
    init(props.modelValue)
  }
})
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
</style>
