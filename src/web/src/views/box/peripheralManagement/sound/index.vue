<template>
  <div class="mv-wrap">
    <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="60" @search="searchList" />

    <div class="table-container">
      <div class="table-header">
        <div>{{ t('boxOther.soundList') }}</div>
        <div class="table-tools">
          <el-button type="primary" size="small" @click="handleAdd">{{ t('action.add') }}</el-button>
          <el-button type="primary" size="small" @click="deleteSound(multipleSelection)" :disabled="multipleSelection.length === 0">{{ t('action.bulkDelete') }}</el-button>
        </div>
      </div>

      <el-table ref="multipleTable" style="width: 100%" :data="tableData" @selection-change="handleSelectionChange">
        <el-table-column type="selection" width="55px" :selectable="selectable"></el-table-column>
        <el-table-column :label="t('field.no')" width="85">
          <template #default="scope">
            <span>{{((page.pageNum-1)*page.pageSize)+scope.$index+1}}</span>
          </template>
        </el-table-column>
        <el-table-column prop="name" show-overflow-tooltip :label="t('boxOther.soundName')"></el-table-column>
        <el-table-column prop="ip" show-overflow-tooltip :label="t('boxOther.ipAddress')"></el-table-column>
        <el-table-column prop="status" show-overflow-tooltip :label="t('field.status')">
          <template #default="scope">
            <span v-if="scope.row.online" style="color: lightgreen;">{{ t('status.online') }}</span>
            <span v-else style="color:red;">{{ t('status.offline') }}</span>
          </template>
        </el-table-column>
        <el-table-column width="240px" :label="t('field.actions')">
          <template #default="scope">
            <div class="operation-tools">
              <el-button link class="span-right10 primary-text" @click="testSound(scope.row)">{{ t('action.test') }}</el-button>
              <el-button link class="span-right10 primary-text" @click="editSound(scope.row)">{{ t('action.edit') }}</el-button>
              <el-button link class="span-right10 danger-text" @click="deleteSound([scope.row.devId])">{{ t('action.delete') }}</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <div class="pagination-container">
        <el-pagination  @current-change="handleCurrentChange" :page-sizes="[10, 20, 50]" @size-change="handleSizeChange" :current-page="page.pageNum" :page-size="page.pageSize" :total="page.total" layout="total, sizes, prev, pager, next, jumper"></el-pagination>
      </div>
    </div>

    <!-- 添加/编辑音柱 -->
    <el-dialog :title="addDialogTitle" v-model="addDialogVisible" class="dialogtype" center width="500px">
      <div v-if="addDialogVisible">
        <el-form :model="addFormData" ref="formRef" :rules="rules" :label-width="currentLocale === 'en-US' ? '200px' : '150px'" label-position="right">
          <el-form-item :label="t('boxOther.soundName') + localeColon" class="form-content" prop="name">
            <el-input v-model="addFormData.name" size="small" :placeholder="t('boxOther.enterSoundName')" maxlength="32"></el-input>
          </el-form-item>
          <el-form-item :label="t('boxOther.ipAddress') + localeColon" class="form-content" prop="ip">
            <el-input v-model="addFormData.ip" size="small" :placeholder="t('boxOther.enterIpAddress')"></el-input>
          </el-form-item>
          <el-form-item :label="t('boxOther.audioPlaybackPort') + localeColon" prop="ethName">
            <el-select filterable v-model="addFormData.ethName" class="form-content" size="small">
              <el-option v-for="item in netCardList" :key="item.ethName" :value="item.ethName" :label="item.ethName"></el-option>
            </el-select>
          </el-form-item>
        </el-form>
      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="addDialogVisible = false" size="small" style="margin-right: 10px;">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" size="small" @click="saveSound">{{ t('action.save') }}</el-button>
        </span>
      </template>
    </el-dialog>

    <el-dialog :title="t('action.test')" v-model="testDialogVisible" class="dialogtype" center width="600px">
      <div v-if="testDialogVisible">
        <el-tabs v-model="activeName" type="card" @tab-click="handleClick">
          <el-tab-pane :label="t('boxOther.audioPlayback')" name="first">
            <el-form :model="audioForm" ref="audioFormRef" :label-width="currentLocale === 'en-US' ? '200px' : '170px'" label-position="right">
              <el-form-item v-for="item in resolvedAudioConfig" :key="item.key" :prop="item.key" :rules="getFormRules(item)">
                <template #label>
                  <span style="position:relative">
                    <span>{{ item.name }}{{ localeColon }}</span>
                    <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                      <template #content>
                        <p>{{ item.description }}</p>
                      </template>
                      <el-icon><QuestionFilled /></el-icon>
                    </el-tooltip>
                  </span>
                </template>
                <el-input v-if="item.type == 'text'" v-model="audioForm[item.key]" class="form-content" size="small"></el-input>
                <el-select v-else-if="item.type == 'select'" v-model="audioForm[item.key]" class="form-content" size="small">
                  <el-option v-for="obj in audioList" :key="obj.fileId" :label="obj.fileName" :value="obj.fileId"></el-option>
                </el-select>
              </el-form-item>
            </el-form>
          </el-tab-pane>
          <el-tab-pane :label="t('boxOther.textPlayback')" name="second">
            <el-form :model="soundForm" ref="soundFormRef" :label-width="currentLocale === 'en-US' ? '200px' : '170px'" label-position="right">
              <el-form-item v-for="item in resolvedSoundConfig" :key="item.key" :prop="item.key" :rules="getFormRules(item)">
                <template #label>
                  <span style="position:relative">
                    <span>{{ item.name }}{{ localeColon }}</span>
                    <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                      <template #content>
                        <p>{{ item.description }}</p>
                      </template>
                      <el-icon><QuestionFilled /></el-icon>
                    </el-tooltip>
                  </span>
                </template>
                <el-input v-if="item.type == 'text'" v-model="soundForm[item.key]" class="form-content" size="small"></el-input>
                <el-select v-else-if="item.type == 'select'" v-model="soundForm[item.key]" class="form-content" size="small">
                  <el-option v-for="obj in item.options" :key="obj.value" :label="obj.label" :value="obj.value"></el-option>
                </el-select>
              </el-form-item>
            </el-form>
          </el-tab-pane>
        </el-tabs>
      </div>
      <template #footer>
        <span class="dialog-footer">
          <el-button type="primary" @click="testClick" size="small">{{ t('action.test') }}</el-button>
        </span>
      </template>
    </el-dialog>

  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, getCurrentInstance } from 'vue'
