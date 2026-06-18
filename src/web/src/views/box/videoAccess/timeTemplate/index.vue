<template>
  <div class="time-template">
    <div class="template-container">
      <!-- 左侧模板列表 -->
      <div class="template-list">
        <div class="list-header">
          <span class="title">{{ t('boxOther.allTemplates') }}</span>
          <el-icon @click="handleAdd"><Plus /></el-icon>
        </div>
        <div class="list-content">
          <div v-for="(item, index) in templateList" :key="item.scheduleId" class="template-item" :class="{ active: currentTemplate.scheduleId === item.scheduleId }" @click="handleSelectTemplate(item)">
            <div class="item-name">{{ resolveScheduleName(item.scheduleName) }}</div>
            <div class="item-actions" v-if="index > 2">
              <el-icon @click.stop="handleEdit(item)"><EditPen /></el-icon>
              <el-icon @click.stop="handleDelete(item)"><Delete /></el-icon>
            </div>
          </div>
        </div>
      </div>

      <!-- 右侧编辑区域 -->
      <div class="template-edit">
        <div class="edit-header">
          <div class="header-content">
            <el-form ref="rightFormRef" :model="rightTemplate" :rules="rightRules" label-width="80px" class="edit-form">
              <el-form-item :label="t('boxOther.templateName')" prop="scheduleName">
                <el-input v-if="isBuiltinTemplate(rightTemplate)" class="form-content" :model-value="resolveScheduleName(rightTemplate.scheduleName)" disabled size="small"></el-input>
                <el-input v-else class="form-content" v-model="rightTemplate.scheduleName" :placeholder="t('boxOther.templateNameRequired')" size="small"></el-input>
              </el-form-item>
              <el-form-item :label="t('boxOther.templateRemark')" prop="remark">
                <el-input class="form-content" type="textarea" v-model="rightTemplate.remark" :placeholder="t('validate.enterField', { field: t('boxOther.templateRemark') })" size="small"></el-input>
              </el-form-item>
              <el-form-item :label="t('boxOther.templateTime')">
                <div style="flex:1; position:relative">
                  <el-button size="small" style="margin-left:8px" @click="handleClickAllDays">{{ t('boxOther.fullTime') }}</el-button>
                  <el-button size="small" @click="handleClickWeekDays">{{ t('boxOther.weekday') }}</el-button>
                  <el-button size="small" @click="handleClickWeekendDays">{{ t('boxOther.weekend') }}</el-button>
                  <el-button size="small" @click="clearWeektime">{{ t('action.clear') }}</el-button>
                </div>
              </el-form-item>
            </el-form>
            <div class="save-btn">
              <el-button type="primary" @click="handleSave" size="small">{{ t('action.save') }}</el-button>
            </div>
          </div>
        </div>
        <div style="display: flex;overflow: auto;">
          <MiniDragWeektime ref="dwt" style=" width:calc(100% - 70px);min-width:800px;" v-model="weektimeData"></MiniDragWeektime>
          <div class="week-copy">
            <el-icon v-for="(item,index) in everyWeek" :key="index" @click="document(item)"><DocumentCopy /></el-icon>
          </div>
        </div>
      </div>
    </div>

    <!-- 新增/编辑弹窗 -->
    <el-dialog :title="dialogTitle" v-model="dialogVisible" width="500px" center>
      <el-form ref="formRef" :model="formData" :rules="rules" label-width="100px">
        <el-form-item :label="t('boxOther.templateName')" prop="scheduleName">
          <el-input v-model="formData.scheduleName" class="form-content300" :placeholder="t('boxOther.templateNameRequired')" size="small"></el-input>
        </el-form-item>
        <el-form-item :label="t('boxOther.templateRemark')" prop="description">
          <el-input type="textarea" class="form-content300" v-model="formData.remark" :placeholder="t('validate.enterField', { field: t('boxOther.templateRemark') })" size="small"></el-input>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false" size="small">{{ t('action.cancel') }}</el-button>
        <el-button type="primary" @click="handleSubmit" size="small">{{ t('action.ok') }}</el-button>
      </template>
    </el-dialog>

    <el-dialog :title="t('common.notice')" v-model="centerDialogVisible" width="30%" :show-close="false" center>
      <template #header>
        <span class="dialog-footer">
          <span style="line-height: 40px;">{{ t('boxOther.copyTo') }}</span>
          <span style="float: right;">
            <el-checkbox-button :indeterminate="isIndeterminate" v-model="checkAll" @change="handleCheckAllChange" border>{{ t('action.selectAll') }}</el-checkbox-button>
          </span>
        </span>
      </template>
      <span>
        <el-checkbox-group v-model="checkedCities" @change="handleCheckedCitiesChange">
          <el-checkbox-button v-for="(city,index) in everyWeek" :value="city.id" :key="index" :disabled="currentWeek==city.id">{{ weekLabel(city.id) }}</el-checkbox-button>
        </el-checkbox-group>
      </span>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="closeWindow" size="small">{{ t('action.cancel') }}</el-button>
          <el-button type="primary" @click="confirm" size="small">{{ t('action.ok') }}</el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, getCurrentInstance } from 'vue'
