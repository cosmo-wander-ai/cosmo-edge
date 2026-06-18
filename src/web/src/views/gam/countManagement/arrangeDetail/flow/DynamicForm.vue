<template>
  <div class="form-body">
    <el-form label-position="left">
      <div v-for="(item, index) in paramConfigs" :key="index">
        <el-form-item v-if="!specialTypes.includes(item.type) && showFormItem(item)" class="form-flex" :prop="item.key">
          <template #label>
            <span style="display:inline-block;">
              {{ resolveI18nText(item, 'name') }}
              <el-tooltip effect="dark" :content="resolveI18nText(item, 'description')" placement="top">
                <el-icon class="help-icon">
                  <QuestionFilled />
                </el-icon>
              </el-tooltip>
            </span>
          </template>

          <!-- 下拉选择框 -->
          <div v-if="item.type.includes('modelSelect')">
            <el-select v-model="item.value" class="form-content" @change="modelSelectChange" :placeholder="t('validate.pleaseSelect', { name: '' })" filterable size="small">
              <el-option v-for="obj in atomicModelList" :key="obj.atomicCode" :label="`${obj.atomicName}（${obj.atomicCode}）`" :value="obj.atomicCode">
              </el-option>
            </el-select>
          </div>

          <div v-else-if="item.type === 'trackSelect'">
            <el-select v-model="item.value" class="form-content" @change="trackSelectChange" :placeholder="t('validate.pleaseSelect', { name: '' })" filterable size="small">
              <el-option v-for="obj in options" :key="obj.value" :label="obj.name" :value="obj.value">
              </el-option>
            </el-select>
          </div>

          <div v-else-if="item.type === 'select'">
            <el-select v-model="item.value" class="form-content" :placeholder="t('validate.pleaseSelect', { name: '' })" filterable size="small">
              <el-option v-for="obj in item.options" :key="obj.value" :label="resolveI18nOptionLabel(obj)" :value="obj.value">
              </el-option>
            </el-select>
          </div>

          <!-- 单选框 -->
          <div v-else-if="item.type === 'radio'">
            <el-radio-group v-model="item.value" size="small">
              <el-radio v-for="obj in item.options" :key="obj.value" :value="obj.value">{{ resolveI18nOptionLabel(obj) }}</el-radio>
            </el-radio-group>
          </div>

          <!-- 开关 -->
          <div v-else-if="item.type=== 'switch'">
            <el-switch v-model="item.value" active-value="1" inactive-value="0"></el-switch>
          </div>

          <!-- 输入框 -->
          <div v-else-if="item.type=== 'text'">
            <el-input v-model="item.value" class="form-content" size="small"></el-input>
          </div>

          <!-- 树选择器多选 -->
          <div v-else-if="item.type === 'labelsSelect'">
            <tree-select-multiple v-model="item.value" :data="treeSelectData" :multiple="true"></tree-select-multiple>
          </div>

        </el-form-item>

        <!-- labelFilterList 标签过滤定制组件 -->
        <el-form-item v-else-if="(item.type === 'labelFilterList' || item.type === 'labelAdjust') && showFormItem(item)" :prop="item.key" label-width="90px">
          <template #label>
            <span style="display:inline-block;">
              {{ resolveI18nText(item, 'name') }}
              <el-tooltip effect="dark" :content="resolveI18nText(item, 'description')" placement="top">
                <el-icon class="help-icon">
                  <QuestionFilled />
                </el-icon>
              </el-tooltip>
            </span>
          </template>

          <div class="collapse-body">
            <div v-for="(subItem, index) in targetLabelArr" :key="index">
              <div class="collapse-top">
                <el-icon class="collapse-arrow" @click="toggleCollapse(subItem)">
                  <ArrowDown v-if="subItem.collapse" />
                  <ArrowRight v-else />
                </el-icon>
                <span class="target-label">{{ t('glossary.targetLabel') }}</span>
                <el-select v-model="subItem.labelCode" class="collapse-select" size="small" @change="collapseSelectChange(subItem)">
                  <div v-for="obj in labelFilterList" :key="obj.atomicCode">
                    <div class="group-span">{{ `${obj.atomicName}（${obj.atomicCode}）`}}</div>
                    <div>
                      <el-option v-for="subObj in obj.labelList" :key="subObj.label" :label="`${subObj.nameCN}（${subObj.class_name}）`" :value="subObj.class_name">
                      </el-option>
                    </div>
                  </div>
                </el-select>
                <el-icon class="plus-icon" @click="addTargetLabel">
                  <CirclePlus />
                </el-icon>
                <el-icon class="close-icon" @click="deleteTargetLabel(index)">
                  <CircleClose />
                </el-icon>
              </div>
              <div class="collapse-content" v-show="subItem.collapse">
                <div v-if="item.type === 'labelFilterList'">
                  <span>{{ t('glossary.minSizeEnabled') }}{{ localeColon }}</span>
                  <el-switch v-model="subItem.sideMinIsEnable" active-value="1" inactive-value="0"></el-switch>
                </div>
                <div v-else-if="item.type === 'labelAdjust'">
                  <span>{{ t('glossary.confidenceAdjust') }}{{ localeColon }}</span>
                  <el-radio-group v-model="subItem.confidenceValue">
                    <el-radio value="1">{{ t('glossary.positiveAdjust') }}</el-radio>
                    <el-radio value="0">{{ t('glossary.noAdjust') }}</el-radio>
                    <el-radio value="-1">{{ t('glossary.negativeAdjust') }}</el-radio>
                  </el-radio-group>
                </div>
              </div>
            </div>
          </div>
        </el-form-item>

        <!-- labelList 标签过滤定制组件 -->
        <el-form-item v-else-if="item.type === 'labelList' && showFormItem(item)" :prop="item.key">
          <template #label>
            <span style="display:inline-block;">
              {{ resolveI18nText(item, 'name') }}
              <el-tooltip effect="dark" :content="resolveI18nText(item, 'description')" placement="top">
                <el-icon class="help-icon">
                  <QuestionFilled />
                </el-icon>
              </el-tooltip>
            </span>
          </template>

          <el-select v-model="item.value" class="form-content" size="small">
            <div v-for="obj in labelFilterList" :key="obj.atomicCode">
              <div class="group-span">{{ `${obj.atomicName}（${obj.atomicCode}）`}}</div>
              <div>
                <el-option v-for="subObj in obj.labelList" :key="subObj.label" :label="`${subObj.nameCN}（${subObj.class_name}）`" :value="subObj.class_name">
                </el-option>
              </div>
            </div>
          </el-select>
        </el-form-item>

        <!--  labelTargetLimit 标签目标数量定制组件-->
        <el-form-item v-else-if="item.type === 'labelTargetLimit' && showFormItem(item)" :prop="item.key" label-width="90px">
          <template #label>
            <span style="display:inline-block;">
              {{ resolveI18nText(item, 'name') }}
              <el-tooltip effect="dark" :content="resolveI18nText(item, 'description')" placement="top">
                <el-icon class="help-icon">
                  <QuestionFilled />
                </el-icon>
              </el-tooltip>
            </span>
          </template>

          <div class="collapse-body">
            <div v-for="(item, index) in targetLimitLabelArr" :key="index">
              <div class="collapse-top">
                <el-icon class="collapse-arrow" @click="toggleCollapse(item)">
                  <ArrowDown v-if="item.collapse" />
                  <ArrowRight v-else />
                </el-icon>
                <span class="target-label">{{ t('glossary.targetLabel') }}</span>
                <el-select v-model="item.labelCode" class="collapse-select" size="small" @change="collapseSelectChange(item, 'labelTargetLimit')">
                  <div v-for="obj in atomicList" :key="obj.atomicCode">
                    <div class="group-span">{{ `${obj.atomicName}（${obj.atomicCode}）`}}</div>
                    <div>
                      <el-option v-for="subObj in obj.labelList" :key="subObj.label" :label="`${subObj.nameCN}（${subObj.class_name}）`" :value="subObj.class_name">
                      </el-option>
                    </div>
                  </div>
                </el-select>
                <el-icon class="plus-icon" @click="addTargetLabel('labelTargetLimit')">
                  <CirclePlus />
                </el-icon>
                <el-icon class="close-icon" @click="deleteTargetLabel(index,'labelTargetLimit')">
                  <CircleClose />
                </el-icon>
              </div>
              <div class="collapse-content" v-show="item.collapse">
                <span>{{ t('glossary.alertCondition') }}{{ localeColon }}</span>
                <el-select v-model="item.targetCondition" style="width:130px;" size="small">
                  <el-option v-for="obj in warnConditions" :key="obj.value" :label="resolveI18nOptionLabel(obj)" :value="obj.value"></el-option>
                </el-select>
                <span style="margin-left:10px;">{{ t('glossary.targetCount') }}{{ localeColon }}</span>
                <el-input v-model="item.targetNum" style="width:130px;" size="small" type="number"></el-input>
              </div>
            </div>
          </div>
        </el-form-item>

        <!-- modelSelect 定制化组件  -->
        <div class="select-table-body" v-if="item.type.includes('modelSelect') && !['modelSelect_dino','modelSelect_sam2','modelSelect_qwen3vl','modelSelect_qwen3_5'].includes(item.type)">
          <div class="select-table-title">{{ t('glossary.selectLabel') }}</div>
          <el-table :data="labelList" border size="small">
            <el-table-column type="index" :label="t('field.no')" align="center" width="70">
            </el-table-column>
            <el-table-column prop="nameCN" :label="t('field.name')" align="center">
            </el-table-column>
            <el-table-column prop="class_name" :label="t('glossary.classId')" align="center">
            </el-table-column>
            <el-table-column prop="used" :label="t('glossary.isUsed')" align="center">
              <template #default="scope">
                <el-checkbox v-model="scope.row.used"></el-checkbox>
              </template>
            </el-table-column>
          </el-table>
        </div>

        <div v-if="item.type === 'taskList'" class="select-table-body">
          <div class="select-table-title">{{ t('glossary.selectAlgorithm') }}</div>
          <tree-transfer ref="transferRef" v-model:fromData="fromData" v-model:toData="toData" :titleList="[t('glossary.unselectedAlg'), t('glossary.selectedAlg')]" :defaultProps="{
              id: 'id',
              parentId: 'parentId',
              label: 'label',
              children: 'children',
              disabled: 'disabled'
            }" rootPid="0" @add="handleAdd" @remove="handleremove">
            <template #from-title>{{ t('glossary.unselectedAlg') }} {{ countLeafNodes(fromData) }}</template>
            <template #to-title>{{ t('glossary.selectedAlg') }} {{ countLeafNodes(toData) }}</template>
          </tree-transfer>
        </div>

        <!-- 条件判断定制组件  -->
        <div v-else-if="item.type === 'condition'">
          <condition-view ref="conditionRef" :condition="condition" :flowData="flowData" :atomicList="atomicList" @onClick="onClick"></condition-view>
        </div>
      </div>
    </el-form>
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount, getCurrentInstance, watch, computed, toRef } from 'vue'
import ConditionView from './ConditionView.vue'
import { v4 } from 'uuid'
import EventBus from '@/components/eventBus.js'
import TreeSelectMultiple from './TreeSelectMultiple.vue'
import TreeTransfer from 'tree-transfer-vue3'
import _ from 'lodash'
import {
  QuestionFilled,
  ArrowDown,
  ArrowRight,
  CirclePlus,
  CircleClose
} from '@element-plus/icons-vue'
import { t, localeColon } from '@/i18n'
import { resolveI18nText, resolveI18nOptionLabel } from '@/utils/i18nResource'

