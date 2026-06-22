<template>
  <el-dialog 
    v-if="useDialog" 
    modal-append-to-body 
    append-to-body 
    :model-value="selfVisible" 
    :width="width + 'px'" 
    :before-close="beforeClose" 
    :close-on-click-modal="closable" 
    @opened="init"
  >
    <template #header>
      <div class="mini-video-play-header">
        <div class="mini-video-play-title">{{ props.title || t('event.videoPlayback') }}</div>
        <el-button 
          class="mini-video-play-download" 
          type="primary" 
          size="small" 
          @click="downloadVideo"
        >
          {{ t('action.videoDownload') }}
        </el-button>
      </div>
    </template>

    <div ref="wrapRef" class="mini-video-play-wrap" :style="{ height: height + 'px' }">
      <p class="isBoolean" v-if="isBoolean">{{ serviceTitle }}</p>
      
      <!-- 加载 -->
      <div class="mini-video-play-loading-container" v-if="loading">
        <div class="mini-video-play-loading">
          <div class="mini-video-play-loading-el" data-spin-1>
            <div data-circle-1></div>
            <div data-circle-2></div>
            <div data-circle-3></div>
            <div data-circle-4></div>
          </div>
          <div class="mini-video-play-loading-el" data-spin-2>
            <div data-circle-1></div>
            <div data-circle-2></div>
            <div data-circle-3></div>
            <div data-circle-4></div>
          </div>
          <div class="mini-video-play-loading-el" data-spin-3>
            <div data-circle-1></div>
            <div data-circle-2></div>
            <div data-circle-3></div>
            <div data-circle-4></div>
          </div>
        </div>
      </div>
      
      <!-- 重播按钮 -->
      <div class="mini-video-play-replay-container" v-if="showReplay">
        <div class="mini-video-play-replay playButton" @click="init">
          <img src="@/assets/play.png" />
        </div>
      </div>
    </div>
  </el-dialog>

  <div v-else>
    <div 
      ref="wrapRef" 
      class="mini-video-play-wrap" 
      :style="{ width: width + 'px', height: height + 'px' }"
    >
      <!-- 加载 -->
      <div class="mini-video-play-loading-container" v-if="loading">
        <div class="mini-video-play-loading">
          <div class="mini-video-play-loading-el" data-spin-1>
            <div data-circle-1></div>
            <div data-circle-2></div>
            <div data-circle-3></div>
            <div data-circle-4></div>
          </div>
          <div class="mini-video-play-loading-el" data-spin-2>
            <div data-circle-1></div>
            <div data-circle-2></div>
            <div data-circle-3></div>
            <div data-circle-4></div>
          </div>
          <div class="mini-video-play-loading-el" data-spin-3>
            <div data-circle-1></div>
            <div data-circle-2></div>
            <div data-circle-3></div>
            <div data-circle-4></div>
          </div>
        </div>
      </div>
      
      <!-- 重播按钮 -->
      <div class="mini-video-play-replay-container" v-if="showReplay">
        <div class="mini-video-play-replay" @click="init">
          <img src="@/assets/play.png" />
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, onBeforeUnmount } from 'vue'
import { t } from '@/i18n'

const props = defineProps({
  // 是否使用弹窗
  useDialog: {
    type: Boolean,
    default: true
  },
  // 是否显示
  visible: {
    type: Boolean,
    default: false
  },
  // 标题
  title: {
    type: String,
    default: ''
  },
  // 是否点击遮罩层关闭
  closable: {
    type: Boolean,
    default: true
  },
  // 视频容器宽度
  width: {
    type: Number,
    default: 800
  },
  // 视频容器高度
  height: {
    type: Number,
    default: 450
  },
  // 播放的视频url
  url: {
    type: String,
    default: ''
  },
  // 视频叠加结构化数据url
  structureDataUrl: {
    type: String,
    default: null
  },
  // 高空抛物使用的叠加结构化数据
  highVideoPointList: {
    type: Object,
    default: null
  },
  // 算法code
  algorithmCode: {}
})

const emit = defineEmits(['update:visible'])

