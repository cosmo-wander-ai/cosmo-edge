<template>
  <div>
    <el-form class="info-list" :label-width="currentLocale === 'en-US' ? '160px' : '120px'" label-position="right">
      <el-form-item v-for="item in deviceInfo" :key="item.key" :label="resolveLabel(item)">
        <span v-if="item.key === 'aiAuthorization'">
          <template v-if="resolveAuthParts(item.value).length > 1">
            <span class="status-success">{{ resolveAuthParts(item.value)[0] }}</span>
            <span>&nbsp;</span>
            <span>{{ resolveAuthParts(item.value)[1] }}</span>
          </template>
          <template v-else>
            {{ resolveAuthValue(item.value) }}
          </template>
        </span>
        <span v-else>{{ item.key === 'devRunTimeSec' ? formattedRunTime : item.value }}</span>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import { t, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const deviceInfo = ref([])
const timer = ref(null)
const runTime = ref(0)

const formattedRunTime = computed(() => formatRunTime(runTime.value))

// Map API item.key to i18n key under systemManage.*
const LABEL_KEY_MAP = {
  deviceType: 'deviceModel',
  hardwareVersion: 'firmwareVersion',
  softwareVersion: 'softwareVersion',
  deviceSn: 'deviceSN',
  aiAuthorization: 'authStatus',
  devRunTimeSec: 'deviceUptime'
}

const resolveLabel = (item) => {
  const i18nKey = LABEL_KEY_MAP[item.key]
  const colon = currentLocale.value === 'en-US' ? ':' : '：'
  if (i18nKey) {
    return t(`systemManage.${i18nKey}`) + colon
  }
  // Fallback: use original name from API
  return item.name + colon
}

// Map Chinese auth status values to i18n keys
const AUTH_STATUS_MAP = {
  '已授权': 'authStatusLicensed',
  '未授权': 'authStatusUnlicensed',
  '授权失败': 'authStatusFailed'
}

const resolveAuthValue = (value = '') => {
  const text = String(value)
  const key = AUTH_STATUS_MAP[text]
  return key ? t(`systemManage.${key}`) : text
}

const resolveAuthParts = (value = '') => {
  const text = String(value)
  const parts = text.split(' ')
  if (parts.length > 1) {
    return [resolveAuthValue(parts[0]), parts[1]]
  }
  return [resolveAuthValue(value)]
}

const getDeviceInfo = () => {
  $API.queryDeviceInfo().then((res) => {
    const { resData } = res
    deviceInfo.value = resData?.devInfoList || []
    startTimer()
  })
}

const startTimer = () => {
  if (timer.value) {
    clearInterval(timer.value)
  }
  const runTimeItem = deviceInfo.value.find(
    (item) => item.key === 'devRunTimeSec'
  )
  if (runTimeItem) {
    runTime.value = parseInt(runTimeItem.value)
  }
  timer.value = setInterval(() => {
    runTime.value++
    if (runTimeItem) {
      runTimeItem.value = String(runTime.value)
    }
  }, 1000)
}

const formatRunTime = (seconds) => {
  if (!seconds) return t('systemManage.zeroSeconds')
  const sec = parseInt(seconds)
  const days = Math.floor(sec / (24 * 60 * 60))
  const hours = Math.floor((sec % (24 * 60 * 60)) / (60 * 60))
  const minutes = Math.floor((sec % (60 * 60)) / 60)
  const remainSeconds = sec % 60

  let result = ''
  if (days > 0) result += t('systemManage.days', { n: days })
  if (hours > 0) result += t('systemManage.hours', { n: hours })
  if (minutes > 0) result += t('systemManage.minutes', { n: minutes })
  if (remainSeconds > 0) result += t('systemManage.seconds', { n: remainSeconds })

  return result || t('systemManage.zeroSeconds')
}

onMounted(() => {
  getDeviceInfo()
})

onBeforeUnmount(() => {
  if (timer.value) {
    clearInterval(timer.value)
  }
})
</script>

<style lang="scss" scoped>
.info-list {
  padding: 20px;

  :deep(.el-form-item) {
    margin-bottom: 5px;

    .el-form-item__content {
      font-size: 14px;
      color: #303133;
    }
  }
}

.status-success {
  color: #67c23a;
}
</style>
