<template>
  <div>
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="60" @search="searchList" />

    <div class="table-container">
      <div class="table-header">
        <div>{{ t('boxOther.audioList') }}</div>
        <div class="table-tools">
          <el-button v-if="runMode  != 1" type="primary" size="small" @click="handleAdd">{{ t('action.add') }}</el-button>
          <el-button v-if="runMode  != 1" type="primary" size="small" @click="handleBatchDelete" :disabled="multipleSelections.length === 0">{{ t('action.bulkDelete') }}</el-button>
        </div>
      </div>

      <el-table ref="multipleTable" style="width: 100%" :data="tableData" @selection-change="handleSelectionChange">
        <el-table-column type="selection" width="55px" :selectable="selectable"></el-table-column>
        <el-table-column :label="t('field.no')" width="80">
          <template #default="scope">
            <span>{{((pageData.pageNum-1)*pageData.pageSize)+scope.$index+1}}</span>
          </template>
        </el-table-column>
        <el-table-column show-overflow-tooltip :label="t('boxOther.audioName')">
          <template #default="scope">
            <span>{{ resolveAudioName(scope.row) }}</span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.updateTime')">
          <template #default="scope">
            <span>{{ formatDate(scope.row.timestamp) }}</span>
          </template>
        </el-table-column>
        <el-table-column width="240px" :label="t('field.actions')">
          <template #default="scope">
            <div class="operation-tools">
              <el-button link class="span-right10 primary-text" @click="playAudio(scope.row)">{{ t('action.play') }}</el-button>
              <el-button link class="span-right10 primary-text" @click="downloadAudio(scope.row)">{{ t('action.download') }}</el-button>
              <el-button v-if="runMode != 1" link class="span-right10 danger-text" @click="deleteAudio(scope.row)" :disabled="scope.row.fileId == '1234567890'">{{ t('action.delete') }}</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>
      <!-- 分页 -->
      <div class="pagination-container">
        <el-pagination  @current-change="handleCurrentChange" :page-sizes="[10, 20, 50]" @size-change="handleSizeChange" :current-page="pageData.pageNum" :page-size="pageData.pageSize" :total="pageData.total" layout="total, sizes, prev, pager, next, jumper"></el-pagination>
      </div>
    </div>

    <el-dialog :title="t('boxOther.addAudio')" v-model="addDialogVisible" class="dialogtype" center width="500px" @close="closeAdd">
      <div v-if="addDialogVisible">
        <el-form>
          <el-form-item :label="t('boxOther.audioFile') + localeColon">
            <el-upload ref="audioRef" class="form-upload" action="#" :auto-upload="false" :show-file-list="false" :on-change="handleChange" :accept="'.mp3,.wav'">
              <el-input :value="fileNameString" :placeholder="t('boxOther.selectFile')" size="small" readonly class="el-input"></el-input>
              <el-button type="primary" size="small" class="preview-btn">{{ t('boxOther.browse') }}</el-button>
              <template #tip>
                <div class="el-upload__tip">
                  <el-icon class="warning-icon"><WarningFilled /></el-icon>
                  {{ t('boxOther.audioUploadTip') }}
                </div>
              </template>
            </el-upload>
          </el-form-item>
        </el-form>
      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button size="small" @click="addDialogVisible = false">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" size="small" @click="saveAudio">{{ t('action.save') }}</el-button>
        </span>
      </template>
    </el-dialog>

    <audio ref="audio"></audio>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, getCurrentInstance } from 'vue'
import { WarningFilled } from '@element-plus/icons-vue'
import TopBar from '@/components/TopBar.vue'
import moment from 'moment'
import { t, localeColon, currentLocale } from '@/i18n'

// Resolve default audio display name from backend Chinese
const AUDIO_NAME_MAP = {
  '默认': 'boxOther.defaultAudio'
}
const resolveAudioName = (row) => {
  const name = row.fileName || row.name || ''
  const key = AUDIO_NAME_MAP[name]
  return key ? t(key) : name
}

// 定义组件名称
defineOptions({
  name: 'AudioManagement'
})

const { proxy } = getCurrentInstance()

// Refs
const topBarRef = ref(null)
const multipleTable = ref(null)
const audioRef = ref(null)
const audio = ref(null)

// Reactive data
const runMode = ref(0) // 0 单机版 1 联网版
const tableData = ref([])
const multipleSelections = ref([])

const topBarData = computed(() => ({
  formList: [
    {
      label: t('boxOther.audioName'),
      model: 'fileName'
    }
  ]
}))

const formData = reactive({
  fileName: ''
})

