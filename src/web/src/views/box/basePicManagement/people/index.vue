<template>
  <div class="people-management ui-admin-page">
    <header class="ui-page-heading"><h1>{{ t('nav.faceLibrary') }}</h1></header>
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
              <el-button link class="ui-action" @click.stop="handleEdit(item)"><el-icon><EditPen /></el-icon>{{ t('action.edit') }}</el-button>
              <el-button link class="ui-action danger-text" @click.stop="handleDelete(item)"><el-icon><Delete /></el-icon>{{ t('action.delete') }}</el-button>
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
                <el-button v-if="isUploading" style="background-color: var(--theme-warning, #E6A23C); color: #fff; padding: 8px 16px;" @click="showUploadingDialog" :disabled="faceLibList.length === 0" size="small">{{ t('basePic.batchImporting') }}</el-button>
                <el-button v-else type="primary" @click="handleBatchImport" size="small" :disabled="faceLibList.length === 0" style="padding: 8px 16px;" class="ui-secondary-button">{{ t('basePic.batchImport') }}</el-button>
                <el-button type="primary" @click="handleBatchRemove" size="small" :disabled="multipleSelections.length === 0" style="padding: 8px 16px;" class="ui-secondary-button">{{ t('action.bulkDelete') }}</el-button>
                <el-button type="primary" @click="handleClear" size="small" :disabled="tableData.length === 0" style="padding: 8px 16px;" class="ui-secondary-button">{{ t('basePic.clear') }}</el-button>
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
                      <el-button link class="span-right10 primary-text ui-action ui-action-edit" @click="handleEditPeople(item)">{{ t('action.edit') }}</el-button>
                      <el-button link class="span-right10 danger-text ui-action ui-action-delete" @click="handleRemovePeople([item.id])">{{ t('action.delete') }}</el-button>
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

    <el-dialog :title="faceDialogTitle" v-model="faceDialogVisible" center width="450px" class="ui-admin-dialog">
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
  background: var(--bg-primary);
}

