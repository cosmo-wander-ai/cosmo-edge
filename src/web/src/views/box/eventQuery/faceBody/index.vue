<template>
  <div class="alarm-record">
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="60" :defaultExpand="true" @search="searchList" />
    <div class="table-container" ref="tableContainerRef">
      <div class="table-header">
        <div class="table-tools">
          <!-- <el-button v-if="runMode != 1" type="primary" size="small" @click="handleBatchDelete"
            :disabled="multipleSelections.length === 0">批量删除</el-button> -->
          <el-button type="primary" size="small" @click="handleExport"
            :disabled="tableData.length === 0">{{ t('event.dataExport') }}</el-button>
        </div>
      </div>

      <!-- 列表视图 -->
      <el-table :data="tableData" :header-cell-style="{ background: '#fafafa' }" style="width: 100%" :height="tableHeight"
        @selection-change="handleSelectionChange">
        <el-table-column type="selection" min-width="55"></el-table-column>
        <el-table-column type="index" :index="getIndex" :label="t('field.no')" width="80"></el-table-column>
        <el-table-column :label="t('event.eventType')" min-width="110">
          <template #default="{ row }">
            {{ resolveResourceAlgorithmName(row) }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.captureImage')" min-width="115">
          <template #default="scope">
            <el-image
              v-if="scope.row.detectedPicture"
              :src="getImageSrc(scope.row, 'detected')"
              fit="scale-down"
              :lazy="true"
              style="width: 60px; height: 60px; cursor: pointer;"
              @click="handleImageClick(scope.row, 'detected')"
              @error="handleImgError($event, scope.row, 'detected')">
            </el-image>
          </template>
        </el-table-column>
        <el-table-column :label="t('event.faceBaseImage')" min-width="110">
          <template #default="scope">
            <el-image
              v-if="returnBaseInfo(scope.row.property, 'LibImage')"
              :src="getImageSrc(scope.row, 'lib')"
              fit="scale-down"
              :lazy="true"
              style="width: 60px; height: 60px; cursor: pointer;"
              @click="handleImageClick(scope.row, 'lib')"
              @error="handleImgError($event, scope.row, 'lib')">
            </el-image>
          </template>
        </el-table-column>
        <el-table-column :label="t('event.fullImage')" min-width="100">
          <template #default="scope">
            <el-image
              v-if="scope.row.fullPicture"
              :src="getImageSrc(scope.row, 'full')"
              fit="scale-down"
              :lazy="true"
              style="width: 60px; height: 60px; cursor: pointer;"
              @click="handleImageClick(scope.row, 'full')"
              @error="handleImgError($event, scope.row, 'full')">
            </el-image>
          </template>
        </el-table-column>
        <el-table-column :label="t('event.faceLibrary')" min-width="115">
          <template #default="scope">
            {{ returnBaseInfo(scope.row.property, 'matchLibName') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.similarity')" min-width="110">
          <template #default="scope">
            <span
              v-if="returnBaseInfo(scope.row.property, 'matchDegree') && returnBaseInfo(scope.row.property, 'matchDegree') != '-1'">
              {{ returnBaseInfo(scope.row.property, 'matchDegree') }}
            </span>
          </template>
        </el-table-column>
        <el-table-column :label="t('event.personName')" min-width="80">
          <template #default="scope">
            {{ returnBaseInfo(scope.row.property, 'matchName') }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.personCode')" min-width="120">
          <template #default="scope">
            {{ returnBaseInfo(scope.row.property, 'personCode') }}
          </template>
        </el-table-column>
        <el-table-column prop="channelName" :label="t('field.channelName')" min-width="130"></el-table-column>
        <el-table-column :label="t('event.captureTime')" min-width="155">
          <template #default="scope">
            {{ dateFormat(scope.row.timestamp) }}
          </template>
        </el-table-column>
        <el-table-column :label="t('field.status')" min-width="90">
          <template #default="scope">
            <span :style="{ color: scope.row.reportStatus === 1 ? '#67C23A' : '#F56C6C' }">
              {{ scope.row.reportStatus === 1 ? t('event.uploaded') : t('event.notUploaded') }}
            </span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.actions')" fixed="right" :width="currentLocale === 'en-US' ? '160' : '100'">
          <template #default="scope">
            <div class="operation-tools">
              <el-button link class="primary-text" @click="handleDetail(scope.row)">{{ t('action.details') }}</el-button>
              <el-button link class="primary-text" v-if="scope.row.video" @click="onCheckVideo(scope.row, 1)">{{ t('event.videoPlayback') }}</el-button>
              <!-- <el-button link class="danger-text" v-if="runMode != 1" @click="handleDelete(scope.row)">删除</el-button> -->
            </div>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <div class="pagination-container">
        <el-pagination @size-change="handleSizeChange" @current-change="handleCurrentChange"
          :current-page="pageData.pageNum" :page-sizes="[10, 20, 50, 100]" :page-size="pageData.pageSize"
          :total="pageData.total" layout="total, sizes, prev, pager, next, jumper">
        </el-pagination>
      </div>
    </div>

    <capture-dialog v-model:visible="detailDialogVisible" :detailData="detailData"></capture-dialog>

    <video-frequency v-model:visible="videoDialogVisiable"
      :algorithmCode="currentEvent ? currentEvent.algorithmCode : ''" :closable="false"
      :url="currentEvent ? currentEvent.video : ''" :structureDataUrl="currentEvent ? currentEvent.videostructured : ''"
      :title="downloadName"></video-frequency>

  </div>
</template>

<script setup>
import { ref, reactive, getCurrentInstance, onMounted, onBeforeUnmount, nextTick, watch } from 'vue'
import TopBar from '@/components/TopBar.vue'
import moment from 'moment'
import CaptureDialog from '../components/captureDialog.vue'
import videoFrequency from '../components/videoPlaying265.vue'
import { t, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const { proxy } = getCurrentInstance()

const runMode = ref(0) // 0 单机版 1 联网版
const errorImageUrl = new URL('@/assets/error-image.png', import.meta.url).href
const tableContainerRef = ref(null)
const rawAlgorithmList = ref([])
const tableHeight = ref(400)
let resizeTimer = null
const updateTableHeight = () => {
  nextTick(() => {
    const el = tableContainerRef.value
    const rect = el?.getBoundingClientRect()
    const viewportH = window.innerHeight
    const bottomPadding = 140
    tableHeight.value = Math.max(300, viewportH - (rect?.top || 0) - bottomPadding)
  })
}
const handleResize = () => {
  if (resizeTimer) clearTimeout(resizeTimer)
  resizeTimer = setTimeout(() => updateTableHeight(), 100)
}

const topBarData = reactive({
  formList: [
    {
      labelI18nKey: 'event.period',
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
      dataList: [
        {
          labelI18nKey: 'common.all',
          value: ''
        }
      ]
    },
    {
      labelI18nKey: 'event.sceneTask',
      type: 'select',
      model: 'algorithmCode',
      value: '',
      dataList: []
    },
    {
      labelI18nKey: 'event.personName',
      model: 'personName'
    },
    {
      labelI18nKey: 'event.personCode',
      model: 'personCode'
    },
    {
      labelI18nKey: 'event.faceLibrary',
      type: 'select',
      model: 'matchLibName',
      filterable: true,
      dataList: [
        {
          labelI18nKey: 'common.all',
          value: ''
        }
      ]
    },
    {
      labelI18nKey: 'field.status',
      type: 'select',
      model: 'reportStatus',
      filterable: true,
      dataList: [
        {
          labelI18nKey: 'common.all',
          value: ''
        },
        {
          labelI18nKey: 'event.notUploaded',
          value: 0
        },
        {
          labelI18nKey: 'event.uploaded',
          value: 1
        }
      ]
    }
  ]
})

const formData = reactive({
  times: null,
  videoChannelName: null,
  algorithmCode: '',
  reportStatus: null
})

const pageData = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})

const viewType = ref('grid')
const tableData = ref([])
const multipleSelections = ref([])
const detailDialogVisible = ref(false)
const detailData = ref({})
const videoDialogVisiable = ref(false)
const currentEvent = ref(null)
const downloadName = ref('')

const algorithmCategoryList = [
  { label: '人脸人体', value: '1' },
  { label: '检测/分析', value: '2' },
  { label: '检测/分析', value: '3' },
  { label: '计数统计', value: '8' },
  { label: '计数统计', value: '9' },
  { label: '车辆分析', value: '10' },
  { label: '计数统计', value: '11' }
]

// Date format helper
const dateFormat = (value) => {
  if (!value) return ''
  return moment(value).format('YYYY-MM-DD HH:mm:ss')
}

// Watch viewType
watch(viewType, () => {
  multipleSelections.value = []
})

// Methods
// 更新算法下拉列表选项
const updateAlgorithmOptions = () => {
  const newAlgorithmList = []
  rawAlgorithmList.value.forEach((item) => {
    item.algorithmCategory == '1' &&
      newAlgorithmList.push({
        label: resolveResourceAlgorithmName(item),
        value: item.algorithmId
      })
  })
  topBarData.formList[2].dataList = [
    {
      labelI18nKey: 'common.all',
      value: ''
    },
    ...newAlgorithmList
  ]
}

const getAlgorithmInfo = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  proxy.$API.boxAllAlgorithmInfo(params).then((res) => {
    const { resData } = res
    rawAlgorithmList.value = resData.rows || []
    updateAlgorithmOptions()
  })
}

// 监听语言切换，更新下拉列表名称
watch(currentLocale, () => {
  updateAlgorithmOptions()
})

// 获取通道列表
const getChannelList = () => {
  proxy.$API.getChannelList({ pageNum: 1, pageSize: 1000 }).then((res) => {
    const { resData } = res
    const channelList = resData.rows.map((item) => {
      return {
        label: item.channelName,
        value: item.channelName
      }
    })
    topBarData.formList[1].dataList = [
      {
        labelI18nKey: 'common.all',
        value: ''
      },
      ...channelList
    ]
  })
}

const queryFaceLib = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  proxy.$API.queryFaceLibInfo(params).then((res) => {
    const { resData } = res
    const faceLibList = resData.faceLibList.map((item) => {
      return {
        label: item.name,
        value: item.id
      }
    })
    topBarData.formList[5].dataList = [
      {
        labelI18nKey: 'common.all',
        value: ''
      },
      ...faceLibList
    ]
  })
}

const init = () => {
  const params = {
    timeBegin: moment(formData.times[0]).valueOf(),
    timeEnd: moment(formData.times[1]).valueOf(),
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize,
    algorithmCodes: formData.algorithmCode
      ? [formData.algorithmCode]
      : [],
    categorys: ['1'],
    videoChannelName: formData.videoChannelName || null,
    reportStatus: formData.reportStatus || null,
    personName: formData.personName,
    personCode: formData.personCode,
    matchLibName: formData.matchLibName
  }
  proxy.$API.boxQueryEvent(params).then((res) => {
    const { resData } = res
    tableData.value = resData.rows || []
    pageData.total = resData.total || 0
  })
}

// 新增分页方法
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
    const params = {
      ids: []
    }
    params.ids.push(row.id)
    // proxy.$API.boxDeleteEvent(params).then((res) => {
    //   proxy.$message.success('操作成功')
    //   init()
    // })
    console.log('======删除', params)
  })
}