import { QuestionFilled } from '@element-plus/icons-vue'
import moment from 'moment'
import { audioConfig, soundConfig } from './config'
import TopBar from '@/components/TopBar.vue'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveI18nConfigItem } from '@/utils/i18nResource'

// 定义组件名称
defineOptions({
  name: 'SoundManagement'
})

const { proxy } = getCurrentInstance()

// Refs
const topBarRef = ref(null)
const multipleTable = ref(null)
const formRef = ref(null)
const audioFormRef = ref(null)
const soundFormRef = ref(null)

// Reactive data
const deviceStatusEnum = reactive({
  map: {
    0: 'status.offline',
    1: 'status.online'
  },
  options: [
    { value: '0', labelI18nKey: 'status.offline' },
    { value: '1', labelI18nKey: 'status.online' }
  ]
})

const topBarData = computed(() => ({
  formList: [
    {
      label: t('boxOther.soundName'),
      model: 'name'
    }
  ]
}))

const formData = reactive({
  name: ''
})

const tableData = ref([])

const page = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})

// Use dialogMode for stable state, compute title for display
const dialogMode = ref('add')
const addDialogTitle = computed(() => {
  return dialogMode.value === 'add' ? t('boxOther.addSound') : t('boxOther.editSound')
})
const addDialogVisible = ref(false)
const eidtSound = ref({})
const multipleSelection = ref([])
const testDialogVisible = ref(false)

const addFormData = reactive({
  name: '',
  ip: '',
  ethName: '',
  devId: ''
})

