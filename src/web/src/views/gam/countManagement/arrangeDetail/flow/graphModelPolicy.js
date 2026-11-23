// The server owns admission. This module only presents its incremental result.
export const modelCode = model => String(model?.atomicCode ?? model?.modelCode ?? '')
export function graphModelOptions(generic, additional, result, status) {
  const byCode = new Map()
  for (const model of [...additional, ...generic]) {
    const code = modelCode(model)
    if (code && !byCode.has(code)) byCode.set(code, model)
  }
  if (status !== 'ready') {
    // Until the server responds do not permit a new AX-only benchmark model.
    return [...byCode.values()].map(model => ({ model, disabled: ['9275710', '1001003'].includes(modelCode(model)), reason: '' }))
  }
  const allowed = new Set((result?.allowed || []).map(String))
  const denied = new Map((result?.denied || []).map(item => [String(item.modelCode), String(item.reason || '')]))
  return [...byCode.values()].map(model => ({
    model, disabled: !result?.passthrough && (denied.has(modelCode(model)) || (modelCode(model) === '1001003' && !allowed.has('1001003'))),
    reason: !result?.passthrough ? (denied.get(modelCode(model)) || '') : ''
  }))
}
export const allowedAdditionalModels = (all, result) => {
  if (result?.passthrough) return []
  const allowed = new Set((result?.allowed || []).map(String))
  return all.filter(model => allowed.has(modelCode(model)))
}

export function createLatestRequest() {
  let version = 0
  return { next: () => ++version, current: id => id === version, invalidate: () => { version += 1 } }
}
