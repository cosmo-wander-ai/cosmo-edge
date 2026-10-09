// Both box and orientedCorners use source-image pixels. Image-analysis canvases
// use that same resolution; CSS scales the image and canvas together.
const hasCorners = corners => Array.isArray(corners) && corners.length === 4 &&
  corners.every(point => Number.isFinite(point?.x) && Number.isFinite(point?.y))

export function drawTargetGeometry(ctx, target) {
  const corners = target.orientedCorners
  if (hasCorners(corners)) {
    ctx.beginPath()
    ctx.moveTo(corners[0].x, corners[0].y)
    corners.slice(1).forEach(point => ctx.lineTo(point.x, point.y))
    ctx.closePath()
    ctx.stroke()
    return
  }

  const { x, y, width, height } = target.box
  ctx.strokeRect(x, y, width, height)
}

// Recorded AABBs retain the legacy ratio keys. Optional corners remain source
// pixels, so use the recorded frame dimensions before placing them in the
// video's actual displayed rectangle (which may have letterbox margins).
export function drawAlarmVideoTargetGeometry(ctx, rect, frame, viewport) {
  const box = {
    x: viewport.x + rect.xRatio * viewport.width,
    y: viewport.y + rect.yRatio * viewport.height,
    width: rect.wRatio * viewport.width,
    height: rect.hRatio * viewport.height
  }
  let orientedCorners
  if (Number.isFinite(frame.sourceWidth) && frame.sourceWidth > 0 &&
      Number.isFinite(frame.sourceHeight) && frame.sourceHeight > 0 &&
      hasCorners(rect.orientedCorners)) {
    orientedCorners = rect.orientedCorners.map(point => ({
      x: viewport.x + point.x * viewport.width / frame.sourceWidth,
      y: viewport.y + point.y * viewport.height / frame.sourceHeight
    }))
  }

  ctx.save()
  ctx.beginPath()
  ctx.rect(viewport.x, viewport.y, viewport.width, viewport.height)
  ctx.clip()
  drawTargetGeometry(ctx, { box, orientedCorners })
  ctx.restore()
}
