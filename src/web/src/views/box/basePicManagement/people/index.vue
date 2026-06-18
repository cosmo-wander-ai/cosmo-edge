<template>
  <div class="people-management">
    <div class="template-container">
      <!-- 左侧模板列表 -->
      <div class="template-list">
        <div class="list-header">
          <span class="title">{{ t('basePic.allFaceLibs') }}</span>
          <el-icon @click="handleAdd"><Plus /></el-icon>
        </div>
        <div class="list-content">
          <div v-for="item in faceLibList" :key="item.id" class="template-item" :class="{ active: currentFaceLib.id === item.id }" @click="handleSelectFaceLib(item)">
            <div class="item-name">{{ item.name }}</div>
            <div class="item-actions">
              <el-icon @click.stop="handleEdit(item)"><EditPen /></el-icon>
              <el-icon style="color:red;" @click.stop="handleDelete(item)"><Delete /></el-icon>
            </div>
          </div>
        </div>
      </div>

      <!-- 右侧编辑区域 -->
      <div class="template-body">
        <TopBar ref="topBarRef" :dataSouce="topBarData" :formData="formData" :labelWidth="60" @search="searchList" />
        <div class="search-result-body">
          <div class="search-result-header">
            <div class="header-left">
              <span class="title">{{ t('basePic.personList') }}</span>
              <span class="template-num">{{ t('basePic.personCount', { n: pageData.total }) }}</span>
            </div>
            <div class="header-right">
              <div class="operation-tools">
                <el-button type="primary" @click="handleAddPeople" size="small" :disabled="faceLibList.length === 0" style="padding: 8px 16px;">{{ t('action.add') }}</el-button>
                <el-button v-if="isUploading" style="background-color: #E6A23C; color: #fff; padding: 8px 16px;" @click="showUploadingDialog" :disabled="faceLibList.length === 0" size="small">{{ t('basePic.batchImporting') }}</el-button>
                <el-button v-else type="primary" @click="handleBatchImport" size="small" :disabled="faceLibList.length === 0" style="padding: 8px 16px;">{{ t('basePic.batchImport') }}</el-button>
                <el-button type="primary" @click="handleBatchRemove" size="small" :disabled="multipleSelections.length === 0" style="padding: 8px 16px;">{{ t('action.bulkDelete') }}</el-button>
                <el-button type="primary" @click="handleClear" size="small" :disabled="tableData.length === 0" style="padding: 8px 16px;">{{ t('basePic.clear') }}</el-button>
              </div>
            </div>
          </div>
          <div class="search-result-container">
            <template v-if="tableData.length">
              <el-card v-for="item in tableData" :key="item.id" class="grid-item" :class="{'grid-item-selected': multipleSelections.includes(item.id)}">
                <div class="grid-checkbox">
                  <el-checkbox :value="multipleSelections.includes(item.id)" @change="checked => handleGridSelect(checked, item)"></el-checkbox>
                </div>
                <div class="grid-content">
                  <el-image :src="item.pictureList[0].url" fit="cover" class="grid-image" @click="proxy.$imgView(item.pictureList[0].url)"></el-image>
                  <div class="grid-info">
                    <div class="info-item">
                      <span class="label">{{ t('basePic.personName') }}{{ localeColon }}</span>
                      <el-tooltip :content="item.name" placement="top">
                        <span>{{ item.name }}</span>
                      </el-tooltip>
                    </div>
                    <div class="info-item">
                      <span class="label">{{ t('basePic.personCode') }}{{ localeColon }}</span>
                      <el-tooltip :content="item.serialNumber" placement="top">
                        <span>{{ item.serialNumber }}</span>
                      </el-tooltip>
                    </div>
                  </div>
                  <div class="grid-actions">
                    <div class="operation-tools">
                      <el-button link class="span-right10 primary-text" @click="handleEditPeople(item)">{{ t('action.edit') }}</el-button>
                      <el-button link class="span-right10 danger-text" @click="handleRemovePeople([item.id])">{{ t('action.delete') }}</el-button>
                    </div>
                  </div>
                </div>
              </el-card>
            </template>
            <div v-else class="empty-block">
              <el-empty :description="t('common.noData')"></el-empty>
            </div>
          </div>

          <div class="pagination-container">
            <el-pagination @size-change="handleSizeChange" @current-change="handleCurrentChange" :current-page="pageData.pageNum" :page-sizes="[10, 20, 50, 100]" :page-size="pageData.pageSize" :total="pageData.total" layout="total, sizes, prev, pager, next, jumper">
            </el-pagination>
          </div>
        </div>
      </div>
    </div>

    <el-dialog :title="faceDialogTitle" v-model="faceDialogVisible" center width="450px">
      <el-form :model="faceFormData" :rules="faceFormRules" ref="faceFormRef" :label-width="currentLocale === 'en-US' ? '130px' : '80px'" label-position="right">
        <el-form-item :label="t('basePic.faceLibName')" prop="name">
          <el-input v-model.trim="faceFormData.name" class="form-content" size="small" autocomplete="off" />
        </el-form-item>
        <el-form-item :label="t('basePic.threshold')" prop="threshold">
          <template #label>
            <span style="display:inline-block;">
              {{ t('basePic.threshold') }}
              <el-tooltip effect="dark" :content="t('basePic.thresholdRange', { n: 82 })" placement="top">
                <el-icon><QuestionFilled /></el-icon>
              </el-tooltip>
            </span>
          </template>
          <el-input v-model="faceFormData.threshold" class="form-content" size="small" autocomplete="off" @input="(e)=>handleInput(e, 'threshold')" />
        </el-form-item>
      </el-form>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="faceDialogVisible = false" size="small">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="handleAddFaceSubmit" size="small">{{ t('action.save') }}</el-button>
        </span>
      </template>
    </el-dialog>

    <InfoCreate :title="addPeopleDialogTitle" v-model:visible="addPeopleDialogVisible" :data="addPeopleDialogInfomation" @updatePage="handleAddPeopleDialogClose" />

    <ImportDialog ref="batchUpload" :title="t('basePic.batchImport')" :faceLibId="currentFaceLib.id" @update-status="updateUploadStatus" />

  </div>
