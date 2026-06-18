<template>
  <div ref="videoContainerRef" class="video-container" @mouseover="handleControls('over')" @mouseleave="handleControls('leave')">
    <video v-if="isShowVideo" ref="videoRef" class="video" muted>
      Sorry, your browser doesn't support embedded videos.
    </video>

    <div v-if="isShowLoading" class="loading-spinner"></div>

    <transition v-if="isShowVideo" name="fade">
      <div class="video-top-control" v-show="!hideControl">
        <el-icon class="icon" @click="stopPreview"><Close /></el-icon>
      </div>
    </transition>

    <transition v-if="isShowVideo" name="fade">
      <div class="video-bottom-control" v-show="!hideControl">
        <el-icon v-if="isFullScreen" class="icon" @click="handleExitFullScreen"><CopyDocument /></el-icon>
        <el-icon v-else class="icon" @click="handleFullScreen"><FullScreen /></el-icon>
      </div>
    </transition>

    <div v-if="isShowStopPreview" class="stop-preview">
      <img src="@/assets/video_preview.png">
      <p>{{ t('common.stopPreview') }}</p>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import { Close, CopyDocument, FullScreen } from '@element-plus/icons-vue'
import flvjs from 'flv.js'
import { createWhepPlayer } from '@/utils/whepPlayer'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()

// Props
const props = defineProps({
  channelId: [String, Number],
  isOverlay: Boolean,
  taskType: [String, Number]
})

// Emits
const emit = defineEmits(['stop'])

// Refs
const videoContainerRef = ref(null)
const videoRef = ref(null)

// Reactive data
const player = ref(null)
const webRtcPlayer = ref(null)
const sourceUrl = ref('')
const fallbackFlvUrl = ref('')
const playbackProtocol = ref('httpflv')
const hideControl = ref(true)
const isFullScreen = ref(false)
const isShowStopPreview = ref(false)
const isShowVideo = ref(true)
const timers = ref(null)
const chaseFrameTimer = ref(null)
const isShowLoading = ref(false)
const flvRetryCount = ref(0)
const flvRetryTimer = ref(null)
let _onCanPlay = null

// Computed
const streamType = computed(() => {
  return props.isOverlay ? 1 : 0
})

// Watchers
watch(sourceUrl, (val, oldVal) => {
  console.log('val = ' + val + ' , oldValue = ' + oldVal)
  if (val != oldVal) {
    playVideo()
  }
})

watch(() => props.channelId, () => {
  isShowStopPreview.value = false
  getrequestLiveStream()
}, { immediate: true })

watch(() => props.isOverlay, () => {
  isShowStopPreview.value = false
  getrequestLiveStream()
})

watch(() => props.taskType, () => {
  isShowStopPreview.value = false
  getrequestLiveStream()
})

