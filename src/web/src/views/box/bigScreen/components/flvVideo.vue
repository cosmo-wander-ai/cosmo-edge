<template>
  <div ref="videoContainer" class="video-container" @mouseover="handleControls('over')" @mouseleave="handleControls('leave')">
    <video v-if="isShowVideo" ref="video" class="video" muted>
      Sorry, your browser doesn't support embedded videos.
    </video>

    <div v-if="isShowLoading" class="loading-spinner"></div>

    <transition name="fade">
      <div class="video-top-control">
        <div class="camera-name">{{ t('event.currentSelected') }}{{ localeColon }}{{ cameraName }}</div>
        <div>
          <span>{{ t('event.algorithmOverlay') }}{{ localeColon }}</span>
          <el-select v-model="algorithmId" popper-class="custom-select-popper2" class="overlay-select" size="small" id="onboarding-overlay-select" @change="overlayAlgorithmChange">
            <el-option v-for="item in algorithmOverlayList" :label="resolveResourceAlgorithmName(item)" :value="item.algorithmId" :key="item.algorithmId"></el-option>
          </el-select>
        </div>
        <div>
          <img v-if="isFullScreen !== null" class="icon" src="@/assets/video_zoom_out.png" @click="handleExitFullScreen">
          <img v-else class="icon" src="@/assets/video_zoom_in.png" @click="handleFullScreen">
          <img class="icon" src="@/assets/video_close.png" @click="stopPreview">
        </div>
      </div>
    </transition>

    <div v-if="isShowStopPreview" class="stop-preview">
      <img src="@/assets/big_screen_no_camera.png" alt />
      <span>{{ t('event.noVideoSignal') }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, nextTick, watch, onBeforeUnmount, onMounted, getCurrentInstance } from 'vue'
import flvjs from 'flv.js'
import { createWhepPlayer } from '@/utils/whepPlayer'
import { t, localeColon } from '@/i18n'
import { resolveResourceAlgorithmName } from '@/utils/i18nResource'

const props = defineProps({
  channelId: String,
  runAlgorithmId: String,
  cameraName: String,
  videoCodeFormat: String,
  isOverlay: Boolean,
  index: Number,
  isFullScreen: [Number, null],
  taskList: Array
})

const emit = defineEmits(['runAlgorithmIdChange', 'stop', 'fullScreen'])

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const deviceType = localStorage.getItem('deviceType')
const player = ref(null)
const webRtcPlayer = ref(null)
const sourceUrl = ref('')
const fallbackFlvUrl = ref('')
const playbackProtocol = ref('httpflv')
const hideControl = ref(true)
const isShowStopPreview = ref(false)
const isShowVideo = ref(true)
const timers = ref(null)
const isShowLoading = ref(false)
const restartInterval = ref(null)
const algorithmId = ref('')
const algorithmOverlayList = ref([])
const flvRetryCount = ref(0)
const flvRetryTimer = ref(null)
const chaseFrameTimer = ref(null)
const loadingFallbackTimer = ref(null)
const video = ref(null)
const videoContainer = ref(null)
let _onCanPlay = null
let _onScriptData = null
let _onError = null

// 函数定义必须在 watch 之前，并按调用顺序排列
const getstreamkeepalive = () => {
  const params = {
    channelId: props.channelId,
    algorithmId: algorithmId.value
  }
  $API
    .boxStreamKeepAlive(params)
    .then(() => {})
    .catch(() => {
      console.log('error=>')
      emit('runAlgorithmIdChange', {
        channelId: '',
        runAlgorithmId: '',
        index: props.index
      })
      clearInterval(timers.value)
    })
}

const sendHeartBeat = (time) => {
  timers.value && clearInterval(timers.value)
  timers.value = setInterval(() => {
    getstreamkeepalive()
  }, time)
}

