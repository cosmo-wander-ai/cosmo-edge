<template>
  <div class="canvas-body">
    <canvas
      class="canvas"
      ref="canvasRef"
      :width="width"
      :height="height"
      @mousedown="handleMouseDown"
      @mousemove="handleMouseMove"
      @mouseup="handleMouseUp"
      @mouseleave="handleMouseUp"
    ></canvas>
    <img
      ref="imageRef"
      :style="{ width: width + 'px', height: height + 'px', objectFit: 'fill', userSelect: 'none' }"
      :src="imageSrc"
      @error="setDefaultImage"
      @load="handleImageLoad"
      style="display: none"
    />
  </div>
</template>

<script setup>
import { ref, watch, onMounted, getCurrentInstance } from 'vue'
import { t } from '@/i18n'
import defaultImage from '@/assets/CatchPhoto.png'
import { getDefaultPoints } from './defaultConfig.js'

const { proxy } = getCurrentInstance()

const props = defineProps({
  width: Number,
  height: Number,
  imageSrc: String,
  allPoints: Array,
  activeIndex: Number,
  shieldActiveIndex: Number,
  regionType: String,
  isDrawingLine: Boolean,
  associatedAreaType: String,
  associatedAreaConfig: Array,
  retroDirectType: String
})

const colors = [
  { fillColor: 'rgba(24,144,255,.2)', strokeStyle: 'rgb(24,144,255)' },
  { fillColor: 'rgba(255,115,24,.2)', strokeStyle: 'rgb(255,115,24)' },
  { fillColor: 'rgba(24,255,89,.2)', strokeStyle: 'rgb(24,255,89)' },
  { fillColor: 'rgba(255,239,24,.2)', strokeStyle: 'rgb(255,239,24)' },
  { fillColor: 'rgba(255,255,255,.4)', strokeStyle: 'rgb(255,255,255)' }
]

const shieldColor = {
  fillColor: 'rgba(178, 178, 178, 0.5)',
  strokeStyle: 'rgba(255, 255, 255, 0.8)'
}

const canvasRef = ref(null)
const imageRef = ref(null)
const ctx = ref(null)
const polygons = ref([])
const isDragging = ref(false)
const selectedPolygon = ref(null)
const selectedPoint = ref(null)
const startDragOffset = ref({})
const pointSize = ref(10)
const drawingLinePoints = ref([])
const directionType = ref('0')
const isAssociatedArea = ref(false)
const associatedAreaPoints = ref([])
const isMovingLine = ref(false)
const isReversed = ref(false)

const platformType = localStorage.getItem('platformType') || ''

const setDefaultImage = (e) => {
  e.target.src = defaultImage
  // 设置默认图片后也要重新绘制
  setTimeout(() => {
    drawBackgroundImage()
    redrawCanvas()
  }, 100)
}

const handleImageLoad = () => {
  console.log('图片加载完成:', props.imageSrc)
  initCanvas()
}

const initCanvas = () => {
  if (!canvasRef.value) return
  const canvas = canvasRef.value
  ctx.value = canvas.getContext('2d')
  console.log('Canvas初始化:', { width: canvas.width, height: canvas.height })
  drawBackgroundImage()
  redrawCanvas()
}

const drawBackgroundImage = () => {
  if (!ctx.value || !imageRef.value || !canvasRef.value) return
  const context = ctx.value
  const canvas = canvasRef.value
  const img = imageRef.value
  
  // 清除画布
  context.clearRect(0, 0, canvas.width, canvas.height)
  
  // 绘制背景图片
  if (img.complete && img.naturalWidth > 0) {
    try {
      context.drawImage(img, 0, 0, canvas.width, canvas.height)
    } catch (error) {
      console.error('绘制图片失败:', error)
      // 如果绘制失败，绘制一个占位背景
      context.fillStyle = '#f0f0f0'
      context.fillRect(0, 0, canvas.width, canvas.height)
      context.fillStyle = '#999'
      context.font = '16px Arial'
      context.textAlign = 'center'
      context.fillText(t('common.imageLoadFailed'), canvas.width / 2, canvas.height / 2)
    }
  } else {
    // 图片未加载完成时显示占位背景
    context.fillStyle = '#f8f9fa'
    context.fillRect(0, 0, canvas.width, canvas.height)
    context.fillStyle = '#6c757d'
    context.font = '14px Arial'
    context.textAlign = 'center'
    context.fillText(t('common.imageLoading'), canvas.width / 2, canvas.height / 2)
  }
}

