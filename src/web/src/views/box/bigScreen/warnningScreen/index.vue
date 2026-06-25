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
                <span class="match-score">{{ currentSocketData.property.recognition.matchDegree != '-1' ? currentSocketData.property.recognition.matchDegree : '' }}</span>
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
      <div class="time">{{ currentTime }}</div>
      <div class="setting-btn" @click="handleSettingClick">{{ t('action.settings') }}</div>
      <div class="right-tools">
        <div class="full-screen-btn" @click="toggleFullScreen">{{ isFullScreen ? t('event.exitFullscreen') : t('event.fullscreen') }}</div>
        <img src="@/assets/screen-exit.png" @click="exitFullScreen" />
      </div>
    </div>

    <div class="content">
      <div class="left-panel">
        <!-- 摄像机列表 -->
        <div ref="selectAreaEl" class="select-area" :class="{ 'expanded': cameraDrawerVisible }">
          <div v-if="cameraDrawerVisible" class="camera-list-wrap">
            <div class="title">
              <span>{{ t('event.channelList') }}</span>
            </div>
            <div class="exseach">
              <el-input size="small" :placeholder="t('event.searchKeyword')" maxlength="32" v-model="cameraFilterText">
                <template #suffix>
                  <i class="el-icon-search el-input__icon"></i>
                </template>
              </el-input>
              <div class="tree-body">
                <el-tree id="onboarding-camera-tree" ref="tree" class="filter-tree" :data="camearList" :highlight-current="true" node-key="id" default-expand-all :filter-node-method="filterNode" draggable @node-drag-start="handleDragStart" :allow-drop="handleDrop">
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
          <div id="onboarding-camera-toggle" class="select-area-tools" @click="toggleCameraDrawer">
            <img src="@/assets/screen-camera.png" :class="{ 'expanded': cameraDrawerVisible }">
            <div class="el-icon-d-arrow-right" :class="{ 'expanded': cameraDrawerVisible }"></div>
          </div>
          <div class="el-icon-refresh" v-if="cameraDrawerVisible" @click="initCameraList"></div>
        </div>

        <!-- 摄像机播放窗口 -->
        <div class="video-grid">
          <div class="screen-control">
            <div class="btns-wrap">
              <div class="btn" :class="{'btn-active': screenType === 1}" @click="switchScreen(1)">{{ t('event.oneScreen') }}</div>
              <div class="btn" :class="{'btn-active': screenType === 4}" @click="switchScreen(4)">{{ t('event.fourScreens') }}</div>
            </div>
          </div>
          <div class="video-container" :class="'grid-' + screenType">
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
        <div class="record-body">
          <div class="record-header">
            <span>{{ t('event.eventRecords') }}
              <i class="el-icon-refresh" v-if="recordDrawerVisible" style="cursor: pointer;" @click="queryWarnRecord"></i>
            </span>
            <span class="today-count">{{ t('event.todayAlarmCount', { n: alarmCount }) }}</span>
          </div>
          <div class="record-search">
            <TreeSelect class="tree-select" :treeData="algorithmInfoList" v-model="selectedAlgorithmList" />
            <el-button size="small" @click="searchRecord">
              <el-icon><Search /></el-icon>
            </el-button>
          </div>
          <div class="record-content">
            <template v-if="eventList.length > 0">
              <div v-for="(event, index) in eventList" :key="index" class="event-item" @click="eventDetail(event)">
                <div v-if="checkPropertyKey(event,'recognition') && event.property.recognition.matchDegree != '-1'" class="event-image2">
                  <div class="event-image2-body">
                    <el-image :src="event.detectedPicture">
                      <template #error>
                        <div class="image-slot">
                          <img src="@/assets/error-image.png">
                        </div>
                      </template>
                    </el-image>
                    <span>{{ t('event.captureImage') }}</span>
                  </div>

                  <div class="event-image2-body">
                    <el-image :src="event.property.recognition.LibImage">
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
                    {{ event.property.recognition.matchDegree != '-1' ? event.property.recognition.matchDegree : '' }}
                  </div>
                </div>
                <div v-else class="event-image">
                  <el-image :src="event.fullPicture">
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
              <span style="color: #1e293b; font-weight: 500;">{{ t('event.alarmPopup') }}</span>
            </template>
            <el-switch size="small" v-model="settingForm.popUpSwitch" :active-value="1" :inactive-value="0"></el-switch>
          </el-form-item>
          <el-form-item>
            <template #label>
              <span style="color: #1e293b; font-weight: 500;">{{ t('event.alarmSound') }}</span>
            </template>
            <el-switch size="small" v-model="settingForm.audioPlay" :active-value="1" :inactive-value="0"></el-switch>
          </el-form-item>
          <el-form-item :label="t('event.popupDurationSeconds')" prop="popUpDuration">
            <template #label>
              <span style="color: #1e293b; font-weight: 500; display:inline-block;">
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
    <audio ref="audio" src="@/assets/beep.ogg"></audio>
  </div>
