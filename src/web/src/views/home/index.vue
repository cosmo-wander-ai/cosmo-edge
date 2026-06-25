<template>
  <div class="overview-page">
    <!-- 资源概览条 -->
    <div class="resource-bar">
      <div class="resource-bar-header">
        <div>
          <div class="resource-bar-title">{{ $t('home.systemResource') }}</div>
          <div class="resource-bar-subtitle">{{ $t('home.loadOverview') }}</div>
        </div>
        <div class="resource-bar-meta">
          <span class="resource-run-status">
            <span class="resource-status-dot"></span>
            {{ $t('status.running') }}
          </span>
          <span>{{ $t('home.updateTime', { time: resourceUpdateTime || '--:--:--' }) }}</span>
        </div>
      </div>
      <div class="resource-grid">
        <div
          v-for="item in displayResourceList"
          :key="item.key"
          class="resource-item"
        >
          <div class="resource-item-top">
            <span class="resource-label">{{ item.name }}</span>
            <span class="resource-state" :class="'state-' + item.level">{{ item.statusText }}</span>
          </div>
          <div class="resource-value-row">
            <span class="resource-value">{{ item.usedPercent }}</span>
            <span class="resource-unit">%</span>
          </div>
          <div class="resource-progress">
            <div
              class="resource-progress-fill"
              :class="'fill-' + item.level"
              :style="{ width: item.safePercent + '%' }"
            ></div>
          </div>
          <div class="resource-detail">
            <span>{{ item.usedLabel }}</span>
            <span>{{ item.unusedLabel }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- 场景任务卡片 -->
    <div class="section-header">
      <div class="section-title">
        {{ $t('home.runningSceneTasks') }}
        <span class="count">{{ $t('home.taskCount', { n: taskList.length }) }}</span>
      </div>
      <div class="section-action" @click="$router.push('/videoAccess')">
        {{ $t('home.allTasks') }}
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
      </div>
    </div>

    <div class="task-grid">
      <div
        v-for="task in taskList"
        :key="task.algorithmId"
        class="task-card"
      >
        <div class="task-card-header">
          <div class="task-card-title-area">
            <div class="task-card-name">{{ resolveResourceAlgorithmName(task) || task.name }}</div>
            <div class="task-card-meta">
              <span class="task-tag" :class="getTaskTypeClass(task)">{{ getTaskTypeLabel(task) }}</span>
            </div>
          </div>
          <div class="task-status">
            <span class="status-dot" :class="{ 'running': task.taskState === 1, 'warn': task.taskState === 2 }"></span>
            <span>{{ task.taskState === 1 ? $t('status.running') : task.taskState === 2 ? $t('status.abnormal') : $t('status.paused') }}</span>
          </div>
        </div>

        <!-- 统计信息 -->
        <div class="task-card-stats">
          <div class="task-stat">
            <div class="task-stat-label">{{ $t('home.todayAlerts') }}</div>
            <div class="task-stat-value warn-text">{{ task.todayEvents || 0 }}</div>
          </div>
          <div class="task-stat">
            <div class="task-stat-label">{{ $t('home.runningChannels') }}</div>
            <div class="task-stat-value">{{ $t('home.channelUnit', { n: task.channelCount || 1 }) }}</div>
          </div>
        </div>

        <!-- 通道标签 -->
        <div class="task-card-tags" v-if="task.channels && task.channels.length > 0">
          <span class="task-channel-tag" v-for="ch in task.channels" :key="ch.channelId">
            <span class="tag-dot"></span>
            {{ ch.channelName }}
          </span>
        </div>

        <!-- 操作按钮 -->
        <div class="task-card-actions">
          <button class="task-btn task-btn-primary" @click="openRunningView(task)">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/></svg>
            {{ $t('home.viewRunning') }}
          </button>
          <button class="task-btn task-btn-secondary" @click="toggleTask(task)">
            {{ task.taskState === 1 ? $t('home.stop') : $t('home.start') }}
          </button>
        </div>
      </div>

      <!-- 新建任务卡片 -->
      <div class="task-card-new" @click="$router.push('/gam/countManagement')">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
        <span>{{ $t('home.newSceneTask') }}</span>
      </div>
    </div>

    <!-- 实时事件流 -->
    <div class="event-feed">
      <div class="event-feed-header">
        <div class="event-feed-title">
          <span class="live-dot"></span>
          {{ $t('home.liveEvents') }}
        </div>
        <div class="section-action" @click="$router.push('/eventQuery/alarmRecord')">
          {{ $t('home.viewAll') }}
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
        </div>
      </div>
      <div class="event-list">
        <div
          v-for="(evt, idx) in recentEvents"
          :key="evt.eventKey || idx"
          class="event-item"
          @click="openEventDetail(evt)"
        >
          <div class="event-thumb" @click.stop="previewEventImages(evt)">
            <el-image v-if="getEventImage(evt)" :src="getEventImage(evt)" fit="cover">
              <template #error>
                <div class="event-thumb-placeholder">{{ $t('home.noImage') }}</div>
              </template>
            </el-image>
            <div v-else class="event-thumb-placeholder">{{ $t('home.noImage') }}</div>
          </div>
          <div class="event-main">
            <div class="event-title-row">
              <div class="event-title-wrap">
                <span class="event-level" :class="evt.level || 'info'"></span>
                <span class="event-content">{{ resolveResourceAlgorithmName(evt) }}</span>
              </div>
              <span class="event-upload-status" :class="getReportStatusClass(evt)">
                {{ getReportStatusText(evt) }}
              </span>
            </div>
            <div class="event-meta">
              <span>{{ $t('home.channel') }}{{ localeColon }}{{ evt.channelName || '-' }}</span>
              <span>{{ $t('home.area') }}{{ localeColon }}{{ evt.areaName || '-' }}</span>
              <span>{{ $t('home.time') }}{{ localeColon }}{{ formatEventDateTime(evt.time) }}</span>
              <span>{{ $t('home.source') }}{{ localeColon }}{{ resolveResourceAlgorithmName(evt) }}</span>
            </div>
            <div class="event-actions">
              <span class="event-level-badge" :class="evt.level || 'info'">{{ getLevelText(evt) }}</span>
              <button class="event-link-btn" @click.stop="openEventDetail(evt)">{{ $t('home.details') }}</button>
              <button v-if="evt.video" class="event-link-btn" @click.stop="openEventVideo(evt)">{{ $t('home.video') }}</button>
            </div>
          </div>
        </div>
        <div v-if="recentEvents.length === 0" class="event-empty">
          {{ $t('home.noLiveEvents') }}
        </div>
      </div>
    </div>

    <DetailDialog v-model:visible="detailDialogVisible" :detailData="detailData" />
    <VideoFrequency
      v-model:visible="videoDialogVisiable"
      :algorithmCode="currentEvent?.algorithmCode || ''"
      :closable="false"
      :url="currentEvent?.video || ''"
      :structureDataUrl="currentEvent?.videostructured || ''"
      :title="videoTitle"
    />
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount, getCurrentInstance, computed } from 'vue'
import { useRouter } from 'vue-router'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'
import DetailDialog from '../box/eventQuery/components/detailDialog.vue'
import VideoFrequency from '../box/eventQuery/components/videoPlaying265.vue'