const redrawCanvas = () => {
  if (!ctx.value || !canvasRef.value) return
  
  // 先绘制背景图片
  drawBackgroundImage()
  
  const context = ctx.value

  if (props.retroDirectType === '1') {
    drawDirectionArrow(context, props.width - 20, 10, props.width - 20, 60)
  } else if (props.retroDirectType === '0') {
    drawDirectionArrow(context, props.width - 20, 60, props.width - 20, 10)
  }

  polygons.value.forEach((polygon, polygonIndex) => {
    if (polygonIndex !== props.activeIndex) {
      drawPolygons(polygon.points, polygonIndex)
      drawLineVertexs(polygon.linePoints, polygonIndex)
      if (polygon.associatedAreas) {
        drawPolygons(polygon.associatedAreas, polygonIndex)
      }
    }
    if (polygonIndex !== props.shieldActiveIndex) {
      drawPolygons(polygon.shieldPoints, polygonIndex, shieldColor)
    }

    if (props.activeIndex !== null && polygonIndex === polygons.value.length - 1) {
      drawPolygons(polygons.value[props.activeIndex].points, props.activeIndex)
      drawLineVertexs(polygons.value[props.activeIndex].linePoints, props.activeIndex)
      if (polygon.associatedAreas) {
        drawPolygons(polygons.value[props.activeIndex].associatedAreas, props.activeIndex)
      }
    }
    if (props.shieldActiveIndex !== null && polygonIndex === props.shieldActiveIndex) {
      drawPolygons(polygons.value[props.shieldActiveIndex].shieldPoints, props.shieldActiveIndex, shieldColor)
    }
  })

  if (associatedAreaPoints.value.length > 0) {
    drawPolygons(associatedAreaPoints.value, polygons.value.length === 0 ? 0 : polygons.value.length)
  }

  if (props.associatedAreaConfig && props.associatedAreaConfig.length > 0 && props.activeIndex !== null && props.regionType !== 'cordon' && props.regionType !== 'oneWayCordon') {
    if (isAssociatedArea.value) {
      drawText(props.associatedAreaConfig[1].name)
    } else {
      drawText(props.associatedAreaConfig[0].name)
    }
  }
}

const drawPolygons = (points, polygonIndex, colorConfig) => {
  if (!points || points.length === 0) return
  const context = ctx.value
  context.beginPath()
  points.forEach((point, index) => {
    if (index === 0) {
      context.moveTo(...point)
    } else {
      context.lineTo(...point)
    }
  })
  context.closePath()
  context.fillStyle = colorConfig ? colorConfig.fillColor : colors[polygonIndex].fillColor
  context.fill()
  context.lineWidth = 2
  context.strokeStyle = colorConfig ? colorConfig.strokeStyle : colors[polygonIndex].strokeStyle
  context.stroke()

  if (polygonIndex === props.activeIndex) {
    if (isAssociatedArea.value || props.regionType === 'oneWayCordon' || props.regionType === 'cordon') {
      drawPolygonVertexs(polygons.value[props.activeIndex].associatedAreas)
    } else {
      drawPolygonVertexs(polygons.value[props.activeIndex].points)
    }
  }
  if (polygonIndex === props.shieldActiveIndex) {
    drawPolygonVertexs(polygons.value[props.shieldActiveIndex].shieldPoints)
  }
}

const drawPolygonVertexs = (points) => {
  if (!points) return
  const context = ctx.value
  context.lineWidth = 2
  context.strokeStyle = 'white'
  points.forEach((point) => {
    context.strokeRect(point[0] - pointSize.value / 2, point[1] - pointSize.value / 2, pointSize.value, pointSize.value)
  })
}

