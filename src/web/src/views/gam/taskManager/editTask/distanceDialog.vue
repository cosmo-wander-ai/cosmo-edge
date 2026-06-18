<template>
  <el-dialog :title="t('glossary.distanceMeasure')" v-model="show" width="700px" center append-to-body @closed="handleClose">
    <div class="distance-dialog">
      <el-alert :title="t('validate.distanceMeasureTip')" type="info" show-icon :closable="false" />
      <div class="map-container" ref="mapContainer">
        <canvas ref="canvasRef" class="distance-canvas" @mousedown="handleMouseDown" @mousemove="handleMouseMove" @mouseup="handleMouseUp" @mouseout="handleMouseOut" />
        <img :src="imgSrc" ref="mapImage" class="map-image" @load="initCanvas" style="display: none" />
      </div>
    </div>
    <template #footer>
      <span class="dialog-footer">
        <el-button @click="resetPoints">{{ t('action.reset') }}</el-button>
        <el-button type="primary" @click="handleConfirm">{{ t('action.ok') }}</el-button>
      </span>
    </template>
  </el-dialog>
</template>

<script setup>
import { ref, watch, onUnmounted, getCurrentInstance, nextTick } from 'vue'
import { t } from '@/i18n'
import defaultImage from '@/assets/CatchPhoto.png'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  channelId: {
    type: String
  }
})

const emit = defineEmits(['update:visible', 'confirm'])

const { proxy } = getCurrentInstance()

const show = ref(false)
const points = ref([])
const ctx = ref(null)
const isDragging = ref(false)
const dragPoint = ref(null)
const canvasWidth = ref(0)
const canvasHeight = ref(0)
const pointRadius = ref(4)
const imgSrc = ref('')
const pixelDistance = ref(0)
const actualDistance = ref(0)

const canvasRef = ref(null)
const mapImage = ref(null)
const mapContainer = ref(null)

watch(() => props.visible, (val) => {
  show.value = val
  if (val) {
    getImage()
  }
})

watch(show, (val) => {
  emit('update:visible', val)
})

const getImage = () => {
  const params = {
    channelId: props.channelId,
    cacheEnable: true
  }
  proxy.$API.recaptureImage(params).then((res) => {
    const { resData } = res
    if (resData === 'DEFAULT_EMPTY_SNAP') {
      imgSrc.value = defaultImage
    } else {
      imgSrc.value = resData
    }
  })
}

const initCanvas = () => {
  if (!canvasRef.value || !mapContainer.value) return

  // 设置canvas尺寸
  canvasWidth.value = mapContainer.value.clientWidth
  canvasHeight.value = mapContainer.value.clientHeight
  canvasRef.value.width = canvasWidth.value
  canvasRef.value.height = canvasHeight.value

  ctx.value = canvasRef.value.getContext('2d')

  // 绘制初始状态
  draw()
}

const draw = () => {
  if (!ctx.value || !imgSrc.value || !mapImage.value) return
  const context = ctx.value
  const img = mapImage.value
  
  // 清空画布
  context.clearRect(0, 0, canvasWidth.value, canvasHeight.value)
  // 绘制背景图
  context.drawImage(img, 0, 0, canvasWidth.value, canvasHeight.value)
  
  // 绘制线段
  if (points.value.length === 2) {
    const [p1, p2] = points.value
    // 绘制线段
    context.beginPath()
    context.moveTo(p1.x, p1.y)
    context.lineTo(p2.x, p2.y)
    context.strokeStyle = '#409EFF'
    context.lineWidth = 2
    context.stroke()

    // 计算距离
    const dx = p2.x - p1.x
    const dy = p2.y - p1.y
    pixelDistance.value = Math.sqrt(dx * dx + dy * dy)

    // 计算相对比例 (相对于图片宽度的比例)
    actualDistance.value = (pixelDistance.value / canvasWidth.value).toFixed(2)

    // 绘制距离文字
    const midX = (p1.x + p2.x) / 2
    const midY = (p1.y + p2.y) / 2
    context.font = '14px Arial'
    context.fillStyle = 'red'
    context.textAlign = 'center'
    context.textBaseline = 'bottom'
    context.fillText(actualDistance.value, midX, midY - 5)
  }

  // 绘制点
  points.value.forEach((point) => {
    context.beginPath()
    context.arc(point.x, point.y, pointRadius.value, 0, Math.PI * 2)
    context.fillStyle = '#409EFF'
    context.fill()
  })
}

const handleMouseDown = (e) => {
  if (!imgSrc.value) return

  const rect = e.target.getBoundingClientRect()
  const x = e.clientX - rect.left
  const y = e.clientY - rect.top

  // 检查是否点击了已有的点
  const clickedPoint = points.value.find(
    (point) =>
      Math.sqrt(Math.pow(point.x - x, 2) + Math.pow(point.y - y, 2)) <
      pointRadius.value * 2
  )

  if (clickedPoint) {
    isDragging.value = true
    dragPoint.value = clickedPoint
  } else if (points.value.length < 2) {
    points.value.push({ x, y })
    draw()
  }
}

const handleMouseMove = (e) => {
  if (!isDragging.value || !dragPoint.value) return

  const rect = e.target.getBoundingClientRect()
  const x = e.clientX - rect.left
  const y = e.clientY - rect.top

  dragPoint.value.x = x
  dragPoint.value.y = y
  draw()
}

const handleMouseUp = () => {
  isDragging.value = false
  dragPoint.value = null
}

const handleMouseOut = () => {
  if (isDragging.value) {
    isDragging.value = false
    dragPoint.value = null
  }
}

const resetPoints = () => {
  points.value = []
  pixelDistance.value = 0
  actualDistance.value = 0
  draw()
}

const handleClose = () => {
  resetPoints()
}

const handleConfirm = () => {
  if (points.value.length === 2) {
    emit('confirm', actualDistance.value)
  }
  show.value = false
}
</script>

<style lang="scss" scoped>
.distance-dialog {
  .control-panel {
    display: flex;
    align-items: center;
    margin-bottom: 15px;
    gap: 20px;

    .scale-control,
    .ratio-control {
      display: flex;
      align-items: center;

      .label {
        margin-right: 8px;
        white-space: nowrap;
      }
    }
  }

  .map-container {
    position: relative;
    width: 100%;
    height: 400px;
    background: #f5f5f5;
    overflow: hidden;
    border: 1px solid #dcdfe6;
    border-radius: 4px;

    .distance-canvas {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      cursor: crosshair;
    }

    .empty-tip {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      color: #909399;
      font-size: 14px;
    }
  }

  .info-panel {
    margin-top: 15px;
    padding: 10px;
    background: #f5f7fa;
    border-radius: 4px;

    .distance-info {
      display: flex;
      gap: 20px;
      color: #606266;
      font-size: 14px;
    }
  }
}

.el-alert {
  margin-bottom: 20px;
}
</style>
