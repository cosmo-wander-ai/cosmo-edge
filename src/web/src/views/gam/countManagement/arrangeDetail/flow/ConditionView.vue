<template>
  <div v-if="condition">
    <div v-if="condition.list && condition.list.length > 0" class="condition-body">
      <div class="triangle-div">
        <el-button class="condition-btn" @click="logicalBtnClick(condition)">{{ getOperationLabel(condition.type) }}</el-button>
      </div>
      <div class="condition-children">
        <div v-for="childCondition in condition.list" :key="childCondition.key">
          <recursive-condition :condition="childCondition" :flowData="flowData" :atomicList="atomicList" @onClick="onClick"></recursive-condition>
        </div>
        <div class="add-btn">
          <el-icon class="add-icon" @click="addCondition(condition)"><CirclePlus /></el-icon>
        </div>
      </div>
    </div>
    <div v-else class="condition-row" @mouseenter="mouseenterHandle($event, condition)" @mouseleave="mouseleaveHandle($event, condition)">
      <div class="condition-item">
        <!-- 左边树 -->
        <tree-select v-model="condition.keyL" class="condition-select" :data="leftData"></tree-select>
        <!-- 条件 -->
        <el-select v-model="condition.type" class="operation-select" :placeholder="t('validate.pleaseSelect', { name: '' })" filterable size="small">
          <el-option v-for="obj in getOperations(condition.rightType)" :key="obj.value" :label="obj.label" :value="obj.value">
          </el-option>
        </el-select>
        <!-- 右边关联结构 -->
        <template v-if="condition.keyL">
          <el-switch v-if="condition.rightType === 'switch'" v-model="condition.keyR" active-value="1" inactive-value="0" size="small"></el-switch>
          <el-select
            v-else-if="condition.rightType === 'select' || condition.rightType === 'select2' || condition.rightType === 'radio' || condition.rightType === 'check'"
            :multiple="condition.rightType === 'check'"
            v-model="condition.keyR"
            size="small"
            :disabled="String(condition.keyL || '').includes('aiOut') && !String(condition.keyL || '').includes('aiOut.attr')"
            class="condition-select"
            :key="condition.key"
            filterable
            collapse-tags
          >
            <el-option v-for="obj in rightData" :key="obj.id" :label="obj.label" :value="obj.id"></el-option>
          </el-select>
          <el-input v-else v-model="condition.keyR" size="small" class="condition-select" :placeholder="t('validate.pleaseEnter', { name: '' })"></el-input>
        </template>
      </div>
      <div v-show="condition.showTools" class="tool-buttons">
        <el-button @click="handleTool1(condition)" :disabled="condition.level === 3" circle>
          <el-icon><CirclePlus /></el-icon>
        </el-button>
        <el-button @click="deleteCondition(condition)" circle>
          <el-icon class="close-icon"><CircleClose /></el-icon>
        </el-button>
      </div>
    </div>
  </div>  
</template>

<script setup>
import { ref, watch, onMounted, onBeforeUnmount, getCurrentInstance, toRef } from 'vue'
import TreeSelect from '@/components/TreeSelect.vue'
import { getLogicalOperations, getOperations } from './dataTools.js'
import EventBus from '@/components/eventBus.js'
import _ from 'lodash'
import { CirclePlus, CircleClose } from '@element-plus/icons-vue'
import { t } from '@/i18n'
defineOptions({ name: 'RecursiveCondition' })

// Props
const props = defineProps({
  condition: {
    type: Object,
    default: () => null
  },
  atomicList: {
    type: Array,
    default: () => []
  }
})

// Emits
const emit = defineEmits(['onClick', 'change'])

// Data
const logicalIndex = ref(1)
const focusedView = ref(null)
const leftData = ref([])
const rightData = ref([])
const rightType = ref('')
const tasksArr = ref([])
const thresholdArr = ref([])
const categoriesLabelListObj = ref({})
const categoriesLabelListArr = ref([])
const flowData = ref([])

