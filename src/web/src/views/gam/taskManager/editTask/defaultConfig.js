export const getDefaultPoints = function (width, height, type, isFullScreen, edgeWidth) {
  let points = []
  const space = 10  // 是否需要边界间隔
  let rectHeight = edgeWidth ? edgeWidth : 200  // 四边形、六边形 高
  if (type === 'quadrilateral') { // 四边形  兼容老四边形数据  逆时针
    points = [
      {
        x: isFullScreen ? space : (width / 2.0 - (rectHeight - space * 2) / 2.0),
        y: isFullScreen ? space : (height / 2.0 - (rectHeight - space * 2) / 2.0),
      },
      {
        x: isFullScreen ? space : (width / 2.0 - (rectHeight - space * 2) / 2.0),
        y: isFullScreen ? (height - space) : (height / 2.0 + (rectHeight - space * 2) / 2.0),
      },
      {
        x: isFullScreen ? (width - space) : (width / 2.0 + (rectHeight - space * 2) / 2.0),
        y: isFullScreen ? (height - space) : (height / 2.0 + (rectHeight - space * 2) / 2.0),
      },
      {
        x: isFullScreen ? (width - space) : (width / 2.0 + (rectHeight - space * 2) / 2.0),
        y: isFullScreen ? space : (height / 2.0 - (rectHeight - space * 2) / 2.0),
      }
    ]
  } else if (type === 'hexagon') { // 多边形  顺时针
    points = [
      {
        x: isFullScreen ? space : (width / 2.0 - (rectHeight - space * 2) / 2.0),
        y: isFullScreen ? space : (height / 2.0),
      },
      {
        x: isFullScreen ? (width / 2.0) : (width / 2.0 - (rectHeight - space * 2) / 4.0),
        y: isFullScreen ? space : (height / 2.0 - (rectHeight - space * 2) / 2.0 * Math.sqrt(3) / 2),
      },
      {
        x: isFullScreen ? (width - space) : (width / 2.0 + (rectHeight - space * 2) / 4.0),
        y: isFullScreen ? space : (height / 2.0 - (rectHeight - space * 2) / 2.0 * Math.sqrt(3) / 2),
      },
      {
        x: isFullScreen ? (width - space) : (width / 2.0 + (rectHeight - space * 2) / 2.0),
        y: isFullScreen ? (height - space) : (height / 2.0),
      },
      {
        x: isFullScreen ? (width / 2.0) : (width / 2.0 + (rectHeight - space * 2) / 4.0),
        y: isFullScreen ? (height - space) : (height / 2.0 + (rectHeight - space * 2) / 2.0 * Math.sqrt(3) / 2),
      },
      {
        x: isFullScreen ? space : (width / 2.0 - (rectHeight - space * 2) / 4.0),
        y: isFullScreen ? (height - space) : (height / 2.0 + (rectHeight - space * 2) / 2.0 * Math.sqrt(3) / 2),
      },
    ]
  }
  return points
}

export const getDefaultPointsRatio = function (width, height, type, isFullScreen, edgeWidth) {
  let points = []
  points = getDefaultPoints(...arguments)
  const pointsRatio = points.map((item) => {
    return pointsToRatio(item, width, height)
  })
  return pointsRatio
}

export const getAssociatedAreaPoints = function (points) {

}


function pointsToRatio(point, width, height) {
  return {
    xRatio: (point.x / width).toFixed(6),
    yRatio: (point.y / height).toFixed(6)
  }
}