const handleBatchDelete = () => {
  proxy.$confirm(t('validate.deleteConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = {
      ids: []
    }
    // params.ids.push(row.id)
    // proxy.$API.boxDeleteEvent(params).then((res) => {
    //   proxy.$message.success('操作成功')
    //   init()
    // })
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
    categorys: ['1'],
    videoChannelName: formData.videoChannelName,
    reportStatus: formData.reportStatus || null,
    personName: formData.personName,
    personCode: formData.personCode,
    matchLibName: formData.matchLibName,
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
  // type 1  播放   2  下载
  var video = document.createElement('video')
  video.src = detail.video
  video.onerror = function () {
    proxy.$message.error(t('event.fileExpired'))
    // Clean up temporary video element
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
    } else if (type === 2) {
      // downloadVideo(detail)
    }
    // Clean up temporary video element
    video.remove()
  }
}

const checkObj = (obj) => {
  if (obj) {
    return Object.keys(obj).length
  }
  return 0
}

const returnBaseInfo = (objProperty, key) => {
  if (!objProperty) return ''
  try {
    const result = JSON.parse(objProperty)
    const recognitionData = result?.recognition || {}
    const workClothesRecognitionData = result?.workClothesRecognition || {}
    if (checkObj(recognitionData)) {
      return recognitionData[key] || ''
    } else if (checkObj(workClothesRecognitionData)) {
      return workClothesRecognitionData[key] || ''
    }
  } catch (e) {
    console.error('Parse property error:', e)
  }
  return ''
}

