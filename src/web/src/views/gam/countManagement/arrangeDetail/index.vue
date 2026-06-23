<template>
  <div class="main-body">
    <div class="top-tool">
      <div class="name-version">
        <span class="arrange-name" @click="showVersionClick">{{ displayAlgorithmName }}</span>
      </div>
      <div>
        <el-button class="save-btn" type="primary" size="small" @click="saveClick()">{{ t('action.save') }}</el-button>
        <el-button class="save-btn" type="primary" size="small" @click="saveClick('export')">{{ t('action.saveAndExport') }}</el-button>
        <el-button class="save-btn" size="small" @click="goBack">{{ t('action.goBack') }}</el-button>
      </div>
    </div>

    <div class="main-container">
      <div class="right-body">
        <el-tabs v-model="tabActiveName" type="border-card">
          <el-tab-pane :label="t('glossary.businessFlow')" name="flow">
            <div id="arrange-content" class="arrange-content" ref="arrangeContentRef">
              <div class="arrange-select">
                <el-select v-model="selectedTemplate" :placeholder="t('field.selectTemplate')" size="small" style="width: 200px" filterable @change="handleTemplateChange">
                  <el-option v-for="item in templateList" :key="item.algorithmCode" :label="resolveResourceAlgorithmName(item)" :value="item.algorithmCode" />
                </el-select>
              </div>
              <arrange-flow v-if="showArrangeFlow" ref="flowRef" :width="width" :height="height" :algorithmData="algorithmData" :algorithmMetadata="algorithmMetadata" :actionList="actionList" :atomicCode="$route.query.algorithmId" @onMetadata="syncMetadata"></arrange-flow>
            </div>
          </el-tab-pane>
          <el-tab-pane :label="t('glossary.detectionArea')" name="detection">
            <detection ref="detectionRef" :algorithmMetadata="algorithmMetadata"></detection>
          </el-tab-pane>
          <el-tab-pane :label="t('glossary.paramConfig')" name="config">
            <parameter-setting ref="configRef" :algorithmMetadata="algorithmMetadata"></parameter-setting>
          </el-tab-pane>
        </el-tabs>
      </div>
    </div>
  </div>
</template>

<script setup>
import {
  ref,
  computed,
  watch,
  onMounted,
  onBeforeUnmount,
  nextTick,
  getCurrentInstance
} from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessageBox, ElMessage } from 'element-plus'
import ArrangeFlow from './flow/ArrangeFlow.vue'
import Detection from './flow/Detection.vue'
import ParameterSetting from './flow/ParameterSetting.vue'
import { v4 } from 'uuid'
import moment from 'moment'
import _ from 'lodash'
import EventBus from '@/components/eventBus.js'
import { currentLocale, t } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const route = useRoute()
const router = useRouter()
const { proxy } = getCurrentInstance()
const $API = proxy.$API

// Refs
const flowRef = ref(null)
const detectionRef = ref(null)
const configRef = ref(null)
const arrangeContentRef = ref(null)

// Data
const supplier = ref('')
const algorithmUsage = ref('')
const showArrangeFlow = ref(false)
const algorithmName = ref('')
const baseAlgorithmData = ref(null)
const algorithmVersion = ref('')
const actionList = ref([])
const algorithmData = ref({})
const algorithmMetadata = ref({})
const tabActiveName = ref('flow')
const templateList = ref([])
const selectedTemplate = ref('')
const width = ref(0)
const height = ref(600)

const displayAlgorithmName = computed(() => {
  currentLocale.value
  return resolveResourceAlgorithmName(baseAlgorithmData.value) || algorithmName.value
})

// Watch：切换页签时保存离开页签的数据，确保修改保留
watch(
  () => tabActiveName.value,
  (newVal, oldVal) => {
    if (oldVal === 'detection') {
      handleDetcetionData()
    } else if (oldVal === 'config') {
      handleConfigData()
    } else if (oldVal === 'flow') {
      showArrangeFlow.value && handleMetaData()
    }
  },
  { flush: 'post' }
)

