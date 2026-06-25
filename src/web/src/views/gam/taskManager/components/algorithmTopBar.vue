<template>
  <div class="topBar-wrap">
    <div class="form-search">
      <div class="formDiv">
        <span class="formTitle">{{ t('field.channelName') }}{{ localeColon }}</span>
        <el-input class="el-form" v-model="formData.channelName" :placeholder="t('placeholder.enter', { field: t('field.channelName') })" size="small"></el-input>
      </div>
      <!-- <div class="formDiv">
        <span class="formTitle">{{ t('glossary.accessType') }}{{ localeColon }}</span>
        <el-select class="el-form" v-model="formData.channelType"  size="small" clearable>
          <el-option v-for="item in channelTypeList" :key="item.value" :label="item.label" :value="item.value">
          </el-option>
        </el-select>
      </div> -->
      <div class="formDiv" v-if="active">
        <span class="formTitle">{{ t('field.channelStatus') }}{{ localeColon }}</span>
        <el-select class="el-form" v-model="formData.channelStatus" :placeholder="t('placeholder.select', { field: t('field.channelStatus') })" size="small">
          <el-option v-for="item in channelStatusList" :key="item.value" :label="item.label" :value="item.value">
          </el-option>
        </el-select>
      </div>
      <!-- <div class="formDiv">
        <span class="formTitle">{{ t('glossary.algorithmService') }}{{ localeColon }}</span>
        <el-select class="el-form" filterable v-model="formData.algorithmUsage"  size="small" @change="dataSourceChange">
          <el-option v-for="item in algorithmTree" :key="item.value" :label="item.label" :value="item.value">
          </el-option>
        </el-select>
      </div>
      <div class="formDiv" v-if="active">
        <span class="formTitle">{{ t('glossary.runningStatus') }}{{ localeColon }}</span>
        <el-select class="el-form" v-model="formData.taskStatus"  size="small">
          <el-option v-for="item in stateData" :key="item.value" :label="item.label" :value="item.value">
          </el-option>
        </el-select>
      </div> -->

    </div>
    <div class="btnBar">
      <el-button class="mv-el-button" type="primary" size="small" @click="getFormData">{{ t('action.search') }}</el-button>
      <el-button size="small" @click="resetFormData">{{ t('action.reset') }}</el-button>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, getCurrentInstance } from 'vue'
import { t, localeColon } from '@/i18n'

const props = defineProps({
  active: {
    type: Boolean,
    default: false
  }
})
const emit = defineEmits(['init', 'search', 'reset1', 'reset'])
const { proxy } = getCurrentInstance()

const platformType = ref(window.localStorage.getItem('platformType'))
const formData = reactive({
  channelName: '',
  taskStatus: '',
  status: '',
  channelType: '',
  channelStatus: -1,
  algorithmUsage: ''
})

const algorithmTree = computed(() => [{ label: t('common.all'), value: '' }])
const intellectData = ref([])
const dataSource = computed(() => [
  { label: t('common.all'), value: '' },
  { label: t('glossary.videoAnalysis'), value: '1' },
  { label: t('glossary.imageAnalysis'), value: '2' }
])
const stateData = computed(() => [
  { label: t('common.all'), value: '' },
  { label: t('status.stopped'), value: '0' },
  { label: t('status.inProgress'), value: '1' },
  { label: t('status.paused'), value: '2' }
])
const channelStatusList = computed(() => [
  { label: t('common.all'), value: -1 },
  { label: t('status.offline'), value: 0 },
  { label: t('status.online'), value: 1 }
])
const runTypeData = computed(() => [
  { label: t('common.all'), value: '' },
  { label: t('glossary.realtime'), value: '0' },
  { label: t('glossary.polling'), value: '1' }
])
const timeTemplateList = ref([])
const schedulePollingList = ref([])
const channelTypeList = computed(() => [
  { label: t('common.all'), value: '' },
  { label: 'RTSP', value: 0 },
  { label: t('glossary.offlineVideo'), value: 3 }
])
const algorithmCategoryList = computed(() => [
  { label: t('glossary.faceAndBody'), value: '1' },
  { label: t('glossary.detection'), value: '2' },
  { label: t('glossary.detection'), value: '3' },
  { label: t('glossary.countingAnalytics'), value: '8' },
  { label: t('glossary.countingAnalytics'), value: '9' },
  { label: t('glossary.vehicleAnalysis'), value: '10' },
  { label: t('glossary.countingAnalytics'), value: '11' }
])

