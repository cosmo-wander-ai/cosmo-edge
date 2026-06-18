<template>
  <div class="time-config">
    <div class="time-config-body">
      <div class="time-config-form">
        <el-form ref="formRef" :model="formData" :label-width="currentLocale === 'en-US' ? '200px' : '150px'">
          <el-form-item :label="t('systemManage.timezone')">
            <el-select v-model="formData.timeZoneId" class="form-content" :placeholder="t('systemManage.selectTimezone')" filterable size="small">
              <el-option v-for="zone in zoneInfoList" :key="zone.id" :label="resolveTimezoneName(zone)" :value="zone.id"></el-option>
            </el-select>
          </el-form-item>

          <el-form-item :label="t('systemManage.deviceTime')">
            <el-input v-model="formData.deviceTime" class="form-content" size="small" disabled></el-input>
          </el-form-item>

          <div class="divider">{{ t('systemManage.calibrationSettings') }}</div>

          <el-form-item>
            <el-radio-group v-model="formData.ntp.enable">
              <el-radio :value="1">{{ t('systemManage.ntpCalibration') }}</el-radio>
              <el-radio :value="0">{{ t('systemManage.manualCalibration') }}</el-radio>
            </el-radio-group>

          </el-form-item>

          <template v-if="formData.ntp.enable == 1">
            <el-form-item prop="ntp.server" :label="t('systemManage.serverAddress')" :rules="[
          { required: true, message: t('systemManage.enterServerAddress'), trigger: 'blur' }
          ]">
              <el-input v-model="formData.ntp.server" class="form-content" size="small" :placeholder="t('systemManage.enterServerAddress')"></el-input>
              <el-button type="primary" @click="handleTest(2)" size="small">{{ t('action.test') }}</el-button>
            </el-form-item>

            <el-form-item prop="ntp.port" :label="t('systemManage.ntpPort')" :rules="[
          { required: true, message: t('systemManage.enterNtpPort'), trigger: 'blur' }
          ]">
              <el-input v-model="formData.ntp.port" @input="(e)=>handleInput(e, 'port')" size="small" class="form-content" :placeholder="t('systemManage.enterPortNumber')"></el-input>
            </el-form-item>

            <el-form-item prop="ntp.interval" :label="t('systemManage.calibrationInterval') + localeColon" :rules="[
            { required: true, message: t('systemManage.enterCalibrationInterval'), trigger: 'blur' },
            { type: 'number', min: 1, max: 1440, message: t('systemManage.calibrationIntervalRange'), trigger: 'change' }
          ]">
              <template #label>
                {{ t('systemManage.calibrationInterval') }}
                <el-tooltip :content="t('systemManage.calibrationIntervalTooltip')" placement="right">
                  <i class="el-icon-question"></i>
                </el-tooltip>
                {{ localeColon }}
              </template>
              <el-input v-model.number="formData.ntp.interval" class="form-content" size="small" :placeholder="t('systemManage.enterCalibrationInterval')"></el-input>
            </el-form-item>
          </template>

          <template v-else>
            <el-form-item :label="t('systemManage.setTime')" prop="manualTime" :rules="[{ required: true, message: t('systemManage.setTimeRequired'), trigger: 'change' }]">
              <el-date-picker v-model="formData.manualTime" class="form-content" type="datetime" size="small" :disabled="formData.syncWithPC" :placeholder="t('systemManage.selectDateTime')">
              </el-date-picker>
              <el-checkbox v-model="formData.syncWithPC">{{ t('systemManage.syncWithPC') }}</el-checkbox>
            </el-form-item>
          </template>
        </el-form>
      </div>
      <div>
        <el-button type="primary" @click="handleSave" size="small">{{ t('action.save') }}</el-button>
      </div>
    </div>

    <el-dialog :title="t('common.notice')" v-model="testDialogVisible" :show-close="false" :close-on-click-modal="false" :close-on-press-escape="false" width="30%" center>
      <div style="text-align:center">
        <el-icon class="is-loading" style="font-size:30px;color:#409EFF;margin:10px 0;display:block;width:100%">
          <Loading />
        </el-icon>
        <p>{{ t('systemManage.testingInProgress') }}</p>
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import { ElMessage } from 'element-plus'
import { Loading } from '@element-plus/icons-vue'
import { t, localeColon, currentLocale } from '@/i18n'
import moment from 'moment'

// Standard English timezone names keyed by UTC offset value
const TIMEZONE_EN_MAP = {
  '-12:00': 'International Date Line West',
  '-11:00': 'Midway Island, Samoa',
  '-10:00': 'Hawaii',
  '-09:00': 'Alaska',
  '-08:00': 'Pacific Time (US & Canada)',
  '-07:00': 'Mountain Time (US & Canada)',
  '-06:00': 'Central Time (US & Canada)',
  '-05:00': 'Eastern Time (US & Canada)',
  '-04:30': 'Caracas',
  '-04:00': 'Atlantic Time (Canada)',
  '-03:30': 'Newfoundland',
  '-03:00': 'Buenos Aires, Georgetown',
  '-02:00': 'Mid-Atlantic',
  '-01:00': 'Azores, Cape Verde Islands',
  '+00:00': 'London, Dublin, Lisbon',
  '+01:00': 'Berlin, Paris, Rome, Madrid',
  '+02:00': 'Cairo, Athens, Helsinki',
  '+03:00': 'Moscow, Baghdad, Kuwait',
  '+03:30': 'Tehran',
  '+04:00': 'Abu Dhabi, Muscat, Baku',
  '+04:30': 'Kabul',
  '+05:00': 'Islamabad, Karachi',
  '+05:30': 'Mumbai, New Delhi, Kolkata',
  '+05:45': 'Kathmandu',
  '+06:00': 'Dhaka, Almaty',
  '+06:30': 'Yangon (Rangoon)',
  '+07:00': 'Bangkok, Hanoi, Jakarta',
  '+08:00': 'Beijing, Hong Kong, Singapore',
  '+09:00': 'Tokyo, Seoul',
  '+09:30': 'Adelaide, Darwin',
  '+10:00': 'Sydney, Melbourne, Guam',
  '+11:00': 'Solomon Islands',
  '+12:00': 'Auckland, Wellington, Fiji'
}