// Lifecycle
onMounted(() => {
  supplier.value = route.query?.supplier
  algorithmUsage.value = route.query?.algorithmUsage
  showArrangeFlow.value = true
  tabActiveName.value = showArrangeFlow.value ? 'flow' : 'detection'

  // 自动收起侧边栏，增大画布可视范围
  EventBus.$emit('sidebar:collapse')

  updateCanvasSize()
  window.addEventListener('resize', handleResize)
  getActionList(algorithmUsage.value)
  getTemplateList()
  EventBus.$on('layout:resize', () => {
    setTimeout(() => updateCanvasSize(), 150)
  })
  if (arrangeContentRef.value && typeof ResizeObserver !== 'undefined') {
    const ro = new ResizeObserver(() => updateCanvasSize())
    ro.observe(arrangeContentRef.value)
    arrangeContentRef.value.__ro = ro
  }
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  EventBus.$off && EventBus.$off('layout:resize')
  if (arrangeContentRef.value && arrangeContentRef.value.__ro) {
    arrangeContentRef.value.__ro.disconnect()
    arrangeContentRef.value.__ro = null
  }
  // 离开编排页面时恢复侧边栏
  EventBus.$emit('sidebar:expand')
})

let resizeTimer = null
const handleResize = () => {
  if (resizeTimer) clearTimeout(resizeTimer)
  resizeTimer = setTimeout(() => updateCanvasSize(), 100)
}

const updateCanvasSize = () => {
  nextTick(() => {
    requestAnimationFrame(() => {
      const el = arrangeContentRef.value
      if (!el) {
        width.value = Math.max(300, window.innerWidth - 160)
        height.value = Math.max(300, window.innerHeight - 260)
        return
      }
      const rect = el.getBoundingClientRect()
      const viewportH = window.innerHeight
      const bottomPadding = 24
      width.value = Math.max(300, rect.width)
      height.value = Math.max(300, viewportH - rect.top - bottomPadding)
    })
  })
}

const getDetail = (algorithmId, isChooseModel) => {
  const params = {
    id: algorithmId,
    filePath: '/appfs/cosmo_wander/cwai_data/resource/algorithm'
    // id: '110020'
  }
  $API.algorithmLayoutDetail(params).then((res) => {
    const { resData } = res
    const prevName = algorithmName.value
    if (!isChooseModel) {
      baseAlgorithmData.value = resData
    }
    algorithmName.value = isChooseModel ? prevName : resolveResourceAlgorithmName(resData)
    algorithmData.value = resData
    algorithmMetadata.value = JSON.parse(algorithmData.value.algorithmMetadata)
    if (isChooseModel) {
      algorithmData.value.algorithmCode = route.query.algorithmId
    }

    // 处理自定义参数
    let customMetadata = []
    algorithmMetadata.value.params.forEach((item) => {
      if (!item.level) {
        customMetadata.push(item)
      }
    })
    // 如果存在自定义任务参数，保存到编排中，条件判断使用
    if (customMetadata.length > 0) {
      localStorage.setItem('customMetadata', JSON.stringify(customMetadata))
    }

    if (algorithmMetadata.value?.region?.heads?.length == 0) {
      initMetadata()
    }
  })
}

const initMetadata = () => {
  algorithmMetadata.value = {
    params: [],
    region: {
      heads: [
        {
          defaultValue: '',
          description: t('glossary.regionNameDesc'),
          failedTip: t('glossary.regionNameFailedTip'),
          isColumn: true,
          key: 'name',
          name: t('glossary.regionName'),
          options: null,
          range: '',
          regexpr: '/^\\S{1,32}$/',
          step: null,
          type: 'text'
        }
      ]
    },
    regionType: 'hexagon',
    scheduleSupport: false
  }
}

const getTemplateList = () => {
  const params = {
    algorithmUsage: algorithmUsage.value ? parseInt(algorithmUsage.value) : -1,
    filePath: '/appfs/cosmo_wander/cwai_data/resource/algorithm_template',
  }
  $API.algorithmLayoutList(params).then((res) => {
    const { resData } = res
    templateList.value = resData?.list || []
  })
}

const handleTemplateChange = (val) => {
  if (!val) return
  ElMessageBox.confirm(t('validate.confirmSwitchTemplate'), t('common.notice'), {
    confirmButtonText: t('action.confirm'),
    cancelButtonText: t('action.cancel'),
    type: 'warning'
  })
    .then(() => {
      getTemplateDetail(val)
    })
    .catch(() => {
      selectedTemplate.value = ''
    })
}