.template-container {
  display: flex;
  height: 100%;
  background: var(--theme-surface, #fff);
  border-radius: 8px;
  box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
  overflow: hidden;
}

.template-list {
  min-width: 240px;
  border-right: 1px solid var(--border-color);
  display: flex;
  flex-direction: column;
  background: var(--bg-primary);

  .list-header {
    padding: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid var(--border-color);
    background: var(--theme-surface, #fff);

    .title {
      font-size: 16px;
      font-weight: 600;
      color: var(--text-primary);
    }

    .el-icon {
      font-size: 20px;
      color: var(--primary-color);
      cursor: pointer;
      padding: 4px;
      border-radius: 4px;
      transition: all 0.3s;

      &:hover {
        background: var(--el-color-primary-light-9);
        color: var(--primary-dark);
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
      background: var(--el-color-primary-light-9);
      border-color: var(--el-color-primary-light-7);
    }

    &.active {
      background: var(--bg-secondary);
      color: var(--text-primary);
      border-color: var(--primary-color);
      box-shadow: 0 0 0 2px rgba(88, 82, 223, 0.3), 0 6px 16px rgba(88, 82, 223, 0.15);
      transform: scale(1.02);

      .item-actions .el-icon {
        color: var(--primary-color);
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
        color: var(--primary-color);
        cursor: pointer;
        padding: 6px;
        border-radius: 4px;
        transition: all 0.3s;
        font-size: 26px;

        &:hover {
          background: var(--theme-accent-soft, rgba(88, 82, 223, 0.1));
          transform: scale(1.1);
        }
      }
    }
  }
}

.template-body {
  flex: 1;
  min-height: 0;
  min-width: 0;
  display: flex;
  flex-direction: column;
  background: var(--theme-surface, #fff);
}

.search-result-body {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  padding: 16px;
  overflow: hidden;
}

.empty-block {
  grid-column: 1 / -1;
  height: 100%;
  min-height: 300px;
  display: flex;
  justify-content: center;
  align-items: center;
  background: var(--bg-secondary);
  border-radius: 8px;
  border: 2px dashed var(--theme-border, #dee2e6);
}

.search-result-header {
  flex-shrink: 0;
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
  padding: 16px 20px;
  background: var(--bg-secondary);
  border-radius: 8px;
  border: 1px solid var(--border-color);

  .header-left {
    display: flex;
    align-items: center;
    gap: 20px;

    .title {
      font-size: 18px;
      font-weight: 600;
      color: var(--text-primary);
    }

    .template-num {
      font-size: 14px;
      color: var(--text-secondary);
      background: var(--theme-surface-soft, #f0f2f5);
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
  min-height: 0;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
  grid-auto-rows: max-content;
  align-content: start;
  gap: 16px;
  overflow-y: auto;
  padding: 2px;

  .grid-item {
    position: relative;
    min-width: 0;
    border: 1px solid var(--border-color);
    border-radius: 8px;
    overflow: hidden;
    box-shadow: var(--shadow-sm);
    transition: border-color 0.15s;

    &:hover {
      border-color: var(--theme-border, #c5c9d4);
    }

    &-selected {
      border-color: var(--primary-color);
      box-shadow: 0 0 0 1px var(--primary-color);
    }

    .grid-checkbox {
      .el-checkbox {
        position: absolute;
        top: 12px;
        left: 12px;
        z-index: 2;
        background: transparent;
        padding: 0;
      }

      :deep(.el-checkbox__inner) {
        width: 18px;
        height: 18px;
        border-radius: 4px;
        border: 1px solid var(--primary-color);
        background-color: var(--theme-surface, #fff);
      }

      :deep(.is-checked .el-checkbox__inner) {
        background-color: var(--primary-color);
      }
    }

    :deep(.el-card__body) {
      padding: 0;
      height: auto;
    }

    // Image, metadata and actions are distinct rows. Nothing overlays the
    // thumbnail except its existing selection control.
    .grid-content {
      display: flex;
      flex-direction: column;
      min-width: 0;

      .grid-image {
        display: block;
        flex: 0 0 220px;
        width: 100%;
        height: 220px;
        object-fit: cover;
        cursor: pointer;
      }

      .grid-info {
        position: static;
        padding: 12px;
        color: var(--text-primary);
        background: var(--theme-surface, #fff);
        border-top: 1px solid var(--border-light);

        .info-item {
          display: flex;
          align-items: center;
          min-width: 0;
          gap: 4px;
          font-size: 13px;
          line-height: 22px;

          .label {
            color: var(--text-secondary);
            flex-shrink: 0;
            font-weight: 400;
          }

          span:not(.label) {
            flex: 1;
            min-width: 0;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
            color: var(--text-primary);
          }
        }
      }

      .grid-actions {
        position: static;
        opacity: 1;
        margin-top: auto;
        padding: 8px 12px;
        background: var(--theme-surface, #fff);
        border-top: 1px solid var(--border-light);

        .operation-tools {
          display: flex;
          flex-direction: row;
          flex-wrap: wrap;
          justify-content: flex-end;
          gap: 8px;

          .el-button {
            margin: 0;
            background: transparent;
            border: 0;
            box-shadow: none;
            font-size: 13px;
          }
        }
      }
    }
  }
}

.pagination-container {
  flex-shrink: 0;
  padding: 20px;
  text-align: center;
  background: var(--bg-primary);
  border-top: 1px solid var(--border-color);
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
    color: var(--danger-color) !important;
  }
}

.primary-text {
  color: var(--el-color-primary) !important;

  &:hover {
    color: var(--primary-dark) !important;
  }
}

// 滚动条样式优化
:deep(.search-result-container::-webkit-scrollbar) {
  width: 6px;
}

:deep(.search-result-container::-webkit-scrollbar-track) {
  background: var(--theme-surface-soft, #f1f1f1);
  border-radius: 3px;
}

:deep(.search-result-container::-webkit-scrollbar-thumb) {
  background: var(--theme-border, #c1c1c1);
  border-radius: 3px;

  &:hover {
    background: var(--theme-border, #a8a8a8);
  }
}



// 表单样式优化
:deep(.el-form-item__label) {
  font-weight: 500;
  color: var(--text-secondary);
}

:deep(.el-input__inner) {
  border-radius: 6px;
  transition: all 0.3s;

  &:focus {
    box-shadow: 0 0 0 2px rgba(88, 82, 223, 0.2);
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