// Props
const props = defineProps({
  flowData: {
    type: Object,
    default: () => ({})
  },
  actionDetail: {
    type: Object,
    required: true
  },
  atomicList: {
    type: Array,
    default: () => []
  }
})
const emit = defineEmits(['config-change'])

// Get instance
const { proxy } = getCurrentInstance()
const $API = proxy.$API

// Refs
const conditionRef = ref(null)

// Data
const specialTypes = ref([
  'labelFilterList',
  'labelList',
  'labelAdjust',
  'labelTargetLimit',
  'taskList'
])
const condition = ref(null)
const value = ref('')
const options = ref([])
const paramConfigs = ref([])
const tableData = ref([1, 2, 3])
const atomicModelList = ref([])
const selectedAtomic = ref({})
const selectedTrackAtomic = ref({})
const labelFilterList = ref([])
const labelList = ref([])
const categoriesLabelList = ref([])
const targetLabelArr = ref([
  {
    atomicCode: '',
    labelCode: '',
    labelName: '',
    sideMinIsEnable: '0',
    collapse: true,
    confidenceValue: '1'
  }
])
const modelSelectType = ref('')
const labelFilterListType = ref(false)
const labelAdjustType = ref(false)
const labelListType = ref(false)
const trackSelectType = ref(false)
const conditionType = ref(false)
const treeSelectData = ref([])
const labelTargetLimitType = ref(false)
const warnConditions = ref([
  {
    name: t('glossary.warnLtTarget'),
    value: '0'
  },
  {
    name: t('glossary.warnGtTarget'),
    value: '1'
  },
  {
    name: t('glossary.warnLeTarget'),
    value: '2'
  },
  {
    name: t('glossary.warnGeTarget'),
    value: '3'
  },
  {
    name: t('glossary.warnEqTarget'),
    value: '4'
  }
])
const targetLimitLabelArr = ref([
  {
    atomicCode: '',
    labelCode: '',
    labelName: '',
    targetCondition: '',
    targetNum: '',
    collapse: true
  }
])

