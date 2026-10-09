const decode = value => {
  if (typeof value !== 'string') return value
  try { return JSON.parse(value) } catch { return null }
}

export function supportsFaceComparison(algorithm) {
  const metadata = decode(algorithm?.algorithmMetadata)
  if (metadata?.params?.some(p => p.type === 'faceSet' || p.key === 'param.faceSet')) return true
  const flow = decode(algorithm?.algorithmProcessdata)
  return Array.isArray(flow) && flow.some(node => node.actionId === 'PA_00005' &&
    node.configObject?.params?.some(p => p.key === 'featureInput' && String(p.value) === '0'))
}

export function faceTaskConfig(enabled, libraries, threshold) {
  if (!enabled) return { params: [{ key: 'param.faceCompare', value: '0' }, { key: 'param.faceSet', value: '' }] }
  const ids = [...new Set(libraries.map(String).filter(Boolean))]
  const score = Number(threshold)
  if (!ids.length || !Number.isFinite(score) || score <= 0 || score > 100) throw new Error('Invalid face comparison settings')
  return { params: [
    { key: 'param.faceSet', value: ids.join(',') },
    { key: 'param.limitScore', value: String(score) },
    { key: 'param.faceCompare', value: '1' }
  ] }
}

export function imageTargets(result) {
  if (!result) return []
  if (Array.isArray(result.targetList) && result.targetList.length) return result.targetList
  const seen = new Set()
  return (result.areaList || []).flatMap(area => area.targetList || []).filter(target => {
    const key = JSON.stringify([target.box, target.matchInfo, target.confidence])
    if (seen.has(key)) return false
    seen.add(key)
    return true
  })
}
