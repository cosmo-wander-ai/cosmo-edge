<template>
  <div ref="contentBox" class="main-body" id="screen-body">
    <!-- 告警弹窗 -->
    <transition name="alert-slide">
      <div v-if="showAlert" class="alert-popup">
        <div class="alert-top-title">{{ resolveResourceAlgorithmName(currentSocketData) }}</div>
        
        <div class="alert-body">
          <div class="alert-left">
            <div v-if="checkPropertyKey(currentSocketData, 'recognition') && currentSocketData.property.recognition.matchDegree != '-1'" class="warn-two-body">
              <div class="event-image-container">
                <div class="event-image-item">
                  <el-image :src="currentSocketData.detectedPicture" fit="contain">
                    <template #error>
                      <div class="image-slot"><img src="@/assets/error-image.png"></div>
                    </template>
                  </el-image>
                  <span class="image-label">{{ t('event.captureImage') }}</span>
                </div>
                <div class="event-image-item">
                  <el-image :src="currentSocketData.property.recognition.LibImage" fit="contain">
                    <template #error>
                      <div class="image-slot"><img src="@/assets/error-image.png"></div>
                    </template>
                  </el-image>
                  <span class="image-label">{{ t('event.baseImage') }}</span>
                </div>
              </div>

              <div class="match-info">
                <span class="match-name">{{ currentSocketData.property.recognition.matchName }}</span>
                <span class="match-score">{{ formatSimilarity(currentSocketData.property.recognition.matchDegree) }}</span>
              </div>
            </div>
            
            <div v-else class="warn-one-body">
              <el-image :src="currentSocketData.fullPicture" fit="contain">
                <template #error>
                  <div class="image-slot"><img src="@/assets/error-image.png"></div>
                </template>
              </el-image>
            </div>
          </div>

          <div class="alert-right">
            <div class="info-item">
              <div class="info-label">{{ t('event.alarmLocation') }}</div>
              <div class="info-value">{{ currentSocketData.channelName }}</div>
            </div>
            <div class="info-item">
              <div class="info-label">{{ t('event.alarmTime') }}</div>
              <div class="info-value">{{ dateFormat(currentSocketData.timestamp) }}</div>
            </div>
          </div>
        </div>
      </div>
    </transition>
    <div class="header">
      <div class="title">{{ t('event.aiVideoAnalysis') }}</div>
      <div class="screen-control">
        <button type="button" class="screen-btn" :class="{ 'is-active': screenType === 1 }" :aria-pressed="screenType === 1" @click="switchScreen(1)">{{ t('event.oneScreen') }}</button>
        <button type="button" class="screen-btn" :class="{ 'is-active': screenType === 4 }" :aria-pressed="screenType === 4" @click="switchScreen(4)">{{ t('event.fourScreens') }}</button>
      </div>
      <div class="time">{{ currentTime }}</div>
      <div class="right-tools">
        <ThemeSwitcher />
        <button type="button" class="header-btn" @click="handleSettingClick"><el-icon><Setting /></el-icon>{{ t('action.settings') }}</button>
        <button type="button" class="header-btn" @click="toggleFullScreen"><el-icon><FullScreen /></el-icon>{{ isFullScreen ? t('event.exitFullscreen') : t('event.fullscreen') }}</button>
        <button type="button" class="header-btn" @click="exitFullScreen"><el-icon><Back /></el-icon>{{ t('action.goBack') }}</button>
      </div>
    </div>

    <div class="content">
      <div class="left-panel">
        <!-- 摄像机列表 -->
        <div ref="selectAreaEl" class="select-area" :class="{ 'expanded': cameraDrawerVisible }">
          <div v-if="cameraDrawerVisible" class="camera-list-wrap">
            <div class="title">
              <span>{{ t('event.channelList') }}</span>
              <button type="button" class="panel-icon" :title="t('action.refresh')" :aria-label="t('action.refresh')" @click="initCameraList"><el-icon><Refresh /></el-icon></button>
            </div>
            <div class="exseach">
              <el-input size="small" :placeholder="t('event.searchKeyword')" maxlength="32" v-model="cameraFilterText">
                <template #suffix>
                  <el-icon><Search /></el-icon>
                </template>
              </el-input>
              <div class="tree-body">
                <el-tree id="onboarding-camera-tree" ref="tree" class="filter-tree" :data="camearList" :highlight-current="true" node-key="id" default-expand-all :filter-node-method="filterNode">
                  <template #default="{ node, data }">
                    <div class="custom-tree-node" :class="{'padding-left-18': nodeLabel(data) !== t('common.all')}" @dblclick="handleCameraNodeClick(data)">
                      <div v-if="data.channelType == 0 && data.status == 0" class="stnode">
                        <img src="@/assets/close-circle.png" />
                        <span>{{ node.label }}</span>
                      </div>
                      <div v-else-if="nodeLabel(data) !== t('common.all')" class="stnode">
                        <img src="@/assets/check-circle.png" />
                        <span>{{ nodeLabel(data) }}</span>
                      </div>
                      <div v-else>
                        <span>{{ nodeLabel(data) }}</span>
                      </div>
                    </div>
                  </template>
                </el-tree>
              </div>
            </div>
          </div>
          <button id="onboarding-camera-toggle" type="button" class="select-area-tools panel-icon" :title="t(cameraDrawerVisible ? 'action.collapse' : 'event.channelList')" :aria-label="t(cameraDrawerVisible ? 'action.collapse' : 'event.channelList')" :aria-expanded="cameraDrawerVisible" @click.stop="toggleCameraDrawer">
            <el-icon><ArrowLeft v-if="cameraDrawerVisible" /><VideoCamera v-else /></el-icon>
          </button>
        </div>

        <!-- 摄像机播放窗口 -->
        <div class="video-grid">
          <div class="video-wall" :class="'grid-' + screenType">
            <!-- 当screenType为1时，只渲染当前选中的窗口 -->
            <div v-if="screenType === 1" class="video-item video-active video-one">
              <flv v-if="playedCameraList[currentSelectedIndex].id" :channelId="playedCameraList[currentSelectedIndex].id" :runAlgorithmId="playedCameraList[currentSelectedIndex].runAlgorithmId" :taskList="playedCameraList[currentSelectedIndex].taskList" :cameraName="playedCameraList[currentSelectedIndex].name" :index="currentSelectedIndex" :isFullScreen="isVideoFullScreen" @runAlgorithmIdChange="handleRunAlgorithmIdChange" @stop="handleCameraClose" @fullScreen="handleVideoFullScreen(currentSelectedIndex)"></flv>
              <div class="no-camera" v-else>
                <img src="@/assets/big_screen_no_camera.png" />
                <span>{{ t('event.noVideoSignal') }}</span>
              </div>
            </div>
            <!-- 当screenType为4时，渲染所有四个窗口 -->
            <div v-else v-for="(n,cameraIndex) in screenType" :key="n" class="video-item" :class="{'video-active': cameraIndex === currentSelectedIndex, 'video-one': (cameraIndex === currentSelectedIndex) && (isVideoFullScreen !== null)}" @click="currentSelectedIndex = cameraIndex">
              <flv v-if="playedCameraList[cameraIndex].id" :channelId="playedCameraList[cameraIndex].id" :runAlgorithmId="playedCameraList[cameraIndex].runAlgorithmId" :taskList="playedCameraList[cameraIndex].taskList" :cameraName="playedCameraList[cameraIndex].name" :index="cameraIndex" :isFullScreen="(cameraIndex === currentSelectedIndex) ? isVideoFullScreen : null" @runAlgorithmIdChange="handleRunAlgorithmIdChange" @stop="handleCameraClose" @fullScreen="handleVideoFullScreen(cameraIndex)"></flv>
              <div class="no-camera" v-else>
                <img src="@/assets/big_screen_no_camera.png" />
                <span>{{ t('event.noVideoSignal') }}</span>
              </div>
            </div>
          </div>
        </div>

      </div>

      <div class="right-panel" :class="{ 'expanded': recordDrawerVisible }">
        <button type="button" class="record-toggle panel-icon" :title="t(recordDrawerVisible ? 'action.collapse' : 'event.eventRecords')" :aria-label="t(recordDrawerVisible ? 'action.collapse' : 'event.eventRecords')" :aria-expanded="recordDrawerVisible" @click="recordDrawerVisible = !recordDrawerVisible">
          <el-icon><ArrowRight v-if="recordDrawerVisible" /><Bell v-else /></el-icon>
        </button>
        <div v-show="recordDrawerVisible" class="record-body">
          <div class="record-header">
            <span>{{ t('event.eventRecords') }}
              <button type="button" class="panel-icon" :title="t('action.refresh')" :aria-label="t('action.refresh')" @click="queryWarnRecord"><el-icon><Refresh /></el-icon></button>
            </span>
            <span class="today-count">{{ t('event.todayAlarmCount', { n: alarmCount }) }}</span>
          </div>
          <div class="record-search">
            <TreeSelect class="tree-select" :treeData="algorithmInfoList" v-model="selectedAlgorithmList" />
            <el-button size="small" :title="t('action.search')" :aria-label="t('action.search')" @click="searchRecord">
              <el-icon><Search /></el-icon>
            </el-button>
          </div>
          <div class="record-content">
            <template v-if="eventList.length > 0">
              <div v-for="(event, index) in eventList" :key="index" class="event-item" @click="eventDetail(event)">
                <div v-if="checkPropertyKey(event,'recognition') && event.property.recognition.matchDegree != '-1'" class="event-image2">
                  <div class="event-image2-body">
                    <el-image :src="event.detectedPicture" fit="contain">
                      <template #error>
                        <div class="image-slot">
                          <img src="@/assets/error-image.png">
                        </div>
                      </template>
                    </el-image>
                    <span>{{ t('event.captureImage') }}</span>
                  </div>

                  <div class="event-image2-body">
                    <el-image :src="event.property.recognition.LibImage" fit="contain">
                      <template #error>
                        <div class="image-slot">
                          <img src="@/assets/error-image.png">
                        </div>
                      </template>
                    </el-image>
                    <span>{{ t('event.baseImage') }}</span>
                  </div>

                  <div class="match-div" v-if="event.property.recognition.matchName && event.property.recognition.matchDegree != '-1'">
                    {{ event.property.recognition.matchName }}
                    {{ formatSimilarity(event.property.recognition.matchDegree) }}
                  </div>
                </div>
                <div v-else class="event-image">
                  <el-image :src="event.fullPicture" fit="contain">
                    <template #error>
                      <div class="image-slot">
                        <img src="@/assets/error-image.png">
                      </div>
                    </template>
                  </el-image>
                </div>
                <div class="event-info">
                  <div>{{ t('event.eventType') }}{{ localeColon }}{{ resolveResourceAlgorithmName(event) }}</div>
                  <div>{{ t('event.channel') }}{{ localeColon }}{{ event.channelName }}</div>
                  <div>{{ t('event.alarmTime') }}{{ localeColon }}{{ dateFormat(event.timestamp) }}</div>
                </div>
              </div>
            </template>
            <template v-else>
              <div class="empty-body">
                <el-empty :image-size="80"></el-empty>
              </div>
            </template>
          </div>
        </div>
      </div>
    </div>

    <el-dialog class="tip-dialog" :title="t('action.settings')" v-model="settingDialogVisible" center width="500px">
      <div class="tip-content">
        <el-form :model="settingForm" :rules="settingRules" ref="settingFormRef" label-position="right" label-width="180px">
          <el-form-item>
            <template #label>
              <span style="color: var(--text-primary); font-weight: 500;">{{ t('event.alarmPopup') }}</span>
            </template>
            <el-switch size="small" v-model="settingForm.popUpSwitch" :active-value="1" :inactive-value="0"></el-switch>
          </el-form-item>
          <el-form-item>
            <template #label>
              <span style="color: var(--text-primary); font-weight: 500;">{{ t('event.alarmSound') }}</span>
            </template>
            <el-switch size="small" v-model="settingForm.audioPlay" :active-value="1" :inactive-value="0"></el-switch>
          </el-form-item>
          <el-form-item :label="t('event.popupDurationSeconds')" prop="popUpDuration">
            <template #label>
              <span style="color: var(--text-primary); font-weight: 500; display:inline-block;">
                {{ t('event.popupDurationSeconds') }}
                <el-tooltip effect="dark" :content="t('event.popupDurationTip')" placement="top">
                  <i class='el-icon-question' />
                </el-tooltip>
              </span>
            </template>
            <el-input v-model="settingForm.popUpDuration" class="form-content" @input="(e)=>handleInput(e, 'popUpDuration')" size="small"></el-input>
          </el-form-item>
        </el-form>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button type="primary" @click="saveSettingClick" size="small">{{ t('action.save') }}</el-button>
          <el-button @click="settingDialogVisible = false" size="small">{{ t('action.cancel') }}</el-button>
        </div>
      </template>
    </el-dialog>

    <detail-dialog v-model:visible="detailDialogVisible" :detailData="detailData" />
    <capture-dialog v-model:visible="captureDialogVisible" :detailData="detailData" />
    <audio ref="audio" :src="beepOgg"></audio>
  </div>