import { Plus, EditPen, Delete, DocumentCopy } from '@element-plus/icons-vue'
import MiniDragWeektime from './components/DragWeekTime.vue'
import moment from 'moment'
import { ElMessageBox } from 'element-plus'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

// Refs
const formRef = ref(null)
const rightFormRef = ref(null)
const dwt = ref(null)

// Data
const templateList = ref([])

// Stable week IDs — display labels come from i18n
const everyWeek = [
  { id: 1 },
  { id: 2 },
  { id: 3 },
  { id: 4 },
  { id: 5 },
  { id: 6 },
  { id: 0 }
]

// Week label lookup by id
const WEEK_I18N_MAP = {
  1: 'boxOther.monday',
  2: 'boxOther.tuesday',
  3: 'boxOther.wednesday',
  4: 'boxOther.thursday',
  5: 'boxOther.friday',
  6: 'boxOther.saturday',
  0: 'boxOther.sunday'
}

const weekLabel = (id) => t(WEEK_I18N_MAP[id])

// Map backend default schedule names → i18n keys
const SCHEDULE_NAME_MAP = {
  '全天候': 'boxOther.scheduleNameAllDay',
  '工作日': 'boxOther.scheduleNameWeekday',
  '周末工作': 'boxOther.scheduleNameWeekend'
}

const resolveScheduleName = (name) => {
  const key = SCHEDULE_NAME_MAP[name]
  return key ? t(key) : name
}

// Check if a template is one of the 3 built-in defaults (not editable name)
const BUILTIN_NAMES = new Set(Object.keys(SCHEDULE_NAME_MAP))
const isBuiltinTemplate = (template) => {
  return BUILTIN_NAMES.has(template?.scheduleName)
}

const dialogVisible = ref(false)
const dialogType = ref('add')
const formData = ref({
  scheduleName: '',
  remark: ''
})
const rules = computed(() => ({
  scheduleName: [
    { required: true, message: t('boxOther.templateNameRequired'), trigger: 'blur' },
    { max: 32, message: t('boxOther.templateNameMaxChars', { n: 32 }), trigger: 'blur' }
  ],
  remark: [
    { max: 256, message: t('boxOther.templateRemarkMaxChars', { n: 256 }), trigger: 'blur' }
  ]
}))
const rightRules = computed(() => ({
  scheduleName: [
    { required: true, message: t('boxOther.templateNameRequired'), trigger: 'blur' },
    { max: 32, message: t('boxOther.templateNameMaxChars', { n: 32 }), trigger: 'blur' }
  ],
  remark: [
    { max: 256, message: t('boxOther.templateRemarkMaxChars', { n: 256 }), trigger: 'blur' }
  ]
}))
const currentTemplate = ref({})
const rightTemplate = ref({})
const editingTemplate = ref({})
const editForm = ref({
  name: '',
  description: '',
  timeType: 'allDay'
})
const weektimeData = ref([])
const centerDialogVisible = ref(false)
const isIndeterminate = ref(true)
const checkAll = ref(false)
const checkedCities = ref([])
const currentWeek = ref(null)

// Computed
const dialogTitle = computed(() => {
  return dialogType.value === 'add' ? t('boxOther.addTemplate') : t('boxOther.editTemplate')
})

