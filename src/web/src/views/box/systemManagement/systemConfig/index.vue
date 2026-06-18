<template>
  <div class="system-config">
    <el-tabs v-model="activeName">
      <el-tab-pane :label="t('systemManage.deviceInfo')" name="device">
        <device-info v-if="activeName === 'device'" />
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.resourceConsumption')" name="resource">
        <resource-consume v-if="activeName === 'resource'" />
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.timeSettings')" name="time">
        <time-config-set v-if="activeName === 'time'" />
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.warningSettings')" name="warning">
        <warning-set v-if="activeName === 'warning'" />
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.restartSettings')" name="restart">
        <div class="restart-setting">
          <div class="restart-setting-form">
            <el-form :label-width="currentLocale === 'en-US' ? '170px' : '120px'">
              <el-form-item :label="t('systemManage.timedRestart')">
                <el-switch v-model="restartConfig.isTimingRestart" :active-value="1" :inactive-value="0" />
              </el-form-item>
              <el-form-item v-if="restartConfig.isTimingRestart" :label="t('systemManage.restartTime')">
                <el-select v-model="restartConfig.weekDay" size="small" style="width: 100px">
                  <el-option v-for="item in dayOptions" :key="item.value" :label="item.label" :value="item.value">
                  </el-option>
                </el-select>
                <el-time-picker v-model="restartConfig.restartTime" format="HH:mm" size="small" :placeholder="t('systemManage.selectTime')" style="width:100px;margin-left: 10px" />
              </el-form-item>
            </el-form>
          </div>
          <div class="restart-setting-tools">
            <el-button type="primary" @click="handleSaveConfig" size="small">{{ t('action.save') }}</el-button>
          </div>
        </div>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup>
import { ref, computed, watch, getCurrentInstance } from 'vue'
import { ElMessage, ElLoading } from 'element-plus'
import DeviceInfo from './components/DeviceInfo.vue'
import ResourceConsume from '@/views/home/components/ResourceConsume.vue'
import TimeConfigSet from './components/TimeConfigSet.vue'
import WarningSet from './components/WarningSet.vue'
import { t, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const activeName = ref('device')
const restartConfig = ref({
  isTimingRestart: 1,
  weekDay: 0,
  restartTime: new Date(2000, 0, 1, 2, 0, 0)
})

const dayOptions = computed(() => [
  { value: 0, label: t('systemManage.everyday') },
  { value: 1, label: t('systemManage.everyMonday') },
  { value: 2, label: t('systemManage.everyTuesday') },
  { value: 3, label: t('systemManage.everyWednesday') },
  { value: 4, label: t('systemManage.everyThursday') },
  { value: 5, label: t('systemManage.everyFriday') },
  { value: 6, label: t('systemManage.everySaturday') },
  { value: 7, label: t('systemManage.everySunday') }
])

watch(activeName, (val) => {
  if (val === 'restart') {
    queryDevRestartParam()
  }
})

const queryDevRestartParam = () => {
  $API.boxQueryDevRestartParam().then((res) => {
    const { resData } = res
    restartConfig.value = {
      isTimingRestart: resData.isTimingRestart,
      weekDay: resData.weekDay,
      restartTime: resData.restartTime
        ? (() => {
            const [hours, minutes] = resData.restartTime.split(':')
            const date = new Date()
            date.setHours(parseInt(hours, 10))
            date.setMinutes(parseInt(minutes, 10))
            return date
          })()
        : null
    }
  })
}

const handleSaveConfig = () => {
  const params = {
    isTimingRestart: restartConfig.value.isTimingRestart
  }
  if (!restartConfig.value.restartTime) {
    ElMessage.warning(t('systemManage.selectRestartTime'))
    return
  }
  params.weekDay = parseInt(restartConfig.value.weekDay)
  params.restartTime = restartConfig.value.restartTime
    ? restartConfig.value.restartTime
        .getHours()
        .toString()
        .padStart(2, '0') +
      ':' +
      restartConfig.value.restartTime
        .getMinutes()
        .toString()
        .padStart(2, '0')
    : null
  $API.boxModifyDevRestartParam(params).then(() => {
    ElMessage.success(t('common.operationSucceeded'))
  })
}

const confirmRestart = () => {
  $API.boxResetSystem({ resetOperation: 0 }).then(() => {
    const loading = ElLoading.service({
      lock: true,
      text: t('systemManage.restartingCountdown', { n: 60 }),
      background: 'rgba(0, 0, 0, 0.7)'
    })

    let countdown = 60
    const timer = setInterval(() => {
      countdown--
      loading.setText(t('systemManage.restartingCountdown', { n: countdown }))
      if (countdown <= 0) {
        clearInterval(timer)
        loading.close()
        localStorage.removeItem('mtk')
        localStorage.removeItem('token')
        window.location.href = '/box/#/boxLogin'
      }
    }, 1000)
  })
}
</script>
<style lang="scss" scoped>
.system-config {
  padding: 20px;
  background: #fff;
  border-radius: 4px;
}

.restart-setting {
  display: flex;
  flex-wrap: wrap;
}

.restart-setting-form {
  width: 400px;
}

.restart-setting-tools {
  margin-top: 5px;
}
</style>