</template>

<script setup>
import { ref, reactive, onMounted, watch, getCurrentInstance } from 'vue'
import { Plus, EditPen, Delete, QuestionFilled } from '@element-plus/icons-vue'
import TopBar from '@/components/TopBar.vue'
import InfoCreate from './components/infoCreate.vue'
import ImportDialog from './components/ImportDialog.vue'
import { t, localeColon, currentLocale } from '@/i18n'

// 定义组件名称
defineOptions({
  name: 'PeopleManagement'
})

const { proxy } = getCurrentInstance()

// Refs
const topBarRef = ref(null)
const faceFormRef = ref(null)
const batchUpload = ref(null)

// Reactive data
const topBarData = reactive({
  formList: [
    {
      labelI18nKey: 'basePic.personName',
      type: 'text',
      model: 'personName'
    },
    {
      labelI18nKey: 'basePic.personCode',
      type: 'text',
      model: 'serialNumber'
    }
  ]
})

const formData = reactive({
  personName: '',
  serialNumber: ''
})

const faceFormData = reactive({
  name: '',
  threshold: 82
})

const faceFormRules = reactive({
  name: [
    { required: true, message: t('basePic.enterLibName', { name: t('basePic.faceLibName') }), trigger: 'blur' },
    { max: 32, message: t('basePic.lengthLimit'), trigger: 'blur' }
  ],
  threshold: [
    { required: true, message: t('basePic.enterThreshold'), trigger: 'blur' },
    {
      validator: (rule, value, callback) => {
        const num = parseFloat(value)
        if (isNaN(num) || num < 0 || num > 100) {
          callback(new Error(t('basePic.thresholdRangeError')))
        } else {
          callback()
        }
      },
      trigger: 'blur'
    }
  ]
})

const faceLibList = ref([])
const pageData = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})

const templateList = ref([])
const multipleSelections = ref([])
const tableData = ref([])
const faceDialogVisible = ref(false)
const faceDialogTitle = ref('')
const isEditFaceLib = ref(false)
const currentFaceLib = ref({}) // 右侧显示的脸库
const faceLibIdList = ref([])
const addPeopleDialogTitle = ref('')
const addPeopleDialogVisible = ref(false)
const addPeopleDialogInfomation = ref({})
const isUploading = ref(false)

// Watch
watch(() => currentFaceLib.value.id, (newVal) => {
  if (newVal) {
    queryFaces()
  }
}, { immediate: true })

// Methods
function queryFaceLib() {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  return proxy.$API.queryFaceLibInfo(params).then((res) => {
    const { resData } = res
    faceLibList.value = resData.faceLibList || []
    if (Object.keys(currentFaceLib.value).length === 0) {
      currentFaceLib.value = faceLibList.value.length > 0 ? faceLibList.value[0] : {}
    }
    return faceLibList.value
  })
}

