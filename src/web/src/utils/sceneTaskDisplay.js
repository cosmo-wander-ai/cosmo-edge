// camera/page exposes at most 100 tasks per channel. Its task status is distinct
// from channel online status and from model availability (CameraMsgTypes.h).
const TASK_LIST_LIMIT = 100
const STATUS_KEYS = { 1: 'running', 0: 'stopped', '-1': 'paused', 2: 'error' }

export const emptyTaskChannelSummary = () => ({ total: 0, running: 0, paused: 0, error: 0, stopped: 0, unknown: 0 })

export function summarizeTaskChannels(data) {
  if (!Array.isArray(data?.rows)) return { available: false, complete: false, byAlgorithm: {} }
  const total = data.total === null || data.total === '' || data.total === undefined ? NaN : Number(data.total)
  let complete = Number.isSafeInteger(total) && total >= 0 && total <= data.rows.length
  const algorithms = new Map()
  const channelIds = new Set()
  for (const channel of data.rows) {
    const channelId = channel?.videoChannelId
    if (channelId === undefined || channelId === null || channelId === '' || !Array.isArray(channel.taskList)) {
      complete = false
      continue
    }
    channelIds.add(String(channelId))
    if (channel.taskList.length >= TASK_LIST_LIMIT) complete = false
    for (const task of channel.taskList) {
      if (task?.algorithmId === undefined || task.algorithmId === null || task.algorithmId === '') {
        complete = false
        continue
      }
      const algorithmId = String(task.algorithmId)
      if (!algorithms.has(algorithmId)) algorithms.set(algorithmId, new Map())
      const channels = algorithms.get(algorithmId)
      if (!channels.has(String(channelId))) channels.set(String(channelId), new Set())
      channels.get(String(channelId)).add(Object.hasOwn(STATUS_KEYS, task.status) ? STATUS_KEYS[task.status] : 'unknown')
    }
  }
  if (channelIds.size < total) complete = false
  const byAlgorithm = Object.fromEntries([...algorithms].map(([algorithmId, channels]) => {
    const summary = emptyTaskChannelSummary()
    summary.total = channels.size
    for (const states of channels.values()) {
      // Conflicting duplicate records cannot establish one current state.
      summary[states.size === 1 ? [...states][0] : 'unknown']++
    }
    return [algorithmId, summary]
  }))
  return { available: true, complete, byAlgorithm }
}

export function taskCategoryIcon(category) {
  return ({ 1: 'person', 2: 'target', 3: 'target', 8: 'count', 9: 'count', 10: 'vehicle', 11: 'count' })[String(category)] || 'default'
}