// IP 验证函数
function validateIP(rule, value, callback) {
  if (value === '' || typeof value === 'undefined' || value == null) {
    callback(new Error(t('boxOther.ipFormatError')))
  } else {
    const reg = /^(\d{1,2}|1\d\d|2[0-4]\d|25[0-5])\.(\d{1,2}|1\d\d|2[0-4]\d|25[0-5])\.(\d{1,2}|1\d\d|2[0-4]\d|25[0-5])\.(\d{1,2}|1\d\d|2[0-4]\d|25[0-5])$/
    if (!reg.test(value) && value !== '') {
      callback(new Error(t('boxOther.ipFormatError')))
    } else {
      callback()
    }
  }
}

const rules = computed(() => ({
  name: [{ required: true, message: t('boxOther.enterSoundName'), trigger: 'blur' }],
  ip: [
    { required: true, message: t('boxOther.enterIpAddress'), trigger: 'blur' },
    {
      required: true,
      message: t('boxOther.ipFormatError'),
      validator: validateIP,
      trigger: 'blur'
    }
  ],
  ethName: [{ required: true, message: t('boxOther.selectNetCard'), trigger: 'blur' }]
}))

const netCardList = ref([])
const audioList = ref([])
const activeName = ref('first')

const audioForm = reactive({
  audioDevPlayAudioFile: '',
  audioDevVolume: '50',
  audioDevDuration: '60',
  audioDevPlayCount: '1',
  audioDevPlayInterval: '1'
})

const soundForm = reactive({
  audioDevPlayString: '',
  audioDevPlayColor: '0',
  audioDevPlaySpeed: '50',
  audioDevVolume: '50',
  audioDevDuration: '60',
  audioDevPlayCount: '1',
  audioDevPlayInterval: '1'
})

const testSoundObj = ref({})

// Resolve config i18n keys to display strings via computed
const resolvedAudioConfig = computed(() => {
  return audioConfig.map(resolveI18nConfigItem)
})

const resolvedSoundConfig = computed(() => {
  return soundConfig.map(resolveI18nConfigItem)
})

// Methods
function initTestFormData() {
  audioConfig.forEach((element) => {
    audioForm[element.key] = element.defaultValue
  })
  soundConfig.forEach((element) => {
    soundForm[element.key] = element.defaultValue
  })
}

function initAudioList() {
  const params = {
    fileName: '',
    pageNum: page.pageNum,
    pageSize: page.pageSize
  }
  proxy.$API.queryAudioFile(params).then((res) => {
    audioList.value = res.resData.audioFileList
  })
}

function initSoundList() {
  const params = {
    name: formData.name,
    pageNum: page.pageNum,
    pageSize: page.pageSize
  }
  proxy.$API.queryAudioDevice(params).then((res) => {
    tableData.value = res.resData.audioDevList
    page.total = res.resData.totalCount
  })
}

function queryNetCard() {
  proxy.$API.queryNetCard().then((res) => {
    netCardList.value = res.resData.netCardList
  })
}

function searchList() {
  page.pageNum = 1
  initSoundList()
}

function selectable(row) {
  if (row.fileId != '1234567890') {
    return true
  }
}

function handleCurrentChange(pageNum) {
  page.pageNum = pageNum
  initSoundList()
}

function handleSizeChange(val) {
  page.pageSize = val
  initSoundList()
}

function handleAdd() {
  addFormData.name = ''
  addFormData.ip = ''
  addFormData.ethName = netCardList.value.length > 0 ? netCardList.value[0].ethName : ''
  addFormData.devId = ''
  dialogMode.value = 'add'
  addDialogVisible.value = true
}

function saveSound() {
  const ref = formRef.value
  ref.validate((valid) => {
    if (valid) {
      const params = {
        devOperation: 1,
        audioDev: {
          ...addFormData
        }
      }
      if (dialogMode.value === 'edit') {
        params.devOperation = 2
      } else {
        delete params.audioDev.devId
      }
      proxy.$API.modifyAudioDevice(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        addDialogVisible.value = false
        initSoundList()
      })
    }
  })
}

function handleSelectionChange(val) {
  multipleSelection.value = val.map((item) => {
    return item.devId
  })
}