// Watch
watch(() => props.condition?.keyL, (newVal) => {
  console.log(props.condition, tasksArr.value, 'condition.keyL', newVal)
  if (newVal && props.condition) {
    props.condition.keyR = ''
    props.condition.type = ''
    if (typeof newVal === 'string' && newVal.includes('aiOut') && !newVal.includes('aiOut.attr')) {
      props.condition.rightType = 'select'
      let match = newVal.match(/\.([^\.]+)\./)
      if (match) {
        let betweenDots = match[1]
        props.condition.keyR = `aiParam.${betweenDots}.confidence`
      }
    } else if (
      typeof newVal === 'string' &&
      newVal.includes('aiOut.attr')
    ) {
      props.condition.rightType = 'select2'
    } else {
      const result = _.find(tasksArr.value, { key: newVal })
      props.condition.rightType = result ? result.type : 'select'
    }
    handleRightData(props.condition.keyL)
  } else if (props.condition) {
    props.condition.rightType = 'select'
  }
})

// Lifecycle
onMounted(() => {
  console.log('condition  mounted', props.condition?.keyL)
  handleTaskData()
  handleLeftData()
  handleRightData(props.condition?.keyL)
  EventBus.$on('onCondition', () => {
    if (props.condition?.rightType === 'check' && props.condition?.keyL) {
      handleRightData(props.condition.keyL)
    }
    getCurrentInstance()?.proxy?.$forceUpdate()
  })
})

onBeforeUnmount(() => {
  EventBus.$off('onCondition')
})

// Methods
const atomicListRef = toRef(props, 'atomicList')

const handleTaskData = () => {
  thresholdArr.value = []
  tasksArr.value = []
  const customMetadata = localStorage.getItem('customMetadata')
  if (!customMetadata) return
  JSON.parse(customMetadata).forEach((obj) => {
    tasksArr.value.push({
      ...obj,
      id: obj.key,
      label: obj.name,
      children: [],
      type: obj.type
    })
  })
  if (!props.flowData) return
  console.log(props.flowData,'=============')
  props.flowData.forEach((item) => {
    const metaParams = item?.configObject?.webConfig?.metaDataParams
    if (Array.isArray(metaParams) && metaParams.length > 0) {
      metaParams.forEach((obj) => {
        const subFlag = _.find(thresholdArr.value, {
          key: obj.key
        })
        if (!subFlag) {
          thresholdArr.value.push({
            id: obj.key,
            label: obj.name,
            children: [],
            type: obj.type
          })
        }
      })
    }
  })
}

const handleLeftData = () => {
  if (!props.atomicList) return
  leftData.value = []
  categoriesLabelListArr.value = []
  if (props.atomicList.length !== 0 && props.atomicList[0]?.labelList?.length > 0) {
    leftData.value = [
      {
        label: t('glossary.algConfidence'),
        id: -1,
        children: []
      }
    ]
    props.atomicList.forEach((item, index) => {
      const children = []
      if (item?.labelList.length > 0) {
        item.labelList.forEach((obj) => {
          children.push({
            id: `aiOut.${obj.class_name}.threshold`,
            label: `${obj.nameCN}（${obj.class_name}）`,
            children: [],
            type: 'select'
          })
        })
      }

      children.length &&
        leftData.value[0].children.push({
          label: `${item.atomicName}（${item.atomicCode}）`,
          id: index,
          children: children
        })

      if (item?.categoriesLabelList?.length > 0) {
        const categoriesChildren = []
        children.forEach((obj) => {
          const labelCode = obj.label.match(/（(.+?)）/)?.[1] || ''
          categoriesChildren.push({
            id: `aiOut.attr.${labelCode}`,
            label: obj.label,
            children: [],
            type: 'select2'
          })

          categoriesLabelListObj.value[labelCode] =
            item.categoriesLabelList.map((subObj) => {
              return {
                id: 'aiParam.attr.' + subObj.class_name,
                label: subObj.nameCN
              }
            })
        })
        categoriesChildren.length &&
          categoriesLabelListArr.value.push({
            label: `${item.atomicName}（${item.atomicCode}）`,
            id: index,
            children: categoriesChildren
          })
      }
    })
  }

  if (tasksArr.value.length !== 0) {
    leftData.value.push({
      label: t('glossary.taskParams'),
      id: -2,
      children: tasksArr.value
    })
  }

  console.log(
    leftData.value,
    '=====leftData====',
    categoriesLabelListObj.value
  )

  if (categoriesLabelListArr.value.length > 0) {
    leftData.value.push({
      label: t('glossary.algAttrResults'),
      id: -3,
      children: categoriesLabelListArr.value
    })
  }
}