const { proxy } = getCurrentInstance()
const router = useRouter()
const DEFAULT_PLATFORM_NAME = t('system.defaultPlatformName')

const normalizePlatformName = (name) => {
  if (!name || name === '智能终端管理平台' || name === '智能终端管理系统' || name === '智能盒子' || name === '边缘智能中枢') {
    return DEFAULT_PLATFORM_NAME
  }
  return name
}

// ─── 资源数据（复用原有 API）───
const resourceList = ref([])
const resourceUpdateTime = ref('')
let resourceTimer = null

const MEMORY_KEYS = [
  'businessMemoryUtilization',
  'generalMemoryUtilization',
  'modelMemoryUtilization',
  'pictureMemoryUtilization',
  'specialMemoryUtilization',
  'TPPMemoryUtilization'
]

const DISPLAY_RESOURCE_KEYS = [
  'cpuUtilization',
  'mergedMemoryUtilization',
  'npuUtilization',
  'eMMCUtilization',
  'packetDiscardUtilization'
]

const queryHardwareResource = () => {
  proxy.$API.queryHardwareResource().then((res) => {
    const { resData } = res
    resourceList.value = resData?.itemList || []
    resourceUpdateTime.value = formatResourceTime(new Date())
  }).catch(() => {})
}

const displayResourceList = computed(() => {
  const itemMap = resourceList.value.reduce((map, item) => {
    if (item?.key) map[item.key] = item
    return map
  }, {})

  const mergedMemory = getMergedMemoryItem()
  if (mergedMemory) itemMap.mergedMemoryUtilization = mergedMemory

  return DISPLAY_RESOURCE_KEYS.map(key => itemMap[key])
    .filter(Boolean)
    .map(normalizeResourceItem)
})

