<template>
  <div>
    <TopBar ref="topBarRef" @init="init" @search="searchList" @reset="resetList" :active="true" />
    <div class="mv-table-wrap">
      <div class="taskButton">
        <span></span>
        <div v-if="runMode == 0">
          <el-button id="onboarding-add-channel" size="small" type="primary" @click="addChannelClick">{{ t('action.add') }}</el-button>
          <el-dropdown @command="handleBatchCommand" :disabled="multipleSelection.length == 0">
            <el-button size="small" type="primary" style="margin-left: 8px;" :disabled="multipleSelection.length == 0">
              {{ t('action.batchOperation') }}
            </el-button>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="deleteChannel">{{ t('action.deleteChannel') }}</el-dropdown-item>
                <el-dropdown-item command="startTask">{{ t('action.startService') }}</el-dropdown-item>
                <el-dropdown-item command="stopTask">{{ t('action.stopService') }}</el-dropdown-item>
                <el-dropdown-item command="deleteTask">{{ t('action.deleteService') }}</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </div>
      <!-- 表格数据 -->
      <el-table ref="tableRef" :data="tableData" tooltip-effect="dark" @selection-change="handleSelectionChange" :row-style="rowClass" stripe class="customer-no-border-table" :max-height="tableMaxHeight">
        <el-table-column type="selection" width="50px"></el-table-column>
        <!-- <el-table-column type="index">
        </el-table-column>-->
        <el-table-column type="index" :label="t('field.no')" width="80">
          <template #default="scope">
            <span>{{ ((pageData.pageNum - 1) * pageData.pageSize) + scope.$index + 1 }}</span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.channelNo')" prop="videoChannelId" min-width="120" show-overflow-tooltip>
        </el-table-column>
        <el-table-column :label="t('field.channelName')" prop="channelName" min-width="120" show-overflow-tooltip>
        </el-table-column>
        <el-table-column :label="t('glossary.scenarioTask')" prop="taskList" min-width="320">
          <template #default="scope">
            <div>
              <div id="onboarding-task-switch" class="task_table" v-for="(item, index) in scope.row.taskList" :key="index">
                <div class="analytical" @click="analyticalEngine(item)">
                  <div class="task_text">{{ resolveResourceAlgorithmName(item) }}</div>
                </div>
                <el-switch class="task-switch" v-model="item.enableStatus" active-color="#00f944" :active-value="1" :inactive-value="0" @change="taskEnableChange(item, scope.row.videoChannelId)"></el-switch>
                <div class="rtspa" v-if="item.status == 1">
                  <div class="rtspa_one">
                    <i style="width: 6px; height: 6px; border-radius: 50%; background-color: #32dda1;display: block; "></i>
                  </div>
                  <div class="Task_status">{{ t('status.inProgress') }}</div>
                </div>
                <div class="run-status" v-if="item.status == 0">
                  <div class="rtspa_one">
                    <i style=" width: 6px;height: 6px;border-radius: 50%;background-color: #f52828; display: block;"></i>
                  </div>
                  <div class="Task_status">{{ t('status.stopped') }}</div>
                </div>
                <div class="rtspa" v-if="item.status == -1">
                  <div class="rtspa_one">
                    <i style=" width: 6px;height: 6px;border-radius: 50%;background-color: #ebb563; display: block;"></i>
                  </div>
                  <div class="Task_status">{{ t('status.paused') }}</div>
                </div>
                <div class="rtspa" v-if="item.status == 2">
                  <div class="rtspa_one">
                    <i style=" width: 6px; height: 6px; border-radius: 50%;background-color: #fca60b; display: block;"></i>
                  </div>
                  <div class="Task_status">{{ t('status.abnormal') }}</div>
                </div>
                <div>{{ resolveScheduleName(item.scheduleName) }}</div>
              </div>
            </div>
          </template>
        </el-table-column>
        <el-table-column :label="t('glossary.accessType')" prop="channelType" width="140" min-width="100" show-overflow-tooltip>
          <template #default="scope">
            <span>{{ channelTypeLabel(scope.row.channelType) }}</span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.resolution')" show-overflow-tooltip min-width="120">
          <template #default="scope">
            <span>{{ scope.row.width ? scope.row.width + ' x ' + scope.row.height : '' }}</span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.videoCodec')" prop="codec" width="120" show-overflow-tooltip>
        </el-table-column>
        <el-table-column :label="t('glossary.frameRate')" prop="fps" show-overflow-tooltip>
          <template #default="scope">
            <span>{{ scope.row.fps ? Math.ceil(scope.row.fps) : '' }}</span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.channelStatus')" :width="currentLocale === 'en-US' ? '150px' : '110px'" fixed="right" show-overflow-tooltip>
          <template #default="scope">
            <template v-if="scope.row.channelStatus == 3">
              <span v-if="scope.row.channelType !== 3" style="color:green;">{{ t('status.online') }}</span>
              <br />
              <span style="color:red;">({{ t('status.unsupportedResolution') }})</span>
            </template>
            <template v-else-if="scope.row.channelStatus == 2">
              <span v-if="scope.row.channelType !== 3" style="color:green;">{{ t('status.online') }}</span>
              <br />
              <span style="color:red;">({{ t('status.validationError') }})</span>
            </template>
            <template v-else-if="scope.row.channelStatus == 1">
              <span style="color:green;">{{ t('status.online') }}</span>
            </template>
            <template v-else>
              <span v-if="scope.row.channelType !== 3">{{ t('status.offline') }}</span>
            </template>
          </template>
        </el-table-column>
        <el-table-column prop="handle" :label="t('field.actions')" :width="currentLocale === 'en-US' ? '280' : '200'" fixed="right" class-name="ops-wrap-cell">
          <template #default="scope">
            <div class="operation-tools">
              <el-button link class="primary-text" @click="handleDetailChannel(scope.row)">{{ t('action.details') }}</el-button>
              <el-button v-if="runMode != 1" link class="primary-text" @click="handleEditChannel(scope.row)">{{ t('action.edit') }}</el-button>
              <el-button :disabled="scope.row.channelStatus == 3 || (scope.row.channelStatus == 0 && scope.row.channelType !== 3)" link class="primary-text" @click="handleChannelPic(scope.row)">{{ t('action.snapshot') }}</el-button>
              <el-button id="onboarding-allocate-btn" v-if="runMode != 1" :disabled="scope.row.channelStatus == 3 || (scope.row.channelStatus == 0 && scope.row.channelType !== 3)" link class="primary-text" @click="handleAllocateClick(scope.row)">{{ t('action.allocateTask') }}</el-button>
              <el-button v-if="scope.row.channelType == 3 && runMode != 1" link class="primary-text" @click="handleVideoDownload(scope.row)">{{ t('action.videoDownload') }}</el-button>
              <el-button v-if="runMode != 1" link class="danger-text" @click="handleDeleteChannel(scope.row)">{{ t('action.delete') }}</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页组件 -->
      <div class="pagination-container">
        <el-pagination ref="paginationRef" v-model:current-page="pageData.pageNum" v-model:page-size="pageData.pageSize" :page-sizes="[10, 20, 50, 100]" :total="pageData.total" layout="total, sizes, prev, pager, next, jumper" @size-change="handleSizeChange" @current-change="handleCurrentChange" />
      </div>
    </div>

    <el-dialog :title="channelDialogTitle" v-model="channelDialogVisible" width="550px" @close="resetchannelForm" center>
      <el-form v-if="channelDialogVisible" :model="channelForm" :rules="channelFormRules" ref="channelFormRef" :label-width="currentLocale === 'en-US' ? '190px' : '140px'">
        <el-form-item :label="t('field.channelNo') + localeColon" prop="videoChannelId" required v-if="channelDialogMode === 'edit'">
          <el-input class="form-item-content" v-model="channelForm.videoChannelId" autocomplete="off" size="small" disabled />
        </el-form-item>
        <el-form-item :label="t('glossary.accessType') + localeColon" prop="channelType">
          <el-select id="onboarding-channel-type" popper-class="onboarding-type-popper" class="form-item-content" v-model="channelForm.channelType" :disabled="channelDialogMode === 'edit'" :placeholder="t('placeholder.select', { field: t('glossary.accessType') })" size="small" @change="channelTypeChange">
            <el-option label="RTSP" :value="0"></el-option>
            <el-option label="HLS" :value="1"></el-option>
            <el-option :label="t('glossary.usbCamera')" :value="6"></el-option>
            <el-option :label="t('glossary.offlineVideo')" :value="3"></el-option>
          </el-select>
        </el-form-item>
        <el-form-item :label="t('field.channelName') + localeColon" prop="channelName">
          <el-input id="onboarding-channel-name" class="form-item-content" v-model="channelForm.channelName" autocomplete="off" size="small" />
        </el-form-item>
        <!-- USB摄像头接入类型 -->
        <template v-if="channelForm.channelType === 6">
          <el-form-item :label="t('field.usbDeviceIndex') + localeColon" prop="usbDeviceIndex">
            <el-select class="form-item-content" v-model="channelForm.usbDeviceIndex" :placeholder="t('placeholder.select', { field: t('field.usbDeviceIndex') })" size="small">
              <el-option v-for="item in usbCameraList" :key="item.value" :label="item.label" :value="item.value"></el-option>
            </el-select>
            <div class="form-item-tip">{{ t('validate.usbDeviceTip') }}</div>
          </el-form-item>
          <el-form-item :label="t('field.usbResolution') + localeColon" prop="usbResolutionTier">
            <el-select class="form-item-content" v-model="channelForm.usbResolutionTier" :placeholder="t('placeholder.select', { field: t('field.usbResolution') })" size="small">
              <el-option label="1080P" :value="0"></el-option>
              <el-option label="720P" :value="1"></el-option>
              <el-option label="640P" :value="2"></el-option>
            </el-select>
          </el-form-item>
        </template>
        <el-form-item :label="t('field.address') + localeColon" prop="url" v-if="channelForm.channelType !== 3 && channelForm.channelType !== 6">
          <el-input class="form-item-content" v-model="channelForm.url" autocomplete="off" size="small" />
        </el-form-item>
        <!-- 离线视频类型 -->
        <el-form-item :label="t('glossary.uploadVideo') + localeColon" prop="videoFileList" v-if="channelForm.channelType === 3 && channelDialogMode !== 'edit'">
          <el-upload id="onboarding-upload-video" class="form-item-content" drag action="" :http-request="handleVideoUpload" :file-list="channelForm.videoFileList" :limit="1" :on-remove="handleRemove" :before-upload="beforeVideoUpload" accept=".avi,.mp4,.dav">
            <i class="el-icon-upload"></i>
            <div class="el-upload__text">{{ t('validate.dragUploadHint', { clickUpload: t('action.clickUpload'), n: 1 }) }}</div>
          </el-upload>
        </el-form-item>
        <el-form-item v-if="false" :label="t('field.externalChannelNo') + localeColon" prop="externalChannelNo">
          <el-input class="form-item-content" v-model="channelForm.externalChannelNo" autocomplete="off" size="small" />
        </el-form-item>
        <!-- 离线视频说明 -->
        <el-form-item v-if="channelForm.channelType === 3 && channelDialogMode !== 'edit'">
          <div class="form-item-content tip-content">
            <i class="el-icon-info"></i>
            {{ t('validate.videoUploadTip', { types: 'AVI、MP4、DAV', n: 1 }) }}
          </div>
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer" style="text-align:center;">
          <el-button size="small" @click="channelDialogVisible = false">{{ t('action.cancel') }}</el-button>
          <el-button id="onboarding-save-channel" size="small" type="primary" @click="submitAddChannel">{{ t('action.save') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <el-dialog :title="t('glossary.channelPhoto')" v-model="channelPicDialogVisible" center width="740px">
      <div>
        <img ref="image" class="channel-img" :src="channelImgSrc" @error="setDefaultImage">
      </div>
    </el-dialog>

    <chanel-detail-dialog v-model:visible="channelDetailVisible" :detailChannel="channelDetailObj"></chanel-detail-dialog>
  </div>
</template>
<script setup>
import {
  ref,
  reactive,
  computed,
  getCurrentInstance,
  nextTick,
  onMounted,
  onUnmounted
} from 'vue'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'
import TopBar from './components/algorithmTopBar.vue'
import chanelDetailDialog from './components/chanelDetailDialog.vue'
import defaultImage from '@/assets/CatchPhoto.png'

// Map backend default schedule names → i18n keys
const SCHEDULE_NAME_MAP = {
  '全天候': 'boxOther.scheduleNameAllDay',
  '工作日': 'boxOther.scheduleNameWeekday',
  '周末工作': 'boxOther.scheduleNameWeekend'
}
const resolveScheduleName = (name) => {
  if (!name) return ''
  const key = SCHEDULE_NAME_MAP[name]
  return key ? t(key) : name
}

const { proxy } = getCurrentInstance()

const runMode = ref(0) // 0 standalone 1 connected
const channelDialogVisible = ref(false)
const channelDialogMode = ref('add') // 'add' | 'edit'
const channelDialogTitle = computed(() =>
  channelDialogMode.value === 'edit' ? t('action.editChannel') : t('action.addChannel')
)
const channelDetailVisible = ref(false)
const channelPicDialogVisible = ref(false)
const channelDetailObj = ref({})
const channelForm = reactive({
  videoChannelId: '',
  channelType: 0,
  channelName: '',
  url: '',
  externalChannelNo: '',
  videoFileList: [],
  tempVideoPath: '',
  usbDeviceIndex: '',
  usbResolutionTier: ''
})
const channelFormRules = {
  channelType: [
    { required: true, message: () => t('validate.selectAccessType'), trigger: 'change' }
  ],
  channelName: [
    { required: true, message: () => t('validate.enterChannelName'), trigger: 'blur' },
    { max: 32, message: () => t('validate.channelNameMax', { n: 32 }), trigger: 'blur' }
  ],
  url: [
    {
      validator: (rule, value, callback) => validateURL(rule, value, callback),
      trigger: 'blur'
    }
  ],
  videoFileList: [
    { required: true, message: () => t('validate.uploadVideoFile'), trigger: 'change' }
  ],
  usbDeviceIndex: [
    { required: true, message: () => t('validate.selectUsbDevice'), trigger: 'change' }
  ],
  usbResolutionTier: [
    { required: true, message: () => t('validate.selectResolution'), trigger: 'change' }
  ],
  externalChannelNo: [
    { max: 128, message: () => t('validate.externalChannelNoMax', { n: 128 }), trigger: 'blur' }
  ]
}
const tableData = ref([])
const pageData = reactive({ pageNum: 1, pageSize: 10, total: 0 })
const formData = reactive({})
const multipleSelection = ref([])
const platformType = ref(localStorage.getItem('platformType'))
const algorithms = ref(JSON.parse(localStorage.getItem('algorithms')))
const analyticalEngineVisible = ref(false)
const analyticalData = ref([])
const taskId = ref('')
const runVideoDialogVisible = ref(false)
const runVideoUrl = ref('')
const runVideoLoading = ref(true)
const runVideoStyle = ref({ height: '300px' })
const imgStyle = ref({})
const runVideoTimestamp = ref(Date.now())
const dialogWidth = ref('700px')
const dialogHeight = ref('500px')
const runDetailDialogVisible = ref(false)
const currentTaskObj = ref({})
const channelImgSrc = ref('')
const usbCameraList = ref([])

const channelFormRef = ref(null)
const tableRef = ref(null)
const paginationRef = ref(null)
const tableMaxHeight = ref(600)

const calculateTableHeight = () => {
  nextTick(() => {
    const tableEl = tableRef.value ? tableRef.value.$el || tableRef.value : null
    const paginationEl = paginationRef.value
      ? paginationRef.value.$el || paginationRef.value
      : null
    const top = tableEl ? tableEl.getBoundingClientRect().top : 0
    const paginationHeight = paginationEl
      ? paginationEl.getBoundingClientRect().height
      : 72
    const bottomPadding = 24
    const viewportH = window.innerHeight
    tableMaxHeight.value = Math.max(
      200,
      viewportH - top - paginationHeight - bottomPadding - 30
    )
  })
}

const handleResize = () => {
  calculateTableHeight()
}

onMounted(() => {
  init()
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
})

const channelTypeLabel = (value) => {
  switch (value) {
    case 0:
      return 'RTSP'
    case 1:
      return 'HLS'
    case 6:
      return t('glossary.usbCamera')
    case 3:
      return t('glossary.offlineVideo')
    default:
      return ''
  }
}

function validateURL(rule, value, callback) {
  if (!value) {
    channelForm.channelType == 0
      ? callback(new Error(t('validate.enterStreamUrl', { protocol: 'RTSP' })))
      : callback(new Error(t('validate.enterStreamUrl', { protocol: 'HLS' })))
  } else {
    if (value.length > 2048) {
      channelForm.channelType == 0
        ? callback(new Error(t('validate.streamUrlMax', { protocol: 'RTSP', n: 2048 })))
        : callback(new Error(t('validate.streamUrlMax', { protocol: 'HLS', n: 2048 })))
    } else if (
      channelForm.channelType == 0 &&
      !value.toLowerCase().startsWith('rtsp')
    ) {
      callback(new Error(t('validate.rtspPrefix')))
    } else if (
      channelForm.channelType == 1 &&
      !value.toLowerCase().startsWith('hls') &&
      !value.toLowerCase().startsWith('http')
    ) {
      callback(new Error(t('validate.hlsPrefix')))
    }
  }
  callback()
}

const handleVideoUpload = (param) => {
  const file = param.file
  channelForm.videoFileList = [file]
  nextTick(() => {
    channelFormRef.value?.clearValidate('videoFileList')
  })
}

const handleRemove = () => {
  channelForm.videoFileList = []
  channelForm.tempVideoPath = ''
}

const beforeVideoUpload = (file) => {
  const isLimit = file.size / 1024 / 1024 < 1024
  const isType = ['video/mp4', 'video/avi', 'video/x-dav'].includes(file.type)
  if (!isType) {
    proxy.$message.error(t('validate.videoFormatSupported', { types: 'AVI, MP4, DAV' }))
    return false
  }
  if (!isLimit) {
    proxy.$message.error(t('validate.videoMaxSize', { n: 1024 }))
    return false
  }
  return true
}

const composeUsbUrl = (deviceIndex, tier) => {
  const index = deviceIndex !== undefined ? deviceIndex : 0
  const resolutionTier = tier !== undefined ? tier : 0
  return 'usb://' + index + '?tier=' + resolutionTier
}

const uploadVideoByChunk = async (file) => {
  const CHUNK_SIZE = 32 * 1024 * 1024
  const totalSize = file.size || 0
  const totalChunks = Math.max(1, Math.ceil(totalSize / CHUNK_SIZE))
  const uploadId = `${Date.now()}_${Math.random().toString(16).slice(2)}`
  let lastResp = null
  for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
    const start = chunkIndex * CHUNK_SIZE
    const end = Math.min(totalSize, start + CHUNK_SIZE)
    const blob = file.slice(start, end)
    const uploadFormData = new FormData()
    uploadFormData.append('file', blob, file.name)
    uploadFormData.append('uploadId', uploadId)
    uploadFormData.append('chunkIndex', String(chunkIndex))
    uploadFormData.append('totalChunks', String(totalChunks))
    uploadFormData.append('totalSize', String(totalSize))
    uploadFormData.append('chunkSize', String(end - start))
    lastResp = await proxy.$API.uploadAtomicModelTemp(uploadFormData)
    const respData = lastResp?.resData || lastResp?.data
    if (!respData) {
      throw new Error(t('validate.tempUploadNoResponse'))
    }
    const resCode = respData?.resCode ?? respData?.resData?.resCode
    if (resCode !== undefined && resCode !== 1) {
      const msg =
        (respData.resMsg && respData.resMsg[0] && respData.resMsg[0].msgText) ||
        (respData.resData &&
          respData.resData.resMsg &&
          respData.resData.resMsg[0] &&
          respData.resData.resMsg[0].msgText) ||
        t('validate.tempUploadFailed')
      throw new Error(msg)
    }
  }
  const finalData = lastResp?.resData || lastResp?.data
  let tempFilePath = ''
  if (finalData?.resData?.filePath) {
    tempFilePath = finalData.resData.filePath
  } else if (finalData?.filePath) {
    tempFilePath = finalData.filePath
  }
  if (!tempFilePath) {
    throw new Error(t('validate.tempVideoPathMissing'))
  }
  return { filePath: tempFilePath, totalSize }
}

const submitAddChannel = () => {
  channelFormRef.value?.validate(async (valid) => {
    if (valid) {
      const loading = proxy.$loading({
        lock: true,
        background: 'rgba(0, 0, 0, 0.7)'
      })
      try {
        if (channelForm.channelType === 6) {
          channelForm.url = composeUsbUrl(
            channelForm.usbDeviceIndex,
            channelForm.usbResolutionTier
          )
        }
        if (
          channelForm.channelType === 3 &&
          channelDialogMode.value !== 'edit'
        ) {
          if (!channelForm.videoFileList.length) {
            proxy.$message.error(t('validate.uploadVideoFile'))
            loading.close()
            return
          }
          const rawFile = channelForm.videoFileList[0]
          const file = rawFile.raw || rawFile
          const { filePath, totalSize } = await uploadVideoByChunk(file)
          const payload = {
            channelName: channelForm.channelName,
            externalChannelNo: channelForm.externalChannelNo,
            filePath,
            contentLength: String(totalSize)
          }
          await proxy.$API.boxAddVideoChannel(payload)
        } else if (channelDialogMode.value === 'edit') {
          await proxy.$API.boxUpdateCamera(channelForm)
        } else {
          delete channelForm.videoFileList
          await proxy.$API.boxAddCamera(channelForm)
        }
        delayInit(loading)
      } catch (error) {
        loading.close()
      }
    }
  })
}

const delayInit = (loading) => {
  setTimeout(() => {
    proxy.$message.success(t('common.operationSucceeded'))
    channelDialogVisible.value = false
    loading.close()
    init()
  }, 3000)
}

const queryUsbCameraList = () => {
  proxy.$API.queryUsbCameraList({}).then((res) => {
    const { resData } = res
    usbCameraList.value = (resData?.rows || []).map((item) => ({
      label: `${item.devicePath}（usb://${item.usbDeviceIndex}）`,
      value: item.usbDeviceIndex
    }))
  })
}

const channelTypeChange = (val) => {
  if (val === 6) {
    queryUsbCameraList()
  }
  channelFormRef.value && channelFormRef.value.clearValidate()
}

const resetchannelForm = () => {
  if (!channelForm.videoFileList) {
    channelForm.videoFileList = []
  }
}

const handleDetailChannel = (row) => {
  channelDetailVisible.value = true
  channelDetailObj.value = {
    videoChannelId: row.videoChannelId,
    channelTypeLabel: channelTypeLabel(row.channelType),
    channelName: row.channelName,
    usbDeviceIndex: row.usbDeviceIndex || '',
    usbResolutionTier: row.usbResolutionTier || '',
    codec: row.codec || '',
    fps: row.fps || '',
    url: row.url || '',
    externalChannelNo: row.externalChannelNo || '',
    algorithms: (row.taskList || []).map((item) => ({
      name: item.algorithmName,
      algorithmCode: item.algorithmCode,
      algorithmId: item.algorithmId,
      status: item.status,
      statusText:
        item.status === 1
          ? t('status.running')
          : item.status === 0
          ? t('status.stopped')
          : item.status === -1
          ? t('status.paused')
          : t('status.abnormal'),
      workday: resolveScheduleName(item.scheduleName)
    })),
    channelStatus: row.channelStatus
  }
}

const handleEditChannel = (row) => {
  channelDialogVisible.value = true
  channelFormRef.value && channelFormRef.value.resetFields?.()
  channelDialogMode.value = 'edit'
  let usbDeviceIndex = row.usbDeviceIndex || ''
  let usbResolutionTier = row.usbResolutionTier || ''
  if (row.channelType === 6 && row.url) {
    const urlMatch = row.url.match(/usb:\/\/(\d+)\?tier=(\d+)/)
    if (urlMatch) {
      usbDeviceIndex = Number(urlMatch[1])
      usbResolutionTier = Number(urlMatch[2])
    }
  }
  Object.assign(channelForm, {
    videoChannelId: row.videoChannelId,
    channelType: row.channelType,
    channelName: row.channelName,
    url: row.url,
    externalChannelNo: row.externalChannelNo,
    videoFileList: [],
    usbDeviceIndex,
    usbResolutionTier
  })
  if (row.channelType === 6) {
    queryUsbCameraList()
  }
}

const handleChannelPic = (row) => {
  channelPicDialogVisible.value = true
  proxy.$API
    .boxRecaptureImage({ videoChannelId: row.videoChannelId })
    .then((res) => {
      const { resData } = res
      channelImgSrc.value = resData.url
    })
}

const handleVideoDownload = async (row) => {
  const fileName = row.videoChannelId ? row.videoChannelId + '.mp4' : 'download.mp4'
  const response = await fetch(row.url)
  if (!response.ok) return proxy.$message.error(t('api.fileFetchFailed'))
  const blob = await response.blob()
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.style.display = 'none'
  link.href = url
  link.download = fileName
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.URL.revokeObjectURL(url)
}

const handleAllocateClick = (row) => {
  proxy.$router.push({
    path: '/gam/taskManager/realEditingTask',
    query: {
      resetUrl: proxy.$route.path,
      channelId: row.videoChannelId,
      channelName: row.channelName,
      joinType: row.channelType == 0 ? 0 : -1
    }
  })
}

const handleCurrentChange = (page) => {
  pageData.pageNum = page
  init()
}

const handleSizeChange = (pageSize) => {
  pageData.pageSize = pageSize
  init()
}

const searchList = (data) => {
  Object.assign(formData, data)
  pageData.pageNum = 1
  init(true)
}

const resetList = (data, type) => {
  Object.assign(formData, data)
  formData.channelType = type
  formData.category = ''
  pageData.pageNum = 1
  init()
}

const init = () => {
  multipleSelection.value = []
  const params = {
    channelName: formData.channelName,
    channelStatus: formData.channelStatus,
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize
  }
  proxy.$API.boxCameraPage(params).then((res) => {
    if (!res) return
    const { resData } = res
    tableData.value = resData.rows
    pageData.total = resData.total
    calculateTableHeight()
  })
}

const handleBatchCommand = (command) => {
  switch (command) {
    case 'deleteChannel': {
      const deleteChannels = multipleSelection.value.map(
        (item) => item.videoChannelId
      )
      const params = { videoChannelIds: deleteChannels }
      proxy
        .$confirm(t('validate.deleteConfirm'), t('common.notice'), {
          confirmButtonText: t('action.delete'),
          cancelButtonText: t('action.cancel'),
          type: 'warning'
        })
        .then(() => proxy.$API.boxBatchDeleteCamera(params))
        .then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
          init()
        })
      break
    }
    case 'deleteTask': {
      const deleteTasks = []
      multipleSelection.value.forEach((item) => {
        item.taskList.forEach((task) => {
          deleteTasks.push({
            channelId: item.videoChannelId,
            algorithmId: task.algorithmId
          })
        })
      })
      if (deleteTasks.length === 0)
        return proxy.$message.warning(t('common.noService'))
      const params = { tasks: deleteTasks }
      proxy
        .$confirm(t('validate.deleteConfirm'), t('common.notice'), {
          confirmButtonText: t('action.delete'),
          cancelButtonText: t('action.cancel'),
          type: 'warning'
        })
        .then(() => proxy.$API.boxBatchdeleteTask(params))
        .then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
          init()
        })
      break
    }
    case 'startTask': {
      const startTasks = []
      multipleSelection.value.forEach((item) => {
        item.taskList.forEach((task) => {
          startTasks.push({
            channelId: item.videoChannelId,
            algorithmId: task.algorithmId,
            switch: 1
          })
        })
      })
      if (startTasks.length === 0) return proxy.$message.warning(t('common.noService'))
      const params = { tasks: startTasks }
      proxy.$API.boxBatchSwitchTask(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        init()
      })
      break
    }
    case 'stopTask': {
      const stopTasks = []
      multipleSelection.value.forEach((item) => {
        item.taskList.forEach((task) => {
          stopTasks.push({
            channelId: item.videoChannelId,
            algorithmId: task.algorithmId,
            switch: 0
          })
        })
      })
      if (stopTasks.length === 0) return proxy.$message.warning(t('common.noService'))
      const params = { tasks: stopTasks }
      proxy.$API.boxBatchSwitchTask(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        init()
      })
      break
    }
  }
}