const handleRightData = (val) => {
  if (!val) return
  let inTask = _.find(tasksArr.value, { key: val })
  let inThreshold = ''

  const isString = typeof val === 'string'

  if (isString && !val.includes('aiOut.attr')) {
    let match = val.match(/\.([^\.]+)\./)
    if (match) {
      let betweenDots = match[1]
      inThreshold = _.find(thresholdArr.value, (item) => {
        return item.id.includes(betweenDots)
      })
    }
  }
  if (
    inTask &&
    (inTask.type == 'select' ||
      inTask.type == 'radio' ||
      inTask.type == 'check')
  ) {
    rightData.value = Array.isArray(inTask.options) ? inTask.options.map((item) => {
      return { id: item.value, label: item.name }
    }) : []
  } else if (inThreshold) {
    rightData.value = thresholdArr.value
  } else if (isString && val.includes('aiOut.attr')) {
    const labelCode = val.split('.')[2]
    rightData.value = categoriesLabelListObj.value[labelCode] || []
  } else {
    rightData.value = []
  }
  console.log(rightData.value, '=====rightData====')
}

watch(
  atomicListRef,
  () => {
    handleLeftData()
    if (props.condition?.rightType === 'check' && props.condition?.keyL) {
      handleRightData(props.condition.keyL)
    }
  },
  { deep: true, immediate: true }
)

const mouseenterHandle = (e, item) => {
  item.showTools = true
  focusedView.value = e.target
}

const mouseleaveHandle = (e, item) => {
  item.showTools = false
  if (focusedView.value) {
    focusedView.value.blur()
    focusedView.value = null
  }
}

const logicalBtnClick = (condition) => {
  condition.type++
  if (condition.type > getLogicalOperations().length) {
    condition.type = 1
  }
}

const getOperationLabel = (type) => {
  const result = _.find(getLogicalOperations(), {
    value: type
  })
  return result.label ? result.label : ''
}

const handleTool1 = (item) => {
  item.showTools = false
  emit('onClick', { type: 1, onNode: item })
}

const addCondition = (item) => {
  emit('onClick', { type: 2, onNode: item })
}

const deleteCondition = (item) => {
  emit('onClick', { type: 99, onNode: item })
}

const onClick = (obj) => {
  emit('onClick', obj)
}
</script>

<style lang="scss" scoped>
.condition-body {
  display: flex;
  align-items: center;
}

.condition-children {
  padding-left: 5px;
  margin-bottom: 5px;
  box-sizing: border-box;
  border-left: 2px solid #1890ff;
  border-top-left-radius: 5%;
  border-bottom-left-radius: 5%;
}

.condition-btn {
  padding: 8px;
}

.condition-row {
  display: flex;
  align-items: center;
}

.condition-item {
  padding: 2px;

  .el-input {
    width: 110px;
  }
}

.triangle-div::after {
  content: '';
  display: inline-block;
  width: 0;
  height: 0;
  border-top: 5px solid transparent;
  border-bottom: 5px solid transparent;
  border-right: 5px solid #1890ff; /* 可以根据需要设置三角形的颜色 */
  margin-left: 5px; /* 可以根据需要调整三角形与按钮之间的距离 */
}

.tool-buttons {
  margin-left: 5px;

  .el-button {
    background: transparent;
    border: none;
    padding: 0;
    margin: 0;
    color: #499df3;
    font-size: 22px;
  }

  .close-icon {
    color: red;
  }
}

.condition-select {
  width: 100px;
  :deep(.el-input__inner) {
    padding: 0 25px 0 8px;
  }
}

.operation-select {
  width: 90px;

  :deep(.el-input__inner) {
    padding: 0 25px 0 8px;
  }
}

.add-btn {
  .el-icon-plus {
    padding: 5px;
    font-size: 22px;
    color: #1890ff;
    cursor: pointer;
  }
}

:deep(.el-tag--mini) {
  padding: 0 1px;
}
</style>