const getMergedMemoryItem = () => {
  const memoryItems = resourceList.value.filter(item => MEMORY_KEYS.includes(item?.key))
  if (!memoryItems.length) return null

  const capacityItems = memoryItems
    .map(item => ({
      used: parseSizeToMB(item.usedSize),
      unused: parseSizeToMB(item.unusedSize)
    }))
    .filter(item => item.used !== null && item.unused !== null)

  if (capacityItems.length) {
    const used = capacityItems.reduce((sum, item) => sum + item.used, 0)
    const unused = capacityItems.reduce((sum, item) => sum + item.unused, 0)
    const total = used + unused

    return {
      key: 'mergedMemoryUtilization',
      name: t('resource.memoryUsage'),
      usedPercent: total ? Math.round((used / total) * 100) : 0,
      usedSize: formatCapacity(used),
      unusedSize: formatCapacity(unused)
    }
  }

  const percentSum = memoryItems.reduce((sum, item) => sum + toPercentNumber(item.usedPercent), 0)
  return {
    key: 'mergedMemoryUtilization',
    name: t('resource.memoryUsage'),
    usedPercent: Math.round(percentSum / memoryItems.length),
    usedSize: t('resource.usedLabel'),
    unusedSize: t('resource.unusedLabel')
  }
}

const normalizeResourceItem = (item) => {
  const usedPercent = toPercentNumber(item.usedPercent)
  const levelInfo = getResourceLevelInfo(item.key, usedPercent)

  return {
    ...item,
    name: getResourceName(item),
    usedPercent,
    safePercent: Math.min(Math.max(usedPercent, 0), 100),
    level: levelInfo.level,
    statusText: levelInfo.text,
    usedLabel: getResourceUsedLabel(item),
    unusedLabel: getResourceUnusedLabel(item)
  }
}

const getResourceName = (item) => {
  const nameMap = {
    cpuUtilization: t('resource.cpuUsage'),
    mergedMemoryUtilization: t('resource.memoryUsage'),
    npuUtilization: t('resource.npuUsage'),
    eMMCUtilization: t('resource.emmcUsage'),
    packetDiscardUtilization: t('resource.packetLoss')
  }
  return nameMap[item.key] || item.name
}

const getResourceLevelInfo = (key, percentage) => {
  if (key === 'packetDiscardUtilization') {
    if (percentage <= 0) return { level: 'idle', text: t('resource.noPacketLoss') }
    if (percentage <= 1) return { level: 'low', text: t('resource.lowPacketLoss') }
    if (percentage <= 5) return { level: 'medium', text: t('resource.somePacketLoss') }
    return { level: 'high', text: t('resource.highPacketLoss') }
  }

  if (percentage <= 0) return { level: 'idle', text: t('resource.idle') }
  if (percentage <= 30) return { level: 'low', text: t('resource.lowLoad') }
  if (percentage <= 70) return { level: 'medium', text: t('resource.mediumLoad') }
  if (percentage <= 90) return { level: 'high', text: t('resource.highUsage') }
  return { level: 'limit', text: t('resource.nearLimit') }
}

const stripCnUnit = (val) => {
  if (!val || currentLocale.value === 'zh-CN') return val
  return String(val).replace(/个$/, '')
}