// Methods
const queryTimeTemplatePage = (currentId) => {
  $API.queryTimeTemplatePage().then((res) => {
    const { resData } = res
    templateList.value = resData.rows || []
    if (currentId) {
      currentTemplate.value = templateList.value.find(
        (item) => item.scheduleId === currentId
      )
      rightTemplate.value = JSON.parse(JSON.stringify(currentTemplate.value))
      handleWeektimeData(rightTemplate.value.scheduleConfig)
    } else if (
      !Object.keys(currentTemplate.value).length &&
      templateList.value.length > 0
    ) {
      currentTemplate.value = templateList.value[0]
      rightTemplate.value = JSON.parse(JSON.stringify(currentTemplate.value))
      handleWeektimeData(rightTemplate.value.scheduleConfig)
    } else if (templateList.value.length === 0) {
      weektimeData.value = []
      rightTemplate.value = {}
      currentTemplate.value = {}
    }
  })
}

const handleAdd = () => {
  dialogType.value = 'add'
  formData.value = { scheduleName: '', remark: '' }
  dialogVisible.value = true
}

const handleEdit = (item) => {
  dialogType.value = 'edit'
  editingTemplate.value = { ...item }
  formData.value = { scheduleName: item.scheduleName, remark: item.remark }
  dialogVisible.value = true
}

const handleDelete = (item) => {
  ElMessageBox.confirm(t('boxOther.deleteTemplateConfirm'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const params = {
      scheduleId: item.scheduleId
    }
    $API.deleteTimeTemplate(params).then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      if (currentTemplate.value.scheduleId === item.scheduleId) {
        currentTemplate.value = {}
      }
      queryTimeTemplatePage()
    })
  })
}

const handleSubmit = () => {
  formRef.value.validate((valid) => {
    if (valid) {
      if (dialogType.value === 'add') {
        const params = {
          ...formData.value,
          scheduleConfig: []
        }
        $API.addTimeTemplate(params).then((res) => {
          const { resData } = res
          proxy.$message.success(t('common.operationSucceeded'))
          dialogVisible.value = false
          queryTimeTemplatePage(resData.id)
        })
      } else {
        const params = {
          ...formData.value,
          scheduleId: editingTemplate.value.scheduleId,
          scheduleConfig: editingTemplate.value.scheduleConfig
        }
        $API.updateTimeTemplate(params).then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
          editingTemplate.value = null
          dialogVisible.value = false
          queryTimeTemplatePage()
        })
      }
    }
  })
}

const handleSelectTemplate = (item) => {
  currentTemplate.value = item
  rightTemplate.value = JSON.parse(JSON.stringify(currentTemplate.value))
  handleWeektimeData(rightTemplate.value.scheduleConfig)
}

const document = (item) => {
  checkedCities.value = []
  currentWeek.value = item.id
  if (
    weektimeData.value.filter((item) => item.week == currentWeek.value).length
  ) {
    centerDialogVisible.value = true
  } else {
    proxy.$message.warning(t('boxOther.selectTimeRange'))
  }
}

const closeWindow = () => {
  centerDialogVisible.value = false
  checkedCities.value = []
  currentWeek.value = null
}

const clearWeektime = () => {
  weektimeData.value = []
}

const handleCheckAllChange = () => {
  if (checkedCities.value.length == everyWeek.length - 1) {
    checkedCities.value = []
  } else {
    checkedCities.value = everyWeek
      .map((item) => {
        return item.id
      })
      .filter((item) => {
        return item != currentWeek.value
      })
  }
}

const handleCheckedCitiesChange = () => {
  if (checkedCities.value.length == everyWeek.length - 1) {
    checkAll.value = true
  } else {
    checkAll.value = false
  }
}

const confirm = () => {
  if (checkedCities.value.length == 0) {
    proxy.$message.warning(t('boxOther.selectWeekday'))
    return
  }
  let weektimeDataCopy = JSON.parse(JSON.stringify(weektimeData.value))
  const days = weektimeDataCopy.filter((item) => item.week == currentWeek.value)
  checkedCities.value.forEach((item) => {
    weektimeDataCopy = weektimeDataCopy.filter((el) => {
      return el.week != item
    })
    let newDays = days.map((el) => {
      return {
        week: item,
        begin: el.begin,
        end: el.end
      }
    })
    weektimeDataCopy.push(...newDays)
  })
  weektimeData.value = dwt.value.mergeSelectFragments(weektimeDataCopy)
  centerDialogVisible.value = false
}

const handleClickWeekDays = () => {
  weektimeData.value = []
  everyWeek.forEach((item) => {
    if (item.id > 0 && item.id < 6) {
      let a = {
        week: item.id,
        begin: '00:00',
        end: '23:59'
      }
      weektimeData.value.push(a)
    }
  })
}