function queryFaces() {
  if (!currentFaceLib.value || !currentFaceLib.value.id) {
    return
  }

  const params = {
    faceLibIdList: [currentFaceLib.value.id],
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize,
    personName: formData.personName,
    serialNumber: formData.serialNumber
  }
  proxy.$API.queryFaces(params).then((res) => {
    const { resData } = res
    tableData.value = resData?.personList || []
    pageData.total = resData.totalCount
  })
}

function searchList() {
  pageData.pageNum = 1
  queryFaces()
}

// 重置
function handleReset() {
  formData.personName = ''
  formData.serialNumber = ''
  searchList()
}

function handleSelectFaceLib(item) {
  currentFaceLib.value = item
}

// 添加
function handleAdd() {
  if (faceLibList.value.length >= 100) {
    return proxy.$message.warning(t('basePic.faceLibLimitWarning'))
  }
  faceFormRef.value && faceFormRef.value.resetFields()
  faceFormData.name = ''
  faceFormData.threshold = 82
  faceDialogTitle.value = t('basePic.addFaceLib')
  isEditFaceLib.value = false
  faceDialogVisible.value = true
}

function handleInput(val, key) {
  // 使用正则表达式替换非数字字符
  faceFormData[key] = val.replace(/[^\d]/g, '')
}

function handleAddFaceSubmit() {
  faceFormRef.value.validate((valid) => {
    if (valid) {
      const params = {
        faceLib: {
          maxFaceNumber: 20000,
          name: faceFormData.name,
          threshold: Number(faceFormData.threshold)
        },
        faceLibOperation: 1
      }
      if (isEditFaceLib.value) {
        params.faceLib.id = faceFormData.id
        params.faceLibOperation = 2
      }
      proxy.$API.modifyFaceLib(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        queryFaceLib()
        faceDialogVisible.value = false
      })
    }
  })
}

function handleAddPeople() {
  addPeopleDialogTitle.value = t('basePic.addPerson')
  addPeopleDialogInfomation.value = {
    faceLibId: [currentFaceLib.value.id],
    pictureList: [],
    personName: '',
    serialNumber: ''
  }
  addPeopleDialogVisible.value = true
}

function handleEditPeople(item) {
  addPeopleDialogTitle.value = t('basePic.editPerson')
  addPeopleDialogInfomation.value = {
    faceLibId: [currentFaceLib.value.id],
    personName: item.name,
    personId: item.id,
    serialNumber: item.serialNumber,
    pictureList: item.pictureList
  }
  addPeopleDialogVisible.value = true
}

function handleAddPeopleDialogClose() {
  searchList()
}

// 批量导入
function handleBatchImport() {
  const upload = batchUpload.value
  upload.open()
}

function updateUploadStatus(isUploadingStatus) {
  isUploading.value = isUploadingStatus
  if (!isUploading.value) {
    searchList()
  }
}

function showUploadingDialog() {
  const upload = batchUpload.value
  upload.openUploadingDialog()
}

// 删除
function handleDelete(item) {
  if (item.personNumber) {
    proxy.$confirm(t('basePic.faceLibHasFaces'), t('common.notice'), {
      type: 'warning',
      confirmButtonText: t('action.confirm'),
      showCancelButton: false
    })
  } else {
    proxy.$confirm(t('validate.deleteConfirm'), t('common.notice'), {
      type: 'warning'
    }).then(() => {
      const params = {
        faceLibIdList: [item.id]
      }
      proxy.$API.deleteFaceLib(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        queryFaceLib().then(() => {
          // 删除后选中第一个人脸库
          if (faceLibList.value.length > 0) {
            currentFaceLib.value = faceLibList.value[0]
          } else {
            currentFaceLib.value = {}
          }
        })
      })
    })
  }
}

// 清空
function handleClear() {
  proxy.$confirm(t('basePic.clearPersonConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = {
      removeAll: 1,
      faceLibId: currentFaceLib.value.id
    }
    proxy.$API.boxDeletePerson(params).then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      queryFaces()
      queryFaceLib()
    })
  })
}

// 编辑
function handleEdit(item) {
  faceDialogTitle.value = t('basePic.editFaceLib')
  isEditFaceLib.value = true
  faceFormData.id = item.id
  faceFormData.name = item.name
  faceFormData.threshold = item.threshold
  faceDialogVisible.value = true
}