const wrapRef = ref(null)
const videoRatio = ref(1920 / 1080)
const frames = ref(40)
const showReplay = ref(false)
const loading = ref(false)
const duration = ref(0)
const currentClip = ref(0)
const timer = ref(null)
const alarmVideoPointList = ref({})
const elVideo = ref(null)
const elCanvas = ref(null)
const isBoolean = ref(false)
const serviceTitle = ref('')
const selfVisible = ref(false)

const totalClips = ref(0)

// 初始化
const init = () => {
  loading.value = true
  showReplay.value = false
  createVideo()
  createCanvas()
  
  if (props.algorithmCode) {
    serviceTitle.value = ''
  }
  
  if (elVideo.value) {
    elVideo.value.src = props.url
  }
}

// 关闭
const close = (replay = false) => {
  destroyVideo()
  destroyCanvas()
  
  if (timer.value) {
    clearInterval(timer.value)
    timer.value = null
  }
  
  isBoolean.value = false
  showReplay.value = !!replay
}

// 创建视频元素
const createVideo = () => {
  if (elVideo.value) return
  
  elVideo.value = document.createElement('video')
  elVideo.value.muted = 'muted'
  elVideo.value.width = props.width
  elVideo.value.height = props.height
  elVideo.value.style.width = `${props.width}px`
  elVideo.value.style.height = `${props.height}px`
  elVideo.value.style.objectFit = 'contain'
  elVideo.value.style.backgroundColor = '#000'
  elVideo.value.style.opacity = 0
  elVideo.value.src = ''
  
  if (wrapRef.value) {
    wrapRef.value.appendChild(elVideo.value)
  }
  
  elVideo.value.onloadedmetadata = onLoaded
  elVideo.value.onended = onEnded
  elVideo.value.onerror = onError
}

// 销毁视频元素
const destroyVideo = () => {
  if (!elVideo.value) return
  
  if (!elVideo.value.paused) {
    elVideo.value.pause()
  }
  
  elVideo.value.onloadedmetadata = null
  elVideo.value.onended = null
  elVideo.value.onerror = null
  elVideo.value.src = ''
  elVideo.value.remove()
  elVideo.value = null
}

// 创建画布元素
const createCanvas = () => {
  if (elCanvas.value) return
  
  elCanvas.value = document.createElement('canvas')
  elCanvas.value.width = props.width
  elCanvas.value.height = props.height
  elCanvas.value.style.width = `${props.width}px`
  elCanvas.value.style.height = `${props.height}px`
  elCanvas.value.style.position = 'absolute'
  elCanvas.value.style.left = 0
  elCanvas.value.style.top = 0
  elCanvas.value.style.zIndex = 10
  
  if (wrapRef.value) {
    wrapRef.value.appendChild(elCanvas.value)
  }
}

// 销毁画布元素
const destroyCanvas = () => {
  if (!elCanvas.value) return
  
  const ctx = elCanvas.value.getContext('2d')
  ctx.clearRect(0, 0, props.width, props.height)
  elCanvas.value.remove()
  elCanvas.value = null
}

// 弹窗关闭前
const beforeClose = (done) => {
  console.log('------------视频播放弹窗关闭之前------------')
  close()
  emit('update:visible', false)
  done()
}