const handleImageClick = (row, type) => {
  // 只有当图片加载成功时才显示预览
  if (!row[`${type}ImageError`]) {
    let imageUrl
    if (type === 'lib') {
      imageUrl = returnBaseInfo(row.property, 'LibImage')
    } else if (type === 'detected') {
      imageUrl = row.detectedPicture
    } else if (type === 'full') {
      imageUrl = row.fullPicture
    }
    if (imageUrl) {
      proxy.$imgView([imageUrl])
    }
  }
}

const handleImgError = (e, row, type) => {
  row[`${type}ImageError`] = true
}

const getImageSrc = (row, type) => {
  if (row[`${type}ImageError`]) return errorImageUrl
  if (type === 'lib') {
    return returnBaseInfo(row.property, 'LibImage') || errorImageUrl
  }
  if (type === 'detected') {
    return row.detectedPicture || errorImageUrl
  }
  if (type === 'full') {
    return row.fullPicture || errorImageUrl
  }
  return errorImageUrl
}

// Lifecycle
onMounted(() => {
  getAlgorithmInfo()
  getChannelList()
  queryFaceLib()
  const startDate = moment().startOf('day').format('YYYY-MM-DD HH:mm:ss')
  const endDate = moment().endOf('day').format('YYYY-MM-DD HH:mm:ss')
  formData.times = [startDate, endDate]
  init()
  updateTableHeight()
  window.addEventListener('resize', handleResize)
})
onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
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

.grid-view {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 16px;

  .grid-item {
    position: relative;

    &-selected {
      :deep(.el-card__body) {
        background-color: #f5f7fa;
      }
    }

    .grid-checkbox {
      position: absolute;
      top: 10px;
      left: 10px;
      z-index: 1;
      background-color: rgba(255, 255, 255, 0.8);
      padding: 1px 3px;
    }

    :deep(.el-card__body) {
      padding: 10px 10px 0 10px;
    }

    .grid-content {
      font-size: 14px;

      .grid-image {
        width: 100%;
        height: 150px;
        margin-bottom: 10px;
      }

      .grid-info {
        .info-item {
          margin-bottom: 8px;

          .label {
            color: #606266;
            margin-right: 8px;
          }
        }
      }

      .grid-actions {
        text-align: right;
        border-top: 1px solid #ebeef5;
      }
    }
  }
}

.video-dialog {
  :deep(.el-dialog__body) {
    padding: 10px;
  }
}

.video-player {
  width: 100%;
  height: auto;
  max-height: 450px;
  object-fit: contain;
}

.empty-block {
  grid-column: 1 / -1;
  height: 100%;
  min-height: inherit;
  display: flex;
  justify-content: center;
  align-items: center;
  background-color: #fff;
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
