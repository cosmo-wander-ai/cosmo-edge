<template>
  <div class="mv-wrap" v-loading.fullscreen="loading" :element-loading-text="t('common.paramSyncing')" element-loading-background="rgba(0, 0, 0, 0.8)">
    <div class="container">
      <div class="serve-type-container" v-if="arithmeticShow">
        <el-card class="box-card">
          <template #header>
            <div class="clearfix">
              <span>{{ t('glossary.scenarioTask') }}</span>
            </div>
          </template>
          <el-input :placeholder="t('field.filterKeyword')" v-model="filterText" size="small" clearable style="margin-left: 10px;width: 220px;"></el-input>
          <div class="tree-body">
            <el-tree id="onboarding-algorithm-tree" class="filter-tree" :data="arithmeticTree" :props="defaultProps" highlight-current default-expand-all :filter-node-method="filterNode" ref="tree" @node-click="chooseType" node-key="algorithmId">
              <template #default="{ data }">
                <span class="custom-tree-node">
                  <span v-if="!data.algorithmCode" class="custom-treeText">{{ data.algorithmCategoryName }}</span>
                  <span v-else class="custom-treeText">{{ resolveAlgorithmName(data) }}</span>
                  <span v-if="data.algorithmCode">
                    <el-icon v-if="data.isSave == true" class="el-menu-icon" color="#67C23A">
                      <CircleCheckFilled />
                    </el-icon>
                  </span>
                </span>
              </template>
            </el-tree>
          </div>
        </el-card>
      </div>
      <div style="margin-left: 15px;flex:1;">
        <div class="serve-config-container">
          <el-card class="serve-config-container-card">
            <template #header>
              <div class="serve-config-header clearfix">
                <div>
                  <span class="abc">{{ t('glossary.serviceConfig') }}</span>
                  <div :class="['circle', { 'red-circle': taskEnableStatus == 0 }, { 'green-circle': taskEnableStatus == 1 }]" class="abc"></div>
                  <span v-if="taskEnableStatus == 1" style="margin-left: 10px" class="abc">{{ t('common.enabled') }}</span>
                  <span v-else style="margin-left: 10px" class="abc">{{ t('common.disabled') }}</span>
                </div>
                <div class="abc" style="margin-left:10px;">{{ resolvedAlgorithmName }}</div>
                <div class="serve-config-header-btns">
                  <el-button type="danger" v-if="taskEnableStatus == 1 && (taskId != '' || platformType === '15')" @click="stopServe(0)" size="small">{{ t('action.disableService') }}</el-button>
                  <el-button type="primary" v-if="taskEnableStatus == 0 && (taskId != '' || platformType === '15')" @click="startServe(1)" size="small">{{ t('action.enableService') }}</el-button>
                  <el-button type="danger" v-if="(taskId || platformType === '15')" @click="handleDelServe()" size="small">{{ t('action.deleteService') }}</el-button>
                  <el-button id="onboarding-save-service" class="mv-el-button" type="primary" @click="clickSaveServe()" size="small">{{ t('action.save') }}</el-button>
                </div>
              </div>
            </template>
            <el-tabs class="tabbgcolor" v-model.trim="activeName" type="card" style="height: 100%">
              <el-tab-pane :label="t('glossary.detectionArea')" name="area">
                <area-setting v-if="config && config.channelId" ref="areaSettingRef" v-model:config="config" :algorithmCode="algorithmCode"></area-setting>
              </el-tab-pane>

              <el-tab-pane :label="t('glossary.parameterSettings')" name="params">
                <paramSetting class="param-body" v-if="config && config.channelId" :config="config" ref="parm" :algorithmCode="algorithmCode" :activeName="activeName" />

                <div v-if="String(algorithmCode) === '99'" class="LargeModelAlgorithmConfiguration">
                  <el-tooltip style="margin-right: 10px;" class="item" effect="dark" :content="t('validate.saveAndStartFirst')" placement="top" :disabled="false">
                    <span>
                      <el-button type="primary" @click="LargeModelAlgorithmConfiguration" :disabled="taskEnableStatus != 1">{{ t('action.enterDemoScreen') }}</el-button>
                    </span>
                  </el-tooltip>
                </div>
                <div style="margin-left:160px;">
                  <el-button style="margin-right:15px;" @click="parameterVisible = true">{{ t('action.reset') }}</el-button>
                  <el-button type="primary" @click="batch">{{ t('glossary.batchApply') }}</el-button>
                </div>
              </el-tab-pane>

              <el-tab-pane :label="t('glossary.runningStrategy')" name="strategy">
                <el-form class="strategy-body" label-position="right" :label-width="currentLocale === 'en-US' ? '170px' : '120px'">
                  <el-form-item v-if="joinTypeS !== -1" :label="t('field.timeTemplate')">
                    <el-select v-if="config" v-model.trim="config.scheduleId" class="width200" size="small" filterable :placeholder="t('placeholder.select', { field: t('field.timeTemplate') })">
                      <el-option v-for="item in timeTemplateList" :key="item.scheduleId" :label="resolveScheduleName(item.scheduleName)" :value="item.scheduleId"></el-option>
                    </el-select>
                  </el-form-item>
                  <el-form-item v-if="joinTypeS == -1" :label="t('field.playCount')">
                    <template #label>
                      <span style="position:relative">
                        <span>{{ t('field.playCount') }}</span>
                        <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                          <template #content>
                            <p>{{ t('validate.playCountRangeTip') }}</p>
                          </template>
                          <el-icon><QuestionFilled /></el-icon>
                        </el-tooltip>
                      </span>
                    </template>
                    <el-input v-model="videoRepeatCount" class="width200" size="small" oninput="value=value.replace(/[^\d-]/g,'')" @input="handleVideoRepeatInput"></el-input>
                  </el-form-item>
                </el-form>
                <div style="margin: 80px 0px 0px 160px;">
                  <el-button type="primary" @click="batch">{{ t('glossary.batchApply') }}</el-button>
                </div>
              </el-tab-pane>
            </el-tabs>
          </el-card>
        </div>
      </div>
    </div>

    <el-dialog :title="t('common.notice')" v-model="confirmDialogVisible" center width="30%">
      <div style="text-align: center">{{ t('action.confirmDelete') }}</div>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="confirmDialogVisible = false">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="deleteServe">{{ t('action.ok') }}</el-button>
        </span>
      </template>
    </el-dialog>
    <!-- 重置参数 -->
    <el-dialog :title="t('common.notice')" v-model="parameterVisible" center width="550px">
      <div style="text-align: center">{{ t('validate.resetParamsConfirm') }}</div>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="parameterVisible = false">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="resetParameter">{{ t('action.ok') }}</el-button>
        </span>
      </template>
    </el-dialog>
    <!-- 批量应用 -->
    <Batch v-if="config && config.channelId" v-model:BatchApplication="BatchApplication" @confirm="BatchConfirm" :algorithmId="algorithmId" :channelId="config.channelId"></Batch>

    <el-dialog :title="t('common.notice')" v-model="warningVisible" width="400px" center>
      <div class="dialog-content">{{ warningContent }}</div>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="warningVisible = false">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="warningVisible = false">{{ t('action.ok') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, getCurrentInstance, nextTick, computed } from 'vue'
import { t, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'
import { QuestionFilled, CircleCheckFilled } from '@element-plus/icons-vue'
import areaSetting from './areaSetting2.vue'
import paramSetting from './paramSetting.vue'
import EventBus from '@/components/eventBus.js'
import { v4 } from 'uuid'
import Batch from './BatchApplication.vue'

const { proxy } = getCurrentInstance()

const props = defineProps({
  channelId: String,
  channelName: {
    type: String,
    default: ''
  },
  formDataz: Object,
  joinType: Number
})

const platformType = localStorage.getItem('platformType') || ''
const BatchApplication = ref(false)
const taskId = ref('')
const taskEnableStatus = ref(0)
const config = ref({
  channelId: '',
  scheduleId: '',
  pollingId: '',
  taskParam: [],
  taskAreaRows: [],
  taskAreaHeader: [],
  shieldAreaRows: [],
  shieldAreaHeader: [],
  areasTitle: [],
  isValited: true,
  warning: '',
  data: [],
  Multiple: '',
  regionType: ''
})
const videoRepeatCount = ref(1)
const activeName = ref('area')
const confirmDialogVisible = ref(false)
const timer = ref(null)
const algorithmId = ref('')
const algorithmCode = ref('')
const joinTypeEdits = ref(Number(proxy.$route.query.joinTypeEdit))
const joinTypeS = ref(props.joinType)
const filterText = ref('')
const defaultProps = ref({
  children: 'simpleAlgorithmInfos',
  label: 'algorithmCategoryName'
})
const scheduleSupport = ref(null)
const algorithmUsage = window.localStorage.getItem('algorithmUsage')
const arithmeticTree = ref([])
const analyze = ref([
  {
    label: t('glossary.normalSpeed'),
    value: 1
  },
  {
    label: t('glossary.doubleSpeed'),
    value: 2
  }
])
const timeTemplateList = ref([])
const sourceSchedulePollingList = ref([])
const schedulePollingList = ref([])
const category = ref(0)
const colors = ['24,144,255', '255,115,24', '24,255,89', '255,239,24']
const loading = ref(false)
const BatchType = ref(false)
const parameterData = ref({})
const parameterVisible = ref(false)
const arithmeticShow = ref(true)
const algorithmName = ref('')
const runTypeOptions = computed(() => [
  { label: t('glossary.realtime'), value: 0 },
  { label: t('glossary.polling'), value: 1 }
])
const algorithmCategoryOptions = computed(() => [
  { label: t('glossary.faceAndBody'), value: '1' },
  { label: t('glossary.detection'), value: '2' },
  { label: t('glossary.detection'), value: '3' },
  { label: t('glossary.countingAnalytics'), value: '8' },
  { label: t('glossary.countingAnalytics'), value: '9' },
  { label: t('glossary.countingAnalytics'), value: '11' }
])
const warningVisible = ref(false)
const warningContent = ref(t('validate.addDetectionAreaFirst'))

const tree = ref(null)
const selectedAlgorithm = computed(() => {
  for (const group of arithmeticTree.value) {
    const match = group.simpleAlgorithmInfos?.find(
      (item) => item.algorithmCode === algorithmCode.value
    )
    if (match) return match
  }
  return null
})
const resolveAlgorithmName = (item) => resolveResourceAlgorithmName(item)
const resolvedAlgorithmName = computed(() =>
  resolveAlgorithmName(selectedAlgorithm.value) || algorithmName.value
)
const areaSettingRef = ref(null)
const parm = ref(null)

// Map backend default schedule names → i18n keys
const SCHEDULE_NAME_MAP = {
  '全天候': 'boxOther.scheduleNameAllDay',
  '工作日': 'boxOther.scheduleNameWeekday',
  '周末工作': 'boxOther.scheduleNameWeekend'
}
const resolveScheduleName = (name) => {
  const key = SCHEDULE_NAME_MAP[name]
  return key ? t(key) : name
}

watch(filterText, (val) => {
  tree.value.filter(val)
})

watch(BatchApplication, (newVal) => {
  BatchType.value = newVal
})

const init = () => {
  if (getParams('joinType')) {
    joinTypeS.value = Number(getParams('joinType'))
  }
  if (getParams('joinTypeEdit')) {
    let arr = 0
    if (Number(getParams('joinTypeEdit')) == 1) {
      arr = -1
    } else {
      arr = 0
    }
    joinTypeEdits.value = arr
  }
  arithmeticShow.value = true
  if (getParams('channelId')) {
    config.value.channelId = getParams('channelId')
    config.value.channelName = getParams('channelName')
    rest()
  }

  if (!getParams('channelId') && !getParams('channelCode')) {
    config.value.channelId = props.channelId
    config.value.channelName = props.channelName
    rest()
  }

  if (getParams('channelCode') && !getParams('algorithmCode')) {
    let taskCustId = window.localStorage.getItem('taskCustId')
      ? window.localStorage.getItem('taskCustId')
      : window.localStorage.getItem('currentCustId')
    const param = {
      channelCode: getParams('channelCode'),
      custId: taskCustId ? taskCustId : ''
    }
    proxy.$API.channelCodeDetail(param).then((res) => {
      const { resData } = res
      config.value.channelId = resData.channelId
      rest()
    })
  }

  if (getParams('channelCode') && getParams('algorithmCode')) {
    arithmeticShow.value = false
    let taskCustId = window.localStorage.getItem('taskCustId')
      ? window.localStorage.getItem('taskCustId')
      : window.localStorage.getItem('currentCustId')
    const param = {
      channelCode: getParams('channelCode'),
      custId: taskCustId ? taskCustId : ''
    }
    proxy.$API.channelCodeDetail(param).then((res) => {
      const { resData } = res
      config.value.channelId = resData.channelId
      config.value.channelName = resData.channelName
      algorithmCode.value = getParams('algorithmCode')
      algorithmId.value = getParams('algorithmCode')
      getServeTypes()
    })
  }
}

const getParams = (name) => {
  let search = window.location.search.substring(1)
  if (!search) {
    search = window.location.hash.split('?')[1]
  }
  if (search) {
    const params = new URLSearchParams(search)
    const obj = {}
    for (const [key, value] of params.entries()) {
      obj[key] = value
    }
    return name ? obj[name] : obj
  }
}

const normalizeSearchText = (value) => String(value || '').trim().toLowerCase()

const collectModelSearchText = (alg) => {
  const fields = []
  const addModel = (model) => {
    if (!model) return
    fields.push(
      model.modelName,
      model.modelCode,
      model.atomicName,
      model.atomicCode
    )
  }

  ;(alg.models || []).forEach(addModel)
  Object.values(alg.envStatus || {}).forEach((models) => {
    if (Array.isArray(models)) {
      models.forEach(addModel)
    }
  })

  fields.push(
    alg.modelName,
    alg.modelCode,
    alg.atomicName,
    alg.atomicCode
  )

  return fields.filter(Boolean).join(' ')
}

const filterNode = (value, data) => {
  const keyword = normalizeSearchText(value)
  if (!keyword) return true

  const searchText = [
    data.algorithmCategoryName,
    data.algorithmName,
    data.algorithmCode,
    data.algorithmId,
    data.modelSearchText
  ].map(normalizeSearchText).join(' ')

  return searchText.includes(keyword)
}

const rest = () => {
  let taskCustId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')
  let photograph = localStorage.getItem('photograph')
  const param = {
    channelId: config.value.channelId,
    algorithmUsage: algorithmUsage,
    custId: taskCustId ? taskCustId : '',
    mode: photograph ? photograph : ''
  }
  getServeTypes()
}

const getServeTypes = () => {
  let taskCustId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')

  // 使用 algorithmInquire 获取场景任务列表（与场景任务管理页面一致）
  const params = {
    algorithmId: '',
    algorithmCategory: '',
    algorithmUsage: '1',  // 仅视频分析类型
    algorithmName: '',
    supplier: '',
    engineTypeList: [],
    pageNum: 1,
    pageSize: 9999
  }

  // 同时查询当前通道已分配的任务ID列表
  const channelParam = {
    channelId: config.value.channelId,
    algorithmUsage: algorithmUsage,
    custId: taskCustId ? taskCustId : '',
    mode: localStorage.getItem('photograph') || ''
  }

  Promise.all([
    proxy.$API.algorithmInquire(params),
    proxy.$API.selectAllAlgorithmInfo(channelParam)
  ]).then(([algRes, channelRes]) => {
    const rows = algRes.resData?.rows || []
    const savedAlgIds = channelRes.resData?.algorithmIds || []

    // 按 algorithmCategory 分组，构建与场景任务页面一致的树结构
    const groupMap = {}
    let selectedList = []

    rows.forEach((alg) => {
      const catValue = String(alg.algorithmCategory || '0')
      const catOption = algorithmCategoryOptions.value.find(opt => opt.value === catValue)
      const catLabel = catOption ? catOption.label : t('glossary.other')

      if (!groupMap[catLabel]) {
        groupMap[catLabel] = {
          algorithmCategoryName: catLabel,
          algorithmCategory: catValue,
          simpleAlgorithmInfos: []
        }
      }

      const isSave = savedAlgIds.some(id => String(id) === String(alg.algorithmId))
      const algItem = {
        algorithmId: alg.algorithmId,
        algorithmCode: alg.algorithmCode || alg.algorithmId,
        algorithmName: alg.algorithmName,
        algorithmCategoryName: catLabel,
        models: alg.models || [],
        envStatus: alg.envStatus || {},
        modelSearchText: collectModelSearchText(alg),
        isSave: isSave
      }
      groupMap[catLabel].simpleAlgorithmInfos.push(algItem)

      if (isSave) {
        selectedList.push(algItem)
      }
    })

    const algorithmList = Object.values(groupMap).filter(
      g => g.simpleAlgorithmInfos.length > 0
    )

    if (algorithmList.length === 0) {
      proxy.$message.error(t('validate.noAvailableScenarioTask'))
    }

    arithmeticTree.value = algorithmList

    if (!algorithmCode.value && !algorithmId.value) {
      const firstAlg = selectedList.length > 0
        ? selectedList[0]
        : algorithmList[0]?.simpleAlgorithmInfos[0]
      if (firstAlg) {
        algorithmCode.value = firstAlg.algorithmCode
        algorithmId.value = firstAlg.algorithmId
      }
    }

    nextTick(() => {
      if (tree.value) {
        tree.value.setCurrentKey(algorithmId.value)
      }
      selectTree(algorithmCode.value)
      getSelectConfig()
    })
  })
}

const resetConfig = () => {
  config.value.taskParam = []
  config.value.taskAreaRows = []
  config.value.taskAreaHeader = []
  config.value.shieldAreaRows = []
  config.value.shieldAreaHeader = []
  config.value.areasTitle = []
  EventBus.$emit('resetSelectPoints', '')
}

const getSelectConfig = () => {
  schedulePollingList.value = sourceSchedulePollingList.value.filter(
    (item) => item.algorithmId === algorithmId.value
  )

  resetConfig()
  scheduleSupport.value = 1
  let taskCustId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')
  proxy.$API
    .selectConfigByAlgorithmId({
      channelId: config.value.channelId,
      algorithmId: algorithmId.value,
      custId: taskCustId ? taskCustId : ''
    })
    .then((res) => {
      const { resData } = res
      if (platformType == '15') {
        resData.algorithmMetadata = JSON.parse(resData.algorithmMetadata)
      }
      category.value = resData.category ? resData.category : 0
      if (joinTypeS.value == -1 || joinTypeEdits.value == -1) {
        scheduleSupport.value = 0
      }
      if (resData.algorithmMetadata.scheduleSupport != null) {
        if (joinTypeS.value == -1 || joinTypeEdits.value == -1) {
          scheduleSupport.value = 0
        } else {
          scheduleSupport.value = resData.algorithmMetadata.scheduleSupport
        }
      }
      config.value.regionType = resData.algorithmMetadata.regionType
      config.value.maxAreaCount = resData.algorithmMetadata.maxAreaCount || 4
      config.value.defaultFullScreen =
        resData.algorithmMetadata.defaultFullScreen

      let metaData = resData.algorithmMetadata,
        metaParams = metaData.params,
        taskParam = resData.taskConfig.params
      taskId.value = resData.taskId
      taskEnableStatus.value = resData.taskEnableStatus
      config.value.scheduleId = resData.scheduleId || ''
      config.value.pollingId = resData.pollingId || ''

      metaParams.forEach((metaItem) => {
        taskParam.forEach((taskItem, index) => {
          if (taskItem.key == metaItem.key) {
            if (metaItem.type == 'slider') {
              metaItem.value = Number(taskItem.value)
            } else {
              metaItem.value = taskItem.value
            }
          }
          if (
            (joinTypeS.value == -1 || joinTypeEdits.value == -1) &&
            taskItem.key === 'param.videoRepeatCount'
          ) {
            videoRepeatCount.value = taskItem.value
          }
        })
        if (metaItem.type == 'check') {
          metaItem.value = metaItem.value == '' ? [] : metaItem.value.split(',')
        }

        metaItem.isColumn = true
        if (metaItem.key !== 'param.videoRepeatCount') {
          config.value.taskParam.push(metaItem)
        }
      })

      config.value.taskAreaHeader = metaData.region?.heads
      config.value.shieldAreaHeader = metaData.shieldedRegion?.heads
      config.value.areasTitle = metaData.region?.areasTitle

      resData.taskConfig.areas.forEach((area) => {
        let row = {
          name: area.name,
          areaId: area.areaId,
          points: area.points,
          retroDirect: area.retroDirect ? area.retroDirect : 0,
          associatedAreas: [],
          linePoints: area.linePoints
        }
        if (config.value.areasTitle) {
          row.associatedAreas =
            area.associatedAreas && area.associatedAreas.length
              ? area.associatedAreas[0].points
              : null
        }
        if (area.params) {
          area.params.forEach((p) => {
            const needKeys = ['id', 'name', 'directionType']
            needKeys.includes(p.key) && (row[p.key] = p.value)
          })
        }
        config.value.taskAreaRows.push(row)
      })

      if (config.value.shieldAreaHeader) {
        resData.taskConfig.shieldedAreas?.forEach((area) => {
          let row = {
            name: area.name,
            areaId: area.areaId,
            shieldPoints: area.points
          }
          if (area.params) {
            area.params.forEach((p) => {
              const needKeys = ['id', 'name', 'directionType']
              needKeys.includes(p.key) && (row[p.key] = p.value)
            })
          }
          config.value.shieldAreaRows.push(row)
        })
      } else {
        config.value.shieldAreaRows = []
      }

      if (scheduleSupport.value == 1 && !config.value.scheduleId) {
        config.value.scheduleId = timeTemplateList.value[0]?.scheduleId
      }
    })
}

const selectTree = (data) => {
  let arr = []
  arithmeticTree.value.forEach((i) => {
    i.simpleAlgorithmInfos.forEach((n) => {
      if (data == n.algorithmCode) {
        arr.push(n)
      }
    })
  })
  if (arr.length > 0) {
    algorithmName.value = arr[0].algorithmName
  }
}

const chooseType = (data) => {
  selectTree(data.algorithmCode)
  if (data.simpleAlgorithmInfos) return
  algorithmCode.value = data.algorithmCode
  algorithmId.value = data.algorithmId
  activeName.value = 'area'
  let taskCustId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')
  getSelectConfig()
}

const startServe = (status) => {
  if (platformType === '15') {
    boxSwitchTask(status)
  } else {
    proxy.$API.startTask({ taskId: taskId.value }).then((res) => {
      proxy.$message.success(t('common.serviceStarted'))
      getServeTypes()
    })
  }
}

const stopServe = (status) => {
  if (platformType === '15') {
    boxSwitchTask(status)
  } else {
    proxy.$API.stopTask({ taskId: taskId.value }).then((res) => {
      proxy.$message.success(t('common.serviceStopped'))
      getServeTypes()
    })
  }
}

const boxSwitchTask = (status) => {
  const params = {
    channelId: config.value.channelId,
    algorithmId: algorithmId.value,
    switch: status
  }
  proxy.$API.boxSwitchTask(params).then((res) => {
    proxy.$message.success(t('common.operationSucceeded'))
    getSelectConfig()
  })
}

const handleDelServe = () => {
  confirmDialogVisible.value = true
}

const deleteServe = () => {
  if (platformType === '15') {
    proxy.$API
      .boxDeleteTask({
        channelId: config.value.channelId,
        algorithmId: algorithmId.value
      })
      .then((res) => {
        proxy.$message.success(t('common.operationSucceeded'))
        getServeTypes()
      })
  } else {
    proxy.$API.deleteTask({ taskId: taskId.value }).then((res) => {
      proxy.$message.success(t('common.operationSucceeded'))
      getServeTypes()
    })
  }
  confirmDialogVisible.value = false
}

const clickSaveServe = () => {
  if (areaSettingRef.value?.isDrawingLine)
    return proxy.$message.warning(t('validate.completeDrawingFirst'))
  const result = areaSettingRef.value.$refs.canvasRef.submit()
  config.value.taskAreaRows.forEach((item, index) => {
    item.points = result[index].points
    item.associatedAreas = result[index].associatedAreas
    item.linePoints = result[index].linePoints
    item.directionType = result[index].directionType
  })

  config.value.shieldAreaRows.forEach((item, index) => {
    item.shieldPoints = result[index].shieldPoints
  })

  clearTimeout(timer.value)
  if (!timer.value) {
    newSave()
  } else {
    proxy.$message.error(t('validate.waitBeforeSave'))
  }
  timer.value = setTimeout(() => {
    timer.value = null
  }, 3000)
}

const newSave = (skipRefresh = false) => {
  EventBus.$emit('validTaskParam')
  if (!config.value.isValited) {
    return
  }

  let params = {
    channelId: config.value.channelId,
    channelName: config.value.channelName,
    algorithmId: algorithmId.value,
    custId: localStorage.getItem('currentCustId') || '',
    category: category.value,
    taskConfig: {
      areas: [],
      shieldedAreas: [],
      params: []
    }
  }

  if (joinTypeS.value == -1 || joinTypeEdits.value == -1) {
    params.analysisSpeed = 1
  } else {
    if (
      params.category == 0 &&
      !config.value.scheduleId &&
      scheduleSupport.value == 1
    ) {
      proxy.$message.warning(t('validate.selectTimeTemplate'))
      return
    } else {
      params.scheduleId = config.value.scheduleId
    }

    if (params.category == 1 && !config.value.pollingId) {
      proxy.$message.warning(t('validate.selectPollingStrategy'))
      return
    } else {
      params.pollingId = config.value.pollingId
    }
  }

  params.taskConfig.areas = config.value.taskAreaRows.map((item, index) => {
    let associatedAreas = []
    if (item.associatedAreas && item.associatedAreas.length > 0) {
      associatedAreas = [
        {
          points: item.associatedAreas,
          areaId: v4()
        }
      ]
    }
    return {
      name: item.name,
      areaId: item.areaId ? item.areaId : v4(),
      points: item.points,
      associatedAreas: associatedAreas,
      linePoints: item.linePoints,
      rgb: colors[index],
      params: [
        { key: 'name', value: item.name },
        {
          key: 'directionType',
          value: item.directionType
        }
      ]
    }
  })

  params.taskConfig.shieldedAreas = config.value.shieldAreaRows.map(
    (item, index) => {
      return {
        name: item.name,
        areaId: item.areaId ? item.areaId : v4(),
        points: item.shieldPoints,
        rgb: '255, 255, 255',
        params: [{ key: 'name', value: item.name }]
      }
    }
  )

  if (
    config.value.regionType === 'cordon' ||
    config.value.regionType === 'oneWayCordon'
  ) {
    if (params.taskConfig.areas.length === 0) {
      warningVisible.value = true
      warningContent.value = t('validate.addDetectionLineFirst')
      return
    }
  }


  if (
    params.taskConfig.shieldedAreas.length === 0 &&
    config.value.shieldAreaHeader
  ) {
    warningVisible.value = true
    warningContent.value = t('validate.addShieldAreaFirst')
    return
  }

  let flag = false
  params.taskConfig.params = config.value.taskParam.map((item) => {
    if (item.type === 'retroDirect') {
      return {
        key: item.key,
        value: areaSettingRef.value.retroDirectType
      }
    } else if (item.type === 'check') {
      return {
        key: item.key,
        value: item.value.join(',')
      }
    } else if (item.type === 'slider') {
      return {
        key: item.key,
        value: item.value + ''
      }
    } else if (item.type == 'confidenceConfig') {
      item.value =
        `${item.confidenceConfigValue1 || 0},${item.confidenceConfigValue2 || 0}`
      return {
        key: item.key,
        value: item.value
      }
    } else {
      if (item.key === 'param.trippingWireType' && item.value == 2) {
        flag = true
      }
      return {
        key: item.key,
        value: typeof item.value === 'number' ? String(item.value) : item.value
      }
    }
  })

  if (flag == true && params.taskConfig.areas.length < 2) {
    return proxy.$message.error(t('validate.doubleLineRequireTwo'))
  }

  if (joinTypeS.value == -1 || joinTypeEdits.value == -1) {
    if (
      videoRepeatCount.value === '' || videoRepeatCount.value === null || videoRepeatCount.value === undefined ||
      Number(videoRepeatCount.value) > 100 ||
      Number(videoRepeatCount.value) < 0
    ) {
      return proxy.$message.warning(t('validate.playCountRange'))
    }
    let videoRepeatCountFlag = false
    params.taskConfig.params.forEach((item) => {
      if (item.key === 'param.videoRepeatCount') {
        item.value = videoRepeatCount.value + ''
        videoRepeatCountFlag = true
      }
    })
    if (!videoRepeatCountFlag) {
      params.taskConfig.params.push({
        key: 'param.videoRepeatCount',
        value: videoRepeatCount.value + ''
      })
    }
  }

  params.taskConfig.params.forEach((item) => {
    if (typeof item.value == 'number') {
      item.value = item.value + ''
    }
  })

  parameterData.value = params

  return proxy.$API.saveOrUpdate(params).then((res) => {
    proxy.$message.success(t('common.saveSucceeded'))
    if (!skipRefresh) {
      getServeTypes()
    }
  })
}

const handleVideoRepeatInput = (val) => {
  if (val === '') {
    videoRepeatCount.value = ''
    return
  }

  if (!/^\d+$/.test(val)) {
    videoRepeatCount.value = ''
    return
  }

  const num = parseInt(val, 10)
  if (num >= 0 && num <= 100) {
    videoRepeatCount.value = String(num)
  } else {
    videoRepeatCount.value = num > 100 ? '100' : '0'
  }
}

const getTimetemplate = () => {
  let custId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')
  const param = {
    custId: custId
  }
  if (platformType === '15') {
    proxy.$API.boxGetTimeTemplate({}).then((res) => {
      const { resData } = res
      timeTemplateList.value = resData.rows || []
    })
  } else {
    proxy.$API.selectScheduleInfo(param).then((res) => {
      const { resData } = res
      timeTemplateList.value = resData || []
    })
  }
}

const getSchedulePollingList = () => {
  let custId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')

  const params = {
    custId: custId
  }
  proxy.$API.schedulePollingList(params).then((res) => {
    const { resData } = res
    sourceSchedulePollingList.value = [...resData]

    if (algorithmId.value) {
      schedulePollingList.value = sourceSchedulePollingList.value.filter(
        (item) => item.algorithmId === algorithmId.value
      )
    }
  })
}

const batch = () => {
  BatchApplication.value = true
}

const BatchConfirm = async (data) => {
  const saveResult = newSave(true)
  if (!saveResult) return // 校验未通过
  try {
    await saveResult
  } catch (e) {
    return
  }
  parameterData.value.targetChannelIds = data.targetChannelIds
  proxy.$API.applyParamsBatch(parameterData.value).then((res) => {
    loading.value = false
    if (res.resCode == 1) {
      proxy.$message.success(t('common.batchSaveSyncing'))
      getServeTypes()
    }
  })
}

const resetParameter = () => {
  config.value.taskParam.forEach((item) => {
    if (item.type == 'check') {
      item.value = item.defaultValue.split(',')
    } else if (item.type == 'confidenceConfig') {
      item.value = item.defaultValue
      item.confidenceConfigValue1 = Number(item.defaultValue.split(',')[0])
      item.confidenceConfigValue2 = Number(item.defaultValue.split(',')[1])
    } else {
      if (item.type == 'slider') {
        item.value = Number(item.defaultValue)
      } else {
        item.value = item.defaultValue
      }
    }
  })

  if (parm.value.$refs.submitForm) {
    parm.value.$refs.submitForm.transferList = []
  }
  parameterVisible.value = false
}

const LargeModelAlgorithmConfiguration = () => {
  window.open(`/sop/#/sop/modelScreen`, '_blank')
}

onMounted(() => {
  init()
  getTimetemplate()
})
</script>

<style scoped lang="scss">
.mv-wrap {
  margin: 0;
  height: 100%;
}

.mv-table-wrap {
  text-align: left;
}

.container {
  display: flex;
  height: 100%;
}

.text {
  font-size: 14px;
}

.clearfix:before,
.clearfix:after {
  display: table;
  content: '';
}

.clearfix:after {
  clear: both;
}

.box-card {
  width: 240px;
  height: 100%;

  :deep(.el-card__body) {
    padding: 20px 0px 10px;
    overflow: scroll;
  }
}

.serve-config-container {
  height: 100%;
}

.serve-config-container-card {
  display: flex;
  height: 100%;
  flex-direction: column;

  :deep(.el-card__body) {
    height: 100%;
    overflow: auto;
  }

  .el-tab-pane {
    overflow-x: scroll;
    padding-bottom: 20px;
  }
}

.step-buttons-wrap {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  margin-top: 12px;
  padding-right: 12px;
  height: 56px;
  background: #ffffff;
}

.serve-types {
  display: flex;
  justify-content: space-between;
  width: 140px;
  cursor: pointer;
  padding: 10px 20px;
}

.c-serve-types {
  background-color: #f1f5f9;
}

.el-menu-icon {
  top: -2px;
  left: 10px;
}

.serve-config-header {
  display: flex;
  position: relative;
}

.serve-config-header-btns {
  position: absolute;
  right: 0px;
  top: 0px;
}

.el-button {
  height: 32px;
  padding-top: 8px;
}

.circle {
  width: 7px;
  height: 7px;
  background-color: red;
  border-radius: 50%;
  display: inline-block;
  position: absolute;
  top: 14px;
}

.red-circle {
  background-color: red;
}

.green-circle {
  background-color: #5cb5b1;
}

:deep(.hint) {
  margin-left: 30px;
}

.abc {
  line-height: 35px;
}

.chooseMultiple {
  display: flex;
  margin: 60px 0 20px 85px;
}

.pitchOn {
  background-color: red;
}

.humanFaceClass {
  vertical-align: middle;
  float: left;
  font-size: 14px;
  margin: 0px 30px 0px 100px;
  color: #606266;
  box-sizing: border-box;
}

:deep(.el-tree--highlight-current
    .el-tree-node.is-current
    > .el-tree-node__content) {
  background-color: rgba(135, 206, 235, 0.2);
}

.LargeModelAlgorithmConfiguration {
  position: absolute;
  top: 15px;
  right: 30px;
}

.custom-treeText {
  width: 160px;
  display: inline-block;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.param-content {
  display: flex;
  align-items: center;
  margin-bottom: 10px;
  color: #606266;

  > :first-child {
    margin-right: 10px;
  }
}

.width200 {
  width: 200px;
}

.filter-tree {
  margin-top: 10px;
}

.tree-body {
  overflow: auto;

  .el-tree {
    padding-bottom: 10px;
  }
}

.strategy-body {
  margin-top: 20px;
  margin-left: 50px;
}

.dialog-content {
  width: 100%;
  height: 40px;
  line-height: 40px;
  text-align: center;
}

</style>
