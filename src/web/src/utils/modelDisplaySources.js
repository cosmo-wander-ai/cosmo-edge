// Model list DTOs do not include provenance. Read the complete repository
// snapshot separately; failed or stale reads leave display names unchanged.
export const loadModelDisplaySources = async (api, isCurrent = () => true) => {
  const sources = new Map()
  if (typeof api?.getAtomicModelPage !== 'function') return sources

  let pageNum = 1
  let received = 0
  let expectedTotal
  try {
    do {
      if (!isCurrent()) return new Map()
      const response = await api.getAtomicModelPage({
        pageNum, pageSize: 100, modelName: '', modelCode: '', gpuCode: ''
      })
      if (!isCurrent()) return new Map()
      const { rows, total } = response?.resData || {}
      const count = Number(total)
      if (!Array.isArray(rows) || !Number.isSafeInteger(count) || count < 0 ||
          (expectedTotal !== undefined && count !== expectedTotal)) return new Map()
      expectedTotal = count
      if (!rows.length && received < expectedTotal) return new Map()
      for (const row of rows) {
        const code = String(row?.modelCode ?? '')
        // A repeated ID may indicate that the repository changed between pages.
        if (!code || sources.has(code)) return new Map()
        sources.set(code, {
          modelCode: code,
          modelName: row.modelName,
          isExportable: row.isExportable
        })
      }
      received += rows.length
      if (received > expectedTotal) return new Map()
      pageNum += 1
    } while (received < expectedTotal)
    return sources
  } catch {
    return new Map()
  }
}

// Return a display-only DTO. Never decorate the atomic model itself: its raw
// atomicName is also used in persisted webConfig and editor updates.
export const withModelDisplaySource = (model, sources) => {
  const modelCode = String(model?.atomicCode ?? model?.modelCode ?? '')
  const modelName = model?.atomicName ?? model?.modelName ?? ''
  const source = sources?.get(modelCode)
  const verifiedPreset = model?.isExportable !== true &&
    source?.isExportable === false && source.modelName === modelName
  return {
    modelCode,
    modelName,
    description: model?.description || model?.remark || '',
    isExportable: verifiedPreset ? false : true
  }
}