const handleClickAllDays = () => {
  weektimeData.value = everyWeek.map((item) => {
    return {
      week: item.id,
      begin: '00:00',
      end: '23:59'
    }
  })
}

const handleClickWeekendDays = () => {
  weektimeData.value = []
  everyWeek.forEach((item) => {
    if (item.id === 6 || item.id === 0) {
      weektimeData.value.push({
        week: item.id,
        begin: '00:00',
        end: '23:59'
      })
    }
  })
}

const handleSave = () => {
  rightFormRef.value.validate((valid) => {
    if (valid) {
      let index = 7
      let arr = []
      for (let i = 0; i < index; i++) {
        let week = weektimeData.value.filter((e) => {
          if (i == e.week) {
            return e
          }
        })
        if (week.length) {
          arr.push(week)
        }
      }
      let config = []
      arr.forEach((item) => {
        if (item.length) {
          let a = item.map((el) => {
            return {
              timeBegin: el.begin,
              timeEnd: el.end
            }
          })
          let configItem = {
            weekDay: item[0].week,
            runTime: a
          }
          config.push(configItem)
        }
      })
      if (templateList.value.length == 0) {
        const params = {
          scheduleName: rightTemplate.value.scheduleName,
          remark: rightTemplate.value.remark,
          scheduleConfig: config
        }
        $API.addTimeTemplate(params).then((res) => {
          const { resData } = res
          proxy.$message.success(t('common.operationSucceeded'))
          dialogVisible.value = false
          queryTimeTemplatePage(resData.id)
        })
      } else {
        const params = {
          scheduleId: rightTemplate.value.scheduleId,
          scheduleName: rightTemplate.value.scheduleName,
          remark: rightTemplate.value.remark,
          scheduleConfig: config
        }
        $API.updateTimeTemplate(params).then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
          queryTimeTemplatePage()
        })
      }
    }
  })
}

const handleWeektimeData = (data) => {
  weektimeData.value = []
  if (data.length == 0) {
    return
  }
  let arr = []
  data.forEach((item) => {
    let week = item.weekDay
    item.runTime.forEach((time) => {
      arr.push({
        week: week,
        begin: moment(`2022-01-01 ${time.timeBegin}`).format('HH:mm'),
        end: moment(`2022-01-01 ${time.timeEnd}`).format('HH:mm')
      })
    })
  })
  weektimeData.value = dwt.value.mergeSelectFragments(arr)
}

onMounted(() => {
  queryTimeTemplatePage()
})
</script>

<style lang="scss" scoped>
.time-template {
  height: 100%;
  box-sizing: border-box;
}

.template-container {
  display: flex;
  height: 100%;
  background: #fff;
  border-radius: 4px;
}

.template-list {
  width: 200px;
  border-right: 1px solid #ebeef5;
  display: flex;
  flex-direction: column;

  .list-header {
    padding: 15px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid #ebeef5;

    .title {
      font-size: 16px;
      font-weight: 500;
    }

    .el-icon {
      font-size: 18px;
      color: #599ef8;
      cursor: pointer;
    }
  }

  .list-content {
    flex: 1;
    overflow-y: auto;
    padding: 10px;
  }

  .template-item {
    font-size: 14px;
    padding: 5px 0 5px 10px;
    border-radius: 4px;
    cursor: pointer;
    transition: all 0.3s;
    display: flex;
    justify-content: space-between;
    align-items: center;

    &:hover {
      background: #f5f7fa;
    }

    &.active {
      background: #ecf5ff;
    }

    .item-name {
      max-width: 120px;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }

    .item-actions {
      .el-icon {
        color: #599ef8;
        cursor: pointer;
        margin-left: 5px;
      }
    }
  }
}

.template-edit {
  flex: 1;
  padding: 20px;
  display: flex;
  flex-direction: column;
  overflow: auto;

  .edit-header {
    padding-bottom: 20px;

    .header-content {
      display: flex;
      margin-bottom: 20px;
    }

    .form-content {
      width: 400px;
    }

    .edit-form {
      flex: 1;
      padding-right: 20px;
    }

    .save-btn {
      padding-top: 5px;
    }
  }
}

.form-content300 {
  width: 300px;
}

.week-copy {
  padding: 60px 0 64px;
  margin-left: 15px;
  .el-icon {
    display: block;
    cursor: pointer;
    font-size: 20px;
    height: 30px;
    line-height: 30px;

    &:hover {
      color: #599ef8;
    }
  }
}
</style>
