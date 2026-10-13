<template>
  <div class="alarm-record ui-admin-page event-console">
    <header class="event-heading">
      <div>
        <h1>{{ t('nav.eventCenter') }}</h1>
        <p>{{ t('nav.detectionAnalysis') }}</p>
      </div>
      <div class="event-heading-actions">
        <el-button @click="layaEventId = ''; layaVisible = true">{{ t('visualReview.title') }}</el-button>
        <el-button @click="handleExport" :disabled="tableData.length === 0">{{ t('event.dataExport') }}</el-button>
      </div>
    </header>
    <LayaReviewDialog v-model="layaVisible" :event-id="layaEventId" />
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="60" :defaultExpand="true" @search="searchList" />

    <div class="table-container">
      <div class="table-header">
        <span class="event-count">{{ t('event.recordCount', { n: pageData.total }) }}</span>
        <span v-if="viewType === 'grid'" class="event-evidence-hint">{{ t('event.fullImageHint') }}</span>
      </div>

      <!-- 列表视图 -->
      <el-table v-show="viewType === 'list'" :data="tableData" :header-cell-style="{background:'var(--theme-surface-soft, #fafafa)'}" style="width: 100%" @selection-change="handleSelectionChange">
        <el-table-column type="selection" width="55" />
        <el-table-column type="index" :index="getIndex" :label="t('field.no')" width="80" />
        <el-table-column :label="t('event.fullImage')" width="120">
          <template #default="{ row }">
            <el-image :src="row.fullPicture" fit="scale-down" style="width: 60px; height: 60px;cursor: pointer;" @click="handleImageView([row.fullPicture])" />
          </template>
        </el-table-column>
        <el-table-column :label="t('event.captureImage')" width="120">
          <template #default="{ row }">
            <el-image :src="row.detectedPicture" fit="scale-down" style="width: 60px; height: 60px;cursor: pointer;" @click="handleImageView([row.detectedPicture])" />
          </template>
        </el-table-column>
        <el-table-column :label="t('event.alarmType')">
          <template #default="{ row }">
            {{ resolveResourceAlgorithmName(row) }}
          </template>
        </el-table-column>
        <el-table-column prop="channelName" :label="t('field.channelName')" />
        <el-table-column prop="areaName" :label="t('event.areaName')" />
        <el-table-column :label="t('event.alarmTime')" min-width="130">
          <template #default="{ row }">
            {{ dateFormat(row.timestamp) }}
          </template>
        </el-table-column>
        <el-table-column :label="t('event.uploadStatus')">
          <template #default="{ row }">
            <span :style="{color: row.reportStatus === 1 ? 'var(--theme-success, #67C23A)' : 'var(--theme-danger, #F56C6C)'}">
              {{ reportStatusText(row.reportStatus) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.actions')" fixed="right" :width="currentLocale === 'en-US' ? '320' : '260'">
          <template #default="{ row }">
            <div class="operation-btns">
              <el-button link class="ui-action ui-action-view" @click="layaEventId = row.id; layaVisible = true">{{ t('visualReview.review') }}</el-button>
              <el-button link @click="handleDetail(row)" class="ui-action ui-action-view">{{ t('action.details') }}</el-button>
              <el-button link class="ui-action ui-action-upload" v-if="runMode != 1 && checkRuku(row)" @click="handleRuku(row)">{{ t('event.captureImageStorage') }}</el-button>
              <el-button v-if="row.video" link class="ui-action ui-action-play" @click="onCheckVideo(row, 1)">{{ t('event.videoPlayback') }}</el-button>
              <el-button v-if="runMode != 1" link style="color:red;" @click="handleDelete(row)" class="ui-action ui-action-delete">{{ t('action.delete') }}</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <!-- 网格视图 -->
      <div v-show="viewType === 'grid'" class="grid-view">
        <template v-if="tableData.length">
          <el-card v-for="item in tableData" :key="item.id" class="grid-item" :class="{'grid-item-selected': multipleSelections.includes(item.id)}">
            <div class="grid-content">
              <button class="event-image-button" type="button" :aria-label="t('event.fullImage')" @click="handleImageView([item.fullPicture])">
                <el-image :src="item.fullPicture" fit="contain" class="grid-image" />
              </button>
              <div class="grid-info">
                <div class="event-card-heading">
                  <h3 :title="resolveResourceAlgorithmName(item)">{{ resolveResourceAlgorithmName(item) }}</h3>
                  <span class="event-report-status" :class="item.reportStatus === 1 ? 'is-uploaded' : 'is-pending'">
                    {{ reportStatusText(item.reportStatus) }}
                  </span>
                </div>
                <div class="event-location">
                  <span :title="`${t('field.channelName')}${localeColon}${item.channelName || '—'}`">{{ item.channelName || '—' }}</span>
                  <span :title="`${t('event.areaName')}${localeColon}${item.areaName || '—'}`">{{ item.areaName || '—' }}</span>
                </div>
                <time class="event-time" :title="t('event.alarmTime')">{{ dateFormat(item.timestamp) }}</time>
              </div>
              <div class="grid-actions">
                <el-button link class="ui-action ui-action-edit" @click="layaEventId = item.id; layaVisible = true">{{ t('visualReview.review') }}</el-button>
                <el-button link class="primary-text ui-action ui-action-view" @click="handleDetail(item)">{{ t('action.details') }}</el-button>
                <el-button link class="ui-action ui-action-upload" v-if="checkRuku(item)" @click="handleRuku(item)">{{ t('event.captureImageStorage') }}</el-button>
                <el-button link class="ui-action ui-action-play" v-if="item.video" @click="onCheckVideo(item, 1)">{{ t('event.videoPlayback') }}</el-button>
                <!-- <el-button v-if="runMode != 1" link class="danger-text" @click="handleDelete(item)">删除</el-button> -->
              </div>
            </div>
          </el-card>
        </template>
        <div v-else class="empty-block">
          <el-empty :description="t('common.noData')" />
        </div>
      </div>
    </div>

    <!-- 分页 -->
    <div class="pagination-container">
      <el-pagination @size-change="handleSizeChange" @current-change="handleCurrentChange" :current-page="pageData.pageNum" :page-sizes="[10, 20, 50, 100]" :page-size="pageData.pageSize" :total="pageData.total" layout="total, sizes, prev, pager, next, jumper" />
    </div>

    <ruku-dialog v-model:visible="rukuDialogVisible" :rukuData="rukuData" />
    <detail-dialog v-model:visible="detailDialogVisible" :detailData="detailData" />
    <video-frequency v-model:visible="videoDialogVisiable" :algorithmCode="currentEvent?.algorithmCode || ''" :closable="false" :url="currentEvent?.video || ''" :structureDataUrl="currentEvent?.videostructured || ''" :title="downloadName" :show-custom-title="false" />
  </div>
</template>

<script setup>
import { ref, reactive, watch, onMounted, getCurrentInstance } from 'vue'
import LayaReviewDialog from '../components/LayaReviewDialog.vue'
import TopBar from '@/components/TopBar.vue'
import moment from 'moment'
import detailDialog from '../components/detailDialog.vue'
import videoFrequency from '@/components/videoPlaying265.vue'
import rukuDialog from '../components/rukuDialog.vue'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const { proxy } = getCurrentInstance()

const runMode = ref(0) // 0 单机版 1 联网版
const topBarRef = ref(null)
const rawAlgorithmList = ref([])
const viewType = ref('grid')
const tableData = ref([])
const multipleSelections = ref([])
const detailDialogVisible = ref(false)
const detailData = ref({})
const videoDialogVisiable = ref(false)
const layaVisible = ref(false)
const layaEventId = ref('')
const currentEvent = ref(null)
const downloadName = ref('')
const rukuDialogVisible = ref(false)
const rukuData = ref({})

const algorithmCategoryList = [
  { labelI18nKey: 'event.categoryFaceBody', value: '1' },
  { labelI18nKey: 'event.categoryDetection', value: '2' },
  { labelI18nKey: 'event.categoryDetection', value: '3' },
  { labelI18nKey: 'event.categoryCounting', value: '8' },
  { labelI18nKey: 'event.categoryCounting', value: '9' },
  { labelI18nKey: 'event.categoryVehicle', value: '10' },
  { labelI18nKey: 'event.categoryCounting', value: '11' }
]

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
      filterable: true,
      dataList: []
    },
    {
      labelI18nKey: 'event.uploadStatus',
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
  videoChannelName: '',
  algorithmCode: '',
  reportStatus: ''
})