const getrequestLiveStream = () => {
  // 停掉旧的 viewer（切换算法/通道时释放后端编码器资源）
  if (player.value) {
    stopRequest()
  }
  algorithmId.value = props.runAlgorithmId
  flvRetryCount.value = 0
  const domain = window.location.hostname
  const streamScheme = window.location.protocol === 'https:' ? 'https' : 'http'
  const params = {
    channelId: props.channelId,
    algorithmId: algorithmId.value
  }
  console.log(props.runAlgorithmId, '====runAlgorithmId====', params)

  $API
    .boxRequestLiveStream(params)
    .then((res) => {
      const { resData } = res
      if (resData) {
        const stream = resData.stream
        playbackProtocol.value = stream.protocol || 'httpflv'
        fallbackFlvUrl.value = stream.flvUrl
          ? `${streamScheme}://${domain}:${stream.httpPort || 8080}${stream.flvUrl}`
          : `${streamScheme}://${domain}:${stream.port}${stream.url}`
        sourceUrl.value = stream.protocol === 'webrtc'
          ? `${streamScheme}://${domain}:${stream.rtcApiPort || stream.port || 1985}${stream.webrtcUrl || stream.url}`
          : `${streamScheme}://${domain}:${stream.httpPort || stream.port}${stream.flvUrl || stream.url}`
        // sourceUrl.value = 'http://192.168.23.82:' + resData.stream.port +  resData.stream.url
        console.log('视频预览地址==>', sourceUrl.value, '--------')
        sendHeartBeat(
          stream.keepAliveInterval * 1000,
          stream.keepAliveUrl
        )
      }
    })
    .catch((e) => {
      if (e?.resMsg[0].msgCode == '12303') {
        emit('runAlgorithmIdChange', {
          channelId: '',
          runAlgorithmId: '',
          index: props.index
        })
      } else {
        emit('runAlgorithmIdChange', {
          channelId: props.channelId,
          runAlgorithmId: '',
          index: props.index
        })
      }
    })
}

watch(sourceUrl, (val, oldVal) => {
  if (val != oldVal) {
    playVideo()
    const filterList = props.taskList.filter(
      (item) => item.enableStatus == 1
    )
    algorithmOverlayList.value = [
      {
        algorithmId: '',
        algorithmName: t('common.none')
      },
      ...filterList
    ]
  }
})

watch(() => props.channelId, () => {
  isShowStopPreview.value = false
  getrequestLiveStream()
}, { immediate: true })

watch(() => props.runAlgorithmId, (newVal) => {
  algorithmId.value = newVal
  getrequestLiveStream()
  console.log('====runAlgorithmId====', algorithmId.value)
})

const getRestartTime = () => {
  return Math.floor((Math.random() * (6 - 3) + 3) * 60 * 60 * 1000)
}

const playVideo = () => {
  if (playbackProtocol.value === 'webrtc') {
    playWebRtc()
    return
  }
  playFlv(sourceUrl.value)
}

const playFallbackFlv = (reason) => {
  if (!fallbackFlvUrl.value) {
    isShowLoading.value = false
    proxy.$message.error(reason?.message || t('event.webRtcConnectionFailed'))
    return
  }

  console.warn('[Video] WebRTC failed, fallback to HTTP-FLV:', reason, fallbackFlvUrl.value)
  playbackProtocol.value = 'httpflv'
  flvRetryCount.value = 0
  playFlv(fallbackFlvUrl.value)
}

const playWebRtc = () => {
  if (!window.RTCPeerConnection) {
    playFallbackFlv(new Error('Browser does not support WebRTC'))
    return
  }
  isShowLoading.value = true
  destroyVideo()
  isShowVideo.value = true
  webRtcPlayer.value = createWhepPlayer({
    video: video.value,
    url: sourceUrl.value,
    onConnected: () => {
      isShowLoading.value = false
      flvRetryCount.value = 0
    },
    onError: (error) => {
      console.log('==========WebRTC ERROR ============', error)
      webRtcPlayer.value && webRtcPlayer.value.stop()
      webRtcPlayer.value = null
      playFallbackFlv(error)
    }
  })
  webRtcPlayer.value.start()
}