const addChannelClick = () => {
  channelDialogMode.value = 'add'
  Object.assign(channelForm, {
    channelType: 0,
    channelName: '',
    url: '',
    externalChannelNo: '',
    videoFileList: [],
    usbDeviceIndex: '',
    usbResolutionTier: ''
  })
  channelDialogVisible.value = true
  channelFormRef.value && channelFormRef.value.resetFields?.()
}

const handleSelectionChange = (data) => {
  multipleSelection.value = data
}

const selectRow = ref([])

const rowClass = ({ rowIndex }) => {
  if (selectRow.value.includes(rowIndex)) {
    return { 'background-color': 'rgba(185, 221, 249, 0.75)' }
  }
}

const analyticalEngine = (data) => {
  if (platformType.value != 1) return
  const custId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')
  const param = { custId: custId || '', algorithmId: data.algorithmId }
  proxy.$API.allHostInfo(param).then((res) => {
    const { resData } = res.data
    analyticalData.value = resData
  })
  taskId.value = data.taskId
  analyticalEngineVisible.value = true
}

const handleDeleteChannel = (row) => {
  proxy
    .$confirm(t('validate.deleteConfirm'), t('common.notice'), {
      confirmButtonText: t('action.delete'),
      cancelButtonText: t('action.cancel'),
      type: 'warning'
    })
    .then(() => {
      const params = { videoChannelIds: [row.videoChannelId] }
      return proxy.$API.boxBatchDeleteCamera(params)
    })
    .then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      init()
    })
}