const resolveTimezoneName = (zone) => {
  if (currentLocale.value !== 'en-US') return zone.name
  // Extract UTC offset from name like "(UTC+08:00)..."
  const match = zone.name.match(/\(UTC([+-]\d{2}:\d{2})\)/)
  if (match) {
    const offset = match[1]
    const cities = TIMEZONE_EN_MAP[offset] || ''
    return `(UTC${offset}) ${cities}`.trim()
  }
  // Fallback: use zone.value if available
  if (zone.value) {
    const cities = TIMEZONE_EN_MAP[zone.value] || ''
    return `(UTC${zone.value}) ${cities}`.trim()
  }
  return zone.name
}

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const timer = ref(null)
const formRef = ref(null)
const formData = ref({
  timeZoneId: '',
  deviceTime: '',
  ntp: {
    server: '',
    enable: 1,
    interval: '60',
    port: ''
  },
  manualTime: null,
  syncWithPC: false,
  timeZoneValue: '',
  timestamp: ''
})
const zoneInfoList = ref([])
const testDialogVisible = ref(false)

watch(() => formData.value.syncWithPC, (val) => {
  if (val) {
    formData.value.manualTime = new Date()
  }
})

const updateDeviceTime = () => {
  const timeZoneOffset = formData.value.timeZoneValue || '+08:00'
  const baseTime = moment(Number(formData.value.timestamp))
  formData.value.deviceTime = baseTime
    .utcOffset(timeZoneOffset)
    .format('YYYY-MM-DD HH:mm:ss')
}

const startTimer = () => {
  if (timer.value) {
    clearInterval(timer.value)
    timer.value = null
  }

  updateDeviceTime()
  timer.value = setInterval(() => {
    formData.value.timestamp = Number(formData.value.timestamp) + 1000
    updateDeviceTime()
    if (formData.value.syncWithPC) {
      formData.value.manualTime = new Date()
    }
  }, 1000)
}

const queryTimeConfig = () => {
  $API.queryTimeConfig().then((res) => {
    const { resData } = res
    zoneInfoList.value = resData.zoneInfoList || []
    formData.value.timeZoneId = resData.timeStatus.timeZoneId
    formData.value.timeZoneValue = resData.timeStatus.timeZoneValue
    formData.value.timestamp = resData.timeStatus.timestamp
    formData.value.ntp.enable = resData.timeStatus.ntp.enable
    formData.value.ntp.server = resData.timeStatus.ntp.server
    formData.value.ntp.port = resData.timeStatus.ntp.port
    formData.value.ntp.interval = resData.timeStatus.ntp.interval

    startTimer()
  })
}

const handleTest = (ntp) => {
  const params = {
    ntpEnable: ntp,
    ntpServer: formData.value.ntp.server,
    ntpPort: Number(formData.value.ntp.port),
    interval: Number(formData.value.ntp.interval),
    timeZoneId: formData.value.timeZoneId
  }
  formRef.value.validate((valid) => {
    if (valid) {
      $API.testNtpdate(params).then((res) => {
        const { resData } = res
        if (resData.status === 1) {
          if (ntp === 1) {
            ElMessage.success(t('common.operationSucceeded'))
            queryTimeConfig()
          } else {
            ElMessage.success(t('systemManage.testSuccess'))
          }
        } else {
          ElMessage.error(res.resData.statusMsg || t('systemManage.testFailed'))
        }
      })
    }
  })
}

const handleInput = (val, key) => {
  formData.value.ntp[key] = val.replace(/[^\d]/g, '')
}

const handleSave = () => {
  if (formData.value.ntp.enable == 1) {
    handleTest(1)
  } else {
    formRef.value.validate((valid) => {
      if (valid) {
        const params = {
          timestamp: new Date(formData.value.manualTime).getTime(),
          timeZoneId: formData.value.timeZoneId
        }
        $API.modifyTime(params).then(() => {
          ElMessage.success(t('common.operationSucceeded'))
          queryTimeConfig()
        })
      }
    })
  }
}

const init = () => {
  queryTimeConfig()
}

onMounted(() => {
  init()
})

onBeforeUnmount(() => {
  if (timer.value) {
    clearInterval(timer.value)
    timer.value = null
  }
})
</script>

<style lang="scss" scoped>
.time-config {
  padding: 20px;

  .divider {
    font-size: 14px;
    color: #606266;
    margin: 20px 0;
    padding-left: 10px;
    border-left: 4px solid #409eff;
  }

  .interval-item {
    :deep(.el-form-item__content) {
      display: flex;
      align-items: center;

      .el-icon-question {
        margin-left: 10px;
        color: #909399;
        cursor: pointer;
      }
    }
  }
}

.time-config-body {
  display: flex;
  flex-wrap: wrap;

  .time-config-form {
    width: 700px;
  }
}

.form-content {
  width: 400px;
  margin-right: 20px;
}
</style>