function editSound(item) {
  dialogMode.value = 'edit'
  addFormData.name = item.name
  addFormData.ip = item.ip
  addFormData.ethName = item.ethName
  addFormData.devId = item.devId
  addDialogVisible.value = true
}

function testSound(item) {
  initTestFormData()
  testSoundObj.value = item
  activeName.value = 'first'
  testDialogVisible.value = true
}

function testClick() {
  if (activeName.value == 'first') {
    const ref = audioFormRef.value
    ref.validate((valid) => {
      if (valid) {
        const params = {
          operation: 1,
          devSn: testSoundObj.value.devId,
          data: audioForm.audioDevPlayAudioFile,
          volume: Number(audioForm.audioDevVolume),
          duration: Number(audioForm.audioDevDuration),
          times: Number(audioForm.audioDevPlayCount),
          gap: Number(audioForm.audioDevPlayInterval)
        }
        proxy.$API.testAudioDevice(params).then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
        })
      }
    })
  } else if (activeName.value == 'second') {
    const ref = soundFormRef.value
    ref.validate((valid) => {
      if (valid) {
        const params = {
          operation: 2,
          devSn: testSoundObj.value.devId,
          data: soundForm.audioDevPlayString,
          volume: Number(soundForm.audioDevVolume),
          duration: Number(soundForm.audioDevDuration),
          times: Number(soundForm.audioDevPlayCount),
          gap: Number(soundForm.audioDevPlayInterval),
          speed: Number(soundForm.audioDevPlaySpeed),
          tone: Number(soundForm.audioDevPlayColor)
        }
        proxy.$API.testAudioDevice(params).then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
        })
      }
    })
  }
}

function deleteSound(ids) {
  const params = {
    devIdList: ids
  }
  proxy.$confirm(t('boxOther.deleteSoundConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    proxy.$API.deleteAudioDevice(params).then((res) => {
      const { resData } = res
      if (resData?.failedList.length != 0) {
        proxy.$message.error(t('boxOther.soundLinkedError'))
      } else {
        proxy.$message.success(t('common.operationSucceeded'))
      }
      multipleSelection.value = []
      initSoundList()
    })
  })
}

function getFormRules(element) {
  const requiredMessage = element.type == 'text'
    ? t('validate.enterField', { field: element.name })
    : t('validate.selectField', { field: element.name })
  const parseRegExpr = (rule) => {
    const r = (rule || '').toString().trim()
    if (!r) return /.*/
    if (r.startsWith('/') && r.lastIndexOf('/') > 0) {
      const last = r.lastIndexOf('/')
      const pattern = r.slice(1, last)
      const flags = r.slice(last + 1)
      try {
        return new RegExp(pattern, flags)
      } catch {
        return /.*/
      }
    }
    try {
      return new RegExp(r)
    } catch {
      return /.*/
    }
  }
  return [
    { required: true, message: requiredMessage, trigger: 'blur' },
    {
      pattern: parseRegExpr(element.regexpr),
      message: element.failedTip,
      trigger: 'blur'
    }
  ]
}

function handleClick(tab) {
  console.log(tab.name)
}

// Lifecycle
onMounted(() => {
  initSoundList()
  initAudioList()
  queryNetCard()
})
</script>

<style lang="scss" scoped>
.other {
  margin-left: 60px;
}

.btnIconfont {
  padding: 0 30px;
  button {
    background-color: #3598ff;
    border-radius: 6px;
    color: #fff;
    font-size: 14px;
    padding: 8px 18px;
    .iconfont {
      font-size: 14px;
    }
  }
  button:nth-last-child(1) {
    background: #f56c6c;
  }
}

.flexjustify {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

:deep(.el-upload--text) {
  display: flex;

  .el-input {
    width: 60%;
    margin-right: 10px;
  }
}

.el-upload__tip {
  margin-top: 30px;
}

.warning-icon {
  color: #1890ff;
  margin-right: 5px;
}

.delte-audio-body {
  padding: 20px 20px;
  text-align: center;
  white-space: pre;
}

.flexdelete {
  display: flex;
  flex-direction: column;
  align-items: center;
}

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

.form-content {
  width: calc(100% - 50px);
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