const getResourceUsedLabel = (item) => {
  if (item.key === 'packetDiscardUtilization') return t('resource.packetLost', { value: stripCnUnit(item.usedSize) || '--' })
  return t('resource.used', { value: item.usedSize || '--' })
}

const getResourceUnusedLabel = (item) => {
  if (item.key === 'packetDiscardUtilization') return t('resource.packetKept', { value: stripCnUnit(item.unusedSize) || '--' })
  return t('resource.available', { value: item.unusedSize || '--' })
}

const toPercentNumber = (value) => {
  const number = Number(value)
  return Number.isNaN(number) ? 0 : Math.round(number)
}

const parseSizeToMB = (value) => {
  if (!value || typeof value !== 'string') return null
  const match = value.trim().match(/^([\d.]+)\s*(GB|MB|KB|B)$/i)
  if (!match) return null

  const number = Number(match[1])
  const unit = match[2].toUpperCase()
  const rateMap = {
    GB: 1024,
    MB: 1,
    KB: 1 / 1024,
    B: 1 / 1024 / 1024
  }

  return Number.isNaN(number) ? null : number * rateMap[unit]
}

const formatCapacity = (valueInMB) => {
  if (valueInMB >= 1024) return `${(valueInMB / 1024).toFixed(2)} GB`
  return `${valueInMB.toFixed(2)} MB`
}

const formatResourceTime = (date) => {
  const pad = value => String(value).padStart(2, '0')
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
}

// ─── 任务列表 ───
const taskList = ref([])

const loadTaskList = () => {
  proxy.$API.boxCameraPage({ pageNum: 1, pageSize: 100 }).then(res => {
    const rows = res?.resData?.rows || []
    const taskMap = {}

    rows.forEach(channel => {
      if (channel.taskList && Array.isArray(channel.taskList)) {
        channel.taskList.forEach(task => {
          // Rule 1: We show ACTIVE tasks on the dashboard (status !== 0 instead of strictly 1)
          // To prevent tasks from silently disappearing if they encounter an error (status = 2)
          if (task.status === 0) return
          
          if (!taskMap[task.algorithmId]) {
            taskMap[task.algorithmId] = {
              ...task,
              taskState: task.status, // We use the first found active status
              channels: [],
              todayEvents: 0
            }
          } else {
            // Upgrade grouped status to running if at least one channel is running
            if (task.status === 1) taskMap[task.algorithmId].taskState = 1
          }
          // Avoid duplicate channels
          const exists = taskMap[task.algorithmId].channels.some(ch => ch.channelId === channel.videoChannelId)
          if (!exists) {
            taskMap[task.algorithmId].channels.push({
              channelId: channel.videoChannelId,
              channelName: channel.channelName || channel.videoChannelId
            })
          }
        })
      }
    })

    // Prepare final task array
    const finalTasks = Object.values(taskMap).map(grouped => ({
      ...grouped,
      channelCount: grouped.channels.length
    }))
    
    // Attempt fetching today's events for each active algorithm
    const startOfDay = new Date()
    startOfDay.setHours(0, 0, 0, 0)
    const endOfDay = new Date()
    endOfDay.setHours(23, 59, 59, 999)
    
    Promise.all(finalTasks.map(task => {
      const params = {
        timeBegin: startOfDay.getTime(),
        timeEnd: endOfDay.getTime(),
        pageNum: 1,
        pageSize: 1,
        algorithmCodes: [task.algorithmId]
      }
      return proxy.$API.boxQueryEvent(params).then(eventRes => {
        task.todayEvents = eventRes?.resData?.total || 0
      }).catch(() => {})
    })).finally(() => {
      taskList.value = finalTasks
    })
  }).catch(() => {})
}

const toggleTask = (taskGroup) => {
  // If no channels remain, do nothing
  if (!taskGroup.channels || taskGroup.channels.length === 0) return
  
  const targetSwitch = taskGroup.taskState === 1 ? 0 : 1
  const params = {
    tasks: taskGroup.channels.map(ch => ({
      channelId: ch.channelId,
      algorithmId: taskGroup.algorithmId,
      switch: targetSwitch
    }))
  }
  
  proxy.$API.boxBatchSwitchTask(params).then(() => {
    proxy.$message.success(t('common.operationSucceeded'))
    loadTaskList()
  }).catch(() => {})
}