const getTemplateDetail = (algorithmId) => {
  const params = {
    id: algorithmId,
    filePath: '/appfs/cosmo_wander/cwai_data/resource/algorithm_template'
  }
  $API.algorithmLayoutDetail(params).then((res) => {
    const { resData } = res
    const prevName = algorithmName.value
    algorithmName.value = prevName
    algorithmData.value = resData
    algorithmMetadata.value = JSON.parse(algorithmData.value.algorithmMetadata)
    algorithmData.value.algorithmCode = route.query.algorithmId

    // 处理自定义参数
    let customMetadata = []
    algorithmMetadata.value.params.forEach((item) => {
      if (!item.level) {
        customMetadata.push(item)
      }
    })
    if (customMetadata.length > 0) {
      localStorage.setItem('customMetadata', JSON.stringify(customMetadata))
    }

    if (algorithmMetadata.value?.region?.heads?.length == 0) {
      initMetadata()
    }
  })
}

const getActionList = (algorithmUsage) => {
  const params = {
    actionUsage: algorithmUsage == '2' ? 2 : 1
  }
  $API.atomicActionList(params).then((res) => {
    const { resData } = res
    actionList.value = resData?.list || []
    getDetail(route.query.algorithmId)
  })
}

// 已移除模板选择与布局列表逻辑（未在页面中使用）

const saveClick = (type) => {
  // 先同步三个页签的数据，确保保存时数据最新
  handleDetcetionData()
  handleConfigData()
  handleMetaData()

  let params =
    showArrangeFlow.value &&
    flowRef.value &&
    typeof flowRef.value.saveFlowData === 'function'
      ? flowRef.value.saveFlowData()
      : {}
  // 覆盖基础信息，确保与当前版本保持一致
  params.algorithmId = route.query.algorithmId || params.algorithmId
  params.algorithmCategory =
    algorithmData.value.algorithmCategory || params.algorithmCategory
  params.algorithmUsage =
    algorithmData.value.algorithmUsage || params.algorithmUsage
  params.remark = algorithmData.value.remark || params.remark
  params.filePath = '/appfs/cosmo_wander/cwai_data/resource/algorithm'
  params.confVersionId = algorithmData.value.confVersionId
  params.configVersionName = algorithmData.value.configVersionName || t('common.default')
  if (!showArrangeFlow.value) {
    params.algorithmId = algorithmData.value.algorithmCode
    params.algorithmMetadata = algorithmMetadata.value
    params.algorithmProcessdata = JSON.parse(
      algorithmData.value.algorithmProcessdata
    )
    params.atomicList = JSON.parse(algorithmData.value.atomicList)
    params.remark = algorithmData.value.remark
  } else {
    // 使用最新的 algorithmMetadata 计算结果
    params.algorithmMetadata = JSON.stringify(algorithmMetadata.value || {})
  }
  console.log(params, '=====params======')
  $API.saveAlgorithmLayout(params).then((res) => {
    if (type === 'sync') {
      const algorithmId = route.query?.algorithmId
      $API.syncAlgParamToTaskConfig({ ids: [algorithmId] }).then(() => {
        ElMessage.success(t('common.syncSucceeded'))
      })
    } else if (type === 'silent') {
      getDetail(algorithmData.value.algorithmCode)
    } else if (type === 'export') {
      exportAlgorithmic()
    } else {
      ElMessage.success(t('common.operationSucceeded'))
    }
  })
}

// 版本弹窗及切换逻辑已移除（页面未使用）

// 启用/停用版本逻辑已移除（页面未使用）

// 编辑版本信息逻辑已移除（页面未使用）

