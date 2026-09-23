import assert from 'node:assert/strict'
import { h } from 'vue'
import lodash from 'lodash'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const pending = () => new Promise(() => {})
const assets = Object.fromEntries([
  'beep.ogg', 'big_screen_btn_bg.png', 'big_screen_no_camera.png', 'check-circle.png',
  'close-circle.png', 'error-image.png', 'screen-camera.png', 'screen-exit.png'
].map(name => [`@/assets/${name}`, { default: '' }]))
const noopComponent = { default: { render: () => null } }
const cases = [
  { name: 'disabled numeric status', selected: 'a', tasks: [{ algorithmId: 'a', enableStatus: 0 }], expected: '' },
  { name: 'disabled string status', selected: 'a', tasks: [{ algorithmId: 'a', enableStatus: '0' }], expected: '' },
  { name: 'deleted algorithm', selected: 'a', tasks: [], expected: '' },
  { name: 'missing task list', selected: 'a', tasks: undefined, expected: '' },
  { name: 'enabled numeric status', selected: 'a', tasks: [{ algorithmId: 'a', enableStatus: 1 }], expected: 'a' },
  { name: 'enabled string status', selected: 'a', tasks: [{ algorithmId: 'a', enableStatus: '1' }], expected: 'a' },
  { name: 'numeric algorithm normalized', selected: '123', tasks: [{ algorithmId: 123, enableStatus: 1 }], expected: '123' },
  { name: 'raw selection stays raw', selected: '', tasks: [{ algorithmId: 'a', enableStatus: 1 }], expected: '' },
  { name: 'other enabled algorithm cannot restore disabled selection', selected: 'a', tasks: [{ algorithmId: 'a', enableStatus: 0 }, { algorithmId: 'b', enableStatus: 1 }], expected: '' },
  { name: 'disabled deep link falls back raw', selected: 'b', tasks: [{ algorithmId: 'a', enableStatus: 0 }, { algorithmId: 'b', enableStatus: 1 }], query: { channelId: 'camera-1', algorithmId: 'a' }, expected: '' },
  { name: 'enabled deep link overrides saved fallback', selected: 'a', tasks: [{ algorithmId: 'a', enableStatus: 0 }, { algorithmId: 'b', enableStatus: 1 }], query: { channelId: 'camera-1', algorithmId: 'b' }, expected: 'b' },
]

for (const scenario of cases) {
  const storage = new Map([['playedCameraList', JSON.stringify([
    { id: 'camera-1', name: 'Outdated name', taskList: [{ algorithmId: 'a', enableStatus: 1 }], runAlgorithmId: scenario.selected }
  ])]])
  const screen = await mountComponent('views/box/bigScreen/warnningScreen/index.vue', {
    mocks: {
      '@element-plus/icons-vue': { Search: 'Search', Refresh: 'Refresh' },
      '../components/flvVideo.vue': { default: { render() { return h('preview-tile', this.$attrs) } } },
      '../components/detailDialog.vue': noopComponent,
      '../components/captureDialog.vue': noopComponent,
      '../components/TreeSelect.vue': noopComponent,
      ...assets,
      '@/components/eventBus': { default: { $emit() {} } },
      '@/utils/i18nResource': { resolveResourceAlgorithmName: () => '' },
      '@/utils/format': { formatSimilarity: value => value },
      moment: { default: () => ({ format: () => '', startOf() { return this }, endOf() { return this }, valueOf: () => 0 }) },
      lodash: { default: lodash }
    },
    globals: {
      localStorage: { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value) },
      document: { addEventListener() {}, removeEventListener() {}, body: {} },
      window: { addEventListener() {}, removeEventListener() {} },
      requestAnimationFrame: () => 1, cancelAnimationFrame() {}
    },
    route: { query: scenario.query || {} },
    api: {
      queryPopUpParam: pending,
      boxQueryCameraList: async () => ({ resData: { rows: [{
        videoChannelId: 'camera-1', channelName: 'Camera', channelStatus: 1, channelType: 0,
        taskList: scenario.tasks
      }] } }),
      boxQueryEvent: pending, boxAllAlgorithmInfo: pending, bigScreenWs: () => ''
    }
  })
  try {
    await screen.settle()
    assert.equal(JSON.parse(storage.get('playedCameraList'))[0].runAlgorithmId, scenario.expected, `${scenario.name}: persisted selection`)
    assert.equal(screen.all(n => n.type === 'preview-tile')[0].props.runAlgorithmId, scenario.expected, `${scenario.name}: child stream selection`)
    assert.equal(screen.all(n => n.type === 'preview-tile')[0].props.cameraName, 'Camera')
    assert.deepEqual(JSON.parse(JSON.stringify(screen.all(n => n.type === 'preview-tile')[0].props.taskList)), scenario.tasks || [])
    console.log(`PASS ${scenario.name}`)
  } finally { screen.unmount() }
}
console.log(`Restoration matrix passed (${cases.length} cases; compiled SFC, actual Lodash; storage and player props verified)`)