const pageData = reactive({
  pageSize: 10,
  pageNum: 1,
  total: 0
})

const addDialogVisible = ref(false)
const fileNameString = ref('')
const audioFile = ref(null)

// Methods
function formatDate(timestamp) {
  return timestamp ? moment(timestamp).format('YYYY-MM-DD HH:mm:ss') : ''
}

function init() {
  const params = {
    fileName: formData.fileName,
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize
  }
  proxy.$API.queryAudioFile(params).then((res) => {
    tableData.value = res.resData.audioFileList
    pageData.total = res.resData.totalCount
  })
}

function searchList() {
  pageData.pageNum = 1
  init()
}

function handleSelectionChange(val) {
  multipleSelections.value = val.map((item) => item.fileId)
}

function handleSizeChange(val) {
  pageData.pageSize = val
  init()
}

function handleCurrentChange(val) {
  pageData.pageNum = val
  init()
}

function selectable(row) {
  if (row.fileId != '1234567890') {
    return true
  }
}

function handleChange(file, fileList) {
  const isMP3orWAV = file.raw.type === 'audio/mpeg' || file.raw.type === 'audio/wav'
  const isLt2M = file.size / 1024 / 1024 < 3
  if (!isMP3orWAV) {
    proxy.$message.error(t('boxOther.audioUploadFormatError'))
    return
  }
  if (!isLt2M) {
    proxy.$message.error(t('boxOther.audioUploadSizeError'))
    return
  }

  const upload = audioRef.value
  if (fileList.length > 1) {
    upload.uploadFiles.shift()
  }
  fileNameString.value = file.name
}

function saveAudio() {
  const upload = audioRef.value
  if (upload.uploadFiles.length === 0) {
    return proxy.$message.warning(t('boxOther.selectAudioFile'))
  }

  const formData = new FormData()
  formData.append('importType', '5')
  formData.append('file', upload.uploadFiles[0].raw)
  proxy.$API.boxImportFile(formData).then(() => {
    proxy.$message.success(t('validate.uploadSucceeded'))
    searchList()
  })
  addDialogVisible.value = false
}

function playAudio(item) {
  audio.value.src = item.filePath
  audio.value.play()
}

function downloadAudio(item) {
  let path = item.filePath
  const xhr = new XMLHttpRequest()
  xhr.open('get', path)
  xhr.responseType = 'blob'
  xhr.send()
  xhr.onload = function () {
    if (this.status === 200 || this.status === 304) {
      const url = URL.createObjectURL(this.response)
      let a = document.createElement('a')
      a.href = url
      a.download = item.fileName
      document.body.appendChild(a)
      a.click()
      document.body.removeChild(a)
      proxy.$message.success(t('common.operationSucceeded'))
    }
  }
}

function deleteAudio(item) {
  proxy.$confirm(t('boxOther.deleteAudioConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    sureDeleteAudio([item.fileId])
  })
}

function sureDeleteAudio(deleteIds) {
  const params = {
    fileIdList: deleteIds
  }
  proxy.$API.deleteAudioFile(params).then((res) => {
    const { resData } = res
    if (resData?.failedList.length != 0) {
      proxy.$message.error(t('boxOther.audioLinkedError'))
    } else {
      searchList()
      proxy.$message.success(t('common.operationSucceeded'))
      multipleSelections.value = []
    }
  })
}

function handleAdd() {
  if (pageData.total >= 1000)
    return proxy.$message.error(t('boxOther.audioLimitReached', { n: 1000 }))
  addDialogVisible.value = true
}

function closeAdd() {
  const upload = audioRef.value
  upload.uploadFiles = []
  fileNameString.value = ''
}

function handleBatchDelete() {
  proxy.$confirm(t('boxOther.deleteAudioConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    sureDeleteAudio(multipleSelections.value)
  })
}

// Lifecycle
onMounted(() => {
  init()
})
</script>

<style lang="scss" scoped>
.table-container {
  background-color: #fff;
  padding: 0 15px;
  margin-top: 16px;

  .table-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 0;
  }
}

:deep(.el-upload--text) {
  display: flex;
  align-items: center;

  .el-input {
    width: 60%;
    margin-right: 10px;
  }

  .preview-btn {
    height: 32px;
  }
}

.warning-icon {
  color: #1890ff;
  margin-right: 5px;
}

.operation-tools {
  .el-button {
    padding: 0;
    margin-left: 0;
  }
}

.span-right10 {
  cursor: pointer;
  margin-right: 10px;
}

.danger-text {
  color: var(--el-color-danger) !important;
}

.primary-text {
  color: var(--el-color-primary) !important;
}
</style>