const drawLineVertexs = (points, polygonIndex) => {
  if (!points || points.length === 0) return
  const context = ctx.value
  const radius = 4
  points.forEach((point) => {
    context.beginPath()
    context.arc(point[0], point[1], radius, 0, 2 * Math.PI)
    context.fillStyle = polygonIndex === props.activeIndex ? 'white' : colors[polygonIndex].strokeStyle
    context.fill()
  })
  drawLines(points, polygonIndex)
}

const drawLines = (points, polygonIndex) => {
  if (!points || points.length < 2) return
  const context = ctx.value
  context.beginPath()
  points.forEach((point, index) => {
    if (index === 0) {
      context.moveTo(...point)
    } else {
      context.lineTo(...point)
    }
  })
  context.lineWidth = 2
  context.strokeStyle = colors[polygonIndex].strokeStyle
  context.stroke()
  drawArrowLine(points, polygonIndex)
}

const drawArrowLine = (points, polygonIndex) => {
  const context = ctx.value
  points.forEach((point, index) => {
    if (index > 0) {
      const midX = (points[index - 1][0] + points[index][0]) / 2
      const midY = (points[index - 1][1] + points[index][1]) / 2
      let perpX = points[index - 1][1] - points[index][1]
      let perpY = points[index][0] - points[index - 1][0]
      const length = Math.sqrt(perpX * perpX + perpY * perpY)
      perpX /= length
      perpY /= length
      const arrowLength = 30
      perpX *= arrowLength
      perpY *= arrowLength

      context.beginPath()
      drawArrow(context, midX, midY, midX + perpX, midY + perpY)

      if (polygons.value[polygonIndex]?.directionType === '1') {
        drawArrow(context, midX, midY, midX - perpX, midY - perpY)
      }
      if (polygonIndex === polygons.value.length && directionType.value === '1') {
        drawArrow(context, midX, midY, midX - perpX, midY - perpY)
      }
      context.stroke()
    }
  })
}

const drawArrow = (ctx, fromx, fromy, tox, toy) => {
  const headlen = 10
  const angle = Math.atan2(toy - fromy, tox - fromx)
  ctx.moveTo(fromx, fromy)
  ctx.lineTo(tox, toy)
  ctx.lineTo(tox - headlen * Math.cos(angle - Math.PI / 6), toy - headlen * Math.sin(angle - Math.PI / 6))
  ctx.moveTo(tox, toy)
  ctx.lineTo(tox - headlen * Math.cos(angle + Math.PI / 6), toy - headlen * Math.sin(angle + Math.PI / 6))
}

const drawDirectionArrow = (ctx, fromX, fromY, toX, toY) => {
  const theta = 30
  const headlen = 15
  const width = 3
  const color = '#f36'
  const angle = (Math.atan2(fromY - toY, fromX - toX) * 180) / Math.PI
  const angle1 = ((angle + theta) * Math.PI) / 180
  const angle2 = ((angle - theta) * Math.PI) / 180
  const topX = headlen * Math.cos(angle1)
  const topY = headlen * Math.sin(angle1)
  const botX = headlen * Math.cos(angle2)
  const botY = headlen * Math.sin(angle2)
  ctx.beginPath()
  let arrowX = fromX - topX
  let arrowY = fromY - topY
  ctx.moveTo(arrowX, arrowY)
  ctx.moveTo(fromX, fromY)
  ctx.lineTo(toX, toY)
  arrowX = toX + topX
  arrowY = toY + topY
  ctx.moveTo(arrowX, arrowY)
  ctx.lineTo(toX, toY)
  arrowX = toX + botX
  arrowY = toY + botY
  ctx.lineTo(arrowX, arrowY)
  ctx.strokeStyle = color
  ctx.lineWidth = width
  ctx.stroke()
}