// ─── 事件流 (WebSocket) ───
const recentEvents = ref([])
const detailDialogVisible = ref(false)
const detailData = ref({})
const videoDialogVisiable = ref(false)
const currentEvent = ref(null)
const videoTitle = ref('')
let eventWs = null

const normalizeEvent = (data = {}) => {
  const timestamp = data.timestamp || data.eventTime || data.time || Date.now()
  const algorithmName = data.algorithmName || data.eventName || data.eventMsg || t('home.detectionEvent')
  const channelName = data.channelName || data.cameraName || data.videoChannelName || ''
  const eventKey = data.id || `${timestamp}_${algorithmName}_${channelName}_${data.areaName || ''}`
  const eventLevel = Number(data.eventLevel || data.level || 1)
  const property = data.property && typeof data.property === 'object'
    ? JSON.stringify(data.property)
    : data.property

  return {
    ...data,
    property,
    eventKey,
    time: timestamp,
    timestamp,
    content: algorithmName,
    algorithmName,
    channelName,
    areaName: data.areaName || '',
    fullPicture: data.fullPicture || data.picture || data.imageUrl || '',
    detectedPicture: data.detectedPicture || data.capturePicture || data.detectPicture || '',
    video: data.video || data.alarmVideoPath || '',
    level: eventLevel >= 3 ? 'critical' : eventLevel >= 2 ? 'warning' : 'info'
  }
}

const loadRecentEvents = () => {
  const startOfDay = new Date()
  startOfDay.setHours(0, 0, 0, 0)
  const endOfDay = new Date()
  endOfDay.setHours(23, 59, 59, 999)

  const params = {
    timeBegin: startOfDay.getTime(),
    timeEnd: endOfDay.getTime(),
    pageNum: 1,
    pageSize: 20
  }

  proxy.$API.boxQueryEvent(params).then(res => {
    const rows = res?.resData?.rows || []
    const evts = rows.map(normalizeEvent)
    // API returns newest first usually, so we can just set it
    recentEvents.value = evts
  }).catch(() => {})
}

const connectEventWs = () => {
  try {
    const wsUrl = proxy.$API.bigScreenWs()
    if (!wsUrl) return
    eventWs = new WebSocket(wsUrl)
    eventWs.onmessage = (e) => {
      try {
        const data = JSON.parse(e.data)
        const evt = normalizeEvent(data)
        recentEvents.value.unshift(evt)
        if (recentEvents.value.length > 20) recentEvents.value.pop()
      } catch {}
    }
    eventWs.onclose = () => {
      setTimeout(connectEventWs, 5000)
    }
  } catch {}
}

const getTaskTypeClass = (task) => {
  const name = (task.algorithmName || task.name || '').toLowerCase()
  if (name.includes('vlm') || name.includes('qwen')) return 'vlm'
  if (name.includes('dino') || name.includes('grounding')) return 'dino'
  return 'cv'
}

const getTaskTypeLabel = (task) => {
  const name = (task.algorithmName || task.name || '').toLowerCase()
  if (name.includes('vlm') || name.includes('qwen')) return 'VLM'
  if (name.includes('dino') || name.includes('grounding')) return 'DINO'
  return 'CV'
}

const openRunningView = (task) => {
  router.push({
    path: '/bigScreen/warnningScreen',
    query: {
      algorithmId: task.algorithmId || '',
      algorithmName: resolveResourceAlgorithmName(task) || task.name || ''
    }
  })
}

const toEventDate = (t) => {
  if (!t) return null
  if (typeof t === 'number' || /^\d+$/.test(String(t))) {
    return new Date(Number(t))
  }
  const date = new Date(t)
  return Number.isNaN(date.getTime()) ? null : date
}

const formatEventDateTime = (t) => {
  const d = toEventDate(t)
  if (!d) return ''
  const date = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
  const time = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
  return `${date} ${time}`
}

const getEventImage = (evt) => evt.detectedPicture || evt.fullPicture || ''