const fromData = ref([])
const toData = ref([])
const transferRef = ref(null)
const formReady = ref(false)

// Lifecycle
onMounted(() => {
  const params = JSON.parse(props.actionDetail.inputParamConfig)
  params.forEach((item, index) => {
    const input = _.find(props.actionDetail.configObject?.params, {
      key: item.key
    })
    if (input) {
      paramConfigs.value.push({
        ...item,
        value: input.value
      })
    } else {
      paramConfigs.value.push({
        ...item,
        value: item.defaultValue || ''
      })
    }

    item.key === 'data' && getAudioFile(index)
    item.key === 'deviceSN' && getAudioDevice(index)
  })

  console.log(paramConfigs.value, '=============paramConfigs', props.atomicList)

  const modelSelect = _.find(paramConfigs.value, function (obj) {
    return obj.type.includes('modelSelect')
  })
  if (modelSelect) {
    modelSelectType.value = modelSelect.type
    if (modelSelect.value) {
      getModelSelectList(modelSelect.value, modelSelect.type)
    } else {
      getModelSelectList('', modelSelect.type)
    }
  }

  const labelFilterListItem = _.find(paramConfigs.value, {
    type: 'labelFilterList'
  })
  if (labelFilterListItem) {
    labelFilterListType.value = true
    labelFilterList.value = props.atomicList
    const targetLabelArrTemp = []
    props.actionDetail.configObject?.webConfig?.labelFilterList.forEach(
      (item) => {
        targetLabelArrTemp.push({
          ...item,
          collapse: true
        })
      }
    )
    if (targetLabelArrTemp.length > 0) {
      targetLabelArr.value = targetLabelArrTemp
    }
  }

  const labelAdjust = _.find(paramConfigs.value, {
    type: 'labelAdjust'
  })
  if (labelAdjust) {
    labelAdjustType.value = true
    labelFilterList.value = props.atomicList
    const targetLabelArrTemp = []
    props.actionDetail.configObject.params.forEach((item) => {
      if (item.key.includes('preParam.confidence'))
        targetLabelArrTemp.push({
          labelCode: item.key.split('.')[2],
          confidenceValue: item.value,
          collapse: true
        })
    })
    if (targetLabelArrTemp.length > 0) {
      targetLabelArr.value = targetLabelArrTemp
    }
  }

  const labelListItem = _.find(paramConfigs.value, {
    type: 'labelList'
  })
  if (labelListItem) {
    labelListType.value = true
    labelFilterList.value = props.atomicList
  }

  const trackSelect = _.find(paramConfigs.value, { type: 'trackSelect' })
  if (trackSelect) {
    trackSelectType.value = true
    if (trackSelect.value) {
      const result = _.find(props.atomicList, {
        atomicCode: trackSelect.value
      })
      if (result) {
        selectedTrackAtomic.value = result
      } else {
        selectedTrackAtomic.value = null
        trackSelect.value = ''
      }
    }
    props.atomicList.forEach((item) => {
      options.value.push({
        name: `${item.atomicName}（${item.atomicCode}）`,
        value: item.atomicCode
      })
    })
  }

  const labelTargetLimit = _.find(paramConfigs.value, {
    type: 'labelTargetLimit'
  })
  if (labelTargetLimit) {
    labelTargetLimitType.value = true
    const targetLimitLabelArrTemp = []
    const resultTargetTypeArr = _.filter(
      props.actionDetail.configObject.params,
      (item) => {
        return item.key.startsWith('areaLimitTargetType')
      }
    )
    const resultTargetCountArr = _.filter(
      props.actionDetail.configObject.params,
      (item) => {
        return item.key.startsWith('areaLimitTargetCount')
      }
    )
    const allLabelCodes = []
    props.atomicList.forEach((item) => {
      item.labelList.forEach((label) => {
        allLabelCodes.push(label.class_name)
      })
    })
    resultTargetTypeArr.forEach((item) => {
      if (allLabelCodes.includes(item.key.split('.')[1])) {
        targetLimitLabelArrTemp.push({
          atomicCode: '',
          labelCode: item.key.split('.')[1],
          labelName: '',
          targetCondition: item.value,
          targetNum: '',
          collapse: true
        })
      }
    })
    targetLimitLabelArrTemp.forEach((item) => {
      const resultTargetCount = _.find(resultTargetCountArr, {
        key: 'areaLimitTargetCount.' + item.labelCode
      })
      if (resultTargetCount) {
        item.targetNum = resultTargetCount.value
      }
    })
    if (targetLimitLabelArrTemp.length > 0) {
      targetLimitLabelArr.value = targetLimitLabelArrTemp
    }
  }

  const labelsSelect = _.find(paramConfigs.value, { type: 'labelsSelect' })
  if (labelsSelect) {
    handleTreeSelectData()
  }

  const conditionItem = _.find(paramConfigs.value, { type: 'condition' })
  if (conditionItem) {
    conditionType.value = true
    if (
      !props.actionDetail.configObject?.condition ||
      Object.keys(props.actionDetail.configObject.condition).length === 0
    ) {
      condition.value = {
        key: v4().slice(0, 8),
        level: 1,
        type: null,
        keyL: null,
        keyR: null,
        list: [],
        showTools: false
      }
    } else {
      condition.value = props.actionDetail.configObject.condition
    }
  }

  const taskList = _.find(paramConfigs.value, { type: 'taskList' })
  if (taskList) {
    getChannelList(taskList.value)
  }
  formReady.value = true
})