const taskEnableChange = (item, channelId) => {
  const params = {
    tasks: [
      {
        channelId: channelId,
        algorithmId: item.algorithmId,
        switch: item.enableStatus
      }
    ]
  }
  proxy.$API.boxBatchSwitchTask(params).then(() => {
    proxy.$message.success(t('common.operationSucceeded'))
    init()
  })
}

const setDefaultImage = (e) => {
  e.target.src = defaultImage
}
</script>

<style scoped lang="scss">
.taskButton {
  display: flex;
  justify-content: space-between;
  margin: 12px 0px 10px 0px;
  padding: 12px 12px 12px 0;

  :deep(.el-button.is-disabled) {
    background-color: #a0cfff !important;
    border-color: #a0cfff !important;
    color: #fff !important;
    cursor: not-allowed;
    opacity: 0.6;
  }
}

.task_table {
  display: flex;
  align-items: center;
}

.mv-table-wrap .stop-button {
  background: #34b7b2;
  border-color: #34b7b2;
}

.mv-table-wrap .delet-button {
  background: #ef5858;
  border-color: #ef5858;
}

.mv-table-wrap {
  background: white;
  border-radius: 8px;
}

.rtspa {
  display: flex;
  min-width: 60px;
}

.rtspa .rtspa_one {
  display: flex;
  align-items: center;
  padding-right: 8px;
}

