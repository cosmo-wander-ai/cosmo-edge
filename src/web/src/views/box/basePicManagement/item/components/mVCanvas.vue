<template>
  <canvas id="canvas" :width="width" :height="height" style="position:absolute;top:0;left:0;opacity: 0.5;"></canvas>
</template>

<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import EventBus from '@/components/eventBus.js'

// Canvas drawing classes and logic
const colors = [
  { fillColor: 'rgba(24,144,255,.2)', strokeStyle: 'rgb(24,144,255)' },
  { fillColor: 'rgba(255,115,24,.2)', strokeStyle: 'rgb(255,115,24)' },
  { fillColor: 'rgba(24,255,89,.2)', strokeStyle: 'rgb(24,255,89)' },
  { fillColor: 'rgba(255,239,24,.2)', strokeStyle: 'rgb(255,239,24)' },
  { fillColor: 'rgba(255,255,255,.4)', strokeStyle: 'rgb(255,255,255)' },
]

class React {
  constructor(params) {
    const { x, y, w, h, lineWidth, strokeStyle } = params
    this.x = x
    this.y = y
    this.w = w
    this.h = h
    this.strokeStyle = strokeStyle ? strokeStyle : 'white'
    this.lineWidth = lineWidth
  }
}

class Path {
  constructor(params) {
    const { startX, startY, endX, endY, strokeStyle, lineWidth } = params
    this.startX = startX
    this.startY = startY
    this.endX = endX
    this.endY = endY
    this.strokeStyle = strokeStyle
    this.lineWidth = lineWidth
  }
}

class MvCanvasForDraw {
  constructor(el, vm) {
    this.canvas = el
    this.ctx = this.canvas.getContext('2d')
    this.vm = vm
    this.dragBlockIndex = []
    this.rectMove = false
    this.startPos = {}
    this.bindEvent()
  }

  bindEvent() {
    this.canvas.onmousedown = this.mousedownEvent.bind(this)
    document.onmouseup = this.mouseupEvent.bind(this)
    this.canvas.onmousemove = this.mousemoveEvent.bind(this)
  }

  unbindEvent() {
    this.canvas.onmousedown = null
    document.onmouseup = null
    this.canvas.onmousemove = null
  }

  mousedownEvent(e) {
    const x = e.offsetX
    const y = e.offsetY
    let pos = this.checkBoundary(x, y)
    if (this.vm.selectPoints.value.length > 1) {
      this.dragBlockIndex = []
      const pointInRect = this.pointInRect(pos, true)
      const flag = this.rayCasting(pos, this.vm.selectPoints.value)
      if (flag && !pointInRect) {
        this.rectMove = true
        this.startPos = pos
      }
      return
    }
    this.vm.blockList.push({
      x: pos.x,
      y: pos.y,
      w: 10,
      h: 10,
      strokeStyle: 'white',
      lineWidth: 2,
    })
  }

  mousemoveEvent(e) {
    const x = e.offsetX
    const y = e.offsetY

    const pos = this.checkBoundary(x, y)
    const flag = this.pointInRect(pos)
    const dragFlag = this.rayCasting(pos, this.vm.selectPoints.value)

    if (flag) {
      this.canvas.style.cursor = 'crosshair'
    } else {
      if (dragFlag) {
        this.canvas.style.cursor = 'move'
      } else {
        this.canvas.style.cursor = 'auto'
      }
    }

    if (this.dragBlockIndex && this.dragBlockIndex.length) {
      this.updateBlockList(pos, [this.dragBlockIndex[0]])
    }

    if (this.rectMove) {
      if (this.vm.isRect) {
        this.updateBlockList(pos, [0, 1, 2, 3])
      } else {
        this.updateBlockList(pos, [0, 1, 2, 3, 4, 5])
      }
    }
  }