const atomicListRef = toRef(props, 'atomicList')

const emitConfigChange = _.debounce(() => {
  if (!formReady.value) return
  const config = submitForm({ persist: false })
  if (config) emit('config-change', config)
}, 100)

onBeforeUnmount(() => {
  emitConfigChange.cancel()
})

const getAudioFile = (index) => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  $API.queryAudioFile(params).then((res) => {
    const { resData } = res || {}
    const options = (resData?.audioFileList || []).map((item) => ({
      name: item.fileName,
      value: item.fileId
    }))
    if (paramConfigs.value[index]) {
      paramConfigs.value[index].options = options
    }
  })
}

const getAudioDevice = (index) => {
  const params = {
    checkAlive: false,
    pageNum: 1,
    pageSize: 1000
  }
  $API.queryAudioDevice(params).then((res) => {
    const { resData } = res || {}
    const options = (resData?.audioDevList || []).map((item) => ({
      name: item.name,
      value: item.devId
    }))
    if (paramConfigs.value[index]) {
      paramConfigs.value[index].options = options
    }
  })
}

const getChannelList = (toValue) => {
  let checkedChannel = []
  if (toValue) {
    try {
      checkedChannel = JSON.parse(toValue)
    } catch (e) {
      checkedChannel = []
    }
  }
  const params = {
    pageSize: 1000,
    pageNum: 1,
    channelStatus: -1
  }
  $API.getChannelList(params).then((res) => {
    const { resData } = res || {}
    const fromList = []
    const toList = []
    ;(resData?.rows || []).forEach((item) => {
      const channelId = String(item.videoChannelId || '')
      const channelName = item.channelName || ''
      const fromChildren = []
      const toChildren = []
      ;(item?.taskList || []).forEach((task) => {
        const algorithmId = String(task.algorithmId || '')
        const node = {
          id: `${channelId}-${algorithmId}`,
          parentId: channelId,
          label: task.algorithmName,
          channelId,
          algorithmId,
          children: []
        }
        const hit = _.find(
          checkedChannel,
          (c) =>
            String(c?.channelId || '') === channelId &&
            String(c?.algorithmId || '') === algorithmId
        )
        if (hit) {
          toChildren.push(node)
        } else {
          fromChildren.push(node)
        }
      })
      if (fromChildren.length) {
        fromList.push({
          id: channelId,
          parentId: '0',
          label: channelName,
          disabled: true,
          children: fromChildren
        })
      }
      if (toChildren.length) {
        toList.push({
          id: channelId,
          parentId: '0',
          label: channelName,
          disabled: true,
          children: toChildren
        })
      }
    })
    fromData.value = fromList
    toData.value = toList
  })
}