const playFlv = async (url) => {
  if (!flvjs.isSupported()) {
    proxy.$message.error(t('event.videoPreviewUnsupported'))
    return
  }

  isShowLoading.value = true

  destroyVideo()
  isShowVideo.value = true

  // 等待 Vue DOM 更新，确保新的 video 元素已挂载
  await nextTick()

  const videoEl = video.value
  if (!videoEl) {
    console.warn('video element not found after nextTick')
    return
  }

  player.value = flvjs.createPlayer(
    {
      type: 'flv',
      isLive: true,
      url: url || sourceUrl.value,
      hasAudio: false,
      hasVideo: true,
      loadStartTimeout: 10000
    },
    {
      enableStashBuffer: false,
      autoCleanupSourceBuffer: true
    }
  )
  player.value.attachMediaElement(videoEl)

  // 等待媒体数据就绪后再播放，同时隐藏 loading
  _onCanPlay = () => {
    videoEl.removeEventListener('canplay', _onCanPlay)
    _onCanPlay = null
    isShowLoading.value = false
    if (player.value) {
      player.value.play()
    }
  }
  videoEl.addEventListener('canplay', _onCanPlay)

  player.value.load()

  // 追帧：延迟超过 3s 时跳到最新帧
  if (chaseFrameTimer.value) {
    clearInterval(chaseFrameTimer.value)
  }
  chaseFrameTimer.value = setInterval(() => {
    if (!videoEl.buffered.length) {
      return
    }
    let end = videoEl.buffered.end(0)
    let diff = end - videoEl.currentTime
    if (diff >= 3) {
      videoEl.currentTime = end
    }
  }, 3000)

  // SCRIPTDATA_ARRIVED 作为辅助隐藏 loading
  _onScriptData = () => {
    isShowLoading.value = false
    if (player.value) {
      player.value.off(flvjs.Events.SCRIPTDATA_ARRIVED, _onScriptData)
    }
    _onScriptData = null
  }
  player.value.on(flvjs.Events.SCRIPTDATA_ARRIVED, _onScriptData)

  // 兜底超时：10s 后无论如何隐藏 loading
  if (loadingFallbackTimer.value) {
    clearTimeout(loadingFallbackTimer.value)
  }
  loadingFallbackTimer.value = setTimeout(() => {
    if (isShowLoading.value) {
      console.warn('Loading fallback timeout triggered')
      isShowLoading.value = false
    }
  }, 10000)

  // FLV 错误时指数退避重试
  _onError = (e) => {
    console.log('==========FLV ERROR ============', e)
    const maxRetry = 3
    if (flvRetryCount.value < maxRetry) {
      const delay = flvRetryCount.value === 0 ? 300 : Math.pow(2, flvRetryCount.value) * 1000
      flvRetryCount.value++
      console.log(`FLV retry ${flvRetryCount.value}/${maxRetry} in ${delay}ms`)
      flvRetryTimer.value = setTimeout(() => {
        playVideo()
      }, delay)
    } else {
      flvRetryCount.value = 0
      setTimeout(() => {
        getrequestLiveStream()
      }, 5000)
    }
  }
  player.value.on(flvjs.Events.ERROR, _onError)
}

const destroyVideo = () => {
  if (webRtcPlayer.value) {
    webRtcPlayer.value.stop()
    webRtcPlayer.value = null
  }
  if (flvRetryTimer.value) {
    clearTimeout(flvRetryTimer.value)
    flvRetryTimer.value = null
  }
  if (chaseFrameTimer.value) {
    clearInterval(chaseFrameTimer.value)
    chaseFrameTimer.value = null
  }
  if (loadingFallbackTimer.value) {
    clearTimeout(loadingFallbackTimer.value)
    loadingFallbackTimer.value = null
  }
  if (_onCanPlay) {
    const videoEl = video.value
    videoEl && videoEl.removeEventListener('canplay', _onCanPlay)
    _onCanPlay = null
  }
  if (player.value) {
    if (_onScriptData) {
      player.value.off(flvjs.Events.SCRIPTDATA_ARRIVED, _onScriptData)
      _onScriptData = null
    }
    if (_onError) {
      player.value.off(flvjs.Events.ERROR, _onError)
      _onError = null
    }
    player.value.pause()
    player.value.unload()
    player.value.detachMediaElement()
    player.value.destroy()
    player.value = null
    isShowVideo.value = false
  }
}

const stop = () => {
  isShowLoading.value = false
  isShowStopPreview.value = true
  timers.value && clearInterval(timers.value)
  destroyVideo()
  stopRequest()
}

const stopPreview = () => {
  stop()
  emit('stop', props.index)
}

const stopRequest = () => {
  const params = {
    channelId: props.channelId,
    algorithmId: algorithmId.value
  }
  $API.boxStreamStop(params).then(() => {})
}

const overlayAlgorithmChange = (val) => {
  emit('runAlgorithmIdChange', {
    channelId: props.channelId,
    runAlgorithmId: val,
    index: props.index
  })
}

const handleControls = (flag) => {
  hideControl.value = flag === 'leave'
}

const handleFullScreen = () => {
  emit('fullScreen', {
    index: props.index,
    isFullScreen: true
  })
}

const handleExitFullScreen = () => {
  emit('fullScreen', {
    index: props.index,
    isFullScreen: false
  })
}

onMounted(() => {
  restartInterval.value = setInterval(() => {
    stop()
    playVideo()
  }, getRestartTime())
})

onBeforeUnmount(() => {
  if (restartInterval.value) {
    clearInterval(restartInterval.value)
    restartInterval.value = null
  }
  stop()
})
</script>

