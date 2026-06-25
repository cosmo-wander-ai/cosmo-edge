<template>
  <div class="alarm-record">
    <TopBar 
      ref="topBarRef" 
      :dataSouce="topBarData" 
      :formData="formData" 
      :labelWidth="60" 
      @search="searchList" 
    />
    
    <div class="table-container">
      <div class="table-header">
        <div class="table-tools">
          <!-- <el-button 
            v-if="runMode != 1" 
            type="primary" 
            size="small" 
            @click="handleBatchDelete"
            :disabled="multipleSelections.length === 0"
          >
            批量删除
          </el-button> -->
          <el-button 
            type="primary" 
            size="small" 
            @click="handleExport"
            :disabled="tableData.length === 0"
          >
            {{ t('event.dataExport') }}
          </el-button>
        </div>
      </div>

      <!-- 列表视图 -->
      <el-table 
        :data="tableData" 
        :header-cell-style="{ background: '#fafafa' }" 
        style="width: 100%"
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" min-width="55" />
        <el-table-column type="index" :index="getIndex" :label="t('field.no')" width="80" />
        <el-table-column prop="plateNumber" :label="t('event.vehiclePlateNumber')" min-width="100px">
          <template #default="{ row }">
            {{ returnBaseInfo(row.property, 'vehicle', 'plate') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.vehiclePlatePhoto')" width="100">
          <template #default="{ row }">
            <el-image 
              v-if="row.detectedPicture"
              :src="row.detectedPicture" 
              fit="scale-down"
              style="width: 60px; height: 60px;cursor: pointer;" 
              @click="handleImageClick(row, 'detected')"
              @error="handleImgError($event, row, 'detected')" 
              :key="row.id + '_detected'"
            />
          </template>
        </el-table-column>
        <el-table-column :label="t('event.fullImage')" width="120">
          <template #default="{ row }">
            <el-image 
              v-if="row.fullPicture"
              :src="row.fullPicture" 
              fit="scale-down" 
              style="width: 60px; height: 60px;cursor: pointer;"
              @click="handleImageClick(row, 'full')" 
              @error="handleImgError($event, row, 'full')"
              :key="row.id + '_full'"
            />
          </template>
        </el-table-column>
        <el-table-column :label="t('event.vehicleType')" width="100">
          <template #default="{ row }">
            {{ returnBaseInfo(row.property, 'vehicle', 'vehicleClass') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.vehiclePlateColor')" width="110">
          <template #default="{ row }">
            {{ returnBaseInfo(row.property, 'vehicle', 'plateColor') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.vehicleBodyColor')" width="110">
          <template #default="{ row }">
            {{ returnBaseInfo(row.property, 'vehicle', 'vehicleColor') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.vehicleOrientation')" width="110">
          <template #default="{ row }">
            {{ returnBaseInfo(row.property, 'vehicle', 'orientation') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.enterTime')" min-width="155">
          <template #default="{ row }">
            {{ returnTime(row.property, 'inAreaTime') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.leaveTime')" min-width="155">
          <template #default="{ row }">
            {{ returnTime(row.property, 'outAreaTime') }}
          </template>
        </el-table-column>
        <el-table-column prop="channelName" :label="t('field.channelName')" width="110"/>
        <el-table-column :label="t('event.captureTime')" min-width="155">
          <template #default="{ row }">
            {{ dateFormat(row.timestamp) }}
          </template>
        </el-table-column>
        <el-table-column :label="t('field.status')" width="90">
          <template #default="{ row }">
            <span :style="{ color: row.reportStatus === 1 ? '#67C23A' : '#F56C6C' }">
              {{ row.reportStatus === 1 ? t('event.uploaded') : t('event.notUploaded') }}
            </span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.actions')" fixed="right">
          <template #default="{ row }">
            <div class="operation-tools">
              <el-button link class="primary-text" @click="handleDetail(row)">{{ t('action.details') }}</el-button>
              <el-button link class="primary-text" v-if="row.video" @click="onCheckVideo(row, 1)">{{ t('event.videoPlayback') }}</el-button>
              <el-button link class="danger-text" v-if="runMode != 1" @click="handleDelete(row)">{{ t('action.delete') }}</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <div class="pagination-container">
        <el-pagination 
          @size-change="handleSizeChange" 
          @current-change="handleCurrentChange"
          :current-page="pageData.pageNum" 
          :page-sizes="[10, 20, 50, 100]" 
          :page-size="pageData.pageSize"
          :total="pageData.total" 
          layout="total, sizes, prev, pager, next, jumper"
        />
      </div>
    </div>

    <car-detail-dialog 
      v-model:visible="detailDialogVisible" 
      :detailData="detailData"
      :vehicleDictMap="vehicleDictMap"
    />

    <video-frequency 
      v-model:visible="videoDialogVisiable" 
      :algorithmCode="currentEvent?.algorithmCode || ''"
      :closable="false" 
      :url="currentEvent?.video || ''"
      :structureDataUrl="currentEvent?.videostructured || ''" 
      :title="downloadName"
    />
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, getCurrentInstance } from 'vue'
import TopBar from '@/components/TopBar.vue'
import moment from 'moment'
import videoFrequency from '../components/videoPlaying265.vue'
import CarDetailDialog from '../components/carDetailDialog.vue'
import { t, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()

const runMode = ref(0)
const topBarRef = ref(null)
const tableData = ref([])
const multipleSelections = ref([])
const detailDialogVisible = ref(false)
const detailData = ref({})
const videoDialogVisiable = ref(false)
const currentEvent = ref(null)
const downloadName = ref('')
const vehicleDictMap = ref({})

const topBarData = reactive({
  formList: [
    {
      labelI18nKey: 'common.date',
      type: 'datetimerange',
      model: 'times',
      valueFormat: 'YYYY-MM-DD HH:mm:ss',
      clearable: false
    },
    {
      labelI18nKey: 'field.channelName',
      type: 'select',
      model: 'videoChannelName',
      filterable: true,
      dataList: [{ labelI18nKey: 'common.all', value: '' }]
    },
    {
      labelI18nKey: 'event.vehiclePlateNumber',
      model: 'propStr'
    },
    {
      labelI18nKey: 'event.vehiclePlateColor',
      type: 'select',
      model: 'propRelatedColor',
      dataList: []
    },
    {
      labelI18nKey: 'event.vehicleBodyColor',
      type: 'select',
      model: 'propColor',
      dataList: []
    },
    {
      labelI18nKey: 'event.vehicleType',
      type: 'select',
      model: 'propType',
      dataList: []
    },
    {
      labelI18nKey: 'event.vehicleOrientation',
      type: 'select',
      model: 'propDirection',
      dataList: []
    },
    {
      labelI18nKey: 'field.status',
      type: 'select',
      model: 'reportStatus',
      clearable: false,
      filterable: true,
      dataList: [
        { labelI18nKey: 'common.all', value: '' },
        { labelI18nKey: 'event.notUploaded', value: 0 },
        { labelI18nKey: 'event.uploaded', value: 1 }
      ]
    }
  ]
})

const formData = reactive({
  times: null,
  videoChannelName: '',
  algorithmCode: '',
  reportStatus: '',
  propStr: '',
  propRelatedColor: '',
  propColor: '',
  propType: '',
  propDirection: ''
})

const pageData = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})

const dateFormat = (value) => {
  if (!value) return ''
  return moment(value).format('YYYY-MM-DD HH:mm:ss')
}

const getCarDict = () => {
  const params = {
    keys: ['vehiclecolor', 'vehicleplatecolor', 'vehicleclass', 'vehicleorientation']
  }
  proxy.$API.getCarDict(params).then((res) => {
    const { resData } = res
    const dictList = resData.infos || []

    const dictMap = {}
    dictList.forEach((item) => {
      if (item && item.key && item.infos) {
        dictMap[item.key] = item.infos.map((info) => ({
          label: info.key,
          value: info.value
        }))
      }
    })

    if (dictMap.vehicleplatecolor) {
      topBarData.formList[3].dataList = [
        { labelI18nKey: 'common.all', value: '' },
        ...dictMap.vehicleplatecolor
      ]
    }

    if (dictMap.vehiclecolor) {
      topBarData.formList[4].dataList = [
        { labelI18nKey: 'common.all', value: '' },
        ...dictMap.vehiclecolor
      ]
    }

    if (dictMap.vehicleclass) {
      topBarData.formList[5].dataList = [
        { labelI18nKey: 'common.all', value: '' },
        ...dictMap.vehicleclass
      ]
    }

    if (dictMap.vehicleorientation) {
      topBarData.formList[6].dataList = [
        { labelI18nKey: 'common.all', value: '' },
        ...dictMap.vehicleorientation
      ]
    }
    
    vehicleDictMap.value = dictMap
  })
}

const getChannelList = () => {
  proxy.$API.getChannelList({ pageNum: 1, pageSize: 1000 }).then((res) => {
    const { resData } = res
    const channelList = resData.rows.map((item) => ({
      label: item.channelName,
      value: item.channelName
    }))
    topBarData.formList[1].dataList = [
      { labelI18nKey: 'common.all', value: '' },
      ...channelList
    ]
  })
}

const init = () => {
  const params = {
    timeBegin: moment(formData.times[0]).valueOf(),
    timeEnd: moment(formData.times[1]).valueOf(),
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize,
    algorithmCodes: formData.algorithmCode ? [formData.algorithmCode] : [],
    categorys: ['10'],
    videoChannelName: formData.videoChannelName || undefined,
    reportStatus: formData.reportStatus === '' ? undefined : formData.reportStatus,
    propStr: formData.propStr,
    propColor: formData.propColor,
    propRelatedColor: formData.propRelatedColor,
    propType: formData.propType,
    propDirection: formData.propDirection
  }
  proxy.$API.boxQueryEvent(params).then((res) => {
    const { resData } = res
    tableData.value = resData.rows || []
    pageData.total = resData.total || 0
  })
}

const handleSizeChange = (val) => {
  pageData.pageSize = val
  init()
}

const handleCurrentChange = (val) => {
  pageData.pageNum = val
  init()
}

const getIndex = (index) => {
  return (pageData.pageNum - 1) * pageData.pageSize + index + 1
}

const searchList = () => {
  init()
}

const handleSelectionChange = (val) => {
  multipleSelections.value = val
}

const handleDetail = (row) => {
  detailData.value = row
  detailDialogVisible.value = true
}

const handleDelete = (row) => {
  proxy.$confirm(t('validate.deleteConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = { ids: [row.id] }
    console.log('======删除', params)
  })
}

const handleBatchDelete = () => {
  proxy.$confirm(t('validate.deleteConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = { ids: multipleSelections.value.map(item => item.id) }
    console.log('======批量删除', params)
  })
}

const handleExport = () => {
  const params = {
    timeBegin: moment(formData.times[0]).valueOf(),
    timeEnd: moment(formData.times[1]).valueOf(),
    pageNum: 1,
    pageSize: 0,
    algorithmCodes: formData.algorithmCode ? [formData.algorithmCode] : [],
    categorys: ['10'],
    videoChannelName: formData.videoChannelName || undefined,
    propStr: formData.propStr,
    propColor: formData.propColor,
    propRelatedColor: formData.propRelatedColor,
    propType: formData.propType,
    propDirection: formData.propDirection,
    reportStatus: formData.reportStatus === '' ? undefined : formData.reportStatus,
    language: currentLocale.value
  }
  proxy.$API.boxExportAlarm(params).then(async (res) => {
    const { resData } = res
    if (resData && resData.fileUrl) {
      downloadFile(resData.fileUrl)
    } else {
      proxy.$message.error(t('event.fileExpired'))
    }
  })
}

const downloadFile = async (fileUrl) => {
  const response = await fetch(fileUrl)
  if (!response.ok) throw new Error(t('event.fileFetchFailed'))
  const blob = await response.blob()
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.style.display = 'none'
  link.href = url
  link.download = fileUrl.split('/').pop() || t('event.alarmRecordCsv')
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.URL.revokeObjectURL(url)
}

const onCheckVideo = (detail, type) => {
  const video = document.createElement('video')
  video.src = detail.video
  video.onerror = function () {
    proxy.$message.error(t('event.fileExpired'))
    video.remove()
  }
  video.oncanplaythrough = function () {
    if (type === 1) {
      currentEvent.value = detail
      videoDialogVisiable.value = true
      downloadName.value =
        detail.channelName +
        '_' +
        moment(detail.timestamp).format('YYYY-MM-DD HH:mm:ss') +
        '_' +
        Math.floor(1000 + Math.random() * 9000) +
        '_' +
        detail.algorithmName +
        '.mp4'
    }
    video.remove()
  }
  setTimeout(() => {
    if (video.readyState < 3) {
      video.remove()
    }
  }, 10000)
}

const checkObj = (obj) => {
  if (obj) {
    return Object.keys(obj).length
  }
  return 0
}

const returnBaseInfo = (objProperty, key, propertyKey) => {
  if (!objProperty) return ''
  const result = JSON.parse(objProperty)
  const vehicleData = result?.vehicle || {}

  if (checkObj(vehicleData)) {
    switch (propertyKey) {
      case 'orientation':
        return vehicleDictMap.value.vehicleorientation?.find(
          (item) => item.value === vehicleData[propertyKey]
        )?.label || ''
      case 'vehicleColor':
        return vehicleDictMap.value.vehiclecolor?.find(
          (item) => item.value === vehicleData[propertyKey]
        )?.label || ''
      case 'vehicleClass':
        return vehicleDictMap.value.vehicleclass?.find(
          (item) => item.value === vehicleData[propertyKey]
        )?.label || ''
      case 'plateColor':
        return vehicleDictMap.value.vehicleplatecolor?.find(
          (item) => item.value === vehicleData[propertyKey]
        )?.label || ''
      case 'plate':
        return vehicleData[propertyKey] || ''
      default:
        return ''
    }
  }
  return ''
}

const returnTime = (objProperty, propertyKey) => {
  if (!objProperty) return ''
  const result = JSON.parse(objProperty)
  const targetData = result?.target || {}
  if (targetData[propertyKey] && targetData[propertyKey] != '0') {
    return moment(Number(targetData[propertyKey])).format('YYYY-MM-DD HH:mm:ss')
  }
  return ''
}

const handleImageClick = (row, type) => {
  if (!row[`${type}ImageError`]) {
    let imageUrl
    if (type === 'detected') {
      imageUrl = row.detectedPicture
    } else if (type === 'full') {
      imageUrl = row.fullPicture
    }
    // proxy.$imgView([imageUrl])
    console.log('预览图片:', imageUrl)
  }
}

const handleImgError = (e, row, type) => {
  row[`${type}ImageError`] = true
  e.target.src = new URL('@/assets/error-image.png', import.meta.url).href
}

onMounted(() => {
  getCarDict()
  getChannelList()
  const startDate = moment().startOf('day').format('YYYY-MM-DD HH:mm:ss')
  const endDate = moment().endOf('day').format('YYYY-MM-DD HH:mm:ss')
  formData.times = [startDate, endDate]
  init()
})
</script>

<style lang="scss" scoped>
.table-container {
  background-color: #fff;
  padding: 0 15px;
  margin-top: 16px;

  .table-header {
    padding: 16px 0;
    overflow: hidden;

    .table-tools {
      float: right;
    }
  }
}

.operation-tools {
  .el-button {
    margin: 0;
    padding: 0 5px;
    font-weight: 500;
  }
}

.primary-text {
  color: var(--el-color-primary) !important;

  &:hover {
    color: #337ecc !important;
  }
}

.danger-text {
  color: var(--el-color-danger) !important;

  &:hover {
    color: #f56c6c !important;
  }
}
</style>