  updateBlockList(pos, points) {
    if (points.length === 1) {
      const index = points[0]
      let widthRadio = 0
      let heightRadio = 0
      if (this.vm.isRect) {
        switch (index) {
          case 0:
            widthRadio = ((this.vm.selectPoints.value[1].x - pos.x) / (this.vm.width - 14)) * 256
            heightRadio = ((this.vm.selectPoints.value[3].y - pos.y) / (this.vm.height - 14)) * 256
            if (widthRadio < 5 || heightRadio < 5) return
            this.vm.selectPoints.value[0].x = pos.x
            this.vm.selectPoints.value[0].y = pos.y
            this.vm.selectPoints.value[1].y = pos.y
            this.vm.selectPoints.value[3].x = pos.x
            break
          case 1:
            widthRadio = ((pos.x - this.vm.selectPoints.value[0].x) / (this.vm.width - 14)) * 256
            heightRadio = ((this.vm.selectPoints.value[2].y - pos.y) / (this.vm.height - 14)) * 256
            if (widthRadio < 5 || heightRadio < 5) return
            this.vm.selectPoints.value[1].x = pos.x
            this.vm.selectPoints.value[1].y = pos.y
            this.vm.selectPoints.value[0].y = pos.y
            this.vm.selectPoints.value[2].x = pos.x
            break
          case 2:
            widthRadio = ((pos.x - this.vm.selectPoints.value[3].x) / (this.vm.width - 14)) * 256
            heightRadio = ((pos.y - this.vm.selectPoints.value[1].y) / (this.vm.height - 14)) * 256
            if (widthRadio < 5 || heightRadio < 5) return
            this.vm.selectPoints.value[2].x = pos.x
            this.vm.selectPoints.value[2].y = pos.y
            this.vm.selectPoints.value[1].x = pos.x
            this.vm.selectPoints.value[3].y = pos.y
            break
          case 3:
            widthRadio = ((this.vm.selectPoints.value[2].x - pos.x) / (this.vm.width - 14)) * 256
            heightRadio = ((pos.y - this.vm.selectPoints.value[0].y) / (this.vm.height - 14)) * 256
            if (widthRadio < 5 || heightRadio < 5) return
            this.vm.selectPoints.value[3].x = pos.x
            this.vm.selectPoints.value[3].y = pos.y
            this.vm.selectPoints.value[0].x = pos.x
            this.vm.selectPoints.value[2].y = pos.y
            break
          default:
            break
        }
        this.paint()
      } else {
        this.vm.selectPoints.value[index].x = pos.x
        this.vm.selectPoints.value[index].y = pos.y
        this.paint()
      }
    } else {
      let disX = pos.x - this.startPos.x,
        disY = pos.y - this.startPos.y
      const xArr = this.vm.selectPoints.value.reduce((p, t) => [...p, t.x], [])
      const yArr = this.vm.selectPoints.value.reduce((p, t) => [...p, t.y], [])
      const minX = Math.min(...xArr) + disX
      const minY = Math.min(...yArr) + disY
      const maxX = Math.max(...xArr) + disX
      const maxY = Math.max(...yArr) + disY
      if (minX < 2 || maxX > this.canvas.width - 12 || minY < 2 || maxY > this.canvas.height - 12) {
        this.startPos = pos
        return
      }
      for (let i = 0; i < points.length; i++) {
        let item = points[i]
        let finalDix = this.vm.selectPoints.value[item].x,
          finalDisY = this.vm.selectPoints.value[item].y
        finalDix += disX
        finalDisY += disY
        this.vm.selectPoints.value[item].x = finalDix
        this.vm.selectPoints.value[item].y = finalDisY
      }
      this.startPos = pos
      this.paint()
    }
  }

  mouseupEvent() {
    this.dragBlockIndex = []
    document.onmousemove = null
    this.rectMove = false
    this.startPos = {}
  }

  pointInRect(pos, pushFlag = false) {
    const flag = this.vm.selectPoints.value.some((item, index) => {
      if (pos.x >= item.x - 2 && pos.x <= item.x + 12 && pos.y >= item.y - 2 && pos.y <= item.y + 12) {
        if (pushFlag) {
          this.dragBlockIndex.push(index)
        }
        return true
      } else {
        return false
      }
    })
    return flag
  }