/*表格全选框去除空框*/
.el-table :deep(.DisabledSelection) .cell .el-checkbox__inner {
  display: none;
  position: relative;
}

/*表格全选框改为：选择*/
.el-table :deep(.DisabledSelection) .cell:before {
  position: absolute;
  left: 18px;
}

/*更改多选表头*/
.el-table :deep(.DisabledSelection) .cell .el-checkbox__inner {
  display: none;
  position: relative;
}

.el-table :deep(.DisabledSelection) .cell:before {
  position: absolute;
  left: 16px;
}

.remind-border {
  overflow: auto;
  height: 300px;
  font-family: PingFangSC-Regular;
}

.remind-border-list {
  padding: 10px 20px;
  display: flex;
  justify-content: space-between;
}

.warning-outline {
  display: inline-block;
  width: 20px;
  height: 20px;
  vertical-align: middle;
  // background: url(../../../../assets/diagnose_warning.png) no-repeat center;
  background-size: 100% 100%;
  margin-right: 10px;
  color: red;
  font-size: 16px;
  cursor: pointer;
}

.explain {
  display: flex;
  margin: 12px 12px 0px 12px;
  padding: 12px;
  background-color: #fff;
}

/*算法授权不足*/
.deficiency {
  padding: 10px 20px;
  display: flex;
  justify-content: space-between;
}