// 统计叶子节点数量
const countLeafNodes = (data) => {
  let count = 0
  if (!data || !Array.isArray(data)) return 0
  data.forEach((node) => {
    // 判断是否为叶子节点（没有children或children为空数组）
    if (
      !node.children ||
      !Array.isArray(node.children) ||
      node.children.length === 0
    ) {
      count++
    } else {
      // 递归统计子节点
      count += countLeafNodes(node.children)
    }
  })
  return count
}

const collectToSelection = () => {
  const result = []
  const walk = (nodes) => {
    ;(nodes || []).forEach((n) => {
      if (n.children && n.children.length) {
        walk(n.children)
      } else if (n.algorithmId && n.channelId) {
        result.push({
          channelId: String(n.channelId),
          algorithmId: String(n.algorithmId)
        })
      }
    })
  }
  walk(toData.value || [])
  return result
}

const handleAdd = () => {
  const list = collectToSelection()
  const param = _.find(paramConfigs.value, { key: 'strageAlgorithms' })
  if (param) {
    param.value = JSON.stringify(list)
  }
}

const handleremove = () => {
  const list = collectToSelection()
  const param = _.find(paramConfigs.value, { key: 'strageAlgorithms' })
  if (param) {
    param.value = JSON.stringify(list)
  }
}

// Methods
const getModelSelectList = (code, type) => {
  const arr = type.split('_')
  const params = {
    modelName: '',
    modelType: '',
    filePath: ''
  }
  if (arr.length > 1) {
    params.modelType = arr[1]
  }

  // qwen3vl 组件同时查询 qwen3_5 模型
  const needMergeQwen35 = (params.modelType === 'qwen3vl')

  const fetchList = (mt) => {
    return $API.atomicModelList({ ...params, modelType: mt }).then((res) => {
      return res?.resData?.list || []
    })
  }

  const promises = [fetchList(params.modelType)]
  if (needMergeQwen35) {
    promises.push(fetchList('qwen3_5'))
  }

  Promise.all(promises).then((results) => {
    let merged = results[0]
    if (results.length > 1) {
      merged = merged.concat(results[1])
    }
    atomicModelList.value = merged
    console.log(atomicModelList.value, '-llllllll')
    const atomic = _.find(atomicModelList.value, {
      atomicCode: code
    })
    if (atomic) {
      selectedAtomic.value = atomic
      let labelListTemp = []

      if (type === 'modelSelectCategories_classify') {
        if (!atomic.categories) return
        labelListTemp = JSON.parse(atomic.categories)
        atomic.label && (categoriesLabelList.value = JSON.parse(atomic.label))
      } else {
        if (!atomic.label) return
        labelListTemp = JSON.parse(atomic.label)
      }
      labelListTemp &&
        labelListTemp.forEach((item) => {
          const flag = _.find(
            props.actionDetail?.configObject?.webConfig.labelList,
            {
              class_name: item.class_name
            }
          )
          labelList.value.push({
            ...item,
            used: flag ? true : false
          })
        })
    }
  })
}

// 隐藏的参数 key 列表（保留后端接口，前端不展示）
const hiddenParamKeys = ['alarmProperty']

const isDependsOnSatisfied = (obj, visited = new Set()) => {
  if (!obj?.dependsOn || obj.dependsOn.key === '') return true
  if (visited.has(obj.key)) return false
  visited.add(obj.key)
  const dependsOn = _.find(paramConfigs.value, { key: obj.dependsOn.key })
  if (!dependsOn || obj.dependsOn.value != dependsOn.value) return false
  return isDependsOnSatisfied(dependsOn, visited)
}

const showFormItem = (obj) => {
  if (obj.level === '2') return false
  if (hiddenParamKeys.includes(obj.key)) return false
  return isDependsOnSatisfied(obj)
}

const handleTreeSelectData = () => {
  let treeData = []
  props.atomicList.forEach((item) => {
    let children = []
    item.labelList.forEach((label) => {
      children.push({
        id: label.class_name,
        label: `${label.nameCN}（${label.class_name}）`
      })
    })
    treeData.push({
      id: item.atomicCode,
      label: `${item.atomicName}（${item.atomicCode}）`,
      children
    })
  })
  treeSelectData.value = [
    {
      id: '0',
      label: t('common.all'),
      children: treeData
    }
  ]
}

const modelSelectChange = (val) => {
  labelList.value = []
  const atomic = _.find(atomicModelList.value, { atomicCode: val })
  selectedAtomic.value = atomic

  let labelListTemp = []
  if (modelSelectType.value === 'modelSelectCategories_classify') {
    if (atomic.categories) {
      labelListTemp = JSON.parse(atomic.categories)
      atomic.label && (categoriesLabelList.value = JSON.parse(atomic.label))
    }
  } else {
    if (atomic.label) {
      labelListTemp = JSON.parse(atomic.label)
    }
  }
  labelListTemp &&
    labelListTemp.forEach((item) => {
      labelList.value.push({
        ...item,
        used: false
      })
    })
  emitAtomicUpdate()
}

