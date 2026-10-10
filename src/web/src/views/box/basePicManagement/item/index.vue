<template>
  <div class="people-management ui-admin-page">
    <header class="ui-page-heading"><h1>{{ t('nav.itemLibrary') }}</h1></header>
    <div class="template-container">
      <!-- 左侧模板列表 -->
      <div class="template-list">
        <div class="list-header">
          <span class="title">{{ t('basePic.allItemLibs') }}</span>
          <el-icon v-if="runMode != 1" @click="handleAdd"><Plus /></el-icon>
        </div>
        <div class="list-content">
          <div v-for="item in thingsLibList" :key="item.id" class="template-item" :class="{ active: currenPersonLib.id === item.id }" @click="handleSelectPersonLib(item)">
            <div class="item-name">{{ item.name }}</div>
            <div v-if="runMode != 1" class="item-actions">
              <el-button link class="ui-action" @click.stop="handleEdit(item)"><el-icon><EditPen /></el-icon>{{ t('action.edit') }}</el-button>
              <el-button link class="ui-action danger-text" @click.stop="handleDelete(item)"><el-icon><Delete /></el-icon>{{ t('action.delete') }}</el-button>
            </div>
          </div>
        </div>
      </div>

      <!-- 右侧编辑区域 -->
      <div class="template-body">
        <div class="search-result-body">
          <div class="search-result-header">
            <div class="header-left">
              <span class="title">{{ t('basePic.itemList') }}
                <span class="title-tip">{{ t('basePic.itemCaptureTip') }}</span>
              </span>
              <span class="template-num">{{ t('basePic.itemCount', { n: pageData.total }) }}</span>
            </div>
            <div v-if="runMode != 1" class="header-right">
              <div class="operation-tools">
                <el-button type="primary" @click="handleCaptureAdd" size="small" :disabled="thingsLibList.length === 0" style="padding: 8px 16px;">{{ t('basePic.captureAdd') }}</el-button>
                <el-button type="primary" @click="handleManualAdd" class="ui-secondary-button" size="small" :disabled="thingsLibList.length === 0" style="padding: 8px 16px;">{{ t('basePic.manualAdd') }}</el-button>
                <el-button type="primary" @click="handleBatchRemove" size="small" :disabled="multipleSelections.length === 0" style="padding: 8px 16px;" class="ui-secondary-button">{{ t('action.bulkDelete') }}</el-button>
                <el-button type="primary" @click="handleClear" size="small" :disabled="thingsLibList.length === 0" style="padding: 8px 16px;" class="ui-secondary-button">{{ t('basePic.clear') }}</el-button>
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
                  <el-image :src="item.pictureUrl" fit="contain" class="grid-image" @click="proxy.$imgView([item.pictureUrl])"></el-image>
                  <div v-if="runMode != 1" class="grid-actions">
                    <div class="operation-tools">
                      <el-button link class="danger-text ui-action ui-action-delete" @click="handleDeleteWorkCloth([item.id])">{{ t('action.delete') }}</el-button>
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
      <el-form :model="personFormData" :rules="personFormRules" ref="personFormRef" :label-width="currentLocale === 'en-US' ? '160px' : '100px'" label-position="right">
        <el-form-item :label="t('basePic.itemLibName')" prop="name">
          <el-input v-model.trim="personFormData.name" class="form-content" size="small" autocomplete="off" />
        </el-form-item>
        <el-form-item :label="t('basePic.threshold')" prop="threshold">
          <template #label>
            <span style="display:inline-block;">
              {{ t('basePic.threshold') }}
              <el-tooltip effect="dark" :content="t('basePic.thresholdRange', { n: 70 })" placement="top">
                <el-icon><QuestionFilled /></el-icon>
              </el-tooltip>
            </span>
          </template>
          <el-input v-model="personFormData.threshold" class="form-content" size="small" autocomplete="off" @input="(e)=>handleInput(e, 'threshold')" />
        </el-form-item>
      </el-form>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="faceDialogVisible = false" size="small">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="handleAddFaceSubmit" size="small">{{ t('action.save') }}</el-button>
        </span>
      </template>
    </el-dialog>

    <InfoCreate :title="infoCreateTitle" v-model:visible="infoCreateVisible" :data="addPeopleDialogInfomation" :thingsLibId="currenPersonLib.id" @updateLib="updateLib"></InfoCreate>
    <Capture v-model:visible="captureVisible" :thingsLibId="currenPersonLib.id" @updateCapture="updateCapture"></Capture>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, watch, getCurrentInstance } from 'vue'