const getServiceList = () => {
  const params = { pageNum: 1, pageSize: 1000 }
  // proxy.$API.algorithmInquire(params)
}

const init = () => {
  emit('init', formData)
}

const getFormData = (id) => {
  window.localStorage.setItem('taskCustId', formData.custId)
  const joinType = window.localStorage.getItem('joinType')
  let regionId = null
  if (id && typeof id === 'number') regionId = id
  emit('search', formData, joinType, regionId)
}

const resetFormData = () => {
  formData.channelName = ''
  window.localStorage.setItem('joinType', '1')
  formData.custId = ''
  formData.algorithmId = ''
  formData.algorithmUsage = ''
  formData.taskStatus = ''
  formData.channelStatus = -1
  intellectData.value = []
  emit('reset1')
  emit('reset', formData)
  localStorage.setItem('currentCustId', '')
}

const dataSourceChange = (value) => {
  const pType = localStorage.getItem('platformType')
  if (!formData.custId && pType == 1) {
    return proxy.$message.warning(t('validate.selectCustomer'))
  }
  if (value == '') {
    formData.algorithmId = ''
    intellectData.value = []
  } else {
    formData.algorithmId = ''
    const params = {
      algorithmUsage: Number(value),
      custId: window.localStorage.getItem('taskCustId')
        ? window.localStorage.getItem('taskCustId')
        : window.localStorage.getItem('currentCustId')
    }
    proxy.$API.selectAlgorithmInfo(params).then((res) => {
      const resData = res.data
      const data = { label: t('common.all'), value: '' }
      const algorithmName = [data]
      for (let item in resData) {
        const exclist = {}
        exclist.value = resData[item].algorithmId
        exclist.label = resData[item].algorithmName
        algorithmName.push(exclist)
      }
      intellectData.value = algorithmName
    })
  }
}

const categoryChange = (val) => {
  if (!formData.custId && platformType.value == '1') {
    return proxy.$message.warning(t('validate.selectCustomerFirst'))
  }
  if (val == '0') {
    getTimetemplate()
  } else if (val == '1') {
    getSchedulePollingList()
  }
}

const getTimetemplate = () => {
  formData.scheduleId = ''
  const params = { custId: formData.custId }
  proxy.$API.selectScheduleInfo(params).then((res) => {
    timeTemplateList.value = [
      { scheduleName: t('common.all'), scheduleId: '' },
      ...res.data
    ]
  })
}

const getSchedulePollingList = () => {
  formData.pollingId = ''
  const params = { custId: formData.custId }
  proxy.$API.schedulePollingList(params).then((res) => {
    const { resData } = res.data
    schedulePollingList.value = [{ name: t('common.all'), id: '' }, ...resData]
  })
}

onMounted(() => {
  getServiceList()
  init()
})
</script>

<style scoped lang="scss">
.topBar-wrap {
  flex-shrink: 0;
  position: relative;
  background: #fff;
  padding: 0 20px 20px 24px;
  margin: 12px 12px 0px 12px;
  // box-shadow: 3px 4px 15px 2px #cacaca;
  border-radius: 2px;
  display: flex;
  overflow: hidden;
  transition: all 300ms;

  .form-search {
    flex: 1;
    display: flex;
    flex-wrap: wrap;
    align-content: flex-start;

    .formDiv {
      display: flex;
      align-items: center;
      margin: 20px 30px 0 0px;

      .formTitle {
        display: inline-block;
        margin-right: 10px;
        font-size: 14px;
        text-align: right;
        color: #303133;
      }

      .el-form {
        width: 160px;
      }

      .el-formDate {
        width: 420px;
      }

      .doublesNum {
        .doublesText-line {
          width: 30px;
          height: 32px;
          line-height: 32px;
          text-align: center;
        }
      }
    }
  }

  .btnBar {
    flex-shrink: 0;
    display: flex;
    align-items: flex-end;
    transition: all 300ms;

    .optionBtn {
      margin-left: 10px;
      line-height: 32px;
      color: #1890ff;
      font-size: 14px;
      cursor: pointer;

      >i {
        display: inline-block;
        transition: all 300ms;

        &.retract {
          transform: rotate(180deg);
        }
      }
    }
  }

  :deep(.el-input__inner) {
    height: 32px;
    line-height: 32px;
  }
}
</style>