</template>
<script setup>
import { ref, watch, onMounted, onBeforeUnmount, nextTick, getCurrentInstance } from 'vue'
import { Search } from '@element-plus/icons-vue'
import flv from '../components/flvVideo.vue'
import DetailDialog from '../components/detailDialog.vue'
import CaptureDialog from '../components/captureDialog.vue'
import moment from 'moment'
import TreeSelect from '../components/TreeSelect.vue'
import EventBus from '@/components/eventBus'
import _ from 'lodash'
import { t, localeColon, currentLocale } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

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
const alertPosition = ref(-383)
const alertTimer = ref(null)
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
let _unlockAudio = null

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

const handleFullScreen = () => {
  const dom = document.body
  if (dom.requestFullscreen) {
    dom.requestFullscreen()
  } else if (dom.mozRequestFullScreen) {
    dom.mozRequestFullScreen()
  } else if (dom.webkitRequestFullScreen) {
    dom.webkitRequestFullScreen()
  }
}

const handleExitFullScreen = () => {
  // 检查是否处于全屏状态
  if (!document.fullscreenElement && 
      !document.webkitFullscreenElement && 
      !document.mozFullScreenElement) {
    return
  }

  const dom = document
  try {
    if (dom.exitFullscreen) {
      dom.exitFullscreen().catch(err => {
        console.log('退出全屏失败:', err)
      })
    } else if (dom.mozCancelFullScreen) {
      dom.mozCancelFullScreen()
    } else if (dom.webkitCancelFullScreen) {
      dom.webkitCancelFullScreen()
    }
  } catch (err) {
    console.log('退出全屏异常:', err)
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

const toggleFullScreen = () => {
  if (!document.fullscreenElement) {
    handleFullScreen()
  } else {
    handleExitFullScreen()
  }
  isFullScreen.value = !isFullScreen.value
}

const exitFullScreen = () => {
  handleExitFullScreen()
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

const handleCameraNodeClick = (node) => {
  if (node.labelI18nKey === 'common.all' || node.label === '全部') return
  if (node.channelType == 0 && node.status == 0)
    return proxy.$message.error(t('event.cameraOffline'))
  cameraDrawerVisible.value = false
  playedCameraList.value[currentSelectedIndex.value] = {
    id: node.id,
    name: node.label,
    taskList: node.taskList,
    runAlgorithmId: ''
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
  playAudio()
  showAlert.value = false
  settingForm.value = { ...realSettingForm.value }
  settingFormRef.value && settingFormRef.value.clearValidate()
  settingDialogVisible.value = true
}

const handleDragStart = () => {}
const handleDrop = () => {}

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
  $API.boxQueryCameraList(params).then((res) => {
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
  $API.boxAllAlgorithmInfo(params).then((res) => {
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

  if (alertTimer.value) {
    clearTimeout(alertTimer.value)
  }

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
    alertTimer.value && clearTimeout(alertTimer.value)
    alertTimer.value = setTimeout(() => {
      alertPosition.value = window.innerWidth
      setTimeout(() => {
        showAlert.value = false
      }, 500)
    }, realSettingForm.value.popUpDuration * 1000)
    return
  }

  alertPosition.value = window.innerWidth
  showAlert.value = true

  requestAnimationFrame(() => {
    setTimeout(() => {
      // 居中位置 = (窗口宽度 - 弹窗宽度) / 2
      alertPosition.value = (window.innerWidth - 900) / 2

      alertTimer.value = setTimeout(() => {
        alertPosition.value = window.innerWidth

        setTimeout(() => {
          showAlert.value = false
        }, 500)
      }, realSettingForm.value.popUpDuration * 1000)
    }, 50)
  })
}

const playAudio = () => {
  // handleSettingClick 是用户点击触发的，利用此交互解锁音频
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
}

const checkPropertyKey = (data, key) => {
  return data?.property?.[key]
}

// Lifecycle
onMounted(() => {
  EventBus.$emit('changeScreen', true)
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

  // 用户首次交互时解锁音频元素，使后续 WebSocket 回调中的 play() 不会被浏览器拦截
  _unlockAudio = () => {
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
    document.removeEventListener('click', _unlockAudio)
    document.removeEventListener('touchstart', _unlockAudio)
  }
  document.addEventListener('click', _unlockAudio)
  document.addEventListener('touchstart', _unlockAudio)

  // 点击通道列表外部时自动收起
  document.addEventListener('click', handleClickOutside)

  // 自动进入全屏
  nextTick(() => {
    if (!document.fullscreenElement) {
      handleFullScreen()
    }
  })
})

onBeforeUnmount(() => {
  EventBus.$emit('changeScreen', false)
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
  // 清理音频解锁监听器
  if (_unlockAudio) {
    document.removeEventListener('click', _unlockAudio)
    document.removeEventListener('touchstart', _unlockAudio)
  }
  document.removeEventListener('click', handleClickOutside)
})
</script>

<style lang="scss" scoped>
// 告警弹窗样式
.alert-popup {
  position: fixed;
  top: 50%;
  left: 50%;
  width: 900px;
  height: 600px;
  transform: translate(-50%, -50%);
  z-index: 9999;
  background: linear-gradient(135deg, rgba(49, 130, 206, 0.95) 0%, rgba(66, 153, 225, 0.95) 100%);
  backdrop-filter: blur(20px);
  color: white;
  border-radius: 24px;
  box-shadow: 0 30px 80px rgba(0, 0, 0, 0.5), 0 0 0 1px rgba(255, 255, 255, 0.2);
  box-sizing: border-box;
  overflow: hidden;
  display: flex;
  flex-direction: column;

  .alert-top-title {
    width: 100%;
    height: 80px;
    background: rgba(255, 255, 255, 0.1);
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    font-size: 36px;
    font-weight: 700;
    line-height: 80px;
    padding: 0 40px;
    box-sizing: border-box;
    letter-spacing: 2px;
    text-shadow: 0 2px 10px rgba(0, 0, 0, 0.3);
  }

  .alert-body {
    flex: 1;
    display: flex;
    padding: 40px;
    gap: 40px;
    box-sizing: border-box;
  }

  .alert-left {
    flex: 1.5;
    position: relative;
    display: flex;
    flex-direction: column;
    justify-content: center;

    .alert-warning-icon {
      position: absolute;
      top: -10px;
      left: -10px;
      z-index: 1;
      
      img {
        width: 80px;
        height: auto;
        animation: twinkle 1s ease-in-out infinite;
      }
    }

    .warn-one-body {
      width: 100%;
      height: 350px;
      border-radius: 16px;
      overflow: hidden;
      border: 2px solid rgba(255, 255, 255, 0.2);
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.3);

      :deep(.el-image) {
        width: 100%;
        height: 100%;
      }
    }

    .warn-two-body {
      display: flex;
      flex-direction: column;
      gap: 20px;

      .event-image-container {
        display: flex;
        gap: 20px;

        .event-image-item {
          flex: 1;
          display: flex;
          flex-direction: column;
          align-items: center;
          gap: 10px;

          :deep(.el-image) {
            width: 100%;
            height: 200px;
            border-radius: 12px;
            border: 2px solid rgba(255, 255, 255, 0.2);
            background: rgba(0, 0, 0, 0.2);
          }

          .image-label {
            font-size: 16px;
            color: rgba(255, 255, 255, 0.8);
          }
        }
      }

      .match-info {
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 15px;
        background: rgba(0, 0, 0, 0.2);
        padding: 12px;
        border-radius: 12px;
        font-size: 20px;
        font-weight: 600;

        .match-score {
          color: #fbbf24;
          font-size: 24px;
        }
      }
    }
  }

  .alert-right {
    flex: 1;
    display: flex;
    flex-direction: column;
    justify-content: center;
    gap: 40px;

    .info-item {
      display: flex;
      flex-direction: column;
      gap: 12px;

      .info-label {
        font-size: 18px;
        color: rgba(255, 255, 255, 0.6);
        letter-spacing: 1px;
      }

      .info-value {
        font-size: 28px;
        font-weight: 600;
        color: #fff;
        line-height: 1.4;
      }
    }
  }
}

// 告警弹窗动画
.alert-slide-enter-active,
.alert-slide-leave-active {
  transition: transform 0.6s cubic-bezier(0.4, 0, 0.2, 1), opacity 0.4s ease;
}

.alert-slide-enter {
  transform: translate(-150%, -50%);
  opacity: 0;
}

.alert-slide-leave-to {
  transform: translate(150%, -50%);
  opacity: 0;
}

.main-body {
  position: relative;
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100vh;
  transform-origin: 0 0;
  background: linear-gradient(135deg, #0F172A 0%, #1E293B 50%, #334155 100%);
  background-size: 100% 100%;
}

.header {
  position: relative;
  height: 80px;
  padding: 0 20px;
  margin-bottom: 20px;
  background:
    linear-gradient(135deg, rgba(49, 130, 206, 0.2) 0%, rgba(66, 153, 225, 0.2) 100%),
    radial-gradient(1200px 120px at 20% 0%, rgba(129, 140, 248, 0.25), transparent 60%),
    radial-gradient(1200px 120px at 80% 0%, rgba(167, 139, 250, 0.2), transparent 60%),
    repeating-linear-gradient(-45deg, rgba(255,255,255,0.03) 0 2px, transparent 2px 6px);
  backdrop-filter: blur(10px);
  border-bottom: 2px solid rgba(49, 130, 206, 0.3);
  box-shadow: 0 4px 20px rgba(49, 130, 206, 0.2);
  color: white;
  overflow: hidden;

  &::before {
    /* 顶部细光线 */
    content: '';
    position: absolute;
    left: 0;
    top: 0;
    width: 100%;
    height: 2px;
    background: linear-gradient(90deg, transparent, rgba(49,130,206,0.8), rgba(66,153,225,0.8), transparent);
    filter: blur(0.2px);
  }

  &::after {
    /* 流动霓虹线 */
    content: '';
    position: absolute;
    left: -40%;
    bottom: 0;
    width: 40%;
    height: 3px;
    background: linear-gradient(90deg, transparent, #63b3ed, #63b3ed, transparent);
    box-shadow: 0 0 12px rgba(129, 140, 248, 0.6);
    animation: header-neon-run 3.2s linear infinite;
  }

  @keyframes header-neon-run {
    0% { left: -40%; }
    60% { left: 100%; }
    100% { left: 100%; }
  }

  .title {
    width: 100%;
    font-size: 40px;
    font-weight: 700;
    text-align: center;
    line-height: 80px;
    background: linear-gradient(135deg, #FFFFFF 0%, #A5B4FC 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
    text-shadow: 0 2px 20px rgba(49, 130, 206, 0.5);
    letter-spacing: 2px;
    animation: title-glow 4s ease-in-out infinite;
  }

  @keyframes title-glow {
    0%, 100% { text-shadow: 0 2px 14px rgba(49,130,206,0.4); }
    50% { text-shadow: 0 2px 24px rgba(66,153,225,0.7); }
  }

  .time {
    position: absolute;
    right: 20px;
    top: 20%;
    transform: translateY(-50%);
    font-size: 14px;
    padding: 6px 10px;
    border-radius: 8px;
    background: linear-gradient(135deg, rgba(255,255,255,0.08) 0%, rgba(255,255,255,0.02) 100%);
    border: 1px solid rgba(255,255,255,0.14);
  }

  .setting-btn {
    position: absolute;
    left: 20px;
    bottom: 10px;
    cursor: pointer;
    padding: 6px 14px;
    font-size: 14px;
    background: linear-gradient(135deg, rgba(49, 130, 206, 0.8) 0%, rgba(66, 153, 225, 0.8) 100%);
    border-radius: 6px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    transition: all 0.3s ease;
    font-weight: 500;
    
    &:hover {
      background: linear-gradient(135deg, rgba(49, 130, 206, 1) 0%, rgba(66, 153, 225, 1) 100%);
      transform: translateY(-2px);
      box-shadow: 0 4px 15px rgba(49, 130, 206, 0.4);
    }
  }

  .test-btn {
    position: absolute;
    left: 100px;
    bottom: 0;
    cursor: pointer;
    padding: 5px 15px;
    font-size: 16px;
    background: url(@/assets/big_screen_btn_bg.png) no-repeat 0 0;
    background-size: 100% 100%;
  }

  .right-tools {
    position: absolute;
    right: 20px;
    bottom: 10px;
    display: flex;
    align-items: center;
    gap: 8px;

    img {
      display: inline-block;
      width: 18px;
      height: 18px;
      cursor: pointer;
      vertical-align: middle;
    }
  }

  .full-screen-btn {
    cursor: pointer;
    padding: 6px 14px;
    font-size: 14px;
    background: linear-gradient(135deg, rgba(49, 130, 206, 0.8) 0%, rgba(66, 153, 225, 0.8) 100%);
    border-radius: 6px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    transition: all 0.3s ease;
    font-weight: 500;
    
    &:hover {
      background: linear-gradient(135deg, rgba(49, 130, 206, 1) 0%, rgba(66, 153, 225, 1) 100%);
      transform: translateY(-2px);
      box-shadow: 0 4px 15px rgba(49, 130, 206, 0.4);
    }
  }
}

.content {
  flex: 1;
  display: flex;
  min-height: 0;
}

.bottom {
  position: relative;
  height: 20px;
  background: #101938;
  transition: height 0.3s ease;

  &.expanded {
    height: 200px;
  }

  .bottom-header {
    position: absolute;
    top: 0;
    left: 20px;
    color: white;
    font-size: 14px;
    cursor: pointer;
  }

  .el-icon-d-arrow-right {
    transform: rotate(-90deg);
    margin-left: 5px;

    &.expanded {
      transform: rotate(90deg);
    }
  }
}

.left-panel {
  flex: 1;
  position: relative;
}

.select-area {
  width: 40px;
  position: relative;
  top: 0;
  height: 100%;
  background: linear-gradient(180deg, rgba(30, 27, 75, 0.8) 0%, rgba(79, 70, 229, 0.2) 100%);
  backdrop-filter: blur(10px);
  border-right: 1px solid rgba(49, 130, 206, 0.3);
  transition: width 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  z-index: 1001;

  .camera-list-wrap {
    position: absolute;
    top: 0;
    left: 0;
    width: 240px;
    padding: 10px 0 0 20px;
    color: white;
    box-sizing: border-box;

  }

  &.expanded {
    width: 240px;
  }

  .el-icon-video-camera-solid {
    display: block;
    position: absolute;
    color: white;
    top: 5px;
    left: 50%;
    transform: translateX(-50%);
    cursor: pointer;

    &.expanded {
      display: none;
    }
  }

  .el-icon-d-arrow-right {
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    color: white;
    cursor: pointer;
    z-index: 1001;
    animation: twinkle 1s linear infinite;

    &.expanded {
      transform: translate(-50%, -50%) rotate(180deg);
    }
  }

  .el-icon-refresh {
    position: absolute;
    top: 15px;
    left: 40%;
    // transform: translate(-50%, -50%);
    color: white;
    cursor: pointer;
  }
}

.video-grid {
  position: absolute;
  top: 0;
  left: 40px;
  height: 100%;
  width: calc(100% - 40px);
  padding: 0 5px 5px 5px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
  background: linear-gradient(135deg, rgba(15, 23, 42, 0.5) 0%, rgba(30, 41, 59, 0.5) 100%);
}

.screen-control {
  display: flex;
  flex-direction: column;

  .btns-wrap {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    padding: 10px;

    .btn {
      width: 60px;
      height: 32px;
      font-size: 13px;
      color: rgba(255, 255, 255, 0.7);
      background: linear-gradient(135deg, rgba(51, 65, 85, 0.6) 0%, rgba(30, 41, 59, 0.6) 100%);
      border: 1px solid rgba(49, 130, 206, 0.3);
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      transition: all 0.3s ease;
      font-weight: 500;
      margin-left: 8px;
      
      &:hover {
        background: linear-gradient(135deg, rgba(49, 130, 206, 0.4) 0%, rgba(66, 153, 225, 0.4) 100%);
        border-color: rgba(49, 130, 206, 0.6);
        color: white;
      }
    }

    .btn-active {
      background: linear-gradient(135deg, rgba(49, 130, 206, 0.9) 0%, rgba(66, 153, 225, 0.9) 100%);
      border-color: rgba(49, 130, 206, 0.8);
      color: #ffffff;
      box-shadow: 0 4px 12px rgba(49, 130, 206, 0.4);
    }
  }
}

.video-container {
  position: relative;
  flex: 1;
  display: flex;
  flex-wrap: wrap;
  justify-content: space-around;
  overflow: hidden;

  &.grid-1 .video-item {
    width: 100%;
    height: 100%;
  }

  &.grid-4 .video-item {
    width: 50%;
    height: 50%;
  }
}

.video-item {
  border: 1px solid transparent;
  box-sizing: border-box;
  color: lightgrey;
  font-size: 14px;

  .video-content {
    background-color: #212839;
    display: flex;
    align-items: center;
    justify-content: center;
  }
}

.video-active {
  border: 2px solid #3182ce;
  box-shadow: 0 0 20px rgba(49, 130, 206, 0.5);
}

.video-one {
  position: absolute;
  top: 0;
  left: 0;
  width: 100% !important;
  height: 100% !important;
  z-index: 1000;
}

.right-panel {
  position: relative;
  width: 40px;
  background: linear-gradient(180deg, rgba(30, 27, 75, 0.8) 0%, rgba(79, 70, 229, 0.2) 100%);
  backdrop-filter: blur(10px);
  border-left: 1px solid rgba(49, 130, 206, 0.3);
  transition: width 0.3s cubic-bezier(0.4, 0, 0.2, 1);

  &.expanded {
    width: 320px;
  }

  .right-panel-tools {
    position: absolute;
    top: 0;
    right: left;
    width: 40px;
    height: 100%;
    cursor: pointer;
  }

  .el-icon-s-order {
    position: absolute;
    display: block;
    top: 10px;
    left: 50%;
    transform: translateX(-50%);
    color: white;

    &.expanded {
      display: none;
    }
  }

  .el-icon-d-arrow-left {
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    cursor: pointer;
    color: white;
    animation: twinkle 1s linear infinite;

    &.expanded {
      transform: rotate(180deg);
      left: 0;
    }
  }
}

.no-camera {
  height: 100%;
  display: flex;
  flex-flow: column;
  align-items: center;
  justify-content: center;
  font-size: 16px;
  color: lightgrey;
  background: #212839;

  img {
    margin-bottom: 30px;
    widows: 80px;
  }

  span {
    font-size: 14px;
  }
}

.overflow-select {
  width: 100px;

  .el-input__inner {
    background: #111a39;
    border: none;
  }
}

.form-content {
  width: calc(100% - 60px);
}

.exseach {
  margin-top: 10px;
  box-sizing: border-box;
  :deep(.el-input) {
    margin-bottom: 20px;
    .el-input__wrapper {
      background: linear-gradient(180deg, rgba(19, 31, 58, 0.6) 0%, rgba(21, 35, 69, 0.8) 100%);
      border: 1px solid rgba(95, 200, 223, 0.3);
      border-radius: 4px;
      box-shadow: none;
      transition: all 0.3s;
      padding: 1px 12px;

      &:hover {
        border-color: rgba(95, 200, 223, 0.5);
        background: linear-gradient(180deg, rgba(19, 31, 58, 0.7) 0%, rgba(21, 35, 69, 0.9) 100%);
      }

      &.is-focus {
        border-color: #5fc8df;
        box-shadow: 0 0 8px rgba(95, 200, 223, 0.3);
      }
    }

    .el-input__inner {
      height: 34px;
      color: #94d0ff;
      font-size: 14px;
      background: transparent;
      border: none;
      box-shadow: none;

      &::placeholder {
        color: rgba(148, 208, 255, 0.5);
      }
    }

    .el-input__suffix,
    .el-input__suffix-inner {
      .el-icon-search,
      .el-input__icon {
        color: #94d0ff;
        font-size: 16px;
      }
    }
  }

  .tree-body {
    background: transparent;
    height: calc(100vh - 200px);
    box-sizing: border-box;
    padding-bottom: 10px;
    overflow-y: scroll;

    &::-webkit-scrollbar-track {
      background: rgba(30, 27, 75, 0.3);
      border-radius: 4px;
    }

    &::-webkit-scrollbar {
      -webkit-appearance: none;
      width: 6px;
      height: 6px;
    }

    &::-webkit-scrollbar-thumb {
      cursor: pointer;
      border-radius: 4px;
      background: rgba(49, 130, 206, 0.5);
      transition: all 0.2s ease;

      &:hover {
        background: rgba(49, 130, 206, 0.7);
      }
    }

    &:deep(.el-tree) {
      background: transparent;
      font-size: 14px;
      color: #90cdf4;

      .el-tree-node__content {
        height: 32px;
        transition: all 0.2s ease;
        border-radius: 6px;

        &:hover {
          background: rgba(49, 130, 206, 0.2);
        }
      }

      .el-tree-node.is-current > .el-tree-node__content {
        background: rgba(49, 130, 206, 0.3);
        color: #A5B4FC;
      }
    }
  }
}

.custom-tree-node {
  .stnode {
    display: flex;
    align-items: center;
  }

  img {
    margin-right: 5px;
  }

  span {
    display: inline-block;
    width: 120px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
}

:deep(.el-icon-caret-right) {
  display: none;
}

.bottom-cotent {
  margin: 25px 20px;
  background: #03091a;
  border-radius: 4px;
}

.bottom-content-body {
  display: flex;
  height: 80px;
  padding-right: 10px;
}

.bottom-content-title {
  width: 80px;
  color: white;
  line-height: 80px;
  text-align: center;
  font-size: 14px;
}

.bottom-content-item-body {
  flex: 1;
  display: flex;
  flex-wrap: nowrap;
  align-items: center;
  gap: 5px;
  overflow-x: auto;
  overflow-y: hidden;
  -webkit-overflow-scrolling: touch;

  .el-image {
    width: 60px;
    height: 60px;
    /* 防止图片缩小 */
    flex-shrink: 0;
  }
}

.record-body {
  height: 100%;
  padding: 10px;
  box-sizing: border-box;
  background: linear-gradient(180deg, rgba(30, 27, 75, 0.6) 0%, rgba(79, 70, 229, 0.1) 100%);
  backdrop-filter: blur(12px);
  display: flex;
  flex-direction: column;
  border-left: 1px solid rgba(49, 130, 206, 0.3);

  .record-header {
    display: flex;
    height: 50px;
    justify-content: space-between;
    align-items: center;
    color: #fff;
    font-size: 16px;
    font-weight: 600;
    padding-bottom: 10px;
    border-bottom: 2px solid rgba(49, 130, 206, 0.3);
    margin-bottom: 10px;

    .today-count {
      color: #A5B4FC;
      font-size: 14px;
      font-weight: 500;
    }

    .el-icon-refresh {
      margin-left: 8px;
      color: #90cdf4;
      transition: all 0.3s ease;

      &:hover {
        color: #A5B4FC;
        transform: rotate(180deg);
      }
    }
  }

  .record-content {
    flex: 1;
    overflow-y: scroll;
    margin-top: 10px;

    .event-item {
      background: linear-gradient(135deg, rgba(30, 27, 75, 0.6) 0%, rgba(79, 70, 229, 0.2) 100%);
      border: 1px solid rgba(49, 130, 206, 0.3);
      border-radius: 12px;
      padding: 12px;
      margin-bottom: 12px;
      cursor: pointer;
      transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
      backdrop-filter: blur(8px);

      &:hover {
        border-color: rgba(49, 130, 206, 0.6);
        background: linear-gradient(135deg, rgba(30, 27, 75, 0.8) 0%, rgba(79, 70, 229, 0.3) 100%);
        box-shadow: 0 8px 20px rgba(49, 130, 206, 0.3);
        transform: translateY(-4px);
      }

      .event-image {
        width: 100%;
        height: 150px;
        margin-right: 12px;
        margin-bottom: 10px;

        .el-image {
          width: 100%;
          height: 100%;
          object-fit: fill;
          border-radius: 4px;
          border: 1px solid rgba(95, 200, 223, 0.15);
        }
      }

      .event-image2 {
        display: flex;
        position: relative;
        justify-content: space-between;
        margin-bottom: 10px;
        width: 100%;
        height: 150px;
      }

      .match-div {
        position: absolute;
        top: 50%;
        left: 50%;
        padding: 6px 12px;
        transform: translate(-50%, -50%);
        background: linear-gradient(135deg, rgba(49, 130, 206, 0.95) 0%, rgba(66, 153, 225, 0.95) 100%);
        color: white;
        font-size: 12px;
        font-weight: 600;
        border-radius: 8px;
        border: 1px solid rgba(255, 255, 255, 0.3);
        backdrop-filter: blur(8px);
        box-shadow: 0 4px 12px rgba(49, 130, 206, 0.4);
      }

      .event-info {
        flex: 1;
        color: #90cdf4;
        font-size: 13px;

        > div {
          margin-bottom: 8px;
          width: 100%;
          overflow: hidden;
          text-overflow: ellipsis;
          white-space: nowrap;
          line-height: 1.6;

          &:last-child {
            margin-bottom: 0;
          }
        }
      }
    }

    .event-item:last-of-type {
      margin-bottom: 0 !important;
    }

    &::-webkit-scrollbar-track {
      background: rgba(19, 31, 58, 0.3);
      border-radius: 3px;
    }

    &::-webkit-scrollbar {
      -webkit-appearance: none;
      width: 6px;
      height: 6px;
    }

    &::-webkit-scrollbar-thumb {
      cursor: pointer;
      border-radius: 3px;
      background: rgba(95, 200, 223, 0.4);
      transition: all 0.2s ease;

      &:hover {
        background: rgba(95, 200, 223, 0.6);
      }
    }
  }

  .el-button {
    border-top-left-radius: 0;
    border-bottom-left-radius: 0;
    background: linear-gradient(135deg, rgba(49, 130, 206, 0.8) 0%, rgba(66, 153, 225, 0.8) 100%);
    border: 1px solid rgba(49, 130, 206, 0.6);
    color: white;
    transition: all 0.3s ease;
    font-weight: 500;

    &:hover {
      background: linear-gradient(135deg, rgba(49, 130, 206, 1) 0%, rgba(66, 153, 225, 1) 100%);
      border-color: #3182ce;
      box-shadow: 0 4px 12px rgba(49, 130, 206, 0.4);
      transform: translateY(-2px);
    }
  }
}

.empty-body {
  height: 100%;
  display: flex;
  justify-content: center;
}

.record-search {
  display: flex;

  .tree-select {
    width: calc(100% - 45px);

    &:deep(.el-input__inner) {
      border-top-right-radius: 0;
      border-bottom-right-radius: 0;
      background: linear-gradient(135deg, rgba(30, 27, 75, 0.6) 0%, rgba(79, 70, 229, 0.2) 100%);
      border: 1px solid rgba(49, 130, 206, 0.4);
      border-right: none;
      color: #90cdf4;
      font-size: 14px;
      transition: all 0.3s ease;

      &:hover {
        background: linear-gradient(135deg, rgba(30, 27, 75, 0.7) 0%, rgba(79, 70, 229, 0.3) 100%);
        border-color: rgba(49, 130, 206, 0.6);
      }

      &:focus {
        border-color: #3182ce;
        box-shadow: 0 0 12px rgba(49, 130, 206, 0.4);
      }

      &::placeholder {
        color: rgba(199, 210, 254, 0.5);
      }
    }

    &:deep(.el-select__caret) {
      color: #90cdf4;
    }
  }
}

.warn-one-body {
  position: static !important;
  width: 100% !important;
  height: auto;
  padding: 0;
  box-sizing: border-box;
}

.warn-two-body {
  position: static !important;
  width: 100% !important;
  height: auto;
  padding: 0;
  box-sizing: border-box;
}

:deep(.el-dialog) {
  background: linear-gradient(135deg, rgba(79, 70, 229, 0.88) 0%, rgba(49, 130, 206, 0.92) 100%);
  backdrop-filter: blur(20px);
  border: 1px solid rgba(49, 130, 206, 0.5);
  border-radius: 12px;
  box-shadow: 0 20px 60px rgba(49, 130, 206, 0.3);

  .el-dialog__header {
    font-size: 18px;
    color: #ffffff;
    border-bottom: 2px solid rgba(49, 130, 206, 0.4);
    padding: 15px 0;
  }

  .el-dialog__title {
    font-size: 18px;
    font-weight: 600;
    color: #ffffff;
  }

  .el-icon-close {
    font-size: 18px;
    color: rgba(255, 255, 255, 0.9);
    transition: all 0.3s ease;
    
    &:hover {
      color: #ffffff;
      transform: rotate(90deg);
    }
  }

  .info-item-title {
    color: #ffffff;
  }

  .info-item-content {
    color: #ffffff;
  }
}

.tip-dialog :deep(.el-dialog) {
  margin: 0 !important;
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);

  .el-form-item__label {
    color: rgba(255, 255, 255, 0.7) !important;
    font-weight: 500;
  }
  
  .el-form-item {
    .el-form-item__label {
      color: rgba(255, 255, 255, 0.7) !important;
    }
  }

  .el-input__inner {
    background: linear-gradient(135deg, rgba(30, 27, 75, 0.6) 0%, rgba(79, 70, 229, 0.2) 100%);
    border: 1px solid rgba(49, 130, 206, 0.4);
    color: #ffffff;
    border-radius: 8px;
    transition: all 0.3s ease;
    
    &:hover {
      border-color: rgba(49, 130, 206, 0.6);
    }
    
    &:focus {
      border-color: #3182ce;
      box-shadow: 0 0 12px rgba(49, 130, 206, 0.4);
    }
  }

  .el-dialog__footer {
    .el-button:nth-child(2) {
      background: linear-gradient(135deg, rgba(49, 130, 206, 0.3) 0%, rgba(66, 153, 225, 0.3) 100%);
      border: 1px solid rgba(49, 130, 206, 0.5);
      color: #ffffff;
      
      &:hover {
        background: linear-gradient(135deg, rgba(49, 130, 206, 0.5) 0%, rgba(66, 153, 225, 0.5) 100%);
      }
    }
  }
}

.select-area-tools {
  position: absolute;
  top: 0;
  right: 0;
  width: 40px;
  height: 100%;
  cursor: pointer;

  img {
    display: inline-block;
    position: absolute;
    top: 10px;
    right: 10px;
    width: 20px;
    height: 20px;

    &.expanded {
      display: none;
    }
  }
}

@keyframes twinkle {
  0% {
    opacity: 0.3;
  }
  50% {
    opacity: 1;
  }
  100% {
    opacity: 0.3;
  }
}

.event-image2-body {
  position: relative;
  width: calc(50% - 5px);
  height: 100%;
  .el-image {
    width: 100%;
    height: 100%;
    object-fit: contain;
    border-radius: 4px;
  }

  span {
    display: block;
    position: absolute;
    background-color: rgba(0, 0, 0, 0.5);
    top: 0;
    left: 0;
    color: red;
    font-size: 12px;
    padding: 2px 5px;
    border-top-left-radius: 4px;
  }
}

:deep(.image-slot) {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
}
</style>