// 监听视频加载完成
const onLoaded = async () => {
  console.log('------------视频加载完成------------', props.structureDataUrl)
  loading.value = false
  
  if (elVideo.value) {
    elVideo.value.style.opacity = 1
    duration.value = elVideo.value.duration
  }
  
  await getAlarmVideoPointList()
  
  if (elCanvas.value) {
    const ctx = elCanvas.value.getContext('2d')
    ctx.clearRect(0, 0, props.width, props.height)
    
    // 画区域
    if (alarmVideoPointList.value.area !== undefined) {
      drawArea(
        ctx,
        alarmVideoPointList.value.area.points,
        alarmVideoPointList.value.area.rgb
      )
      
      // 离岗双框
      if (
        alarmVideoPointList.value.area.associatedAreas &&
        alarmVideoPointList.value.area.associatedAreas[0]
      ) {
        drawArea(
          ctx,
          alarmVideoPointList.value.area.associatedAreas[0].points,
          alarmVideoPointList.value.area.rgb
        )
      }
      
      if (alarmVideoPointList.value.area.linePoints) {
        drawPolyline(
          alarmVideoPointList.value.area.linePoints,
          alarmVideoPointList.value.area.directionType,
          ctx,
          alarmVideoPointList.value.area.rgb
        )
      }
    }
    
    // 设置定时器
    currentClip.value = 0
    totalClips.value = Math.floor(25 * duration.value)
    timer.value = setInterval(() => {
      currentClip.value += 1
      if (props.structureDataUrl && alarmVideoPointList.value.targets) {
        alarmVideoPointList.value.targets.forEach((item) => {
          if (currentClip.value === item.index && item.rects.length !== 0) {
            ctx.strokeStyle = '#ff0000'
            ctx.beginPath()
            ctx.clearRect(0, 0, props.width, props.height)
            
            // 区域框
            drawArea(
              ctx,
              alarmVideoPointList.value.area.points,
              alarmVideoPointList.value.area.rgb
            )
            
            item.rects.forEach((rect) => {
              const axisPoint = transScaleToAxis(rect)
              ctx.beginPath()
              ctx.lineWidth = 2
              ctx.strokeStyle = 'red'
              ctx.rect(
                axisPoint.xAxis,
                axisPoint.yAxis,
                axisPoint.wAxis,
                axisPoint.hAxis
              )
              ctx.stroke()
            })
          }
        })
      }
    }, frames.value)
  }
  
  isBoolean.value = true
  
  if (elVideo.value) {
    elVideo.value.play()
  }
}

// 监听播放结束
const onEnded = () => {
  console.log('------------视频播放结束------------')
  close(true)
}

// 监听出错
const onError = () => {
  console.log('------------视频播放出错------------')
}

// 获取规则框数据
const getAlarmVideoPointList = async () => {
  if (!props.structureDataUrl) return
  
  const data = await fetchGet(props.structureDataUrl)
  alarmVideoPointList.value = data || {}
}

// 缩放转坐标
const transScaleToAxis = (point) => {
  const videoSize = getVideoSize()
  const offsetWidth = Math.abs(props.width - videoSize.width) / 2
  const offsetHeight = Math.abs(props.height - videoSize.height) / 2
  
  return {
    xAxis: point.xRatio * videoSize.width + offsetWidth,
    yAxis: point.yRatio * videoSize.height + offsetHeight,
    wAxis: point.wRatio ? point.wRatio * videoSize.width : 0,
    hAxis: point.hRatio ? point.hRatio * videoSize.height : 0
  }
}

// 获取视频宽高
const getVideoSize = () => {
  const res = props.width / props.height
  if (res < videoRatio.value) {
    return {
      width: props.width,
      height: Number((props.width / videoRatio.value).toFixed(2))
    }
  } else {
    return {
      width: Number((videoRatio.value * props.height).toFixed(2)),
      height: props.height
    }
  }
}

// 区域框
const drawArea = (ctx, areaList, rgb) => {
  ctx.beginPath()
  ctx.lineWidth = 2
  ctx.strokeStyle = rgb ? `rgb(${rgb})` : 'blue'
  
  areaList.forEach((item, itemIndex) => {
    const axisPoint = transScaleToAxis(item)
    if (itemIndex === 0) {
      ctx.moveTo(axisPoint.xAxis, axisPoint.yAxis)
    } else if (itemIndex > 0 && itemIndex < areaList.length) {
      ctx.lineTo(axisPoint.xAxis, axisPoint.yAxis)
    }
  })
  
  ctx.closePath()
  ctx.stroke()
}

// 拌线
const drawPolyline = (points, directionType, ctx, rgb) => {
  ctx.beginPath()
  ctx.strokeStyle = rgb ? `rgb(${rgb})` : '#1890ff'
  ctx.lineWidth = 2
  
  for (let i = 0; i < points.length; i++) {
    const point = transScaleToAxis(points[i])
    if (!i) {
      ctx.moveTo(point.xAxis, point.yAxis)
    } else {
      ctx.lineTo(point.xAxis, point.yAxis)
    }
  }
  
  ctx.stroke()
}

// 下载视频文件
const downloadVideo = async () => {
  const fileName = props.title ? props.title + '.mp4' : t('event.downloadFallbackName')
  const blob = await fetchGet(props.url, true)
  const a = document.createElement('a')
  document.body.appendChild(a)
  a.style.display = 'none'
  const url = window.URL.createObjectURL(blob)
  a.href = url
  a.download = fileName
  a.click()
  document.body.removeChild(a)
  window.URL.revokeObjectURL(url)
}

