import assert from 'node:assert/strict'
import * as Vue from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const selected = Vue.reactive({ channelId: 'channel-a', algorithmId: '' })
const requests = []
const timers = new Map()
let timerId = 0
let closes = 0
const view = await mountComponent('views/box/bigScreen/components/LiveDiagnosticsPanel.vue', {
  wrap: Panel => ({ setup: () => () => Vue.h(Panel, { channelId: selected.channelId, algorithmId: selected.algorithmId, onClose: () => closes++ }) }),
  globals: {
    setTimeout: callback => { const id = ++timerId; timers.set(id, callback); return id },
    clearTimeout: id => timers.delete(id)
  },
  api: {
    queryRunningDetail(data, options) {
      assert.equal(options.silentError, true)
      return new Promise((resolve, reject) => requests.push({ data: JSON.parse(JSON.stringify(data)), resolve, reject }))
    }
  }
})
const flush = async () => { for (let i = 0; i < 4; i++) await view.settle() }
const hasText = text => view.all(node => node.text === text).length > 0
const hasClass = (node, name) => String(node.props?.class || '').split(/\s+/).includes(name)
const task = (extra = {}) => ({ taskId: 'server-task-new', channelId: 'channel-a', algorithmId: 'algorithm-new', ...extra })
try {
  assert.equal(requests.length, 0, 'raw preview does not poll task diagnostics')
  assert.ok(hasText('event.diagnosticsSelectOverlay'))
  selected.algorithmId = 'algorithm-old'
  await flush()
  assert.deepEqual(requests[0].data, {})
  selected.algorithmId = 'algorithm-new'
  await flush()
  assert.deepEqual(requests[1].data, {})
  requests[1].resolve({ resData: { status: [task()] } })
  await flush()
  assert.deepEqual(requests[2].data, { tasks: ['server-task-new'], includeActionDurations: true }, 'only request action metrics for the exact server task')
  requests[2].resolve({ resData: { status: [task({
    actionDurationInfos: [
      { name: 'Decode', durationAvgUs: 3120, durationCount: 4 },
      { name: 'Detect', durationAvgUs: 0, durationCount: 0 },
      { name: 'AreaAlarm', durationAvgUs: 10, durationCount: 4 },
      { name: 'Alarm', durationAvgUs: 380830, durationCount: 1 },
      { name: 'ExtraAction', durationAvgUs: 4400, durationCount: 1 }
    ],
    previewDurationInfos: [{ name: 'OSD', durationAvgUs: 8890, durationCount: 3 }],
    nodeDurationInfos: [{ name: 'UnrelatedNode', durationAvgUs: 9999, durationCount: 1 }]
  })] } })
  await flush()
  assert.ok(hasText('3.12'), 'microseconds are converted to milliseconds')
  assert.ok(hasText('380.83'), 'old-OSD Alarm action is shown')
  assert.ok(hasText('8.89'), 'OSD comes from the independently scoped preview metrics')
  assert.equal(view.all(node => node.text === '—').length, 2, 'missing samples show an em dash for Detect and Encode')
  assert.equal(hasText('UnrelatedNode'), false, 'queue node labels do not replace action statistics')
  assert.equal(hasText('ExtraAction'), false, 'additional action names are tucked into details')
  const details = view.all(node => hasClass(node, 'diagnostics-detail-toggle'))[0]
  details.props.onClick()
  await flush()
  assert.ok(hasText('ExtraAction'), 'extra actions are available in details')
  assert.ok(hasText('event.diagnosticsScope'), 'scope is visible in details')
  assert.ok(hasText('event.diagnosticsPreviewScope'), 'preview scope is distinct')
  requests[0].resolve({ resData: { status: [{ taskId: 'old-task', channelId: 'channel-a', algorithmId: 'algorithm-old' }] } })
  await flush()
  assert.equal(requests.length, 3, 'late discovery results are discarded without further polling')
  const retry = [...timers.entries()][0]
  timers.delete(retry[0]); retry[1]()
  await flush()
  assert.deepEqual(requests[3].data, { tasks: ['server-task-new'], includeActionDurations: true })
  requests[3].resolve({ resData: { status: [task({ actionDurationInfos: [
    { name: 'Alarm', durationAvgUs: 1000, durationCount: 1 }, { name: 'Alarm', durationAvgUs: 2000, durationCount: 1 }
  ], previewDurationInfos: [] })] } })
  await flush()
  assert.equal(hasText('380.83'), false, 'stale data is cleared')
  assert.equal(view.all(node => node.text === '—').length, 6, 'duplicate action names and absent samples never fabricate numbers')
  const close = view.all(node => hasClass(node, 'diagnostics-close'))[0]
  close.props.onClick()
  assert.equal(closes, 1, 'the panel close action is exposed to its owning viewer')
  selected.algorithmId = ''
  await flush()
  assert.equal(timers.size, 0, 'switching to raw preview stops polling')
  assert.ok(hasText('event.diagnosticsSelectOverlay'))
} finally {
  view.unmount()
}
assert.equal(timers.size, 0, 'closing the panel clears its timer')
console.log('Live diagnostics behavior checks passed')