<style lang="scss" scoped>
.video-container {
  width: 100%;
  height: 100%;
  position: relative;
  background: #212839;
  border: 1px solid rgba(95, 200, 223, 0.2);
  box-sizing: border-box;

  .video {
    width: 100%;
    height: 100%;
    object-fit: fill;
    background-color: #000;
  }

  .video-top-control {
    position: absolute;
    top: 0;
    width: 100%;
    height: 36px;
    left: 0;
    background: linear-gradient(180deg, rgba(19, 31, 58, 0.9) 0%, rgba(21, 35, 69, 0.7) 100%);
    backdrop-filter: blur(4px);
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 10px;
    box-sizing: border-box;
    color: #94d0ff;
    font-size: 13px;
    border-bottom: 1px solid rgba(95, 200, 223, 0.15);
    z-index: 10;

    .icon {
      margin-left: 8px;
      width: 20px;
      height: 20px;
      cursor: pointer;
      transition: all 0.2s;
      opacity: 0.85;

      &:hover {
        opacity: 1;
        transform: scale(1.1);
      }
    }
  }
}

.fade-enter-active {
  transition: all 0.2s;
}
.fade-leave-active {
  transition: all 0.8s;
}
.fade-enter-from,
.fade-leave-to {
  visibility: hidden;
  opacity: 0;
}

.stop-preview {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  display: flex;
  flex-flow: column;
  align-items: center;
  justify-content: center;
  font-size: 16px;
  color: #94d0ff;
  background: linear-gradient(180deg, #1a2540 0%, #212839 100%);

  img {
    margin-bottom: 20px;
    width: 80px;
    opacity: 0.6;
  }

  span {
    font-size: 14px;
    color: #6b7a99;
  }
}

$primary-background-color: #131f3a !default;
$primary-background-transparency: 0.85 !default;
$secondary-background-color: #5fc8df !default;
$secondary-background-transparency: 0.6 !default;

.loading-spinner {
  position: absolute;
  top: 50%;
  left: 50%;
  margin: -30px 0 0 -30px;
  opacity: 0.9;
  text-align: left;
  border: 4px solid rgba($primary-background-color, $primary-background-transparency);
  box-sizing: border-box;
  background-clip: padding-box;
  width: 60px;
  height: 60px;
  border-radius: 30px;
  visibility: hidden;
  animation: spinner-show 0s linear 0.2s forwards;
}

.loading-spinner:before,
.loading-spinner:after {
  content: '';
  position: absolute;
  margin: -4px;
  box-sizing: inherit;
  width: inherit;
  height: inherit;
  border-radius: inherit;
  opacity: 1;
  border: inherit;
  border-color: transparent;
  border-top-color: $secondary-background-color;
  animation: spinner-spin 1s cubic-bezier(0.6, 0.2, 0, 0.8) infinite, spinner-fade 1s linear infinite;
}

@keyframes spinner-show {
  to {
    visibility: visible;
  }
}

@keyframes spinner-spin {
  100% {
    transform: rotate(360deg);
  }
}

@keyframes spinner-fade {
  0% {
    border-top-color: rgba($secondary-background-color, 0.3);
  }
  20% {
    border-top-color: rgba($secondary-background-color, 0.5);
  }
  35% {
    border-top-color: $secondary-background-color;
  }
  60% {
    border-top-color: rgba($secondary-background-color, 0.5);
  }
  100% {
    border-top-color: rgba($secondary-background-color, 0.3);
  }
}

.overlay-select {
  width: 130px;
  :deep(.el-input__inner) {
    background: rgba(19, 31, 58, 0.6);
    color: #94d0ff;
    border: 1px solid rgba(95, 200, 223, 0.2);
    font-size: 12px;
    height: 28px;
    line-height: 28px;
    
    &:hover {
      border-color: rgba(95, 200, 223, 0.4);
    }
    
    &:focus {
      border-color: #5fc8df;
    }
  }

  :deep(.el-select__caret) {
    color: #94d0ff !important;
  }
}

.camera-name {
  max-width: 40%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-weight: 500;
  color: #fff;
}
</style>

<style lang="scss">
.custom-select-popper2 {
  border: 1px solid rgba(95, 200, 223, 0.4);
  background: linear-gradient(180deg, rgba(19, 31, 58, 0.98) 0%, rgba(21, 35, 69, 0.98) 100%);
  backdrop-filter: blur(8px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.6);

  .popper__arrow {
    display: none;
  }

  .el-select-dropdown__item {
    color: #94d0ff;
    font-size: 13px;
    padding: 0 16px;
    height: 36px;
    line-height: 36px;
    background: transparent;
    transition: all 0.2s;
    
    &.selected {
      color: #5fc8df;
      font-weight: 500;
      background: rgba(95, 200, 223, 0.2);
    }

    &.hover,
    &:hover {
      background: rgba(95, 200, 223, 0.15) !important;
      color: #5fc8df;
    }
  }

  .el-scrollbar {
    background: transparent;
  }

  .el-scrollbar__wrap {
    margin-right: -17px !important;
  }

  .el-select-dropdown__list {
    padding: 4px 0;
  }
}
</style>