import { Plus, EditPen, Delete, QuestionFilled } from '@element-plus/icons-vue'
import TopBar from '@/components/TopBar.vue'
import InfoCreate from './components/infoCreate.vue'
import Capture from './components/Capture.vue'
import { t, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()

// 定义组件名称
defineOptions({
  name: 'PeopleManagement'
})

// Refs
const personFormRef = ref(null)

// Reactive data
const runMode = 0 // 0 单机版 1 联网版
const topBarData = {
  formList: [
    {
      labelI18nKey: 'basePic.personName',
      type: 'text',
      model: 'name'
    },
    {
      labelI18nKey: 'basePic.personCode',
      type: 'text',
      model: 'code'
    }
  ]
}

const personFormData = reactive({
  name: '',
  threshold: 70
})

const personFormRules = {
  name: [
    { required: true, message: t('basePic.enterLibName', { name: t('basePic.itemLibName') }), trigger: 'blur' },
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
}

const thingsLibList = ref([])
const pageData = reactive({
  pageNum: 1,
  pageSize: 10,
  total: 0
})

const multipleSelections = ref([])
const tableData = ref([])
const faceDialogVisible = ref(false)
const faceDialogTitle = ref('')
const isEditItemLib = ref(false)
const currenPersonLib = ref({}) // 右侧显示的物品库
const thingsLibIdList = ref([])
const infoCreateTitle = ref('')
const addPeopleDialogVisible = ref(false)
const addPeopleDialogInfomation = ref({})
const infoCreateVisible = ref(false)
const captureVisible = ref(false)

// Watch
watch(() => currenPersonLib.value.id, (newVal) => {
  if (newVal) {
    queryThingsPictures()
  }
}, { immediate: true })

// Methods
function queryThingsLibInfo() {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  return proxy.$API.queryThingsLibInfo(params).then((res) => {
    const { resData } = res
    thingsLibList.value = resData.thingsLibList || []
    if (Object.keys(currenPersonLib.value).length === 0) {
      currenPersonLib.value = thingsLibList.value.length > 0 ? thingsLibList.value[0] : {}
    }
    return thingsLibList.value
  })
}

function queryThingsPictures() {
  const params = {
    thingsLibIdList: [currenPersonLib.value.id],
    pageNum: pageData.pageNum,
    pageSize: pageData.pageSize
  }
  proxy.$API.queryThingsPictures(params).then((res) => {
    const { resData } = res
    tableData.value = resData?.thingsList || []
    pageData.total = resData.totalCount
  })
}

function handleSelectPersonLib(item) {
  currenPersonLib.value = item
}

// 添加
function handleAdd() {
  if (thingsLibList.value.length >= 100) {
    return proxy.$message.warning(t('basePic.itemLibLimitWarning'))
  }
  personFormRef.value && personFormRef.value.resetFields()
  Object.assign(personFormData, {
    name: '',
    threshold: 70
  })
  faceDialogTitle.value = t('basePic.addItemLib')
  isEditItemLib.value = false
  faceDialogVisible.value = true
}

function handleInput(val, key) {
  // 使用正则表达式替换非数字字符
  if (!val) return
  personFormData[key] = val.replace(/[^\d]/g, '')
}

function handleAddFaceSubmit() {
  personFormRef.value.validate((valid) => {
    if (valid) {
      const params = {
        thingsLib: {
          maxFaceNumber: 1000,
          name: personFormData.name,
          threshold: Number(personFormData.threshold)
        },
        thingsLibOperation: 1
      }
      if (isEditItemLib.value) {
        params.thingsLib.id = personFormData.id
        params.thingsLibOperation = 2
      }
      proxy.$API.modifyThingsLib(params).then((res) => {
        proxy.$message.success(t('common.operationSucceeded'))
        queryThingsLibInfo()
        faceDialogVisible.value = false
      })
    }
  })
}

function handleDeleteWorkCloth(ids) {
  proxy.$confirm(t('validate.deleteConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = {
      thingsIdList: ids,
      thingsLibId: currenPersonLib.value.id
    }
    proxy.$API.deleteLibThings(params).then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      multipleSelections.value = []
      queryThingsPictures()
    })
  })
}