const trackSelectChange = (val) => {
  const select = _.find(props.atomicList, { atomicCode: val })
  selectedTrackAtomic.value = select
}

const toggleCollapse = (obj) => {
  obj.collapse = !obj.collapse
}

// 向编排实时同步 atomicList（位置、原子信息、选中的标签）
const emitAtomicUpdate = () => {
  const resultFilter = (labelList.value || [])
    .filter((i) => i.used)
    .map((i) => ({
      ...i
    }))
  const position = props.actionDetail?.flowActionId
  const atomicCode =
    selectedAtomic.value?.atomicCode || selectedAtomic.value?.modelCode || ''
  const atomicName =
    selectedAtomic.value?.atomicName || selectedAtomic.value?.modelName || ''
  if (position && (atomicCode || resultFilter.length)) {
    EventBus.$emit('flow:atomic:update', {
      position,
      atomicCode,
      atomicName,
      labelList: resultFilter
    })
  }
}

watch(
  () => labelList.value.map((i) => i.used),
  () => {
    emitAtomicUpdate()
  },
  { deep: true }
)

watch(
  atomicListRef,
  (val) => {
    if (labelFilterListType.value || labelAdjustType.value || labelListType.value) {
      labelFilterList.value = Array.isArray(val) ? val : []
    }
    if (trackSelectType.value) {
      options.value = []
      ;(Array.isArray(val) ? val : []).forEach((item) => {
        options.value.push({
          name: `${item.atomicName}（${item.atomicCode}）`,
          value: item.atomicCode
        })
      })
    }
  },
  { deep: true, immediate: true }
)

watch(
  [
    paramConfigs,
    targetLabelArr,
    targetLimitLabelArr,
    condition,
    labelList,
    selectedAtomic,
    selectedTrackAtomic,
    toData
  ],
  () => {
    emitConfigChange()
  },
  { deep: true }
)

const addTargetLabel = (type) => {
  if (type === 'labelTargetLimit') {
    targetLimitLabelArr.value.push({
      atomicCode: '',
      labelCode: '',
      labelName: '',
      targetCondition: '',
      targetNum: '',
      collapse: true
    })
  } else {
    targetLabelArr.value.push({
      atomicCode: '',
      labelCode: '',
      labelName: '',
      position: '',
      sideMinIsEnable: '0',
      confidenceValue: '1',
      collapse: true
    })
  }
}

const deleteTargetLabel = (index, type) => {
  if (type === 'labelTargetLimit') {
    if (targetLimitLabelArr.value.length == 1) {
      targetLimitLabelArr.value[0] = {
        atomicCode: '',
        labelCode: '',
        labelName: '',
        targetCondition: '',
        targetNum: '',
        collapse: true
      }
    } else {
      targetLimitLabelArr.value.splice(index, 1)
    }
  } else {
    if (targetLabelArr.value.length == 1) {
      targetLabelArr.value[0] = {
        atomicCode: '',
        labelCode: '',
        labelName: '',
        sideMinIsEnable: '0',
        confidenceValue: '1',
        collapse: true
      }
    } else {
      targetLabelArr.value.splice(index, 1)
    }
  }
}

const collapseSelectChange = (item, type) => {
  if (type === 'labelTargetLimit') {
    item.targetCondition = ''
    item.targetNum = ''
  } else {
    const result = _.find(labelFilterList.value, (obj) =>
      _.some(obj.labelList, { class_name: item.labelCode })
    )
    const specificObject = _.find(result.labelList, {
      class_name: item.labelCode
    })
    if (specificObject) {
      item.position = specificObject.position
      item.labelName = specificObject.nameCN
      item.atomicCode = specificObject.atomicCode
    }
  }
}

const onClick = (obj) => {
  console.log(obj, '=======')
  if (obj.type === 1) {
    if (obj.onNode?.list?.length !== 0) {
      const list = obj.onNode.list
      list.push({
        key: v4().slice(0, 8),
        keyL: null,
        keyR: null,
        list: [],
        type: null,
        showTools: false,
        level: obj.onNode.level + 1
      })
      obj.onNode.list = list
    } else {
      const list = [
        {
          ...obj.onNode,
          level: obj.onNode.level + 1
        },
        {
          key: v4().slice(0, 8),
          keyL: null,
          keyR: null,
          type: null,
          list: [],
          showTools: false,
          level: obj.onNode.level + 1
        }
      ]
      obj.onNode.list = list
      obj.onNode.keyL = null
      obj.onNode.keyR = null
      obj.onNode.type = 2
      obj.onNode.key = null
    }
  } else if (obj.type === 2) {
    const list = obj.onNode.list
    list.push({
      key: v4().slice(0, 8),
      keyL: null,
      keyR: null,
      type: null,
      list: [],
      showTools: false,
      level: obj.onNode.level + 1
    })
    obj.onNode.list = list
  } else if (obj.type === 99) {
    if (condition.value.list.length === 0) {
      condition.value = {
        key: v4().slice(0, 8),
        keyL: null,
        keyR: null,
        type: null,
        list: [],
        showTools: false,
        level: 1
      }
    } else {
      removeObject(condition.value.list, obj.onNode.key)
      mergeList(condition.value)
    }
  }

  console.log(condition.value)
  EventBus.$emit('onCondition')
}