const previewEventImages = (evt) => {
  const images = [evt.detectedPicture, evt.fullPicture].filter(Boolean)
  if (images.length) proxy.$imgView(images)
}

const openEventDetail = (evt) => {
  detailData.value = evt
  detailDialogVisible.value = true
}

const openEventVideo = (evt) => {
  if (!evt.video) return
  currentEvent.value = evt
  videoTitle.value = `${evt.channelName || t('home.channel')}_${formatEventDateTime(evt.timestamp)}_${resolveResourceAlgorithmName(evt) || t('home.liveEvents')}.mp4`
  videoDialogVisiable.value = true
}

const getReportStatusText = (evt) => {
  if (evt.reportStatus === 1) return t('home.uploaded')
  if (evt.reportStatus === 0) return t('home.notUploaded')
  return t('home.toConfirm')
}

const getReportStatusClass = (evt) => {
  if (evt.reportStatus === 1) return 'uploaded'
  if (evt.reportStatus === 0) return 'pending'
  return 'unknown'
}

const getLevelText = (evt) => {
  if (evt.level === 'critical') return t('home.critical')
  if (evt.level === 'warning') return t('home.important')
  return t('home.alert')
}

// ─── Favicon & System Config (保留原逻辑) ───
const getSystemConfig = async () => {
  try {
    const res = await proxy.$API.boxGetLogo({})
    const { resData } = res
    if (resData?.logoUrl) {
      localStorage.setItem('logoUrl', resData.logoUrl)
      const ts = Date.now()
      const link = document.querySelector("link[rel*='icon']") || document.createElement('link')
      link.type = 'image/x-icon'
      link.rel = 'icon'
      link.href = `${resData.logoUrl}?v=${ts}`
      if (!link.parentNode) document.head.appendChild(link)
    }
    if (resData?.systemName) {
      const name = normalizePlatformName(resData.systemName)
      window.getGlobalConfig().platformName = name
      localStorage.setItem('platformName', name)
      document.title = name
      const logoTextElement = document.querySelector('.logo-text')
      if (logoTextElement) logoTextElement.textContent = name
    }
  } catch {}
}

// ─── 生命周期 ───
onMounted(() => {
  queryHardwareResource()
  loadTaskList()
  loadRecentEvents()
  getSystemConfig()
  connectEventWs()
  resourceTimer = setInterval(queryHardwareResource, 5000)
})

onBeforeUnmount(() => {
  if (resourceTimer) clearInterval(resourceTimer)
  if (eventWs) eventWs.close()
})
</script>

<style lang="scss" scoped>
.overview-page {
  padding: 0;
}

/* ── 资源概览条 ── */
.resource-bar {
  padding: 14px 18px;
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.05);
  margin-bottom: 24px;
  border: 1px solid #eef2f7;
}

.resource-bar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 12px;
}

.resource-bar-title {
  font-size: 15px;
  font-weight: 700;
  color: #1e293b;
  line-height: 20px;
}

.resource-bar-subtitle {
  margin-top: 2px;
  font-size: 12px;
  color: #94a3b8;
}

.resource-bar-meta {
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 12px;
  color: #94a3b8;
  white-space: nowrap;
}

.resource-run-status {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 3px 9px;
  color: #15803d;
  background: rgba(34, 197, 94, 0.1);
  border-radius: 999px;
}

.resource-status-dot {
  width: 6px;
  height: 6px;
  background: #22c55e;
  border-radius: 50%;
}

.resource-grid {
  display: grid;
  grid-template-columns: repeat(5, minmax(150px, 1fr));
  gap: 10px;
}

.resource-item {
  min-width: 0;
  padding: 10px 12px;
  background: #f8fafc;
  border: 1px solid #eef2f7;
  border-radius: 8px;
}

.resource-item-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.resource-label {
  font-size: 12px;
  color: #64748b;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.resource-state {
  flex-shrink: 0;
  padding: 2px 7px;
  border-radius: 999px;
  font-size: 11px;
  line-height: 16px;
  font-weight: 600;
}

.resource-value-row {
  display: flex;
  align-items: baseline;
  margin-top: 8px;
}

.resource-value {
  font-size: 24px;
  line-height: 28px;
  font-weight: 700;
  color: #1e293b;
}

