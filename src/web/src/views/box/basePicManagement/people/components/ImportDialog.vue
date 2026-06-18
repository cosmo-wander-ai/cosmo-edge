<template>
  <div>
    <el-dialog :title="title" v-model="uploadDialogVisible" :close-on-click-modal="false" center width="480px">
      <el-upload ref="uploadRef" class="upload-person" action="#" drag multiple :limit="1" :auto-upload="false" :http-request="httpRequest" :file-list="formInline.fileList" :on-change="handleFileChange">
        <el-icon class="el-icon--upload"><UploadFilled /></el-icon>
        <div class="el-upload__text">
          <p style="font-size:16px;color:#303133;">
            {{ t('basePic.dragFileHere') }}
            <em>{{ t('basePic.clickUpload') }}</em>
          </p>
          <p style="font-size:14px;color:#000;margin: 0 0">{{ t('basePic.zipFormatTip') }}</p>
          <p style="font-size:14px;color:red;margin: 0 0">{{ t('basePic.photoNamingFormat') }}</p>
        </div>
      </el-upload>
      <template #footer>
        <div class="dialog-footer">
          <el-button @click="uploadDialogVisible = false" size="small">{{ t('action.cancel') }}</el-button>
          <el-button @click="handleUpload" type="primary" size="small">{{ t('action.save') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <el-dialog :title="t('common.notice')" :append-to-body="true" v-model="uploadingDialogVisible" :show-close="false" :close-on-click-modal="false" center width="580px">
      <div class="uploading">
        <span class="uploading-title">{{ t('basePic.batchImportingLabel') }}</span>
        <template v-if="totalNumber != 0">
          <el-progress v-if="totalNumber != 0" :show-text="false" :stroke-width="24" :percentage="uploadProgress"></el-progress>
          <div class="progress-text">
            <span class="current-progress">{{processedNumber}}</span>/{{totalNumber}}
          </div>
        </template>
      </div>

      <div class="uploading uploading-detail">
        <span class="uploading-title">{{ t('basePic.ofWhich') }}</span>
        <template>
          <div class="nums">
            <span class="label">{{ t('basePic.importSuccessCount') }}</span>
            <span class="value" v-if="totalNumber != 0">{{successNum}}</span>
          </div>
          <div class="nums">
            <span class="lable">{{ t('basePic.importFailCount') }}</span>
            <span class="value error-num" v-if="totalNumber != 0">{{failedNum}}</span>
          </div>
        </template>
      </div>
    </el-dialog>

    <el-dialog :title="t('common.notice')" :append-to-body="true" v-model="stopServerTipDialogVisible" :close-on-click-modal="false" center width="332px" class="tip-dialog">
      <div class="dialog-content" :class="{'padding-bottom-20': isBeginUpload}">
        <img v-if="isBeginUpload" class="uploading-icon" src="@/assets/uploading.png">
        <el-icon v-else class="warning-icon"><WarningFilled /></el-icon>

        <span v-if="isBeginUpload">{{ t('basePic.fileTransferring') }}</span>
        <span v-else>{{ t('basePic.serviceStopWarning') }}</span>
      </div>

      <template v-if="!isBeginUpload" #footer>
        <span class="dialog-footer">
          <el-button @click="stopServerTipDialogVisible=false" size="small">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="submitUpload" size="small">{{ t('action.confirm') }}</el-button>
        </span>
      </template>
    </el-dialog>

    <el-dialog :title="t('common.notice')" :append-to-body="true" v-model="successDialogVisible" :close-on-click-modal="false" center width="332px" class="tip-dialog" :class="{'not-all-success': !isAllSuccess}">
      <div class="dialog-content">
        <el-icon style="color:#5ad952"><SuccessFilled /></el-icon>
        <span v-if="isAllSuccess">{{ t('basePic.allImportSuccess') }}</span>
        <span v-else>{{ t('basePic.batchImportComplete') }}</span>

        <div v-if="!isAllSuccess" class="not-all-success-detail">
          （{{ t('basePic.importSuccessLabel') }} <span class="success-num">{{successNum}}</span> {{ t('basePic.importFailLabel') }} <span class="error-num">{{failedNum}}</span>）
        </div>
      </div>

      <template v-if="isAllSuccess" #footer>
        <div class="dialog-footer">
          <el-button type="primary" @click="closeDialog" size="small">{{ t('action.close') }}</el-button>
        </div>
      </template>

      <template v-else #footer>
        <div class="dialog-footer">
          <el-button @click="closeDialog" size="small">{{ t('action.close') }}</el-button>
          <el-button type="primary" @click="exportFailFn" size="small">{{ t('basePic.exportFailReason') }}</el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, getCurrentInstance } from 'vue'
import { UploadFilled, WarningFilled, SuccessFilled } from '@element-plus/icons-vue'
import { t } from '@/i18n'

// 定义组件名称
defineOptions({
  name: 'ImportDialog'
})

// Props
const props = defineProps({
  title: String,
  data: Object,
  faceLibId: [String, Number]
})

// Emits
const emit = defineEmits(['update-status'])

const { proxy } = getCurrentInstance()

// Refs
const uploadRef = ref(null)

// Reactive data
const uploadDialogVisible = ref(false)
const datasuccess = ref(false)

const formInline = reactive({
  fileList: []
})

const uploadingDialogVisible = ref(false)
const uploadProgress = ref(0) // 上传进度
const processedNumber = ref(0) // 已上传个数
const totalNumber = ref(0) // 上传总个数
const successNum = ref(0) // 上传成功个数
const failedNum = ref(0) // 上传失败个数
const failedUrl = ref('') // 查询失败原因 url
const stopServerTipDialogVisible = ref(false)
const tipDialogVisible = ref(false)
const isBeginUpload = ref(false)
const successDialogVisible = ref(false)
const isAllSuccess = ref(true)
const isShowDialog = ref(false) // 批量上传中时，控制 dialog 是否显示

// Methods
function open() {
  formInline.fileList = []
  uploadDialogVisible.value = true
}

function exportFailFn() {
  if (!failedUrl.value) {
    return
  }
  uploadingDialogVisible.value = false
  uploadDialogVisible.value = false
  downloadFile(failedUrl.value)
}

async function downloadFile(fileUrl) {
  const response = await fetch(fileUrl)
  if (!response.ok) throw new Error(t('api.fileFetchFailed'))
  const blob = await response.blob()
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.style.display = 'none'
  link.href = url
  link.download = t('basePic.importFailRecordCsv')
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.URL.revokeObjectURL(url)
}

function httpRequest(event) {
  const { file } = event
  isShowDialog.value = true
  const formData = new FormData()
  formData.append('importType', 2) // 1：升级包，2：人员批量导入
  formData.append('faceLibId', props.faceLibId)
  formData.append('file', file)
  proxy.$API
    .boxImportFile(formData)
    .then(() => {
      uploadProgress.value = 0
      processedNumber.value = 0
      successNum.value = 0
      failedNum.value = 0

      getImportStatus()
      // 清除
      setTimeout(() => {
        const upload = uploadRef.value
        upload.clearFiles()
      }, 350)
    })
    .catch(() => {
      stopServerTipDialogVisible.value = false
      uploadingDialogVisible.value = false
    })
}

function getImportStatus() {
  const params = {
    importType: 2 // 1：升级包，2：人员批量导入
  }
  proxy.$API.queryImportStatus(params).then((res) => {
    const {
      status,
      progress,
      processedNumber: processed,
      totalNumber: total,
      successNumber,
      failedNumber,
      failedUrl: failUrl
    } = res.resData.importStatus
    switch (status) {
      case 0:
        // 未完成
        // 设置进度条
        uploadProgress.value = progress
        processedNumber.value = processed
        totalNumber.value = total
        successNum.value = successNumber
        failedNum.value = failedNumber

        if (isShowDialog.value) {
          stopServerTipDialogVisible.value = false
          uploadDialogVisible.value = false
          uploadingDialogVisible.value = true
        }

        setTimeout(() => {
          getImportStatus()
        }, 1000)
        emit('update-status', true)
        break
      case 1:
        // 上传完成
        // 导出失败为 0

        failedUrl.value = failUrl
        successNum.value = successNumber
        failedNum.value = failedNumber
        isAllSuccess.value = failedNumber === 0
        uploadingDialogVisible.value = false

        if (isShowDialog.value) {
          successDialogVisible.value = true
        }

        emit('update-status', false)
        break
      case 2:
        // 上传失败，接口其他错误
        proxy.$message.error(
          res.resData.importStatus.statusMsg || t('basePic.importError')
        )
        uploadingDialogVisible.value = false
        emit('update-status', false)
        break
    }
  })
}

function handleFileChange(file, fileList) {
  const fileInfo = file.raw
  const zipType = {
    'application/x-zip-compressed': true,
    'application/zip': true
  }
  if (!zipType[fileInfo.type]) {
    proxy.$message.warning(t('basePic.uploadZipWarning'))
    const upload = uploadRef.value
    upload.clearFiles()
    return
  }

  const fileSize = fileInfo.size / 1024 / 1024
  if (fileSize > 500) {
    proxy.$message.warning(t('basePic.fileSizeWarning'))
    const upload = uploadRef.value
    upload.clearFiles()
    return
  }

  formInline.fileList = fileList
}

function handleUpload() {
  const list = (formInline.fileList && formInline.fileList.length !== undefined)
    ? formInline.fileList
    : (uploadRef.value && uploadRef.value.uploadFiles) || []
  if (list.length == 0) {
    return proxy.$message.error(t('basePic.uploadZipRequired'))
  } else {
    submitUpload()
  }
}

function showUploadConfirmDilaog() {
  stopServerTipDialogVisible.value = true
}

function submitUpload() {
  isBeginUpload.value = true
  uploadRef.value.submit()
  stopServerTipDialogVisible.value = true
}

function openUploadingDialog() {
  isShowDialog.value = true
}

function closeDialog() {
  successDialogVisible.value = false
  stopServerTipDialogVisible.value = false
  uploadDialogVisible.value = false
}

// Expose methods
defineExpose({
  open,
  openUploadingDialog
})

// Lifecycle
onMounted(() => {
  getImportStatus()
})
</script>

<style scoped lang="scss">
.obsBtoon {
  text-align: center;
  margin-top: 30px;
  .el-button + .el-button {
    margin-left: 20px;
  }
}
.el-upload__tip {
  text-align: center;
  font-size: 14px;
  color: #1890ff;
  cursor: pointer;
}

.import-succeeded {
  text-align: center;
  > .el-icon {
    font-size: 50px;
    color: #5ad952;
  }
  > p {
    margin-bottom: 0;
    font-size: 16px;
    color: #606266;
    text-align: center;
  }
}
.import-failed {
  > p {
    font-size: 14px;
    color: #606266;
    display: flex;
    align-items: center;
    justify-content: center;
    &:first-child {
      font-size: 16px;
      color: #606266;
    }
    > .el-icon {
      margin-right: 10px;
      font-size: 22px;
    }
  }
}

.upload-person {
  margin-top: 20px;
  width: 100%;
  :deep(.el-upload) {
    width: 100%;
    .el-upload-dragger {
      width: 100%;
      height: 220px;
    }
  }
}

:deep(.el-form-item),
.el-select {
  width: 100%;
}

:deep(.el-dialog__body) {
  padding: 40px;
}

.uploading {
  display: flex;
  align-items: center;

  .uploading-title {
    font-weight: bold;
    width: 86px;
    text-align: right;
  }

  :deep(.el-progress) {
    flex: 1;
  }

  .progress-text {
    text-align: right;
    margin-left: 10px;

    .current-progress {
      color: #1890ff;
    }
  }
}

.uploading-detail {
  margin-top: 24px;

  .nums + .nums {
    margin-left: 48px;
  }

  .nums {
    .value {
      margin-left: 12px;
    }
  }
}

.success-num {
  margin-right: 12px;
}

.error-num {
  color: #d95d52;
}

.tip-dialog {
  :deep(.el-dialog__body) {
    padding: 16px 40px 12px;
  }

  .uploading-icon {
    width: 44px;
    height: 44px;
    margin-bottom: 12px;
    animation: rotate 2s linear infinite;
  }
}

.not-all-success {
  :deep(.el-dialog__body) {
    padding: 16px 20px 12px;
  }

  .not-all-success-detail {
    margin-top: 12px;
  }
}

.dialog-content {
  display: flex;
  flex-flow: column;
  align-items: center;
  text-align: center;

  .el-icon {
    font-size: 44px;
    margin-bottom: 12px;
  }

  .warning-icon {
    color: rgb(249, 179, 103);
  }
}

@keyframes rotate {
  100% {
    -webkit-transform: rotate(360deg);
  }
}

.padding-bottom-20 {
  padding-bottom: 20px;
}
</style>