  rayCasting(p, poly) {
    let px = p.x, py = p.y, flag = false
    for (var i = 0, l = poly.length, j = l - 1; i < l; j = i++) {
      var sx = poly[i].x, sy = poly[i].y, tx = poly[j].x, ty = poly[j].y
      if ((sx === px && sy === py) || (tx === px && ty === py)) {
        return true
      }
      if ((sy < py && ty >= py) || (sy >= py && ty < py)) {
        var x = sx + ((py - sy) * (tx - sx)) / (ty - sy)
        if (x === px) {
          return true
        }
        if (x > px) {
          flag = !flag
        }
      }
    }
    return flag ? true : false
  }

  checkBoundary(x, y) {
    if (x <= 2) x = 2
    if (x >= this.canvas.width - 12) x = this.canvas.width - 12
    if (y <= 2) y = 2
    if (y >= this.canvas.height - 12) y = this.canvas.height - 12
    return { x, y }
  }

  renderReact(block) {
    this.ctx.strokeStyle = 'white'
    this.ctx.lineWidth = 2
    const { x, y } = this.checkBoundary(block.x, block.y)
    this.ctx.strokeRect(x, y, 10, 10)
  }

  paint() {
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height)
    this.ctx.fillStyle = 'rgba(0, 0, 0, 0.2)'
    this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height)
    this.vm.blockList.forEach((points, index) => {
      if (index === this.vm.activeIndex) {
        if (this.vm.isShowMin || this.vm.isShowMax) {
          this.drawPolygon(points, index)
        } else {
          this.vm.selectPoints.value = points
        }
      } else {
        this.drawPolygon(points, index)
      }
    })

    if (this.vm.isShowMin) {
      this.vm.referenceAreaPoints.value = this.getReferenceAreaPoints(
        this.vm.sizeParams.minWidth,
        this.vm.sizeParams.minHeight
      )
      this.vm.selectPoints.value = this.vm.referenceAreaPoints.value
    }

    if (this.vm.isShowMax) {
      this.vm.referenceAreaPoints.value = this.getReferenceAreaPoints(
        this.vm.sizeParams.maxWidth,
        this.vm.sizeParams.maxHeight
      )
      this.vm.selectPoints.value = this.vm.referenceAreaPoints.value
    }

    if (this.vm.isShowMin || this.vm.isShowMax) {
      this.drawPolygon(this.vm.selectPoints.value, 4)
      this.drawText(this.vm.isShowMin ? 'min' : 'max')
      this.vm.$emit('update-reference', this.getReferenceAreaParams())
    } else {
      if (this.vm.activeIndex != -1) {
        this.drawPolygon(this.vm.selectPoints.value, this.vm.activeIndex)
      }
    }
    if (this.vm.activeIndex != -1) {
      this.vm.selectPoints.value.forEach((block) => {
        this.renderReact(block)
      })
    }
  }

  drawPolygon(points, index) {
    if (!points.length) return

    this.ctx.beginPath()
    for (let i = 0; i < points.length; i++) {
      let point = points[i]
      const { x, y } = this.checkBoundary(point.x, point.y)
      if (!i) {
        this.ctx.moveTo(x + 5, y + 5)
      } else {
        this.ctx.lineTo(x + 5, y + 5)
      }
    }
    this.ctx.closePath()
    this.ctx.strokeStyle = colors[index].strokeStyle
    this.ctx.lineWidth = 2
    this.ctx.stroke()
    this.ctx.fillStyle = colors[index].fillColor
    this.ctx.fill()
  }

  drawText(text) {
    const x = Math.round((this.vm.selectPoints.value[1].x - this.vm.selectPoints.value[0].x) / 2 + this.vm.selectPoints.value[0].x) - 8
    const y = Math.round((this.vm.selectPoints.value[3].y - this.vm.selectPoints.value[0].y) / 2 + this.vm.selectPoints.value[0].y) + 8

    this.ctx.font = '16px Arial'
    this.ctx.fillStyle = '#fff'
    this.ctx.fillText(text, x, y)
  }

  getReferenceAreaPoints(widthRatio, heightRatio) {
    if (this.vm.referenceAreaPoints.value.length === 4) {
      return this.vm.referenceAreaPoints.value
    }

    let points = [
      { x: 2, y: 2 },
      { x: 2 + (widthRatio / 256) * (this.vm.width - 14), y: 2 },
      { x: 2 + (widthRatio / 256) * (this.vm.width - 14), y: 2 + (heightRatio / 256) * (this.vm.height - 14) },
      { x: 2, y: 2 + (heightRatio / 256) * (this.vm.height - 14) }
    ]

    points = points.map((point) => {
      return this.checkBoundary(point.x, point.y)
    })

    return points
  }

  getReferenceAreaParams() {
    let param = {
      widthRatio: Math.round(((this.vm.referenceAreaPoints.value[1].x - this.vm.referenceAreaPoints.value[0].x) / (this.vm.width - 14)) * 256),
      heightRatio: Math.round(((this.vm.referenceAreaPoints.value[3].y - this.vm.referenceAreaPoints.value[0].y) / (this.vm.height - 14)) * 256)
    }

    if (this.vm.isShowMin) {
      param = {
        minWidth: param.widthRatio,
        minHeight: param.heightRatio
      }
    } else {
      param = {
        maxWidth: param.widthRatio,
        maxHeight: param.heightRatio
      }
    }

    return param
  }
}

