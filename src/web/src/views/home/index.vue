<template>
  <div class="overview-page">
    <div class="overview-heading">
      <h1>{{ $t('nav.home') }}</h1>
    </div>
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
            <span v-if="item.isAvailable" class="resource-unit">%</span>
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
      <div class="section-actions">
        <button class="section-action" @click="$router.push('/videoAccess')">
          {{ $t('home.allTasks') }}
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
        </button>
        <button class="new-task-button" @click="$router.push('/gam/countManagement')">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
          {{ $t('home.newSceneTask') }}
        </button>
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
          <div class="task-status" :class="{ running: task.taskState === 1, warn: task.taskState === 2 }">
            <span class="status-dot" :class="{ 'running': task.taskState === 1, 'warn': task.taskState === 2 }"></span>
            <span>{{ task.taskState === 1 ? $t('status.running') : task.taskState === 2 ? $t('status.abnormal') : $t('status.paused') }}</span>
          </div>
        </div>

        <!-- 统计信息 -->
        <div class="task-card-stats">
          <div class="task-stat">
            <div class="task-stat-label">{{ $t('home.todayAlerts') }}</div>
            <div class="task-stat-value" :class="{ 'warn-text': task.todayEvents > 0 }">{{ task.todayEvents || 0 }}</div>
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

      <!-- 空状态保留创建入口；有任务时创建按钮位于区域标题旁 -->
      <button v-if="taskList.length === 0" class="task-card-new" @click="$router.push('/gam/countManagement')">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
        <span>{{ $t('home.newSceneTask') }}</span>
      </button>
    </div>

    <!-- 实时事件流 -->
    <div class="event-feed">
      <div class="event-feed-header">
        <div class="event-feed-title">
          <span class="live-dot"></span>
          {{ $t('home.liveEvents') }}
        </div>
        <button class="section-action" @click="$router.push('/eventQuery/alarmRecord')">
          {{ $t('home.viewAll') }}
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
        </button>
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
      :show-custom-title="false"
    />
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount, getCurrentInstance, computed } from 'vue'
import { useRouter } from 'vue-router'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'
import DetailDialog from '../box/eventQuery/components/detailDialog.vue'
import VideoFrequency from '@/components/videoPlaying265.vue'

const { proxy } = getCurrentInstance()
const router = useRouter()
const DEFAULT_PLATFORM_NAME = computed(() => t('system.defaultPlatformName'))