</template>
<script setup>
import { ref, watch, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import beepOgg from '@/assets/beep.ogg'
import { Search, Setting, FullScreen, Back, Refresh, ArrowLeft, ArrowRight, VideoCamera, Bell } from '@element-plus/icons-vue'
import flv from '../components/flvVideo.vue'
import ThemeSwitcher from '@/components/ThemeSwitcher.vue'
import DetailDialog from '../components/detailDialog.vue'
import CaptureDialog from '../components/captureDialog.vue'
import moment from 'moment'
import TreeSelect from '../components/TreeSelect.vue'
import _ from 'lodash'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'
import { formatSimilarity } from '@/utils/format'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

// Refs
const contentBox = ref(null)
const tree = ref(null)
const settingFormRef = ref(null)
const audio = ref(null)
const selectAreaEl = ref(null)

// Data
const currentTime = ref('')
const screenType = ref(4)
const alarmCount = ref(0)
const eventList = ref([])
const rawAlgorithmList = ref([])
const cameraDrawerVisible = ref(false)
const recordDrawerVisible = ref(true)
const settingDialogVisible = ref(false)
const cameraFilterText = ref('')
const camearList = ref([])
const playedCameraList = ref(Array(4).fill({
  id: '',
  name: '',
  taskList: [],
  runAlgorithmId: ''
}))
const currentSelectedIndex = ref(0)
const isVideoFullScreen = ref(null)
const detailDialogVisible = ref(false)
const captureDialogVisible = ref(false)
const showAlert = ref(false)
const alertTimer = ref(null)
const alertEnterTimer = ref(null)
const alertExitTimer = ref(null)
const socket = ref(null)
const socketTimer = ref(null)
const reconnectTimer = ref(null)
const timeInterval = ref(null)
const isDestroyed = ref(false)
const settingForm = ref({
  popUpSwitch: 1,
  audioPlay: 1,
  popUpDuration: 2
})
const realSettingForm = ref({
  popUpSwitch: 1,
  audioPlay: 1,
  popUpDuration: 2
})
const isFullScreen = ref(false)
const algorithmInfoList = ref([])
const algorithmCategoryList = [
  { labelI18nKey: 'event.categoryFaceBody', value: '1' },
  { labelI18nKey: 'event.categoryDetection', value: '2' },
  { labelI18nKey: 'event.categoryDetection', value: '3' },
  { labelI18nKey: 'event.categoryCounting', value: '8' },
  { labelI18nKey: 'event.categoryCounting', value: '9' },
  { labelI18nKey: 'event.categoryVehicle', value: '10' },
  { labelI18nKey: 'event.categoryCounting', value: '11' }
]
const selectedAlgorithmList = ref([])
const detailData = ref({})
const currentSocketData = ref({})
const audioUnlocked = ref(false)
const ALERT_ENTER_DELAY_MS = 50
const ALERT_EXIT_DELAY_MS = 500
let alertAnimationFrame = null

const clearAlertTimers = () => {
  if (alertAnimationFrame !== null) {
    cancelAnimationFrame(alertAnimationFrame)
    alertAnimationFrame = null
  }
  const timers = [alertTimer, alertEnterTimer, alertExitTimer]
  timers.forEach((timer) => {
    if (timer.value) {
      clearTimeout(timer.value)
      timer.value = null
    }
  })
}

const scheduleAlertDismiss = () => {
  alertTimer.value = setTimeout(() => {
    alertTimer.value = null
    alertExitTimer.value = setTimeout(() => {
      alertExitTimer.value = null
      showAlert.value = false
    }, ALERT_EXIT_DELAY_MS)
  }, realSettingForm.value.popUpDuration * 1000)
}

// Validation
const validatePopUpDuration = (rule, value, callback) => {
  if (Number(value) < 1 || Number(value) > 30) {
    callback(new Error(t('event.popupDurationRangeError')))
  } else {
    callback()
  }
}

const settingRules = {
  popUpDuration: [
    { required: true, message: t('event.enterPopupDuration'), trigger: 'blur' },
    {
      validator: validatePopUpDuration,
      message: t('event.popupDurationRangeError'),
      trigger: 'blur'
    }
  ]
}

// Watchers
watch(cameraFilterText, (val) => {
  tree.value.filter(val)
})

watch(playedCameraList, (val) => {
  console.log(val, '=========playedCameraList========')
  localStorage.setItem('playedCameraList', JSON.stringify(val))
}, { deep: true })

// Methods
const dateFormat = (value) => {
  if (!value) return ''
  return moment(Number(value)).format('YYYY-MM-DD HH:mm:ss')
}

const nodeLabel = (data) => {
  return data?.labelI18nKey ? t(data.labelI18nKey) : data?.label
}

const handleFullScreenChange = () => {
  isFullScreen.value =
    !!document.fullscreenElement ||
    !!document.webkitFullscreenElement ||
    !!document.mozFullScreenElement
}

const handleFullScreen = async () => {
  const dom = document.body
  try {
    if (dom.requestFullscreen) {
      await dom.requestFullscreen()
    } else if (dom.mozRequestFullScreen) {
      await dom.mozRequestFullScreen()
    } else if (dom.webkitRequestFullScreen) {
      await dom.webkitRequestFullScreen()
    } else {
      return false
    }
    return true
  } catch {
    return false
  }
}

const handleExitFullScreen = async () => {
  // 检查是否处于全屏状态
  if (!document.fullscreenElement && 
      !document.webkitFullscreenElement && 
      !document.mozFullScreenElement) {
    return
  }

  const dom = document
  try {
    if (dom.exitFullscreen) {
      await dom.exitFullscreen()
    } else if (dom.mozCancelFullScreen) {
      await dom.mozCancelFullScreen()
    } else if (dom.webkitCancelFullScreen) {
      await dom.webkitCancelFullScreen()
    }
    return true
  } catch {
    return false
  }
}

const queryPopUpParam = () => {
  $API.queryPopUpParam({}).then((res) => {
    const { resData } = res
    settingForm.value = {
      popUpSwitch: resData.popUpSwitch,
      audioPlay: resData.audioPlay,
      popUpDuration: resData.popUpDuration
    }
    realSettingForm.value = { ...settingForm.value }
    initsocketUrl()
  })
}

const toggleFullScreen = async () => {
  if (!isFullScreen.value) {
    await handleFullScreen()
  } else {
    await handleExitFullScreen()
  }
}

const exitFullScreen = async () => {
  await handleExitFullScreen()
  proxy.$router.back()
}

const toggleCameraDrawer = () => {
  cameraDrawerVisible.value = !cameraDrawerVisible.value
}

// 点击通道列表以外的区域时自动收起
const handleClickOutside = (e) => {
  if (cameraDrawerVisible.value && selectAreaEl.value && !selectAreaEl.value.contains(e.target)) {
    cameraDrawerVisible.value = false
  }
}

const updateTime = () => {
  const now = new Date()
  const year = now.getFullYear()
  const month = (now.getMonth() + 1).toString().padStart(2, '0')
  const day = now.getDate().toString().padStart(2, '0')
  const hours = now.getHours().toString().padStart(2, '0')
  const minutes = now.getMinutes().toString().padStart(2, '0')
  const seconds = now.getSeconds().toString().padStart(2, '0')
  currentTime.value = `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`
}

const switchScreen = (type) => {
  const prevSelectedIndex = currentSelectedIndex.value
  screenType.value = type
  currentSelectedIndex.value = prevSelectedIndex
  localStorage.setItem(
    'screenStatus',
    JSON.stringify({
      screenType: screenType.value,
      currentSelectedIndex: currentSelectedIndex.value
    })
  )
  isVideoFullScreen.value = null
}

const handleCameraClose = (index) => {
  playedCameraList.value[index] = {
    id: '',
    name: '',
    taskList: [],
    runAlgorithmId: ''
  }
  isVideoFullScreen.value = null
}

const handleVideoFullScreen = (index) => {
  currentSelectedIndex.value = index
  if (isVideoFullScreen.value === null) {
    isVideoFullScreen.value = index
  } else {
    isVideoFullScreen.value = null
  }
}

const getDefaultOverlayAlgorithmId = (taskList) => {
  const enabledTasks = Array.isArray(taskList)
    ? taskList.filter((task) => task.enableStatus == 1)
    : []
  return enabledTasks.length === 1
    ? String(enabledTasks[0].algorithmId || '')
    : ''
}

const handleCameraNodeClick = (node) => {
  if (node.labelI18nKey === 'common.all' || node.label === '全部') return
  if (node.channelType == 0 && node.status == 0)
    return proxy.$message.error(t('event.cameraOffline'))
  cameraDrawerVisible.value = false
  playedCameraList.value[currentSelectedIndex.value] = {
    id: node.id,
    name: node.label,
    taskList: node.taskList,
    // A single enabled task is unambiguous: request its OSD stream directly
    // instead of opening a raw player only to tear it down immediately.
    runAlgorithmId: getDefaultOverlayAlgorithmId(node.taskList)
  }

  if (screenType.value == 1) return
  if (currentSelectedIndex.value >= 3) {
    const zeroIndex = playedCameraList.value.findIndex(
      (item) => item.id == ''
    )
    if (zeroIndex !== -1) {
      currentSelectedIndex.value = zeroIndex
    }
  } else {
    currentSelectedIndex.value++
  }
}

const handleSettingClick = () => {
  unlockAudio()
  clearAlertTimers()
  showAlert.value = false
  settingForm.value = { ...realSettingForm.value }
  settingFormRef.value && settingFormRef.value.clearValidate()
  settingDialogVisible.value = true
}

const handleInput = (val, key) => {
  settingForm.value[key] = val.replace(/[^\d]/g, '')
}

const filterNode = (value, data) => {
  if (!value) return true
  return data.label.indexOf(value) !== -1
}

const initCameraList = () => {
  const screenStatusObj = localStorage.getItem('screenStatus')
  const screenStatus = JSON.parse(screenStatusObj)
  if (screenStatus) {
    screenType.value = screenStatus.screenType
    currentSelectedIndex.value = screenStatus?.currentSelectedIndex || 0
  }

  console.log(
    screenType.value,
    '====this.screenType====',
    currentSelectedIndex.value
  )

  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  $API.getChannelList(params).then((res) => {
    const { resData } = res
    let childCameras = []
    childCameras = resData.rows.map((item) => {
      const {
        videoChannelId,
        channelName,
        channelStatus,
        channelType,
        taskList
      } = item
      return {
        id: videoChannelId,
        label: channelName,
        status: channelStatus,
        channelType: channelType,
        taskList: taskList
      }
    })
    camearList.value = [
      {
        id: -1,
        labelI18nKey: 'common.all',
        children: childCameras
      }
    ]
    console.log(camearList.value, '===========')
    initPlayedCamera(childCameras)
  })
}

const initPlayedCamera = (childCameras) => {
  const playedCameraListStr = localStorage.getItem('playedCameraList')
  if (playedCameraListStr) {
    const localPlayedCameraList = JSON.parse(playedCameraListStr)
    localPlayedCameraList.forEach((item, index) => {
      const resultCamear = _.find(childCameras, { id: item.id })
      if (!resultCamear) {
        localPlayedCameraList[index] = {
          id: '',
          name: '',
          taskList: [],
          runAlgorithmId: ''
        }
      } else {
        const resultAlgorithm = _.find(resultCamear.taskList, {
          algorithmId: item.runAlgorithmId
        })
        if (!resultAlgorithm) {
          localPlayedCameraList[index].runAlgorithmId = ''
        }
      }
    })
    console.log(
      localPlayedCameraList,
      '====localPlayedCameraList===='
    )
    // 并行初始化所有窗口，不再串行等待 2s
    localPlayedCameraList.forEach((item, index) => {
      if (item.id) {
        playedCameraList.value[index] = item
      }
    })
  }

  // The home page links to this screen with the running task context. Honour
  // that context after restoring the user's saved layout so "View" opens a
  // useful OSD preview instead of four empty windows.
  const routeQuery = proxy.$route?.query || {}
  const queryValue = (value) => Array.isArray(value) ? value[0] : value
  const requestedChannelId = queryValue(routeQuery.channelId)
  const requestedAlgorithmId = queryValue(routeQuery.algorithmId)
  const requestedCamera = childCameras.find(
    (camera) => String(camera.id) === String(requestedChannelId || '')
  )

  if (requestedCamera) {
    const taskList = Array.isArray(requestedCamera.taskList)
      ? requestedCamera.taskList
      : []
    const requestedTask = taskList.find(
      (task) => task.enableStatus == 1 &&
        String(task.algorithmId) === String(requestedAlgorithmId || '')
    )

    playedCameraList.value[0] = {
      id: requestedCamera.id,
      name: requestedCamera.label,
      taskList,
      runAlgorithmId: requestedTask ? requestedAlgorithmId : ''
    }
    currentSelectedIndex.value = 0
  }
}

const handleRunAlgorithmIdChange = (obj) => {
  playedCameraList.value[obj.index] = {
    ...playedCameraList.value[obj.index],
    id: obj.channelId,
    runAlgorithmId: obj.runAlgorithmId
  }
}

const searchRecord = () => {
  queryWarnRecord()
}

const queryWarnRecord = () => {
  const params = {
    timeBegin: moment().startOf('day').valueOf(),
    timeEnd: moment().endOf('day').valueOf(),
    algorithmCodes:
      selectedAlgorithmList.value.length > 0
        ? selectedAlgorithmList.value
        : [],
    categorys: [],
    pageNum: 1,
    pageSize: 20
  }
  $API.boxQueryEvent(params).then((res) => {
    const { resData } = res
    eventList.value = resData.rows || []
    eventList.value.forEach((item) => {
      if (item?.property) {
        item.property = JSON.parse(item.property)
      }
    })
    alarmCount.value = resData?.total || 0
  })
}

const updateAlgorithmInfoList = () => {
  const algorithmList = rawAlgorithmList.value
  const groupedByLabel = {}
  algorithmList.forEach((item) => {
    const categoryInfo = algorithmCategoryList.find(
      (c) => c.value === String(item.algorithmCategory)
    )
    const groupLabel = categoryInfo ? t(categoryInfo.labelI18nKey) : t('event.categoryWithValue', { value: item.algorithmCategory })
    if (!groupedByLabel[groupLabel]) {
      groupedByLabel[groupLabel] = []
    }
    groupedByLabel[groupLabel].push(item)
  })

  const newAlgorithmList = Object.entries(groupedByLabel).map(
    ([label, items]) => {
      return {
        label,
        children: items.map((item) => ({
          label: resolveResourceAlgorithmName(item),
          id: item.algorithmId
        }))
      }
    }
  )
  if (newAlgorithmList.length > 0) {
    algorithmInfoList.value = [
      {
        labelI18nKey: 'common.all',
        id: '',
        children: newAlgorithmList
      }
    ]
  }
}

const getAlgorithmInfo = () => {
  const params = {
    pageNum: 1,
    pageSize: 1000
  }
  $API.algorithmInquire(params).then((res) => {
    const { resData } = res
    rawAlgorithmList.value = resData.rows || []
    updateAlgorithmInfoList()
  })
}

// 监听语言切换，更新搜索面板的过滤算法列表
watch(currentLocale, () => {
  updateAlgorithmInfoList()
})

const eventDetail = (event) => {
  if (showAlert.value) return
  detailData.value = event
  if (event.category == '1') {
    captureDialogVisible.value = true
  } else {
    detailDialogVisible.value = true
  }
}

const initsocketUrl = () => {
  const socketUrl = $API.bigScreenWs()
  socket.value = new WebSocket(socketUrl)
  socket.value.onopen = openSocket
  socket.value.onerror = onSocketError
  socket.value.onmessage = socketOnMessage
  socket.value.onclose = closeSocket
}

const openSocket = () => {
  socketTimer.value = setInterval(() => {
    const params = {
      token: localStorage.getItem('token')
    }
    socket.value && socket.value.send(JSON.stringify(params))
  }, 60000)
}

const onSocketError = (err) => {
  console.log('socket错误。。。', err)
}

const closeSocket = () => {
  console.log('socket断开。。。')
  socket.value = null
  socketTimer.value && clearInterval(socketTimer.value)
  reconnectTimer.value && clearTimeout(reconnectTimer.value)
  if (isDestroyed.value) return
  reconnectTimer.value = setTimeout(() => {
    initsocketUrl()
  }, 30000)
}

const saveSettingClick = () => {
  const params = {
    popUpSwitch: settingForm.value.popUpSwitch,
    audioPlay: settingForm.value.audioPlay,
    popUpDuration: Number(settingForm.value.popUpDuration)
  }
  settingFormRef.value.validate().then(() => {
    $API.setPopUpParam(params).then(() => {
      proxy.$message.success(t('common.operationSucceeded'))
      queryPopUpParam()
      settingDialogVisible.value = false
    })
  })
}

const socketOnMessage = (data) => {
  console.log(data,'socket============================');
  currentSocketData.value = JSON.parse(data.data)
//   currentSocketData.value = {
//   "messageId": "17c370ea-1b34-4ef8-a88b-3b6427db25ec",
//   "devId": "",
//   "taskId": "LX0000000212_77777",
//   "videoChannelId": "LX0000000212",
//   "channelName": "挖掘机",
//   "timestamp": "1774367744630",
//   "algorithmId": "77777",
//   "algorithmCode": "77777",
//   "algorithmName": "工程车辆检测",
//   "areaId": "10658d63-45aa-4185-858a-6cb813b403d7",
//   "areaName": "冲冲冲",
//   "orignalPicture": "/event/2026/03/24/17c370ea-1b34-4ef8-a88b-3b6427db25ec_orig.jpg",
//   "fullPicture": "/event/2026/03/24/17c370ea-1b34-4ef8-a88b-3b6427db25ec_full.jpg",
//   "detectedPicture": "/event/2026/03/24/17c370ea-1b34-4ef8-a88b-3b6427db25ec_full.jpg",
//   "video": "",
//   "videostructured": "",
//   "overviewFile": "",
//   "recordId": "",
//   "isRetryMessage": false,
//   "category": "1"
// }


  if (
    selectedAlgorithmList.value.length == 0 ||
    selectedAlgorithmList.value.includes(currentSocketData.value.algorithmId)
  ) {
    if (eventList.value.length >= 20) {
      eventList.value.pop()
    }
    eventList.value.unshift(currentSocketData.value)
  }

  clearAlertTimers()

  if (realSettingForm.value.audioPlay == 1 && audioUnlocked.value) {
    const audioEl = audio.value
    if (audioEl) {
      audioEl.currentTime = 0
      audioEl.play()
    }
  }

  if (
    realSettingForm.value.popUpSwitch == 0 ||
    detailDialogVisible.value ||
    settingDialogVisible.value
  )
    return

  if (showAlert.value) {
    scheduleAlertDismiss()
    return
  }

  showAlert.value = true

  alertAnimationFrame = requestAnimationFrame(() => {
    alertAnimationFrame = null
    alertEnterTimer.value = setTimeout(() => {
      alertEnterTimer.value = null
      scheduleAlertDismiss()
    }, ALERT_ENTER_DELAY_MS)
  })
}

const unlockAudio = () => {
  const audioEl = audio.value
  if (audioEl && !audioUnlocked.value) {
    audioEl.muted = true
    audioEl.play().then(() => {
      audioEl.pause()
      audioEl.muted = false
      audioEl.currentTime = 0
      audioUnlocked.value = true
    }).catch(() => {})
  }
  document.removeEventListener('click', unlockAudio)
  document.removeEventListener('touchstart', unlockAudio)
}

const checkPropertyKey = (data, key) => {
  return data?.property?.[key]
}

// Lifecycle
onMounted(() => {
  queryPopUpParam()
  initCameraList()
  queryWarnRecord()
  getAlgorithmInfo()
  updateTime()
  timeInterval.value = setInterval(updateTime, 1000)

  window.addEventListener('fullscreenchange', handleFullScreenChange)
  window.addEventListener(
    'webkitfullscreenchange',
    handleFullScreenChange
  )
  window.addEventListener('mozfullscreenchange', handleFullScreenChange)

  // Unlock audio on the first user interaction so WebSocket callbacks can play it.
  document.addEventListener('click', unlockAudio)
  document.addEventListener('touchstart', unlockAudio)

  // 点击通道列表外部时自动收起
  document.addEventListener('click', handleClickOutside)
})

onBeforeUnmount(() => {
  clearAlertTimers()
  timeInterval.value && clearInterval(timeInterval.value)
  socketTimer.value && clearInterval(socketTimer.value)
  reconnectTimer.value && clearTimeout(reconnectTimer.value)
  isDestroyed.value = true
  socket.value && socket.value.close()

  // 安全地退出全屏
  if (document.fullscreenElement || 
      document.webkitFullscreenElement || 
      document.mozFullScreenElement) {
    handleExitFullScreen()
  }

  window.removeEventListener('fullscreenchange', handleFullScreenChange)
  window.removeEventListener(
    'webkitfullscreenchange',
    handleFullScreenChange
  )
  window.removeEventListener(
    'mozfullscreenchange',
    handleFullScreenChange
  )
  document.removeEventListener('click', unlockAudio)
  document.removeEventListener('touchstart', unlockAudio)
  document.removeEventListener('click', handleClickOutside)
})
</script>

<style lang="scss" scoped>
.main-body {
  --screen-text: var(--text-primary);
  --screen-muted: var(--text-secondary);
  --screen-border: var(--border-color);
  --screen-accent: var(--primary-color);
  --screen-stage: #171c24;
  position: relative;
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100vh;
  height: 100dvh;
  overflow: hidden;
  background: var(--bg-primary);
  color: var(--screen-text);
}

button {
  font: inherit;
  cursor: pointer;
}

button:focus-visible {
  outline: 2px solid var(--screen-accent);
  outline-offset: -2px;
}

.header {
  box-sizing: border-box;
  flex: 0 0 auto;
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  min-height: 52px;
  gap: 6px 20px;
  padding: 6px 16px;
  border-bottom: 1px solid var(--screen-border);
  background: var(--bg-white);

  .title {
    min-width: 0;
    max-width: 100%;
    font-size: 19px;
    font-weight: 650;
    letter-spacing: .02em;
  }

  .time {
    margin-left: auto;
    color: var(--screen-muted);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
}

.screen-control {
  display: flex;
  flex-shrink: 0;
  gap: 2px;
  padding: 3px;
  border: 1px solid var(--screen-border);
  border-radius: 6px;
  background: var(--bg-primary);
}

.screen-btn {
  min-width: 52px;
  padding: 4px 10px;
  border: 0;
  border-radius: 4px;
  background: transparent;
  color: var(--screen-muted);
  font-size: 12px;
  line-height: 20px;

  &.is-active {
    color: var(--screen-accent);
    background: var(--bg-white);
    box-shadow: 0 1px 3px #20222d14;
    font-weight: 600;
  }
}

.right-tools {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: flex-end;
  min-width: 0;
  max-width: 100%;
  gap: 4px;
}

.header-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  height: 32px;
  padding: 0 9px;
  border: 0;
  border-radius: 5px;
  background: transparent;
  color: var(--screen-muted);
  white-space: nowrap;
  font-size: 13px;

  .el-icon { font-size: 16px; }
  &:hover { color: var(--screen-text); background: var(--bg-primary); }
}

.content,
.left-panel {
  display: flex;
  flex: 1;
  min-width: 0;
  min-height: 0;
}

.select-area,
.right-panel {
  flex: 0 0 40px;
  position: relative;
  width: 40px;
  min-width: 0;
  min-height: 0;
  background: var(--bg-white);
  overflow: hidden;
  box-sizing: border-box;
}

.select-area {
  border-right: 1px solid var(--screen-border);
  &.expanded { flex-basis: 240px; width: 240px; }
}

.right-panel {
  border-left: 1px solid var(--screen-border);
  &.expanded { flex-basis: 304px; width: 304px; }
}

.panel-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  width: 32px;
  height: 32px;
  padding: 0;
  border: 0;
  border-radius: 5px;
  color: var(--screen-muted);
  background: transparent;
  vertical-align: middle;

  .el-icon { font-size: 17px; }
  &:hover { background: var(--bg-primary); color: var(--screen-accent); }
}