// Methods
const dobounce = (fn) => {
  let timer = null
  return function () {
    timer && clearTimeout(timer)
    timer = setTimeout(() => {
      fn.apply(this, arguments)
    }, 100)
  }
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
    video: videoRef.value,
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

const playFlv = (url) => {
  if (flvjs.isSupported()) {
    isShowLoading.value = true

    const video = videoRef.value
    // 切换视频流，先将播放器销毁
    destroyVideo()
    isShowVideo.value = true
    player.value = flvjs.createPlayer(
      {
        type: 'flv',
        isLive: true,
        url,
        hasAudio: false,
        hasVideo: true
      },
      {
        enableStashBuffer: false
      }
    )
    player.value.attachMediaElement(video)

    // 等待媒体数据就绪后再播放，避免 play() 在数据未到达时被 pause() 中断导致 AbortError
    _onCanPlay = () => {
      video.removeEventListener('canplay', _onCanPlay)
      _onCanPlay = null
      if (player.value) {
        player.value.play()
      }
    }
    video.addEventListener('canplay', _onCanPlay)

    player.value.load()

    // 追帧：延迟超过3s时跳到最新帧，降低播放端延迟
    if (chaseFrameTimer.value) {
      clearInterval(chaseFrameTimer.value)
    }
    chaseFrameTimer.value = setInterval(() => {
      if (!video.buffered.length) {
        return
      }
      let end = video.buffered.end(0)
      let diff = end - video.currentTime
      if (diff >= 3) {
        video.currentTime = end
      }
    }, 3000)

    // 通过监听 SCRIPTDATA_ARRIVED 事件，延时隐藏 loading
    player.value.on(flvjs.Events.SCRIPTDATA_ARRIVED, () => {
      setTimeout(() => {
        isShowLoading.value = false
      }, 1000)

      player.value.off(flvjs.Events.SCRIPTDATA_ARRIVED, () => {})
    })

    // FLV 错误时指数退避重试
    player.value.on(flvjs.Events.ERROR, (e) => {
      console.log('==========FLV ERROR ============', e)
      const maxRetry = 3
      if (flvRetryCount.value < maxRetry) {
        const delay = Math.pow(2, flvRetryCount.value) * 1000
        flvRetryCount.value++
        console.log(`FLV retry ${flvRetryCount.value}/${maxRetry} in ${delay}ms`)
        flvRetryTimer.value = setTimeout(() => {
          playFlv(url)
        }, delay)
      } else {
        flvRetryCount.value = 0
        setTimeout(() => {
          getrequestLiveStream()
        }, 5000)
      }
    })
  } else {
    proxy.$message.error(t('common.browserVideoUnsupported'))
  }
}

const destroyVideo = () => {
  if (webRtcPlayer.value) {
    webRtcPlayer.value.stop()
    webRtcPlayer.value = null
  }
  if (chaseFrameTimer.value) {
    clearInterval(chaseFrameTimer.value)
    chaseFrameTimer.value = null
  }
  if (flvRetryTimer.value) {
    clearTimeout(flvRetryTimer.value)
    flvRetryTimer.value = null
  }
  if (_onCanPlay) {
    const video = videoRef.value
    video && video.removeEventListener('canplay', _onCanPlay)
    _onCanPlay = null
  }
  if (player.value) {
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
  clearInterval(timers.value)
  destroyVideo()
}

const stopPreview = () => {
  stop()
  emit('stop')
}

// 视频预览
const getrequestLiveStream = () => {
  let params = {
    channelId: props.channelId
  }

  proxy.$API
    .boxRequestLiveStream(params)
    .then((res) => {
      if (res) {
        const stream = res.resData.stream
        const domain = window.location.hostname
        const streamScheme = window.location.protocol === 'https:' ? 'https' : 'http'
        playbackProtocol.value = stream.protocol || 'httpflv'
        fallbackFlvUrl.value = stream.flvUrl
          ? `${streamScheme}://${domain}:${stream.httpPort || 8080}${stream.flvUrl}`
          : stream.url
        sourceUrl.value = stream.protocol === 'webrtc'
          ? `${streamScheme}://${domain}:${stream.rtcApiPort || stream.port || 1985}${stream.webrtcUrl || stream.url}`
          : (stream.flvUrl || stream.url).endsWith('.flv')
            ? `${streamScheme}://${domain}:${stream.httpPort || stream.port || 8080}${stream.flvUrl || stream.url}`
            : stream.url
        console.log('视频预览地址==>', sourceUrl.value)
        sendHeartBeat(
          stream.keepAliveInterval * 1000,
          stream.keepAliveUrl
        ) // 维持心跳
      }
    })
    .catch(() => {
      // loading.value = false
    })
}

// 视频心跳接口
const getstreamkeepalive = () => {
  const param = {
    channelId: props.channelId
    // algorithmId: algorithmId.value
    // streamType: streamType.value
  }
  proxy.$API
    .boxStreamKeepAlive(param)
    .then((res) => {
      if (res) {
        console.log('==BangBang==')
      }
    })
    .catch((e) => {
      console.log('error=>', e)
      // clearInterval(timers.value)
    })
}

// 每隔时长发送心跳
const sendHeartBeat = (time, url) => {
  timers.value && clearInterval(timers.value)
  timers.value = setInterval(() => {
    getstreamkeepalive(url)
  }, time)
}

// 监听离开或者进入视频区域隐藏或者展示控制栏
const handleControls = (flag) => {
  hideControl.value = flag === 'leave'
}

const handleFullScreen = () => {
  const dom = videoContainerRef.value
  if (dom.requestFullscreen) {
    dom.requestFullscreen()
  } else if (dom.mozRequestFullScreen) {
    dom.mozRequestFullScreen()
  } else if (dom.webkitRequestFullScreen) {
    dom.webkitRequestFullScreen()
  }
}

const handleExitFullScreen = () => {
  if (document.exitFullscreen) {
    document.exitFullscreen()
  } else if (document.mozCancelFullScreen) {
    document.mozCancelFullScreen()
  } else if (document.webkitCancelFullScreen) {
    document.webkitCancelFullScreen()
  }
}

const checkFullScreen = () => {
  const isFull =
    document.fullscreenElement ||
    document.mozFullScreenElement ||
    document.webkitFullscreenElement

  return !!isFull
}

// Lifecycle hooks
onMounted(() => {
  // 由于全屏状态下无法监听 onkeydown 事件，所以通过 onresize 来监听 ESC 退出全屏事件
  window.onresize = dobounce(() => {
    isFullScreen.value = checkFullScreen()
  })
  document.onvisibilitychange = () => {
    if (!document.hidden) {
      getrequestLiveStream()
    }
  }
})

onBeforeUnmount(() => {
  stop()
  document.onvisibilitychange = null
})
</script>

<style lang="scss" scoped>
.video-container {
  flex: 1;
  position: relative;
  width: 100%;
  height: 100%;

  .video {
    width: 100%;
    height: 100%;
    background-color: rgb(0, 0, 0);
    object-fit: fill;
  }

  .video-top-control,
  .video-bottom-control {
    position: absolute;
    width: 100%;
    height: 30px;
    left: 0;
    background-color: rgba(43, 51, 63, 0.7);
    display: flex;
    align-items: center;
    justify-content: flex-end;

    .icon {
      font-size: 16px;
      color: #fff;
      margin-right: 20px;
    }
  }

  .video-top-control {
    top: 0;
  }

  .video-bottom-control {
    bottom: 0;
  }
}

/* 控制栏隐藏动画 */
.fade-enter-active {
  transition: all 0.1s;
}
.fade-leave-active {
  transition: all 1s;
}
.fade-enter,
.fade-leave-to {
  visibility: hidden;
  opacity: 0;
}

.stop-preview {
  width: 100%;
  height: 100%;
  background: #fff;
  display: flex;
  flex-flow: column;
  justify-content: center;
  align-items: center;

  img {
    width: 142px;
    height: 123px;
  }
  p {
    margin: 0 0;
    color: #3598ff;
    font-size: 14px;
    letter-spacing: 2px;
    margin-top: 10px;
  }
}

$primary-background-color: #2b333f !default;
$primary-background-transparency: 0.7 !default;
$secondary-background-color: #2b333f !default;
$secondary-background-transparency: 0.5 !default;

.loading-spinner {
  position: absolute;
  top: 50%;
  left: 50%;
  margin: -25px 0 0 -25px;
  opacity: 0.85;
  text-align: left;
  border: 6px solid
    rgba($primary-background-color, $primary-background-transparency);
  box-sizing: border-box;
  background-clip: padding-box;
  width: 50px;
  height: 50px;
  border-radius: 25px;
  visibility: hidden;
  animation: spinner-show 0s linear 0.3s forwards;
}

.loading-spinner:before,
.loading-spinner:after {
  content: '';
  position: absolute;
  margin: -6px;
  box-sizing: inherit;
  width: inherit;
  height: inherit;
  border-radius: inherit;
  // Keep 100% opacity so they don't show through each other
  opacity: 1;
  border: inherit;
  border-color: transparent;
  border-top-color: white;
  animation: spinner-spin 1.1s cubic-bezier(0.6, 0.2, 0, 0.8) infinite,
    spinner-fade 1.1s linear infinite;
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
    border-top-color: $secondary-background-color;
  }
  20% {
    border-top-color: $secondary-background-color;
  }
  35% {
    border-top-color: white;
  }
  60% {
    border-top-color: $secondary-background-color;
  }
  100% {
    border-top-color: $secondary-background-color;
  }
}
</style>