// 检测区域保存：从子组件读取并合并到 algorithmMetadata
const handleDetcetionData = () => {
  if (
    !detectionRef.value ||
    typeof detectionRef.value.saveDetection !== 'function'
  ) {
    return
  }
  const detection = detectionRef.value.saveDetection()
  algorithmMetadata.value.regionType = detection.areaType
  algorithmMetadata.value.scheduleSupport = detection.scheduleSupport
  algorithmMetadata.value.enableShieldedRegion = detection.enableShieldedRegion
  algorithmMetadata.value.maxAreaCount = detection.maxAreaCount
  algorithmMetadata.value.defaultFullScreen = detection.defaultFullScreen
  if (detection.associatedAreaChecked) {
    algorithmMetadata.value.region = algorithmMetadata.value.region || {}
    algorithmMetadata.value.region.areasTitle = [
      {
        name: detection.associatedAreaConfig.mainName,
        regionType: null,
        dependsOn: null
      },
      {
        name: detection.associatedAreaConfig.associatedName,
        regionType: detection.associatedAreaConfig.regionType,
        dependsOn: null
      }
    ]
  } else {
    if (!algorithmMetadata.value.region) algorithmMetadata.value.region = {}
    algorithmMetadata.value.region.areasTitle = null
  }
}

// 参数配置保存：从子组件读取并合并到 algorithmMetadata
const handleConfigData = () => {
  if (
    !configRef.value ||
    typeof configRef.value.saveParamConfig !== 'function'
  ) {
    return
  }
  const params = configRef.value.saveParamConfig()
  algorithmMetadata.value.params = params
  const customMetadata = []
  algorithmMetadata.value.params.forEach((item) => {
    if (!item.level) {
      customMetadata.push(item)
    }
  })
  if (customMetadata.length > 0) {
    localStorage.setItem('customMetadata', JSON.stringify(customMetadata))
  } else {
    localStorage.removeItem('customMetadata')
  }
}

const handleMetaData = () => {
  if (!showArrangeFlow.value) return
  const metaData = flowRef.value.saveMetaDataParams()
  let newMetaDataParams = []
  //  先处理参数配置中的自定义参数，和业务流程中被删的参数
  algorithmMetadata.value.params.forEach((item) => {
    //  参数配置中自定义参数
    if (!item.level) {
      newMetaDataParams.push(item)
    } else {
      // 判断业务流程metaData中是否还存在，存在则添加，值也不做修改
      const existParam = _.find(metaData, { key: item.key })
      existParam && newMetaDataParams.push(item)
    }
  })
  //  再处理业务流程中新增的参数
  metaData.forEach((item) => {
    const existFlag = _.find(algorithmMetadata.value.params, {
      key: item.key
    })
    delete item.dependsOn
    //  只添加业务流程metaData的数组新增的参数
    !existFlag && newMetaDataParams.push(item)
  })

  // 兼容盒子不支持number类型，暂时未知是否有其他类型
  newMetaDataParams.forEach((item) => {
    if (typeof item.defaultValue == 'number') {
      item.defaultValue = item.defaultValue + ''
    }
    if (typeof item.value == 'number') {
      item.value = item.value + ''
    }
  })

  algorithmMetadata.value.params = newMetaDataParams
  console.log(metaData, '===handleMetaData====', newMetaDataParams)
}

const syncMetadata = (val) => {
  if (val && val.length > 0) {
    val.forEach((obj) => {
      // 判断是否已经存在的参数，存在则不覆盖用户自定义的值
      const existFlag = _.find(algorithmMetadata.value.params, {
        key: obj.key
      })
      if (!existFlag) {
        let newObj = {}
        newObj = Object.assign({}, obj)
        delete newObj.position
        algorithmMetadata.value.params.push(newObj)
      }
    })
  }
  console.log(algorithmMetadata.value.params, 'algorithmMetadata.value')
}

// 版本列表相关旧逻辑已移除（页面未使用）

const exportAlgorithmic = () => {
  let i = {
    algorithmCategory: algorithmData.value.algorithmCategory,
    algorithmCode: algorithmData.value.algorithmCode,
    algorithmName: algorithmData.value.algorithmName,
    algorithmUsage: algorithmData.value.algorithmUsage,
    supplier: supplier.value,
    confVersionId: algorithmData.value.confVersionId
  }
  const year = moment().format('YYYY-MM-DD')
  const time = moment().format('HH:mm:ss')
  const randomNumber = Math.floor(Math.random() * 9000) + 1000
  const zipName = `${year}_${time}_${randomNumber}.tar.gz`
  let x = new XMLHttpRequest()
  x.open('post', $API.exportSingleAlg(), true)
  x.responseType = 'blob'
  x.setRequestHeader('Content-Type', 'application/json;charset=UTF-8')
  x.setRequestHeader('token', localStorage.getItem('token'))
  x.setRequestHeader('lang', localStorage.getItem('language'))
  x.setRequestHeader('mtk', localStorage.getItem('token'))
  x.onload = function (_e) {
    if (this.status == 200) {
      let blob = this.response
      let a = document.createElement('a')
      let url = window.URL.createObjectURL(blob)
      a.href = url
      a.download = zipName
      a.click()
    }
  }
  x.send(JSON.stringify(i))
}