.resource-unit {
  margin-left: 2px;
  font-size: 13px;
  color: #94a3b8;
}

.resource-progress {
  height: 6px;
  margin-top: 8px;
  background: #e2e8f0;
  border-radius: 999px;
  overflow: hidden;
}

.resource-progress-fill {
  height: 100%;
  border-radius: inherit;
  transition: width 0.3s ease;
}

.resource-detail {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-top: 8px;
  font-size: 11px;
  color: #94a3b8;

  span {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}

.state-idle {
  color: #2563eb;
  background: rgba(59, 130, 246, 0.1);
}

.state-low {
  color: #15803d;
  background: rgba(34, 197, 94, 0.1);
}

.state-medium {
  color: #2b6cb0;
  background: rgba(49, 130, 206, 0.1);
}

.state-high {
  color: #b45309;
  background: rgba(245, 158, 11, 0.12);
}

.state-limit {
  color: #dc2626;
  background: rgba(239, 68, 68, 0.1);
}

.fill-idle {
  background: #3b82f6;
}

.fill-low {
  background: #22c55e;
}

.fill-medium {
  background: #3182ce;
}

.fill-high {
  background: #f59e0b;
}

.fill-limit {
  background: #ef4444;
}

/* ── 区域标题 ── */
.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}

.section-title {
  font-size: 15px;
  font-weight: 600;
  color: #1e293b;
  display: flex;
  align-items: center;
  gap: 8px;
  .count { font-size: 12px; color: #94a3b8; font-weight: 400; }
}

.section-action {
  font-size: 13px;
  color: #3182ce;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  font-weight: 500;
  &:hover { opacity: 0.7; }
  svg { width: 16px; height: 16px; }
}

/* ── 任务卡片网格 ── */
.task-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
  margin-bottom: 28px;
}

.task-card {
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.05);
  padding: 20px;
  transition: all 0.2s ease;
  cursor: default;
  border: 1px solid transparent;
  &:hover {
    box-shadow: 0 4px 16px rgba(0,0,0,0.08);
    border-color: rgba(49,130,206,0.2);
    transform: translateY(-1px);
  }
}

.task-card-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 12px;
}

.task-card-title-area { flex: 1; }

.task-card-name {
  font-size: 15px;
  font-weight: 600;
  color: #1e293b;
  margin-bottom: 4px;
}

.task-card-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11.5px;
  color: #94a3b8;
}