const normalizePlatformName = (name) => {
  if (!name || name === '智能终端管理平台' || name === '智能终端管理系统' || name === '智能盒子' || name === '边缘智能中枢') {
    return DEFAULT_PLATFORM_NAME.value
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
  const memoryItems = dedupeMemoryItems(resourceList.value.filter(item => MEMORY_KEYS.includes(item?.key)))
  if (!memoryItems.length) return null

  const capacityItems = memoryItems
    .map(item => ({
      used: parseSizeToMiB(item.usedSize),
      unused: parseSizeToMiB(item.unusedSize),
      memoryDomain: item.memoryDomain || item.key
    }))
    .filter(item => item.used !== null && item.unused !== null)

  if (capacityItems.length) {
    const used = capacityItems.reduce((sum, item) => sum + item.used, 0)
    const unused = capacityItems.reduce((sum, item) => sum + item.unused, 0)
    const total = used + unused
    const memoryDomains = new Set(capacityItems.map(item => item.memoryDomain))

    return {
      key: 'mergedMemoryUtilization',
      memoryDomain: memoryDomains.size === 1 ? capacityItems[0].memoryDomain : 'combined',
      usedPercent: total ? Math.round((used / total) * 100) : 0,
      usedSize: formatCapacity(used),
      unusedSize: formatCapacity(unused)
    }
  }

  const percentSum = memoryItems.reduce((sum, item) => sum + toPercentNumber(item.usedPercent), 0)
  return {
    key: 'mergedMemoryUtilization',
    memoryDomain: memoryItems.length === 1 ? (memoryItems[0].memoryDomain || memoryItems[0].key) : 'combined',
    usedPercent: Math.round(percentSum / memoryItems.length),
    usedSize: t('resource.usedLabel'),
    unusedSize: t('resource.unusedLabel')
  }
}

const dedupeMemoryItems = (items) => {
  const seenDomains = new Set()
  return items.filter(item => {
    if (!item?.memoryDomain) return true
    if (seenDomains.has(item.memoryDomain)) return false
    seenDomains.add(item.memoryDomain)
    return true
  })
}

const normalizeResourceItem = (item) => {
  const isAvailable = item.available !== 0
  if (!isAvailable) {
    return {
      ...item,
      name: getResourceName(item),
      isAvailable: false,
      usedPercent: '--',
      safePercent: 0,
      level: 'unavailable',
      statusText: t('resource.unavailable'),
      usedLabel: t('resource.unavailable'),
      unusedLabel: ''
    }
  }

  const usedPercent = toPercentNumber(item.usedPercent)
  const levelInfo = getResourceLevelInfo(item.key, usedPercent)

  return {
    ...item,
    name: getResourceName(item),
    isAvailable: true,
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
    mergedMemoryUtilization: item.memoryDomain === 'system'
      ? t('resource.systemMemoryUsage')
      : t('resource.memoryUsage'),
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

const parseSizeToMiB = (value) => {
  if (!value || typeof value !== 'string') return null
  const match = value.trim().match(/^([\d.]+)\s*(GiB|MiB|KiB|GB|MB|KB|B)$/i)
  if (!match) return null

  const number = Number(match[1])
  const unit = match[2].toUpperCase()
  const rateMap = {
    GIB: 1024,
    GB: 1024,
    MIB: 1,
    MB: 1,
    KIB: 1 / 1024,
    KB: 1 / 1024,
    B: 1 / 1024 / 1024
  }

  return Number.isNaN(number) ? null : number * rateMap[unit]
}

const formatCapacity = (valueInMiB) => {
  if (valueInMiB >= 1024) return `${(valueInMiB / 1024).toFixed(2)} GiB`
  return `${valueInMiB.toFixed(2)} MiB`
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
  const channelId = Array.isArray(task.channels)
    ? task.channels.find((channel) => channel?.channelId)?.channelId || ''
    : ''

  router.push({
    path: '/bigScreen/warnningScreen',
    query: {
      channelId,
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
  color: var(--text-primary);
}

.overview-heading {
  margin-bottom: 20px;
}

.overview-heading h1 {
  margin: 0;
  font-size: 28px;
  font-weight: 650;
  line-height: 1.3;
  letter-spacing: -.6px;
}

.resource-bar {
  padding: 16px 0 19px;
  margin-bottom: 24px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  box-shadow: var(--shadow-sm);
}

.resource-bar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
  padding: 0 24px 16px;
}

.resource-bar-title {
  font-size: 14px;
  font-weight: 600;
  line-height: 20px;
}

.resource-bar-subtitle {
  margin-top: 2px;
  font-size: 12px;
  color: var(--text-secondary);
}

.resource-bar-meta {
  display: flex;
  align-items: center;
  gap: 14px;
  flex-wrap: wrap;
  font-size: 11px;
  color: var(--text-secondary);
}

.resource-run-status {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: var(--success-color);
}

.resource-status-dot {
  width: 5px;
  height: 5px;
  background: var(--success-color);
  border-radius: 50%;
}

.resource-grid {
  display: grid;
  grid-template-columns: repeat(5, minmax(0, 1fr));
}

.resource-item {
  min-width: 0;
  padding: 0 24px;
  border-right: 1px solid var(--border-color);
}

.resource-item:last-child {
  border-right: 0;
}

.resource-item-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 6px;
}

.resource-label {
  font-size: 12px;
  color: var(--text-secondary);
  overflow-wrap: anywhere;
}

.resource-state {
  padding: 1px 5px;
  border-radius: 3px;
  font-size: 10px;
  line-height: 16px;
  font-weight: 500;
}

.resource-value-row {
  display: flex;
  align-items: baseline;
  margin-top: 7px;
}

.resource-value {
  font-size: 34px;
  line-height: 1.2;
  font-weight: 500;
  letter-spacing: -1px;
  font-variant-numeric: tabular-nums;
}

.resource-unit {
  margin-left: 3px;
  font-size: 15px;
  color: var(--text-secondary);
}

.resource-progress {
  height: 3px;
  margin-top: 13px;
  background: var(--theme-border-light, #e6eaf0);
  border-radius: 4px;
  overflow: hidden;
}

.resource-progress-fill {
  height: 100%;
  border-radius: inherit;
  transition: width .3s ease;
}

.resource-detail {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 4px;
  margin-top: 7px;
  font-size: 10px;
  color: var(--text-secondary);
  line-height: 1.5;
}

.resource-detail span {
  overflow-wrap: anywhere;
}

.state-idle, .state-medium {
  color: var(--theme-accent, #595390);
  background: var(--theme-accent-soft, #f0effb);
}

.state-unavailable {
  color: var(--text-secondary);
  background: var(--theme-surface-soft, #eef1f5);
}

.state-low {
  color: var(--success-color);
  background: var(--theme-success-soft, #edf6ef);
}

.state-high {
  color: var(--warning-color);
  background: var(--theme-warning-soft, #fcf4e7);
}

.state-limit {
  color: var(--danger-color);
  background: var(--theme-danger-soft, #fff0f0);
}

.fill-idle, .fill-low, .fill-medium {
  background: var(--theme-accent, #766de0);
}

.fill-unavailable {
  background: var(--theme-text-muted, #7d8796);
}

.fill-high {
  background: var(--warning-color);
}

.fill-limit {
  background: var(--danger-color);
}

.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 12px;
}

.section-title {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 9px;
  font-size: 16px;
  font-weight: 630;
}

.section-title .count {
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 500;
  color: var(--theme-text-secondary, #526074);
  background: var(--theme-surface-soft, #e9edf2);
}

.section-actions {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 16px;
}

.section-action {
  border: 0;
  background: none;
  padding: 4px 0;
  font-family: inherit;
  font-size: 12px;
  color: var(--theme-text-secondary, #4f596b);
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.section-action:hover {
  color: var(--primary-color);
}

.section-action svg {
  width: 14px;
  height: 14px;
}

.new-task-button {
  border: 1px solid var(--theme-accent-button, var(--primary-color));
  background: var(--theme-accent-button, var(--primary-color));
  color: white;
  border-radius: 5px;
  padding: 7px 12px;
  font-size: 12px;
  line-height: 1.5;
  font-family: inherit;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.new-task-button svg {
  width: 14px;
  height: 14px;
}

.new-task-button:hover {
  background: var(--theme-accent-button-hover, var(--primary-dark));
  border-color: var(--theme-accent-button-hover, var(--primary-dark));
}

.section-action:focus-visible, .new-task-button:focus-visible, .task-btn:focus-visible, .task-card-new:focus-visible, .event-link-btn:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 3px;
}

.task-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(min(100%, max(300px, calc((100% - 48px) / 4))), 1fr));
  gap: 16px;
  align-items: stretch;
  margin-bottom: 25px;
}

.task-card {
  display: flex;
  flex-direction: column;
  min-width: 0;
  min-height: 203px;
  padding: 17px 18px 0;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  box-shadow: var(--shadow-sm);
  transition: border-color .16s;
}

.task-card:hover {
  border-color: var(--theme-border, #bfc7d3);
}

.task-card-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 12px;
}

.task-card-title-area {
  min-width: 0;
  flex: 1;
}

.task-card-name {
  font-size: 14px;
  font-weight: 630;
  line-height: 1.5;
  overflow-wrap: anywhere;
}

.task-card-meta {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 5px;
}

.task-tag {
  padding: 0 5px;
  border: 1px solid var(--theme-border, #e3dfec);
  border-radius: 3px;
  font-size: 9px;
  line-height: 15px;
  font-weight: 600;
  letter-spacing: .4px;
  color: var(--theme-accent, #66557d);
  background: var(--theme-accent-soft, #f5f3f9);
}

.task-status {
  display: flex;
  align-items: center;
  gap: 5px;
  padding-top: 3px;
  font-size: 11px;
  white-space: nowrap;
  color: var(--text-secondary);
}

.task-status.running {
  color: var(--success-color);
}

.task-status.warn {
  color: var(--warning-color);
}

.status-dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--theme-text-muted, #7d8796);
  flex-shrink: 0;
}

.status-dot.running {
  background: var(--success-color);
}

.status-dot.warn {
  background: var(--warning-color);
}

.task-card-stats {
  display: flex;
  gap: 30px;
  margin-bottom: 12px;
}

.task-stat {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.task-stat-label {
  font-size: 11px;
  color: var(--text-secondary);
}

.task-stat-value {
  font-size: 23px;
  font-weight: 550;
  line-height: 1.25;
  letter-spacing: -.5px;
  font-variant-numeric: tabular-nums;
}

.warn-text {
  color: var(--warning-color);
}

.task-card-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 5px;
  margin-bottom: 13px;
}

.task-channel-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  max-width: 100%;
  padding: 1px 6px;
  border-radius: 3px;
  font-size: 11px;
  line-height: 1.7;
  color: var(--theme-text-secondary, #526074);
  background: var(--theme-surface-soft, #eef1f5);
  overflow-wrap: anywhere;
}

.tag-dot {
  flex-shrink: 0;
  width: 4px;
  height: 4px;
  border-radius: 50%;
  background: var(--theme-text-muted, #737f91);
}

.task-card-actions {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-top: auto;
  min-height: 40px;
  border-top: 1px solid var(--border-light);
}

.task-btn {
  border: 0;
  padding: 8px 0;
  background: none;
  font-size: 12px;
  font-weight: 500;
  font-family: inherit;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.task-btn svg {
  width: 14px;
  height: 14px;
}

.task-btn-primary {
  color: var(--primary-color);
}

.task-btn-secondary {
  color: var(--theme-text-secondary, #4f596b);
}

.task-btn:hover {
  text-decoration: underline;
  text-underline-offset: 3px;
}

.task-card-new {
  grid-column: 1 / -1;
  background: var(--bg-white);
  border: 1px dashed var(--border-color);
  border-radius: 8px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 32px;
  min-height: 172px;
  font: inherit;
  font-size: 14px;
  color: var(--text-secondary);
  cursor: pointer;
}

.task-card-new:hover {
  border-color: var(--primary-color);
  color: var(--primary-color);
}

.task-card-new svg {
  width: 24px;
  height: 24px;
}

.event-feed {
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  box-shadow: var(--shadow-sm);
  overflow: hidden;
}

.event-feed-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 14px 18px;
  border-bottom: 1px solid var(--border-light);
}

.event-feed-title {
  font-size: 14px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 7px;
}

.live-dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--success-color);
}

.event-list {
  padding: 0;
}

.event-item {
  display: flex;
  align-items: stretch;
  gap: 14px;
  padding: 14px 18px;
  border-bottom: 1px solid var(--border-light);
  cursor: pointer;
}

.event-item:hover {
  background: var(--theme-surface-soft, #f8f9fb);
}

.event-item:last-child {
  border-bottom: none;
}

.event-thumb {
  width: 96px;
  height: 64px;
  border-radius: 5px;
  overflow: hidden;
  background: var(--theme-surface-soft, #eef1f5);
  flex-shrink: 0;
}

.event-thumb :deep(.el-image) {
  width: 100%;
  height: 100%;
  display: block;
}

.event-thumb-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
  color: var(--text-secondary);
  background: var(--theme-surface-soft, #eef1f5);
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
  width: 5px;
  height: 5px;
  border-radius: 50%;
  flex-shrink: 0;
}

.event-level.critical {
  background: var(--danger-color);
}

.event-level.warning {
  background: var(--warning-color);
}

.event-level.info {
  background: var(--primary-color);
}

.event-content {
  font-size: 13px;
  font-weight: 600;
  overflow-wrap: anywhere;
}

.event-upload-status {
  flex-shrink: 0;
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 11px;
}

.event-upload-status.uploaded {
  color: var(--success-color);
  background: var(--theme-success-soft, #edf6ef);
}

.event-upload-status.pending {
  color: var(--danger-color);
  background: var(--theme-danger-soft, #fff0f0);
}

.event-upload-status.unknown {
  color: var(--text-secondary);
  background: var(--theme-surface-soft, #eef1f5);
}

.event-meta {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px 14px;
  font-size: 11px;
  color: var(--text-secondary);
}

.event-meta span {
  overflow-wrap: anywhere;
}

.event-actions {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
}

.event-level-badge {
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 11px;
}

.event-level-badge.critical {
  color: var(--danger-color);
  background: var(--theme-danger-soft, #fff0f0);
}

.event-level-badge.warning {
  color: var(--warning-color);
  background: var(--theme-warning-soft, #fcf4e7);
}

.event-level-badge.info {
  color: var(--primary-color);
  background: var(--theme-accent-soft, #f0effb);
}

.event-link-btn {
  border: 0;
  background: none;
  padding: 0;
  font-size: 12px;
  color: var(--primary-color);
  cursor: pointer;
  font-family: inherit;
}

.event-link-btn:hover {
  text-decoration: underline;
  text-underline-offset: 3px;
}

.event-empty {
  padding: 24px 18px;
  text-align: center;
  font-size: 12px;
  color: var(--text-secondary);
}

@media (max-width: 1130px) {
  .resource-item {
    padding: 0 16px;
  }
}

@media (max-width: 900px) {
  .resource-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
    row-gap: 20px;
  }
  .resource-item:nth-child(2n) {
    border-right: 0;
  }
  .resource-item:last-child {
    grid-column: 1 / -1;
  }
  .event-meta {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 640px) {
  .overview-heading h1 {
    font-size: 24px;
  }
  .resource-bar-header {
    align-items: flex-start;
    padding: 0 16px 16px;
  }
  .resource-value {
    font-size: 30px;
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
