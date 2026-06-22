<template>
  <div class="image-viewer">
    <div class="image-viewer__mask"></div>
    <div class="image-viewer__wrapper">
      <el-icon class="close-btn" @click="handleClose"><Close /></el-icon>
      <div class="image-viewer__btns">
        <el-icon @click="handleZoomIn"><ZoomIn /></el-icon>
        <el-icon @click="handleZoomOut"><ZoomOut /></el-icon>
        <el-icon @click="handleRotateLeft"><RefreshLeft /></el-icon>
        <el-icon @click="handleRotateRight"><RefreshRight /></el-icon>
      </div>
      <div class="image-viewer__canvas" @mousedown="handleMouseDown" @mousemove="handleMouseMove" @mouseup="handleMouseUp" @mouseleave="handleMouseUp" @wheel="handleWheel">
        <div class="image-container" :style="imageStyle">
          <div v-if="titleName && titleName.length > 0" class="image-viewer__title">{{ titleName }}</div>
          <img :src="urlList[0]" @load="handleImageLoad" ref="previewImage">
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { ElIcon } from 'element-plus'
import { Close, ZoomIn, ZoomOut, RefreshLeft, RefreshRight } from '@element-plus/icons-vue'

// 定义组件名称
defineOptions({
  name: 'ImageViewer',
  components: {
    ElIcon
  }
})

// Props
const props = defineProps({
  urlList: {
    type: Array,
    required: true
  },
  titleName: {
    type: String,
    default: ''
  }
})

// Emits
const emit = defineEmits(['close'])

// Refs
const previewImage = ref(null)

// Reactive data
const scale = ref(1)
const rotate = ref(0)
const offsetX = ref(0)
const offsetY = ref(0)
const isDragging = ref(false)
const startX = ref(0)
const startY = ref(0)
const lastOffsetX = ref(0)
const lastOffsetY = ref(0)

// Computed
const imageStyle = computed(() => ({
  transform: `translate(${offsetX.value}px, ${offsetY.value}px) scale(${scale.value}) rotate(${rotate.value}deg)`,
  cursor: isDragging.value ? 'grabbing' : 'grab'
}))

// Methods
function handleZoomIn() {
  scale.value = Math.min(scale.value * 1.2, 10)
}

function handleZoomOut() {
  scale.value = Math.max(scale.value / 1.2, 0.1)
}

function handleRotateLeft() {
  rotate.value -= 90
}

function handleRotateRight() {
  rotate.value += 90
}

function handleClose() {
  emit('close')
}

function handleMouseDown(e) {
  if (e.button !== 0) return // 只响应左键
  e.preventDefault() // 阻止默认行为
  isDragging.value = true
  startX.value = e.clientX
  startY.value = e.clientY
  lastOffsetX.value = offsetX.value
  lastOffsetY.value = offsetY.value
}

function handleMouseMove(e) {
  if (!isDragging.value) return
  e.preventDefault() // 阻止默认行为
  const deltaX = e.clientX - startX.value
  const deltaY = e.clientY - startY.value
  requestAnimationFrame(() => {
    offsetX.value = lastOffsetX.value + deltaX
    offsetY.value = lastOffsetY.value + deltaY
  })
}

function handleMouseUp(e) {
  if (!isDragging.value) return
  e.preventDefault() // 阻止默认行为
  isDragging.value = false
  // 保存最后的位置
  lastOffsetX.value = offsetX.value
  lastOffsetY.value = offsetY.value
}

function handleWheel(e) {
  e.preventDefault()
  const delta = e.deltaY
  if (delta > 0) {
    // 向下滚动，缩小
    handleZoomOut()
  } else {
    // 向上滚动，放大
    handleZoomIn()
  }
}

function handleImageLoad() {
  reset()
}

function reset() {
  scale.value = 1
  rotate.value = 0
  offsetX.value = 0
  offsetY.value = 0
}
</script>

<style lang="scss" scoped>
.image-viewer {
  position: fixed;
  top: 0;
  right: 0;
  bottom: 0;
  left: 0;
  z-index: 3000;

  &__mask {
    position: absolute;
    width: 100%;
    height: 100%;
    background: rgba(0, 0, 0, 0.7);
  }

  &__wrapper {
    position: relative;
    width: 100%;
    height: 100%;
  }

  &__btns {
    position: fixed;
    left: 50%;
    bottom: 30px;
    transform: translateX(-50%);
    z-index: 1;
    display: flex;
    align-items: center;
    background: rgba(102, 102, 102, 0.8);
    padding: 8px 15px;
    border-radius: 20px;
    backdrop-filter: blur(2px);

    .el-icon {
      margin: 0 15px;
      color: #fff;
      font-size: 24px;
      cursor: pointer;

      &:hover {
        color: #409eff;
      }
    }
  }

  .close-btn {
    position: fixed;
    top: 20px;
    right: 20px;
    color: #fff;
    font-size: 24px;
    cursor: pointer;
    z-index: 2001;
    width: 32px;
    height: 32px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(102, 102, 102, 0.8);
    border-radius: 50%;
    backdrop-filter: blur(2px);

    &:hover {
      background: rgba(102, 102, 102, 0.9);
    }
  }

  &__canvas {
    position: absolute;
    width: 100%;
    height: 100%;
    display: flex;
    justify-content: center;
    align-items: center;
    overflow: hidden;

    .image-container {
      position: relative;
      display: flex;
      justify-content: center;
      align-items: center;
      transition: none; // 移除过渡效果，使拖动更流畅
      will-change: transform; // 优化性能
    }

    img {
      max-width: 90vw;
      max-height: 90vh;
      user-select: none;
      pointer-events: none; // 防止图片被拖动
    }
  }

  &__title {
    position: absolute;
    top: 0;
    left: 0;
    padding: 8px 12px;
    color: #fff;
    font-size: 20px;
    background: rgba(0, 0, 0, 0.7);
    border-radius: 4px;
    z-index: 1;
    pointer-events: none;
  }
}
</style>