.analytical {
  cursor: pointer;
}

.task-node {
  width: 100px;
  white-space: nowrap;
  text-overflow: ellipsis;
  overflow: hidden;
}

.task_text {
  margin-right: 10px;
  width: 100px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.Task_status {
  width: 60px;
  margin-left: 10px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.task_table_text {
  margin-right: 18px;
  width: 120px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.el-table .cell,
.el-table th div {
  padding-right: 0;
}

.operation-tools {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.operation-tools > .el-button {
  margin-left: 0;
  padding: 0;
}

.el-table :deep(.ops-wrap-cell .cell) {
  white-space: normal !important;
}

.task-switch {
  margin-right: 10px;
}

.runBtn {
  padding: 3px;
}

.runDialog :deep(.el-dialog__body) {
  max-height: 100vh;
}

.el-table :deep(.el-table__fixed-right) {
  height: calc(100% - 6px) !important;
}

.primary-text {
  color: var(--el-color-primary) !important;
}

.danger-text {
  color: var(--el-color-danger) !important;
}

.categroy-type {
  width: 80px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.run-status {
  display: flex;
  align-items: center;
}

.form-item-content {
  width: calc(100% - 80px);
}

.form-item-tip {
  margin-top: 4px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
}

.el-upload__tip {
  line-height: 18px;
  margin-top: 0;
}

.channel-img {
  width: 700px;
  object-fit: 'fill';
  user-select: 'none';
}

.tip-content {
  color: #409eff;
  font-size: 13px;
  line-height: 18px;
}
</style>