function handleCaptureAdd() {
  captureVisible.value = true
}

function handleManualAdd() {
  addPeopleDialogInfomation.value = {
    thingsLibId: currenPersonLib.value.id
  }
  infoCreateVisible.value = true
}

function updateLib() {
  infoCreateVisible.value = false
  queryThingsPictures()
}

function updateCapture() {
  captureVisible.value = false
  queryThingsPictures()
}

// 批量导入
function handleBatchImport() {
  // TODO: 实现批量导入功能
  const upload = proxy.$refs.batchUpload
  upload.open()
}

function updateUploadStatus() {}

// 删除
function handleDelete(item) {
  // TODO: 实现删除功能
  if (item.thingsNumber) {
    proxy.$confirm(t('basePic.itemLibHasItems'), t('common.notice'), {
      type: 'warning'
    })
  } else {
    proxy.$confirm(t('basePic.deleteItemLib'), t('common.notice'), {
      type: 'warning'
    }).then(() => {
      const params = {
        thingsLibIdList: [item.id]
      }
      proxy.$API.deleteThingsLib(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        queryThingsLibInfo().then(() => {
          // 删除后选中第一个物品库
          if (thingsLibList.value.length > 0) {
            currenPersonLib.value = thingsLibList.value[0]
          } else {
            currenPersonLib.value = {}
          }
        })
      })
    })
  }
}

function handleBatchRemove() {
  handleDeleteWorkCloth(multipleSelections.value)
}

// 清空
function handleClear() {
  // TODO: 实现清空功能
  proxy.$confirm(t('basePic.clearItemConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = {
      thingsLibId: currenPersonLib.value.id,
      removeAll: 1
    }
    proxy.$API.deleteLibThings(params).then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      queryThingsPictures()
    })
  })
}

// 编辑
function handleEdit(item) {
  personFormRef.value && personFormRef.value.resetFields()
  // TODO: 实现编辑功能
  faceDialogTitle.value = t('basePic.editItemLib')
  isEditItemLib.value = true
  Object.assign(personFormData, {
    id: item.id,
    name: item.name,
    threshold: item.threshold
  })
  faceDialogVisible.value = true
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
  queryThingsPictures()
}

// 页码改变
function handleCurrentChange(val) {
  pageData.pageNum = val
  queryThingsPictures()
}

// Lifecycle hooks
onMounted(() => {
  queryThingsLibInfo()
})
</script>

<style lang="scss" scoped>
@use "../library-layout.scss" as library;
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
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.1);
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
    flex-direction: column;
    gap: 8px;

    .title {
      font-size: 18px;
      font-weight: 600;
      color: var(--text-primary);

      .title-tip {
        font-size: 12px;
        color: var(--text-secondary);
        font-weight: normal;
      }
    }

    .template-num {
      font-size: 14px;
      color: var(--text-secondary);
      background: var(--theme-surface-soft, #f0f2f5);
      padding: 4px 12px;
      border-radius: 12px;
      align-self: flex-start;
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
        object-fit: contain;
        background: var(--bg-primary);
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

@include library.refined-library;
</style>