// 删除单个
function handleRemovePeople(ids) {
  proxy.$confirm(t('validate.deleteConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = {
      faceLibId: currentFaceLib.value.id,
      personIdList: ids
    }
    proxy.$API.boxDeletePerson(params).then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      multipleSelections.value = []
      queryFaces()
      queryFaceLib()
    })
  })
}

function handleBatchRemove() {
  handleRemovePeople(multipleSelections.value)
}

function handleGridSelect(checked, item) {
  if (checked) {
    multipleSelections.value.push(item.id)
  } else {
    const index = multipleSelections.value.indexOf(item.id)
    if (index !== -1) {
      multipleSelections.value.splice(index, 1)
    }
  }
}

// 分页大小改变
function handleSizeChange(val) {
  pageData.pageSize = val
  queryFaces()
}

// 页码改变
function handleCurrentChange(val) {
  pageData.pageNum = val
  queryFaces()
}

// Lifecycle
onMounted(() => {
  queryFaceLib()
})
</script>

<style lang="scss" scoped>
.people-management {
  height: 100%;
  box-sizing: border-box;
  background: #f5f7fa;
}

.template-container {
  display: flex;
  height: 100%;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
  overflow: hidden;
}

.template-list {
  min-width: 240px;
  border-right: 1px solid #e4e7ed;
  display: flex;
  flex-direction: column;
  background: #fafbfc;

  .list-header {
    padding: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid #e4e7ed;
    background: #fff;

    .title {
      font-size: 16px;
      font-weight: 600;
      color: #303133;
    }

    .el-icon {
      font-size: 20px;
      color: #409eff;
      cursor: pointer;
      padding: 4px;
      border-radius: 4px;
      transition: all 0.3s;

      &:hover {
        background: #ecf5ff;
        color: #337ecc;
      }
    }
  }

  .list-content {
    flex: 1;
    overflow-y: auto;
    padding: 12px;
  }

  .template-item {
    font-size: 14px;
    padding: 12px 16px;
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.3s;
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 8px;
    border: 1px solid transparent;

    &:hover {
      background: #f0f9ff;
      border-color: #b3d8ff;
    }

    &.active {
      background: linear-gradient(135deg, #eaf3ff 0%, #dcecff 100%);
      color: #303133;
      border-color: #409eff;
      box-shadow: 0 0 0 2px rgba(64, 158, 255, 0.3), 0 6px 16px rgba(64, 158, 255, 0.15);
      transform: scale(1.02);

      .item-actions .el-icon {
        color: #409eff;
      }
    }

    .item-name {
      max-width: 140px;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      font-weight: 500;
    }

    .item-actions {
      display: flex;
      gap: 8px;

      .el-icon {
        color: #409eff;
        cursor: pointer;
        padding: 6px;
        border-radius: 4px;
        transition: all 0.3s;
        font-size: 26px;

        &:hover {
          background: rgba(64, 158, 255, 0.1);
          transform: scale(1.1);
        }
      }
    }
  }
}

.template-body {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: #fff;
}

.search-result-body {
  flex: 1;
  padding: 20px;
  height: calc(100% - 100px);
}

.empty-block {
  grid-column: 1 / -1;
  height: 100%;
  min-height: 300px;
  display: flex;
  justify-content: center;
  align-items: center;
  background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
  border-radius: 8px;
  border: 2px dashed #dee2e6;
}

.search-result-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
  padding: 16px 20px;
  background: linear-gradient(135deg, #f8f9fa 0%, #ffffff 100%);
  border-radius: 8px;
  border: 1px solid #e4e7ed;

  .header-left {
    display: flex;
    align-items: center;
    gap: 20px;

    .title {
      font-size: 18px;
      font-weight: 600;
      color: #303133;
    }

    .template-num {
      font-size: 14px;
      color: #909399;
      background: #f0f2f5;
      padding: 4px 12px;
      border-radius: 12px;
    }
  }

  .header-right {
    .operation-tools {
      display: flex;
      gap: 12px;

      .el-button {
        border-radius: 6px;
        font-weight: 500;
        transition: all 0.3s;

        &:hover {
          transform: translateY(-1px);
          box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
        }
      }
    }
  }
}

.search-result-container {
  flex: 1;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
  gap: 20px;
  max-height: calc(100% - 120px);
  overflow-y: auto;
  padding: 4px;

  .grid-item {
    position: relative;
    border-radius: 12px;
    overflow: hidden;
    transition: all 0.3s;
    border: 2px solid transparent;

    &:hover {
      transform: translateY(-4px);
      box-shadow: 0 8px 25px rgba(0, 0, 0, 0.15);
      border-color: #409eff;
    }

    &-selected {
      border-color: #409eff;
      box-shadow: 0 4px 20px rgba(64, 158, 255, 0.3);

      :deep(.el-card__body) {
        background: linear-gradient(135deg, #f0f9ff 0%, #e6f4ff 100%);
      }
    }

    .grid-checkbox {
      .el-checkbox {
        position: absolute;
        top: 12px;
        left: 12px;
        z-index: 2;
        background: transparent;
        border-radius: 6px;
        padding: 0;
      }
      :deep(.el-checkbox__inner) {
        width: 18px;
        height: 18px;
        border-radius: 6px;
        border: 2px solid #409eff;
        background-color: rgba(255, 255, 255, 0.95);
      }
      :deep(.is-checked .el-checkbox__inner) {
        background-color: #409eff;
        border-color: #409eff;
      }
      :deep(.el-checkbox:hover .el-checkbox__inner) {
        box-shadow: 0 0 0 2px rgba(64, 158, 255, 0.2);
      }
    }

    :deep(.el-card__body) {
      padding: 0;
      height: 100%;
    }

    .grid-content {
      position: relative;
      height: 100%;

      .grid-image {
        display: block;
        width: 100%;
        height: 220px;
        object-fit: cover;
        transition: all 0.3s;
      }

      .grid-info {
        position: absolute;
        bottom: 0;
        left: 0;
        right: 0;
        background: linear-gradient(transparent, rgba(0, 0, 0, 0.8));
        color: white;
        padding: 20px 12px 12px;

        .info-item {
          font-size: 13px;
          margin-bottom: 4px;
          display: flex;
          align-items: center;

          .label {
            color: #e6f4ff;
            font-weight: 500;
            min-width: 60px;
          }

          span:not(.label) {
            flex: 1;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
            color: #fff;
          }
        }
      }

      .grid-actions {
        position: absolute;
        top: 12px;
        right: 12px;
        z-index: 2;
        opacity: 0;
        transition: all 0.3s;

        .operation-tools {
          display: flex;
          flex-direction: column;
          gap: 6px;

          .el-button {
            padding: 8px 14px;
            font-size: 14px;
            border-radius: 6px;
            background: rgba(255, 255, 255, 0.95);
            backdrop-filter: blur(4px);
            border: 1px solid rgba(255, 255, 255, 0.2);
            font-weight: 500;

            &:hover {
              transform: scale(1.05);
              box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
            }
          }
        }
      }

      &:hover .grid-actions {
        opacity: 1;
      }
    }
  }
}

.pagination-container {
  padding: 20px;
  text-align: center;
  background: #fafbfc;
  border-top: 1px solid #e4e7ed;
  border-radius: 0 0 8px 8px;
}

.form-content {
  width: calc(100% - 60px);
}

.operation-tools {
  .el-button {
    padding: 0;
    margin-left: 0;
    font-weight: 500;
    transition: all 0.3s;

    &:hover {
      transform: translateY(-1px);
    }
  }
}

.span-right10 {
  cursor: pointer;
  margin-right: 12px;
}

.danger-text {
  color: var(--el-color-danger) !important;

  &:hover {
    color: #f56c6c !important;
  }
}

.primary-text {
  color: var(--el-color-primary) !important;

  &:hover {
    color: #337ecc !important;
  }
}

// 滚动条样式优化
:deep(.search-result-container::-webkit-scrollbar) {
  width: 6px;
}

:deep(.search-result-container::-webkit-scrollbar-track) {
  background: #f1f1f1;
  border-radius: 3px;
}

:deep(.search-result-container::-webkit-scrollbar-thumb) {
  background: #c1c1c1;
  border-radius: 3px;

  &:hover {
    background: #a8a8a8;
  }
}



// 表单样式优化
:deep(.el-form-item__label) {
  font-weight: 500;
  color: #606266;
}

:deep(.el-input__inner) {
  border-radius: 6px;
  transition: all 0.3s;

  &:focus {
    box-shadow: 0 0 0 2px rgba(64, 158, 255, 0.2);
  }
}

:deep(.el-button) {
  border-radius: 6px;
  font-weight: 500;
  transition: all 0.3s;

  &:hover {
    transform: translateY(-1px);
  }
}
</style>