const goBack = () => {
  router.back()
}
</script>

<style scoped lang="scss">
.main-body {
  display: flex;
  flex-direction: column;
  padding: 0 10px;
}

.top-tool {
  background: transparent;
  margin-bottom: 10px;
  height: 35px;
  display: flex;
  justify-content: space-between;
  align-content: center;

  .save-btn {
    margin-left: 10px;
  }
}

@keyframes spin {
  from {
    transform: rotate(0deg);
  }

  to {
    transform: rotate(360deg);
  }
}

.spin-element {
  animation: spin 2s linear infinite;
}

.arrange-content {
  position: relative;
}

.arrange-name {
  display: inline-block;
  line-height: 32px;
  margin-right: 10px;
  color: #606266;
  font-size: 20px;
  /*设置字体大小*/
  font-weight: 400;
  /*设置字体粗细*/
  // -webkit-text-stroke: 1px #000; /*文字描边*/
  // -webkit-text-fill-color: transparent; /*设置文字的填充颜色*/
}

.arrange-select {
  position: absolute;
  display: inline-block;
  padding: 10px;
  top: 10px;
  left: 10px;
  z-index: 100;
}

.main-container {
  display: flex;
  flex: 1;
}

.right-body {
  flex: 1;
  height: calc(100vh - 195px);

  :deep(.el-tabs--border-card) {
    border: 1px solid var(--border-color);
    border-radius: var(--radius-sm);
    box-shadow: var(--shadow-sm);
    background: var(--bg-white);
    overflow: hidden;
  }

  :deep(.el-tabs--border-card > .el-tabs__header) {
    background: var(--bg-secondary);
    border-bottom: 1px solid var(--border-color);
    margin: 0;
    padding: 8px 12px;
  }

  :deep(.el-tabs--border-card .el-tabs__item) {
    border-radius: var(--radius-sm);
    color: var(--text-secondary);
    margin-right: 6px;
    transition: all 0.2s ease;
    padding: 8px 14px;
  }

  :deep(.el-tabs--border-card .el-tabs__item:hover) {
    background: rgba(49, 130, 206, 0.08);
    color: var(--primary-color);
  }

  :deep(.el-tabs--border-card .el-tabs__item.is-active) {
    background: linear-gradient(
      135deg,
      var(--primary-color) 0%,
      var(--primary-light) 100%
    );
    color: #ffffff;
    box-shadow: 0 2px 8px rgba(49, 130, 206, 0.25);
  }

  :deep(.el-tabs__content) {
    padding: 10px;
    box-sizing: border-box;
    height: calc(100vh - 200px);
    overflow-y: scroll;
  }
}

.name-version {
  display: flex;
  align-items: center;
  color: #909399;
}

.expand-version {
  cursor: pointer;
  margin-left: 5px;
  // animation: shine 2.4s infinite;
}

.fold-version {
  transform: rotate(180deg);

  @keyframes shine {
    0%,
    100% {
      text-shadow: 0 0 10px white, 0 0 10px white;
    }

    50% {
      text-shadow: 0 0 10px #409eff, 0 0 40px #409eff;
    }
  }
}

.version-span {
  display: inline-block;
  padding: 5px 10px;
  border-radius: 5px;
  box-shadow: 2px 2px 5px rgba(0, 0, 0, 0.3),
    -2px -2px 5px rgba(255, 255, 255, 0.5);
}

.form-content {
  width: calc(100% - 70px);
}

.delete-body {
  line-height: 80px;
  text-align: center;
}

.icon-blue {
  color: #409eff;
}

.icon-red {
  color: red;
}
</style>