const removeObject = (obj, targetKey) => {
  for (let i = obj.length - 1; i >= 0; i--) {
    if (obj[i].key === targetKey) {
      obj.splice(i, 1)
    } else if (obj[i].list) {
      removeObject(obj[i].list, targetKey)
    }
  }
}

const mergeList = (obj) => {
  if (obj.list && obj.list.length === 1) {
    obj.key = obj.list[0].key ? obj.list[0].key : v4().slice(0, 8)
    obj.type = obj.list[0].type
    if (obj.list[0].list.length > 1) {
      obj.list = obj.list[0].list
      obj.list.forEach((item) => {
        item.level--
      })
    } else {
      obj.keyL = obj.list[0].keyL
      obj.keyR = obj.list[0].keyR
      obj.rightType = obj.list[0]?.rightType ? obj.list[0].rightType : ''
      obj.list = []
    }
  } else {
    obj.list.forEach((item) => {
      mergeList(item)
    })
  }
}

const submitForm = ({ persist = true } = {}) => {
  const configObject = {
    webConfig: {
      labelList: [],
      labelFilterList: [],
      metaDataParams: [],
      atomic: {}
    },
    params: []
  }
  paramConfigs.value.forEach((item) => {
    if (item.key && item.key !== 'condition' && item.level !== '2') {
      if (item.dependsOn && item.dependsOn.key) {
        const dependsOn = _.find(configObject.params, {
          key: item.dependsOn.key
        })
        if (dependsOn && item.dependsOn.value === dependsOn.value) {
          configObject.params.push({ key: item.key, value: item.value })
        } else {
          targetLabelArr.value = []
        }
      } else {
        if (item.key === 'strageAlgorithms') {
          const strageAlgorithms = []
          toData.value.forEach((item) => {
            item.children.forEach((subItem) => {
              strageAlgorithms.push({
                channelId: item.id,
                algorithmId: subItem.algorithmId
              })
            })
          })
          configObject.params.push({
            key: item.key,
            value: JSON.stringify(strageAlgorithms)
          })
        } else {
          configObject.params.push({ key: item.key, value: item.value })
        }
      }
    }
  })

  if (modelSelectType.value) {
    const filter = _.filter(labelList.value, { used: true })
    const resultFilter = []
    filter.forEach((item) => {
      resultFilter.push({
        ...item,
        position: props.actionDetail.flowActionId,
        atomicCode: selectedAtomic.value.atomicCode
      })
      configObject.webConfig.metaDataParams.push(
        {
          key: `aiParam.${item.class_name}.confidence`,
          value: '',
          name: t('glossary.labelConfidence', { name: item.nameCN }),
          defaultValue: '',
          description: t('glossary.labelConfidenceDesc', { name: item.nameCN }),
          type: 'text',
          regexpr: '',
          failedTip: t('validate.enterConfidence01'),
          level: '2',
          senior: 1
        },
        {
          key: `aiParam.${item.class_name}.confidenceConfig`,
          value: '',
          name: t('glossary.labelConfidenceOffset', { name: item.nameCN }),
          defaultValue: '0,0',
          description: t('glossary.labelConfidenceOffsetDesc', { name: item.nameCN }),
          type: 'confidenceConfig',
          regexpr: '',
          failedTip: '',
          level: '2',
          senior: 1
        }
      )
      if (modelSelectType.value === 'modelSelect_detector') {
        configObject.webConfig.metaDataParams.push({
          key: `aiParam.${item.class_name}.detPostion`,
          value: '0',
          name: t('glossary.labelDetMethod', { name: item.nameCN }),
          defaultValue: '0',
          description: t('glossary.detMethodDesc'),
          type: 'select',
          regexpr: '/^[0-2]$/',
          failedTip: t('validate.selectCorrectValue'),
          level: '2',
          senior: 0,
          options: [
            {
              name: t('glossary.detBottom'),
              value: '0'
            },
            {
              name: t('glossary.detCenter'),
              value: '1'
            },
            {
              name: t('glossary.detTop'),
              value: '2'
            }
          ]
        })
      }
    })
    configObject.webConfig.labelList = resultFilter
    if (Object.keys(selectedAtomic.value).length !== 0) {
      configObject.webConfig.atomic = {
        atomicCode: selectedAtomic.value.atomicCode,
        atomicName: selectedAtomic.value.atomicName,
        position: props.actionDetail.flowActionId,
        labelList: resultFilter
      }
      resultFilter.length &&
        (configObject.webConfig.atomic.categoriesLabelList =
          categoriesLabelList.value)
    }
  } else if (trackSelectType.value) {
    if (
      selectedTrackAtomic.value &&
      Object.keys(selectedTrackAtomic.value).length !== 0
    ) {
      const param = _.find(configObject.params, { key: 'motionStatus' })
      if (param.value == '1' || param.value == '2') {
        configObject.webConfig.metaDataParams = [
          {
            key: `aiParam.${selectedTrackAtomic.value.atomicCode}.frames`,
            value: '10',
            name: t('glossary.trackHistoryFrames', { name: selectedTrackAtomic.value.atomicName }),
            defaultValue: '10',
            description: t('glossary.trackHistoryFramesDesc'),
            type: 'text',
            regexpr: '/^([2-9]|[1-9][0-9]|100)$/',
            failedTip: t('validate.enterInt2to100'),
            level: '2',
            senior: 1
          },
          {
            key: `aiParam.${selectedTrackAtomic.value.atomicCode}.motion`,
            value: '80.0',
            name: t('glossary.stillThreshold', { name: selectedTrackAtomic.value.atomicName }),
            defaultValue: '80.0',
            description: t('glossary.stillThresholdDesc'),
            type: 'text',
            regexpr: '',
            failedTip: t('validate.enterDec1to99'),
            level: '2',
            senior: 1
          },
          {
            key: `aiParam.${selectedTrackAtomic.value.atomicCode}.trackDynamicMatch`,
            value: '2.3',
            name: t('glossary.trackRadius', { name: selectedTrackAtomic.value.atomicName }),
            defaultValue: '2.3',
            description: t('glossary.trackRadiusDesc'),
            type: 'text',
            regexpr: '',
            failedTip: t('validate.enterDec0to100'),
            level: '2',
            senior: 1
          }
        ]
      } else {
        configObject.webConfig.metaDataParams = [
          {
            key: `aiParam.${selectedTrackAtomic.value.atomicCode}.trackDynamicMatch`,
            value: '2.3',
            name: t('glossary.trackRadius', { name: selectedTrackAtomic.value.atomicName }),
            defaultValue: '2.3',
            description: t('glossary.trackRadiusDesc'),
            type: 'text',
            regexpr: '',
            failedTip: t('validate.enterDec0to100'),
            level: '2',
            senior: 1
          }
        ]
      }
    }
  } else if (labelFilterListType.value) {
    targetLabelArr.value.forEach((item) => {
      configObject.webConfig.labelFilterList.push({
        ...item
      })

      if (item.sideMinIsEnable == '1') {
        configObject.webConfig.metaDataParams.push({
          key: `filter.${item.labelCode}.side.min`,
          value: '60',
          name: t('glossary.minLabelSize', { name: item.labelName }),
          defaultValue: '60',
          description: t('glossary.minLabelSizeDesc', { name: item.labelName }),
          type: 'text',
          regexpr:
            '/^(0|[1-9]|[1-9][0-9]|[1-9][0-9][0-9]|[1-9][0-9][0-9][0-9]|10000)$/',
          level: '2',
          failedTip: t('validate.enterInt0to10000')
        })
      }
    })
  } else if (labelAdjustType.value) {
    targetLabelArr.value.forEach((item) => {
      item.labelCode &&
        configObject.params.push({
          key: `preParam.confidence.${item.labelCode}`,
          value: item.confidenceValue
        })
    })
  } else if (conditionType.value) {
    configObject.condition = condition.value
  }
  if (labelTargetLimitType.value) {
    targetLimitLabelArr.value.forEach((item) => {
      if (item.labelCode) {
        configObject.params.push({
          key: `areaLimitTargetType.${item.labelCode}`,
          value: item.targetCondition
        })
        configObject.params.push({
          key: `areaLimitTargetCount.${item.labelCode}`,
          value: item.targetNum
        })
      }
    })
  }
  paramConfigs.value.forEach((item) => {
    if (item.level === '2') {
      if (item.dependsOn) {
        const dependsOn = _.find(configObject.params, {
          key: item.dependsOn.key
        })
        if (dependsOn && item.dependsOn.value === dependsOn.value) {
          configObject.webConfig.metaDataParams.push({
            ...item,
            position: props.actionDetail.flowActionId
          })
        }
      } else {
        configObject.webConfig.metaDataParams.push({
          ...item,
          position: props.actionDetail.flowActionId
        })
      }
    }
  })
  if (persist) {
    localStorage.setItem('flowConfigObject', JSON.stringify(configObject))
    console.log('========submitForm=========', configObject)
  }
  return configObject
}