const handleMouseDown = (e) => {
  const x = e.offsetX
  const y = e.offsetY

  if (props.isDrawingLine && !selectedPoint.value && !selectedPolygon.value) {
    if (drawingLinePoints.value.length >= 5) {
      proxy.$message.warning(t('validate.maxFourLines'))
      return
    }
    if (isReversed.value) {
      drawingLinePoints.value.unshift([x, y])
    } else {
      drawingLinePoints.value.push([x, y])
    }
    return
  }

  polygons.value.forEach((polygon, polyIndex) => {
    polygon.points.forEach((point, pointIndex) => {
      if (props.activeIndex !== null && pointInRect(x, y, point) && polyIndex === props.activeIndex) {
        selectedPoint.value = { polyIndex, pointIndex }
        isDragging.value = true
        isAssociatedArea.value = false
      }
    })

    polygon.shieldPoints.forEach((point, pointIndex) => {
      if (props.shieldActiveIndex !== null && pointInRect(x, y, point) && polyIndex === props.shieldActiveIndex) {
        selectedPoint.value = { polyIndex, pointIndex }
        isDragging.value = true
        isAssociatedArea.value = false
      }
    })

    polygon.associatedAreas.forEach((point, pointIndex) => {
      if (props.activeIndex !== null && pointInRect(x, y, point) && polyIndex === props.activeIndex) {
        selectedPoint.value = { polyIndex, pointIndex }
        isDragging.value = true
        isAssociatedArea.value = true
      }
    })

    polygon.linePoints.forEach((point, pointIndex) => {
      if (props.activeIndex !== null && pointInRect(x, y, point) && polyIndex === props.activeIndex) {
        selectedPoint.value = { polyIndex, pointIndex }
        isDragging.value = true
        isMovingLine.value = true
      }
    })
  })

  if (selectedPoint.value) return

  selectedPolygon.value = polygons.value.find((polygon, polyIndex) => {
    if (polyIndex === props.activeIndex || polyIndex === props.shieldActiveIndex) {
      let insideFlag = false
      if (props.shieldActiveIndex !== null) {
        insideFlag = pointInPolygon(x, y, polygon.shieldPoints)
      } else {
        insideFlag = pointInPolygon(x, y, polygon.points)
        isAssociatedArea.value = false
        if (polygon.associatedAreas && polygon.associatedAreas.length > 0) {
          const insideAssociatedAreaFlag = pointInPolygon(x, y, polygon.associatedAreas)
          if (insideAssociatedAreaFlag) {
            isAssociatedArea.value = true
            return insideAssociatedAreaFlag
          }
        }
      }
      return insideFlag
    }
    return false
  })

  if (selectedPolygon.value) {
    isDragging.value = true
    if (isAssociatedArea.value) {
      startDragOffset.value.x = x - selectedPolygon.value.associatedAreas[0][0]
      startDragOffset.value.y = y - selectedPolygon.value.associatedAreas[0][1]
    } else {
      if (props.shieldActiveIndex !== null) {
        startDragOffset.value.x = x - selectedPolygon.value.shieldPoints[0][0]
        startDragOffset.value.y = y - selectedPolygon.value.shieldPoints[0][1]
      } else {
        startDragOffset.value.x = x - selectedPolygon.value.points[0][0]
        startDragOffset.value.y = y - selectedPolygon.value.points[0][1]
      }
    }
    redrawCanvas()
  }
}