// get请求封装
const fetchGet = async (url, isFile) => {
  const res = await fetch(url)
  return isFile ? res.blob() : res.json()
}

onBeforeUnmount(() => {
  close()
})

// 监听 visible 变化
watch(() => props.visible, (val) => {
  selfVisible.value = val
  console.log('------------视频弹窗 visible 变化------------', val)
  
  if (!val) {
    // 弹窗关闭时，清理资源
    close()
  }
})
</script>

<style lang="scss" scoped>
:deep(.el-dialog__body),
:deep(.el-dialog__header) {
  position: relative;
  padding: 0 !important;
}

.mini-video-play-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  width: 100%;
  padding: 14px 20px;
  box-sizing: border-box;
  line-height: 1;

  .mini-video-play-title {
    font-size: 18px;
  }
  
  .mini-video-play-download {
    margin-right: 40px;
  }
}

.mini-video-play-wrap {
  display: block;
  width: 100%;
  position: relative;
  background-color: #000;
  box-sizing: border-box;
  overflow: hidden;

  .mini-video-play-loading-container {
    display: flex;
    justify-content: center;
    align-items: center;
    width: 100%;
    height: 100%;
    background-color: rgba(0, 0, 0, 0.6);
    position: absolute;
    left: 0;
    top: 0;
    z-index: 10;

    .mini-video-play-loading {
      display: block;
      width: 100px;
      height: 100px;
      position: relative;

      .mini-video-play-loading-el {
        position: absolute;
        width: 100%;
        height: 100%;

        & > div {
          width: 8px;
          height: 8px;
          background-color: #fff;
          border-radius: 100%;
          position: absolute;
          animation: bouncedelay 1.2s infinite ease-in-out;
          animation-fill-mode: both;

          &[data-circle-1] {
            top: 0;
            left: 0;
          }
          &[data-circle-2] {
            top: 0;
            right: 0;
          }
          &[data-circle-3] {
            right: 0;
            bottom: 0;
          }
          &[data-circle-4] {
            left: 0;
            bottom: 0;
          }
        }

        &[data-spin-1] {
          & > div[data-circle-2] {
            animation-delay: -0.9s;
          }
          & > div[data-circle-3] {
            animation-delay: -0.6s;
          }
          & > div[data-circle-4] {
            animation-delay: -0.3s;
          }
        }

        &[data-spin-2] {
          transform: rotateZ(45deg);

          & > div[data-circle-1] {
            animation-delay: -1.1s;
          }
          & > div[data-circle-2] {
            animation-delay: -0.8s;
          }
          & > div[data-circle-3] {
            animation-delay: -0.5s;
          }
          & > div[data-circle-4] {
            animation-delay: -0.2s;
          }
        }

        &[data-spin-3] {
          transform: rotateZ(90deg);

          & > div[data-circle-1] {
            animation-delay: -1s;
          }
          & > div[data-circle-2] {
            animation-delay: -0.7s;
          }
          & > div[data-circle-3] {
            animation-delay: -0.4s;
          }
          & > div[data-circle-4] {
            animation-delay: -0.1s;
          }
        }
      }
    }
  }

  .mini-video-play-replay-container {
    display: flex;
    justify-content: center;
    align-items: center;
    width: 100%;
    height: 100%;
    background-color: rgba(0, 0, 0, 0.6);
    position: absolute;
    left: 0;
    top: 0;
    z-index: 10;

    .mini-video-play-replay {
      display: block;
      width: 150px;
      height: 150px;
      position: relative;
      cursor: pointer;

      img {
        display: block;
        width: 100%;
        height: 100%;
      }
    }
  }

  .isBoolean {
    position: absolute;
    right: 20px;
    top: 20px;
    padding: 5px;
    border-radius: 5px;
    color: #fff;
    z-index: 999;
  }
}

@keyframes bouncedelay {
  0%,
  80%,
  100% {
    transform: scale(0);
  }
  40% {
    transform: scale(1);
  }
}

:deep(.el-dialog__headerbtn) {
  top: 20px;
  font-size: 20px;
}
</style>