// Exercise the actual drawer and child event handlers, with deferred server
// responses. The tree double renders the production node slot for interaction.
const deferred = () => {
  let resolve, reject
  const promise = new Promise((yes, no) => { resolve = yes; reject = no })
  return { promise, resolve, reject }
}
const enabledTasks = [{ algorithmId: 'a', enableStatus: 1 }]
const disabledTasks = [{ algorithmId: 'a', enableStatus: 0 }]
const response = tasks => ({ resData: { rows: [{ videoChannelId: 'camera-1',
  channelName: 'Fresh camera', channelStatus: 1, channelType: 0, taskList: tasks }] } })
const requests = []
let calls = 0
const liveStorage = new Map([['playedCameraList', JSON.stringify([
  { id: 'camera-1', name: 'Old camera', taskList: enabledTasks, runAlgorithmId: 'a' }
])]])
const live = await mountComponent('views/box/bigScreen/warnningScreen/index.vue', {
  mocks: {
    '@element-plus/icons-vue': { Search: 'Search', Refresh: 'Refresh' },
    '../components/flvVideo.vue': { default: { render() { return h('preview-tile', this.$attrs) } } },
    '../components/detailDialog.vue': noopComponent,
    '../components/captureDialog.vue': noopComponent,
    '../components/TreeSelect.vue': noopComponent,
    ...assets,
    '@/components/eventBus': { default: { $emit() {} } },
    '@/utils/i18nResource': { resolveResourceAlgorithmName: () => '' },
    '@/utils/format': { formatSimilarity: value => value },
    moment: { default: () => ({ format: () => '', startOf() { return this }, endOf() { return this }, valueOf: () => 0 }) },
    lodash: { default: lodash }
  },
  globals: {
    localStorage: { getItem: key => liveStorage.get(key) ?? null, setItem: (key, value) => liveStorage.set(key, value) },
    document: { addEventListener() {}, removeEventListener() {}, body: {} },
    window: { addEventListener() {}, removeEventListener() {} },
    requestAnimationFrame: () => 1, cancelAnimationFrame() {}
  },
  route: { query: { channelId: 'camera-1', algorithmId: 'a' } },
  api: {
    queryPopUpParam: pending,
    boxQueryCameraList: () => {
      if (calls++ === 0) return Promise.resolve(response(enabledTasks))
      const request = deferred()
      requests.push(request)
      return request.promise
    },
    boxQueryEvent: pending, boxAllAlgorithmInfo: pending, bigScreenWs: () => ''
  }
})
try {
  live.instance.$.appContext.components['el-tree'] = {
    methods: { filter() {} },
    render() { return h('el-tree', this.$attrs, (this.$attrs.data?.[0]?.children || [])
      .flatMap(data => this.$slots.default({ node: { label: data.label }, data }))) }
  }
  const tile = () => live.all(n => n.type === 'preview-tile')[0]
  const toggle = async () => {
    live.all(n => n.props.id === 'onboarding-camera-toggle')[0].props.onClick()
    await live.settle()
  }
  const refresh = async () => {
    const button = live.all(n => n.type === 'button' && n.props.class === 'camera-refresh')[0]
    assert.equal(button.props['aria-label'], 'action.refresh', 'visible named refresh control')
    button.props.onClick()
    await live.settle()
  }
  const selectNode = async () => {
    live.all(n => typeof n.props.onDblclick === 'function')[0].props.onDblclick()
    await live.settle()
  }
  assert.equal(tile().props.runAlgorithmId, 'a')
  tile().props.onRunAlgorithmIdChange({ channelId: 'camera-1', index: tile().props.index,
    runAlgorithmId: '', retiredAlgorithmId: 'a' })
  await live.settle()
  assert.equal(tile().props.taskList[0].enableStatus, 0)
  await toggle()
  assert.equal(requests.length, 1, 'drawer opening queries server')
  requests[0].resolve(response(enabledTasks))
  await live.settle()
  assert.equal(tile().props.runAlgorithmId, '', 'metadata refresh preserves raw; does not replay deep link')
  assert.equal(tile().props.taskList[0].enableStatus, 1, 're-enabled task restored without page reload')
  assert.equal(tile().props.cameraName, 'Fresh camera')
  console.log('PASS external re-enable refreshes active metadata and preserves raw selection')

  await toggle() // close
  await toggle() // older request
  await toggle() // close while pending
  await toggle() // newer request
  requests[1].resolve(response(disabledTasks))
  await live.settle()
  assert.equal(tile().props.taskList[0].enableStatus, 1, 'older response cannot overwrite current metadata')
  await selectNode()
  assert.equal(tile().props.runAlgorithmId, '', 'selection blocked until newest request completes')
  requests[2].resolve(response(enabledTasks))
  await live.settle()
  console.log('PASS rapid drawer reopening ignores stale responses and blocks stale selection')

  await refresh()
  requests[3].reject(new Error('test query failure'))
  await live.settle()
  await selectNode()
  assert.equal(tile().props.runAlgorithmId, '', 'failed refresh cannot select cached node')
  await refresh()
  requests[4].resolve(response(enabledTasks))
  await live.settle()
  await selectNode()
  assert.equal(tile().props.runAlgorithmId, 'a', 'successful retry allows fresh algorithm selection')
  console.log('PASS failed refresh leaves stale nodes blocked and visible retry recovers selection')

  tile().props.onStop(0)
  await live.settle()
  assert.equal(tile(), undefined)
  await toggle()
  await selectNode()
  assert.equal(tile(), undefined, 'closed window cannot reopen from a stale node while fetching')
  requests[5].resolve(response(enabledTasks))
  await live.settle()
  await selectNode()
  assert.equal(tile().props.runAlgorithmId, 'a', 'close and reopen uses freshly enabled task')
  console.log('PASS close/reopen waits for server metadata and selects re-enabled algorithm')

  await toggle()
  requests[6].resolve(response(disabledTasks))
  await live.settle()
  assert.equal(tile().props.runAlgorithmId, '', 'refresh also retires a newly disabled active selection')
  assert.equal(tile().props.taskList[0].enableStatus, 0)
  console.log('PASS later disable returns current selection to raw')

  await refresh()
  tile().props.onRunAlgorithmIdChange({ channelId: 'camera-1', index: tile().props.index,
    runAlgorithmId: '', retiredAlgorithmId: 'a' })
  await live.settle()
  requests[7].resolve(response(enabledTasks))
  await live.settle()
  assert.equal(tile().props.taskList[0].enableStatus, 0, 'pre-retirement query cannot resurrect disabled algorithm')
  requests[8].resolve(response(disabledTasks))
  await live.settle()
  assert.equal(tile().props.taskList[0].enableStatus, 0)
  console.log('PASS task-stop event invalidates older in-flight metadata query')
} finally { live.unmount() }