.select-area-tools,
.record-toggle {
  position: absolute;
  top: 6px;
  right: 4px;
  z-index: 1;
}

.camera-list-wrap {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;

  > .title {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex: 0 0 44px;
    padding: 0 40px 0 14px;
    font-size: 14px;
    font-weight: 600;
  }
}

.exseach {
  display: flex;
  flex: 1;
  flex-direction: column;
  min-height: 0;
  padding: 0 10px 10px;

  :deep(.el-input) { flex: 0 0 auto; margin-bottom: 10px; }
  :deep(.el-input__wrapper) { background: var(--bg-white); }
  :deep(.el-input__inner) { height: 30px; color: var(--screen-text); }

  .tree-body {
    flex: 1;
    min-height: 0;
    overflow: auto;
  }

  :deep(.el-tree) {
    color: var(--screen-text);
    background: transparent;
    font-size: 13px;
  }

  :deep(.el-tree-node__content) {
    height: 34px;
    border-radius: 5px;
    &:hover { background: var(--bg-primary); }
  }

  :deep(.el-tree-node.is-current > .el-tree-node__content) {
    background: var(--theme-selected-bg, #efedff);
    color: var(--screen-accent);
  }
}

.custom-tree-node {
  min-width: 0;
  .stnode { display: flex; align-items: center; }
  img { width: 16px; height: 16px; margin-right: 6px; }
  span { display: inline-block; max-width: 148px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
}

.video-grid {
  display: flex;
  flex: 1;
  flex-direction: column;
  min-width: 0;
  min-height: 0;
  padding: 6px;
  background: var(--theme-stage-surround, #e9edf2);
  box-sizing: border-box;
}

.video-wall {
  display: grid;
  position: relative;
  flex: 1;
  min-width: 0;
  min-height: 0;
  gap: 6px;
  overflow: hidden;
  &.grid-1 { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 1fr); }
  &.grid-4 { grid-template-columns: repeat(2, minmax(0, 1fr)); grid-template-rows: repeat(2, minmax(0, 1fr)); }
}

.video-item {
  position: relative;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
  border: 1px solid #303845;
  border-radius: 5px;
  background: var(--screen-stage);
  box-sizing: border-box;
  color: #bbc3cf;
  font-size: 13px;
}

.video-active { border-color: var(--screen-accent); }

.video-one {
  position: absolute;
  inset: 0;
  z-index: 2;
}

.no-camera {
  display: flex;
  flex-flow: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  background: var(--screen-stage);
  color: #a9b3c2;
  font-size: 13px;

  img { width: 56px; height: auto; margin-bottom: 16px; opacity: .6; }
}

.record-body {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 6px 12px 12px;
  box-sizing: border-box;
}

.record-header {
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
  gap: 2px;
  min-height: 64px;
  padding-right: 30px;
  font-size: 14px;
  font-weight: 600;

  > span:first-child { display: flex; align-items: center; gap: 4px; }
  .today-count { font-size: 12px; font-weight: 400; color: var(--screen-muted); }
}

.record-search {
  display: flex;
  flex-shrink: 0;
  gap: 6px;
  .tree-select { flex: 1; min-width: 0; }
  .el-button { width: 30px; height: 30px; padding: 0; }
}

.record-content {
  flex: 1;
  min-height: 0;
  margin-top: 12px;
  overflow-y: auto;
}

.event-item {
  margin-bottom: 10px;
  padding: 10px;
  border: 1px solid var(--screen-border);
  border-radius: 6px;
  background: var(--bg-white);
  cursor: pointer;

  &:hover { border-color: var(--theme-selected-border, #aaa6e8); background: var(--theme-hover-bg, #fcfcff); }
  &:last-child { margin-bottom: 0; }
}

.event-image,
.event-image2 {
  width: 100%;
  height: 142px;
  margin-bottom: 10px;
  background: var(--bg-primary);
  border-radius: 4px;
  overflow: hidden;
}

.event-image :deep(.el-image) { width: 100%; height: 100%; }
.event-image2 { display: flex; position: relative; gap: 6px; }
.event-image2-body {
  position: relative;
  flex: 1;
  min-width: 0;
  height: 100%;
  :deep(.el-image) { width: 100%; height: 100%; }
  span { position: absolute; top: 0; left: 0; padding: 3px 6px; background: #20222dcc; color: #fff; font-size: 11px; }
}
.match-div { position: absolute; bottom: 0; left: 0; right: 0; padding: 6px; background: #20222dcc; color: #fff; font-size: 12px; text-align: center; }
.event-info { font-size: 12px; line-height: 1.8; color: var(--screen-muted); overflow-wrap: anywhere; }
.event-info > div:first-child { color: var(--screen-text); }
.empty-body { display: flex; justify-content: center; height: 100%; }
.empty-body :deep(.el-empty__description p) { color: var(--screen-muted); }

:deep(.image-slot) { display: flex; align-items: center; justify-content: center; height: 100%; }
:deep(.image-slot img) { max-width: 80%; max-height: 80%; }
.form-content { width: calc(100% - 30px); }

// Keep the automatic alarm popup behavior, with a neutral frame and bounded size.
.alert-popup {
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  z-index: 9999;
  display: flex;
  flex-direction: column;
  width: min(900px, calc(100vw - 48px));
  max-height: calc(100dvh - 48px);
  overflow: auto;
  background: var(--bg-white);
  color: var(--screen-text);
  border: 1px solid var(--screen-border);
  border-top: 3px solid #d95545;
  border-radius: 10px;
  box-shadow: 0 20px 64px #151c3640;

  .alert-top-title { padding: 18px 24px; border-bottom: 1px solid var(--screen-border); font-size: 22px; font-weight: 650; }
  .alert-body { display: flex; gap: 24px; padding: 24px; }
  .alert-left { flex: 1.5; min-width: 0; }
  .warn-one-body { width: 100%; height: 340px; background: var(--bg-primary); border-radius: 6px; overflow: hidden; }
  .warn-one-body :deep(.el-image) { width: 100%; height: 100%; }
  .warn-two-body { display: flex; flex-direction: column; gap: 16px; }
  .event-image-container { display: flex; gap: 12px; }
  .event-image-item { display: flex; flex: 1; min-width: 0; flex-direction: column; gap: 8px; text-align: center; }
  .event-image-item :deep(.el-image) { width: 100%; height: 230px; background: var(--bg-primary); border-radius: 6px; }
  .image-label { color: var(--screen-muted); font-size: 13px; }
  .match-info { display: flex; justify-content: center; gap: 14px; padding: 12px; background: var(--bg-primary); border-radius: 6px; font-size: 17px; }
  .match-score { color: var(--screen-accent); font-weight: 600; }
  .alert-right { display: flex; flex: 1; min-width: 0; flex-direction: column; justify-content: center; gap: 28px; }
  .info-label { margin-bottom: 8px; color: var(--screen-muted); font-size: 13px; }
  .info-value { color: var(--screen-text); font-size: 18px; line-height: 1.6; overflow-wrap: anywhere; }
}

.alert-slide-enter-active,
.alert-slide-leave-active { transition: transform .6s ease, opacity .4s ease; }
.alert-slide-enter-from { transform: translate(-150%, -50%); opacity: 0; }
.alert-slide-leave-to { transform: translate(150%, -50%); opacity: 0; }

:deep(.el-dialog) {
  max-width: calc(100vw - 32px);
  background: var(--bg-white);
  border: 1px solid var(--screen-border);
  border-radius: 10px;
  box-shadow: 0 20px 64px #151c3633;
  color: var(--screen-text);
}
:deep(.el-dialog__title) { color: var(--screen-text); font-weight: 600; }
:deep(.el-dialog__body) { color: var(--screen-text); }
:deep(.el-form-item__label) { color: var(--screen-muted); }

@media (max-width: 1080px) {
  .header { gap: 6px 10px; padding: 6px 10px; }
  .header .title { font-size: 17px; }
  .header-btn { padding: 0 6px; }
  .right-panel.expanded { flex-basis: 272px; width: 272px; }
  .select-area.expanded { flex-basis: 220px; width: 220px; }
}

@media (max-width: 800px) {
  .header { flex-wrap: wrap; flex-basis: auto; min-height: 52px; padding: 6px 10px; gap: 6px 12px; }
  .header .time { order: 4; margin-left: 0; }
  .right-tools { margin-left: auto; }
  .right-panel.expanded { flex-basis: 240px; width: 240px; }
  .select-area.expanded { flex-basis: 190px; width: 190px; }
  .custom-tree-node span { max-width: 106px; }
  .alert-popup .alert-body { flex-direction: column; }
  .alert-popup .alert-right { flex-direction: row; justify-content: space-between; }
}
</style>