const handleMouseMove = (e) => {
  if (!selectedPoint.value && !selectedPolygon.value) return

  const canvas = canvasRef.value
  const newX = e.offsetX - startDragOffset.value.x
  const newY = e.offsetY - startDragOffset.value.y

  if (isDragging.value && selectedPoint.value) {
    const { offsetX, offsetY } = e
    const x = Math.min(Math.max(offsetX, 0), canvas.width)
    const y = Math.min(Math.max(offsetY, 0), canvas.height)
    const { polyIndex, pointIndex } = selectedPoint.value

    let points = []
    if (isMovingLine.value) {
      points = polygons.value[polyIndex].linePoints
    } else if (isAssociatedArea.value) {
      points = polygons.value[polyIndex].associatedAreas
    } else {
      if (props.shieldActiveIndex !== null) {
        points = polygons.value[polyIndex].shieldPoints
      } else {
        points = polygons.value[polyIndex].points
      }
    }
    points[pointIndex] = [x, y]

    if (points.length === 4 && !isMovingLine.value) {
      switch (pointIndex) {
        case 0:
          points[1][0] = x
          points[3][1] = y
          break
        case 1:
          points[0][0] = x
          points[2][1] = y
          break
        case 2:
          points[1][1] = y
          points[3][0] = x
          break
        case 3:
          points[0][1] = y
          points[2][0] = x
          break
      }
    }
    redrawCanvas()
  } else if (isDragging.value && selectedPolygon.value) {
    let movedPolygon = []
    if (isMovingLine.value) {
      movedPolygon = selectedPolygon.value.linePoints.map((point) => [
        point[0] + newX - selectedPolygon.value.linePoints[0][0],
        point[1] + newY - selectedPolygon.value.linePoints[0][1]
      ])
    } else if (isAssociatedArea.value) {
      movedPolygon = selectedPolygon.value.associatedAreas.map((point) => [
        point[0] + newX - selectedPolygon.value.associatedAreas[0][0],
        point[1] + newY - selectedPolygon.value.associatedAreas[0][1]
      ])
    } else {
      if (props.shieldActiveIndex !== null) {
        movedPolygon = selectedPolygon.value.shieldPoints.map((point) => [
          point[0] + newX - selectedPolygon.value.shieldPoints[0][0],
          point[1] + newY - selectedPolygon.value.shieldPoints[0][1]
        ])
      } else {
        movedPolygon = selectedPolygon.value.points.map((point) => [
          point[0] + newX - selectedPolygon.value.points[0][0],
          point[1] + newY - selectedPolygon.value.points[0][1]
        ])
      }
    }
    adjustPolygonWithinCanvas(movedPolygon, canvas)
    redrawCanvas()
  }
}

const handleMouseUp = () => {
  isDragging.value = false
  isMovingLine.value = false
  selectedPoint.value = null
  selectedPolygon.value = null
  isAssociatedArea.value = false
}

const adjustPolygonWithinCanvas = (points, canvas) => {
  let minX = Math.min(...points.map((point) => point[0]))
  let maxX = Math.max(...points.map((point) => point[0]))
  let minY = Math.min(...points.map((point) => point[1]))
  let maxY = Math.max(...points.map((point) => point[1]))

  const deltaX = minX < 0 ? -minX : maxX > canvas.width ? canvas.width - maxX : 0
  const deltaY = minY < 0 ? -minY : maxY > canvas.height ? canvas.height - maxY : 0

  if (isAssociatedArea.value) {
    selectedPolygon.value.associatedAreas = points.map((point) => [point[0] + deltaX, point[1] + deltaY])
  } else if (isMovingLine.value) {
    selectedPolygon.value.linePoints = points.map((point) => [point[0] + deltaX, point[1] + deltaY])
  } else {
    if (props.shieldActiveIndex !== null) {
      selectedPolygon.value.shieldPoints = points.map((point) => [point[0] + deltaX, point[1] + deltaY])
    } else {
      selectedPolygon.value.points = points.map((point) => [point[0] + deltaX, point[1] + deltaY])
    }
  }
}

const drawLineOperation = (opt) => {
  switch (opt) {
    case 'type':
      redrawCanvas()
      drawLineVertexs(drawingLinePoints.value, polygons.value.length)
      break
    case 'direction':
      redrawCanvas()
      break
    case 'delete':
      drawingLinePoints.value = []
      redrawCanvas()
      break
    default:
      break
  }
}

const pointInPolygon = (x, y, points) => {
  if (!points) return false
  let inside = false
  for (let i = 0, j = points.length - 1; i < points.length; j = i++) {
    const xi = points[i][0], yi = points[i][1]
    const xj = points[j][0], yj = points[j][1]
    const intersect = yi > y !== yj > y && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi
    if (intersect) inside = !inside
  }
  return inside
}