const pageData = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})

// 日期格式化
const dateFormat = (value) => {
  if (!value) return ''
  return moment(value).format('YYYY-MM-DD HH:mm:ss')
}

const reportStatusText = (status) => {
  return status === 1 ? t('event.uploaded') : t('event.notUploaded')
}

// 更新算法下拉列表选项
const updateAlgorithmOptions = () => {
  const newAlgorithmList = []
  rawAlgorithmList.value.forEach((item) => {
    if (item.algorithmCategory == '2' || item.algorithmCategory == '3') {
      newAlgorithmList.push({
        label: resolveResourceAlgorithmName(item),
        value: item.algorithmId
      })
    }
  })
  topBarData.formList[2].dataList = [
    { labelI18nKey: 'common.all', value: '' },
    ...newAlgorithmList
  ]
}

// 获取算法信息
const getAlgorithmInfo = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  proxy.$API.algorithmInquire(params).then((res) => {
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

// 初始化数据
const init = () => {
  const params = {
    timeBegin: moment(formData.times[0]).valueOf(),
    timeEnd: moment(formData.times[1]).valueOf(),
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize,
    algorithmCodes: formData.algorithmCode ? [formData.algorithmCode] : [],
    categorys: ['2', '3'],
    videoChannelName: formData.videoChannelName || undefined,
    reportStatus:
      formData.reportStatus === '' ? undefined : formData.reportStatus
  }
  proxy.$API.boxQueryEvent(params).then((res) => {
    const { resData } = res
    tableData.value = resData.rows || []
    pageData.total = resData.total || 0
  })
}

// 分页方法
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
  proxy
    .$confirm(t('validate.deleteConfirm'), t('common.notice'), {
      type: 'warning'
    })
    .then(() => {
      const params = {
        ids: [row.id]
      }
      // proxy.$API.boxDeleteEvent(params).then((res) => {
      //   proxy.$message.success('操作成功')
      //   init()
      // })
      console.log('======删除', params)
    })
}

const handleGridSelect = (checked, item) => {
  if (checked) {
    multipleSelections.value.push(item.id)
  } else {
    const index = multipleSelections.value.indexOf(item.id)
    if (index !== -1) {
      multipleSelections.value.splice(index, 1)
    }
  }
}

const handleBatchDelete = () => {
  proxy
    .$confirm(t('validate.deleteConfirm'), t('common.notice'), {
      type: 'warning'
    })
    .then(() => {
      const params = {
        ids: multipleSelections.value
      }
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
    categorys: ['2', '3'],
    videoChannelName: formData.videoChannelName || undefined,
    reportStatus:
      formData.reportStatus === '' ? undefined : formData.reportStatus,
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
  const video = document.createElement('video')
  video.src = detail.video

  video.onerror = function () {
    proxy.$message.error(t('event.fileExpired'))
    // 清理视频元素
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
    // 清理视频元素
    video.remove()
  }

  // 添加超时处理，避免视频一直加载
  setTimeout(() => {
    if (video.readyState < 3) {
      // HAVE_FUTURE_DATA
      video.remove()
    }
  }, 10000) // 10秒超时
}

const checkRuku = (row) => {
  if (row?.property) {
    const result = JSON.parse(row.property)
    return result?.workClothesRecognition || result?.machineMaterial
  }
  return false
}

const handleRuku = (row) => {
  rukuData.value = row
  rukuDialogVisible.value = true
}

const handleImgError = (e) => {
  e.target.src = new URL('@/assets/error-image.png', import.meta.url).href
}

const handleImageView = (images) => {
  proxy.$imgView(images)
  console.log('预览图片:', images)
}

// 监听视图类型变化
watch(viewType, () => {
  // 切换视图时清空选中状态
  multipleSelections.value = []
})

// 组件挂载
onMounted(() => {
  getAlgorithmInfo()
  getChannelList()
  const startDate = moment().startOf('day').format('YYYY-MM-DD HH:mm:ss')
  const endDate = moment().endOf('day').format('YYYY-MM-DD HH:mm:ss')
  formData.times = [startDate, endDate]
  init()
})
</script>

<style lang="scss" scoped>
@use '../event-console.scss';

.event-console .table-container {
  padding: 0;
  border: 0;
  background: transparent;
}

.event-console .event-heading {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.event-heading-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  :deep(.el-button + .el-button) { margin-left: 0; }
}

.event-console > :deep(.topBar-wrap.expanded) {
  display: flex;
  align-items: flex-end;
  height: auto !important;
  overflow: visible;
  padding: 12px 14px !important;
  gap: 12px;

  .form-search {
    display: grid;
    grid-template-columns: minmax(320px, 2.2fr) repeat(2, minmax(140px, 1.15fr)) minmax(110px, .8fr);
    align-items: end;
    gap: 12px;
  }
  .formDiv {
    display: flex;
    flex-direction: column;
    align-items: stretch;
    min-width: 0;
    margin: 0;
    gap: 7px;
  }
  .formTitle {
    width: auto;
    max-width: none;
    margin: 0;
    text-align: left;
    line-height: 18px;
  }
  .el-form, .el-formDate {
    width: 100% !important;
    min-width: 0;
    box-sizing: border-box;
  }
  .el-range-input { min-width: 0; font-size: 12px; }
  .btnBar { align-self: flex-end; padding: 0; }
}

.event-console .table-header {
  justify-content: flex-start;
  min-height: 20px;
  padding-bottom: 10px;
  gap: 14px;
  font-size: 13px;
}

.event-count { color: var(--text-secondary); font-variant-numeric: tabular-nums; }
.event-evidence-hint { color: var(--text-muted); }

.grid-view {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 10px;
  align-items: stretch;
}

.grid-item {
  min-width: 0;
  border-radius: 6px;
  box-shadow: none;

  &-selected { border-color: var(--primary-color); }

  :deep(.el-card__body) {
    height: 100%;
    padding: 8px 8px 0;
    box-sizing: border-box;
  }
}

.grid-content {
  display: flex;
  flex-direction: column;
  height: 100%;
  font-size: 13px;
}

.grid-image {
  display: block;
  width: 100%;
  aspect-ratio: 16 / 9;
  flex: none;
  border-radius: 3px;
  background: var(--bg-subtle);
  cursor: pointer;
}

.event-image-button {
  display: block;
  width: 100%;
  padding: 0;
  border: 0;
  border-radius: 3px;
  background: none;
  cursor: pointer;
  &:focus-visible { outline: 2px solid var(--primary-color); outline-offset: 3px; }
}

.grid-info {
  flex: 1;
  padding: 8px 2px 6px;
}

.event-card-heading {
  display: flex;
  flex-wrap: wrap;
  align-items: baseline;
  gap: 4px 8px;
  margin-bottom: 4px;

  h3 {
    min-width: 0;
    flex: 1;
    margin: 0;
    color: var(--text-primary);
    font-size: 14px;
    line-height: 1.5;
    font-weight: 600;
    overflow-wrap: anywhere;
  }
}

.event-report-status {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 12px;
  white-space: nowrap;

  &::before {
    content: '';
    width: 5px;
    height: 5px;
    border-radius: 50%;
    background: currentColor;
  }

  &.is-uploaded { color: var(--success-color); }
  &.is-pending { color: var(--text-secondary); }
}

.event-location {
  display: flex;
  align-items: baseline;
  gap: 8px;
  color: var(--text-secondary);
  font-size: 12px;
  line-height: 1.5;

  > span {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  > span + span {
    padding-left: 8px;
    border-left: 1px solid var(--border-color);
  }
}

.event-time {
  display: block;
  margin-top: 2px;
  color: var(--text-secondary);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}

.grid-actions {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 2px 8px;
  min-height: 32px;
  padding: 3px 0;
  border-top: 1px solid var(--border-light);
}

.empty-block {
  grid-column: 1 / -1;
  display: grid;
  place-items: center;
  min-height: 260px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 6px;
}

.operation-btns {
  display: flex;
  flex-wrap: wrap;
  gap: 3px 8px;
}

@media (max-width: 1399px) {
  .grid-view { grid-template-columns: repeat(3, minmax(0, 1fr)); }
  .event-console > :deep(.topBar-wrap.expanded) {
    flex-wrap: wrap;
    .form-search { flex-basis: 100%; }
    .btnBar { margin-left: auto; }
  }
}

@media (max-width: 1100px) {
  .grid-view { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .event-console > :deep(.topBar-wrap.expanded) .form-search {
    grid-template-columns: repeat(2, minmax(0, 1fr));
    .formDiv:first-child { grid-column: 1 / -1; }
  }
}

@media (max-width: 720px) {
  .grid-view { grid-template-columns: minmax(0, 1fr); }
  .event-console > :deep(.topBar-wrap.expanded) .form-search { grid-template-columns: minmax(0, 1fr); }
  .event-evidence-hint { display: none; }
}
</style>