// Expose methods
defineExpose({
  submitForm
})
</script>

<style lang="scss" scoped>
/* 滚动条滑块以外区域 */
::-webkit-scrollbar-track-piece {
  background-color: #f2f5f9; /* 设置滑块以外区域的背景颜色 */
}

.form-body {
  padding: 10px 0;
  // height: 450px;
  overflow-y: scroll;
}

.el-form-item {
  margin-bottom: 15px;
}

.form-flex {
  display: flex;
}

.form-content {
  width: 200px;
}

.select-table-body {
  color: #606266;
  font-size: 14px;
  margin-bottom: 15px;
}

.select-table-title {
  padding-bottom: 10px;
}

.collapse-body {
  color: #606266;
  font-size: 14px;
}

.collapse-arrow {
  margin-right: 10px;
}

.plus-icon {
  font-size: 25px;
  margin-left: 10px;
  color: #1890ff;
  cursor: pointer;
}

.close-icon {
  font-size: 25px;
  margin-left: 5px;
  color: red;
  cursor: pointer;
}

.collapse-top {
  display: flex;
  align-items: center;

  i {
    cursor: pointer;
  }
}

.el-icon-arrow-down,
.el-icon-arrow-right {
  font-weight: bold;
  margin-right: 10px;
}

.collapse-content {
  margin-left: 20px;
  font-size: 13px;
}

.collapse-select {
  margin-left: 10px;
}

.group-span {
  padding: 10px 10px;
  font-size: 14px;
}

.target-label {
  display: inline-block;
  width: 140px;
}

.select-table-body {
  color: #606266;
  font-size: 14px;
  margin-bottom: 15px;
}

.select-table-title {
  padding-bottom: 10px;
}
</style>