const pointInRect = (x, y, point) => {
  return (
    x >= point[0] - pointSize.value / 2 &&
    x <= point[0] + pointSize.value / 2 &&
    y >= point[1] - pointSize.value / 2 &&
    y <= point[1] + pointSize.value / 2
  )
}

const drawText = (text) => {
  ctx.value.font = '20px Arial'
  ctx.value.fillStyle = 'white'
  ctx.value.fillText(text, 10, props.height - 10)
}

const submit = () => {
  const result = []
  props.allPoints.forEach((area) => {
    const points = area.points.map((point) => pointsToRadio(point))
    const shieldPoints = area.shieldPoints.map((point) => pointsToRadio(point))
    const associatedAreas = area.associatedAreas.map((point) => pointsToRadio(point))
    const linePoints = area.linePoints.map((point) => pointsToRadio(point))
    result.push({
      points: points,
      shieldPoints: shieldPoints,
      associatedAreas: associatedAreas,
      linePoints: linePoints,
      directionType: area.directionType
    })
  })

  if (props.isDrawingLine) {
    if (drawingLinePoints.value.length < 2) {
      proxy.$message.warning(t('validate.minTwoPoints'))
      return []
    }
    const newDrawingLinePoints = drawingLinePoints.value.map((point) => pointsToRadio(point))
    const newAssociatedAreas = associatedAreaPoints.value.map((point) => pointsToRadio(point))
    result.push({
      points: [],
      associatedAreas: newAssociatedAreas,
      linePoints: newDrawingLinePoints,
      directionType: directionType.value
    })
  }
  return result
}

const pointsToRadio = (point) => {
  if (platformType === '15') {
    return {
      xRatio: Number((point[0] / props.width).toFixed(6)),
      yRatio: Number((point[1] / props.height).toFixed(6))
    }
  } else {
    return {
      xRatio: (point[0] / props.width).toFixed(6),
      yRatio: (point[1] / props.height).toFixed(6)
    }
  }
}

onMounted(() => {
  initCanvas()
})

watch(() => props.imageSrc, () => {
  // 当图片源变化时，等待图片加载完成后重新绘制
  if (imageRef.value) {
    imageRef.value.onload = () => {
      drawBackgroundImage()
      redrawCanvas()
    }
  }
})

watch(() => props.allPoints, (newVal) => {
  associatedAreaPoints.value = []
  polygons.value = newVal
  initCanvas()
}, { deep: true })

watch(() => props.activeIndex, () => {
  redrawCanvas()
})

watch(() => props.shieldActiveIndex, () => {
  redrawCanvas()
})

watch(() => props.retroDirectType, () => {
  redrawCanvas()
})

watch(() => props.isDrawingLine, (newVal) => {
  if (newVal) {
    selectedPolygon.value = null
    if (!props.associatedAreaConfig) return

    const defaultPoints = getDefaultPoints(props.width, props.height, props.associatedAreaConfig[1].regionType, false, 150)
    associatedAreaPoints.value = defaultPoints.map((point) => [point.x, point.y])
    drawPolygons(associatedAreaPoints.value, polygons.value.length === 0 ? 0 : polygons.value.length)
  } else {
    associatedAreaPoints.value = []
  }
})

watch(drawingLinePoints, () => {
  if (drawingLinePoints.value.length === 0) return
  drawLineVertexs(drawingLinePoints.value, polygons.value.length)
}, { deep: true })

defineExpose({ submit, drawLineOperation, drawingLinePoints, directionType, selectedPolygon, isReversed })
</script>

<style scoped>
.canvas-body {
  position: relative;
  width: 100%;
  height: 306px; /* 550 / 1.8 */
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  overflow: hidden;
  background-color: #f8f9fa;
}

.canvas {
  position: absolute;
  top: 0;
  left: 0;
  z-index: 2;
  cursor: crosshair;
  background-color: transparent;
}

.canvas:hover {
  cursor: crosshair;
}
</style>