.task-tag {
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.5px;
  &.cv { background: rgba(59,130,246,0.1); color: #3b82f6; }
  &.vlm { background: rgba(168,85,247,0.1); color: #a855f7; }
  &.dino { background: rgba(20,184,166,0.1); color: #14b8a6; }
}

.task-status {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  white-space: nowrap;
  color: #64748b;
}

.status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  &.running {
    background: #22c55e;
    box-shadow: 0 0 6px rgba(34,197,94,0.4);
    animation: pulse 2s infinite;
  }
  &.warn { background: #fca60b; }
  &.stopped { background: #94a3b8; }
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.task-card-stats {
  display: flex;
  gap: 16px;
  margin-bottom: 14px;
}

.task-stat {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.task-stat-label {
  font-size: 10px;
  color: #94a3b8;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.task-stat-value {
  font-size: 18px;
  font-weight: 700;
  color: #1e293b;
}

.task-card-actions {
  display: flex;
  gap: 8px;
  padding-top: 14px;
  border-top: 1px solid #f1f5f9;
}

.task-btn {
  flex: 1;
  padding: 7px 0;
  border-radius: 6px;
  border: none;
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  font-family: inherit;
  svg { width: 14px; height: 14px; }
}

.task-btn-primary {
  background: #3182ce;
  color: white;
  &:hover { background: #5558e6; }
}

.task-btn-secondary {
  background: #f1f5f9;
  color: #64748b;
  &:hover { background: #e2e8f0; color: #334155; }
}

.task-card-new {
  background: #fff;
  border-radius: 12px;
  border: 2px dashed #e2e8f0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px 20px;
  cursor: pointer;
  transition: all 0.2s ease;
  min-height: 180px;
  &:hover {
    border-color: rgba(49,130,206,0.4);
    background: rgba(49,130,206,0.02);
  }
  svg { width: 36px; height: 36px; color: #94a3b8; margin-bottom: 8px; }
  &:hover svg { color: #3182ce; }
  span { font-size: 13px; color: #94a3b8; font-weight: 500; }
  &:hover span { color: #3182ce; }
}

.warn-text {
  color: #ef4444 !important;
}

.task-card-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  padding-top: 10px;
  border-top: 1px dashed #e2e8f0;
}

.task-channel-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 10.5px;
  color: #475569;
  background: #f1f5f9;
  padding: 3px 8px;
  border-radius: 12px;
  font-weight: 500;
  
  .tag-dot {
    width: 4px;
    height: 4px;
    border-radius: 50%;
    background: #64748b;
  }
}

/* ── 事件流 ── */
.event-feed {
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.05);
  overflow: hidden;
}

.event-feed-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 20px;
  border-bottom: 1px solid #f1f5f9;
}

.event-feed-title {
  font-size: 14px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
  color: #1e293b;
}

.live-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #22c55e;
  animation: pulse 1.5s infinite;
}

.event-list { padding: 0; }

.event-item {
  display: flex;
  align-items: stretch;
  gap: 14px;
  padding: 14px 20px;
  border-bottom: 1px solid #f8fafc;
  transition: background 0.1s;
  cursor: pointer;
  &:hover { background: #fafbfc; }
  &:last-child { border-bottom: none; }
}

.event-thumb {
  width: 96px;
  height: 64px;
  border-radius: 8px;
  overflow: hidden;
  background: #f1f5f9;
  flex-shrink: 0;

  :deep(.el-image) {
    width: 100%;
    height: 100%;
    display: block;
  }
}

.event-thumb-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
  color: #94a3b8;
  background: linear-gradient(135deg, #f8fafc 0%, #eef2f7 100%);
}

.event-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  gap: 8px;
}

.event-title-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.event-title-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.event-level {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex-shrink: 0;
  &.critical { background: #ef4444; }
  &.warning { background: #f59e0b; }
  &.info { background: #3182ce; }
}

.event-content {
  font-size: 13px;
  color: #334155;
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.event-upload-status {
  flex-shrink: 0;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 600;

  &.uploaded {
    color: #16a34a;
    background: rgba(34, 197, 94, 0.1);
  }

  &.pending {
    color: #ef4444;
    background: rgba(239, 68, 68, 0.1);
  }

  &.unknown {
    color: #64748b;
    background: #f1f5f9;
  }
}

.event-meta {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px 14px;
  font-size: 11.5px;
  color: #94a3b8;

  span {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}

.event-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.event-level-badge {
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 600;

  &.critical {
    color: #dc2626;
    background: rgba(239, 68, 68, 0.1);
  }

  &.warning {
    color: #d97706;
    background: rgba(245, 158, 11, 0.12);
  }

  &.info {
    color: #2b6cb0;
    background: rgba(49, 130, 206, 0.1);
  }
}

.event-link-btn {
  border: none;
  background: transparent;
  padding: 0;
  font-size: 12px;
  color: #3182ce;
  cursor: pointer;
  font-family: inherit;

  &:hover {
    color: #2b6cb0;
    text-decoration: underline;
  }
}

.event-empty {
  padding: 32px 20px;
  text-align: center;
  font-size: 13px;
  color: #94a3b8;
}

@media (max-width: 900px) {
  .resource-grid {
    grid-template-columns: repeat(2, minmax(150px, 1fr));
  }

  .event-meta {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 640px) {
  .resource-bar-header {
    align-items: flex-start;
    flex-direction: column;
    gap: 8px;
  }

  .resource-bar-meta {
    flex-wrap: wrap;
    gap: 8px;
  }

  .resource-grid {
    grid-template-columns: 1fr;
  }

  .event-item {
    flex-direction: column;
  }

  .event-thumb {
    width: 100%;
    height: 160px;
  }

  .event-meta {
    grid-template-columns: 1fr;
  }
}
</style>