// Props
const props = defineProps({
  width: {
    type: Number,
    required: true
  },
  height: {
    type: Number,
    required: true
  },
  activeIndex: {
    type: Number,
    default: 0
  },
  value: {
    type: Array
  },
  isRect: {
    type: Boolean
  },
  isShowMin: {
    type: Boolean
  },
  isShowMax: {
    type: Boolean
  },
  sizeParams: {
    default: null
  }
})

// Emits
const emit = defineEmits(['input', 'update-reference'])

// Reactive data
const canvasContext = ref(null)
const selectPoints = ref([]) // 当前选中的多边形的6个点
const referenceAreaPoints = ref([])

// Computed
const blockList = computed(() => {
  return props.value
})

// Watchers
watch(blockList, (newValue) => {
  if (canvasContext.value) {
    emit('input', newValue)
    canvasContext.value.paint()
  }
}, { deep: true })

watch(() => props.activeIndex, () => {
  canvasContext.value.paint()
})

watch(() => props.isShowMin, (newValue) => {
  if (!newValue) {
    selectPoints.value = []
    referenceAreaPoints.value = []
  }
  canvasContext.value.paint()
})

watch(() => props.isShowMax, (newValue) => {
  if (!newValue) {
    selectPoints.value = []
    referenceAreaPoints.value = []
  }
  canvasContext.value.paint()
})

watch(referenceAreaPoints, () => {
  canvasContext.value.paint()
})

// Methods
const init = () => {
  canvasContext.value = null
  const EL = document.getElementById('canvas')
  canvasContext.value = new MvCanvasForDraw(EL, {
    selectPoints,
    referenceAreaPoints,
    blockList: blockList.value,
    activeIndex: props.activeIndex,
    isRect: props.isRect,
    isShowMin: props.isShowMin,
    isShowMax: props.isShowMax,
    sizeParams: props.sizeParams,
    width: props.width,
    height: props.height,
    $emit: emit
  })
  canvasContext.value.paint()
}

const bindEvent = () => {
  canvasContext.value.bindEvent()
}

const unbindEvent = () => {
  canvasContext.value.unbindEvent()
}

// Lifecycle hooks
onMounted(() => {
  EventBus.$on('resetSelectPoints', () => {
    selectPoints.value = []
  })
  init()
})

onBeforeUnmount(() => {
  canvasContext.value.unbindEvent()
})
</script>