import assert from 'node:assert/strict'
import * as Vue from 'vue'
import moment from 'moment'
import lodash from 'lodash'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

// Exercise the real realtime player. Only the transport and browser timers are
// controlled: changing presentation must retain video elements and live viewers.
const hasClass = (node, name) => String(node.props.class || '').split(/\s+/).includes(name)
for (const protocol of ['httpflv', 'webrtc']) {
  // The first player omits fitMode to exercise the production prop default.
  const fits = Vue.reactive([undefined, 'contain'])
  const tasks = Vue.ref([])
  const firstOverlay = Vue.ref('')
  let failRaw = false
  let overlayOpens = 0
  const fullscreen = Vue.ref(null)
  const transports = [], timers = new Map(), calls = { request: [], stop: [], heartbeat: [] }
  let timerId = 0
  const schedule = callback => { const id = ++timerId; timers.set(id, callback); return id }
  const makeTransport = video => {
    const player = Vue.markRaw({ video, starts: 0, stops: 0, destroys: 0,
      play() { this.starts++ }, pause() { this.stops++ }, unload() {}, detachMediaElement() {},
      destroy() { this.destroys++ }, stop() { this.stops++ }
    })
    transports.push(player)
    return player
  }
  const flv = { isSupported: () => true, Events: { ERROR: 'error', SCRIPTDATA_ARRIVED: 'metadata' },
    createPlayer() {
      const listeners = new Map(), transport = makeTransport(null)
      Object.assign(transport, {
        attachMediaElement(video) {
          this.video = Vue.toRaw(video)
          video.buffered = { length: 0 }
          video.addEventListener = (event, fn) => listeners.set(event, fn)
          video.removeEventListener = (event, fn) => { if (listeners.get(event) === fn) listeners.delete(event) }
        },
        on() {}, off() {}, load() { listeners.get('canplay')?.() }
      })
      return transport
    }
  }
  const view = await mountComponent('views/box/bigScreen/components/flvVideo.vue', {
    mocks: {
      'flv.js': { default: flv },
      '@element-plus/icons-vue': Object.fromEntries(['FullScreen', 'ScaleToOriginal', 'Close', 'VideoCamera'].map(name => [name, name])),
      '@/utils/i18nResource': { resolveResourceAlgorithmName: item => item.algorithmName },
      '@/utils/whepPlayer': { createWhepPlayer(options) {
        const transport = makeTransport(Vue.toRaw(options.video))
        transport.start = () => { transport.starts++; options.onConnected() }
        return transport
      } }
    },
    globals: {
      localStorage: { getItem: () => null },
      window: { location: { hostname: 'fixture.invalid', protocol: 'http:' }, RTCPeerConnection: class {} },
      setTimeout: schedule, clearTimeout: id => timers.delete(id),
      setInterval: schedule, clearInterval: id => timers.delete(id),
      console: { log() {}, warn: (...args) => assert.fail(args.join(' ')), error: (...args) => assert.fail(args.join(' ')) }
    },
    api: {
      async boxRequestLiveStream(params) {
        calls.request.push(params)
        if (failRaw && params.channelId === 'channel-0' && !params.algorithmId) throw new Error('Raw stream unavailable')
        return { resData: { stream: { protocol, flvUrl: '/fixture/live.flv', webrtcUrl: '/fixture/whep', httpPort: 8080, rtcApiPort: 1985, keepAliveInterval: 10 } } }
      },
      async boxStreamKeepAlive(params) { calls.heartbeat.push(params) },
      async boxStreamStop(params) { calls.stop.push(params) }
    },
    message: { error: message => assert.fail(message) },
    wrap: Player => ({ setup: () => () => Vue.h('fixture-wall', {}, fits.map((fit, index) => Vue.h(Player, {
      key: index, index, channelId: `channel-${index}`, cameraName: `Camera ${index}`, taskList: tasks.value,
      runAlgorithmId: index === 0 ? firstOverlay.value : '',
      onRunAlgorithmIdChange: ({ runAlgorithmId }) => { if (index === 0) firstOverlay.value = runAlgorithmId },
      onRefreshTasks: () => overlayOpens++,
      ...(fit === undefined ? {} : { fitMode: fit }), 'onUpdate:fitMode': value => { fits[index] = value },
      isFullScreen: fullscreen.value === index ? index : null,
      onFullScreen: event => { fullscreen.value = event.isFullScreen ? index : null }
    }))) })
  })
  const flush = async () => { for (let step = 0; step < 12; step++) await view.settle() }
  const media = () => view.all(node => node.type === 'video')
  const fitButtons = () => view.all(node => hasClass(node, 'video-fit'))
  const diagnosticButtons = () => view.all(node => hasClass(node, 'video-diagnostics'))
  try {
    await flush()
    const originalVideo = media()
    assert.equal(originalVideo.length, 2)
    assert.equal(calls.request.length, 2)
    assert.equal(transports.length, 2)
    tasks.value = [
      { algorithmId: 1, algorithmName: 'Old', enableStatus: 0 },
      { algorithmId: 2, algorithmName: 'New', enableStatus: 1 }
    ]
    await flush()
    const select = view.all(node => node.type === 'el-select' && node.props.id === 'onboarding-overlay-select')[0]
    assert.deepEqual(view.all(node => node.type === 'el-option', select).map(node => node.props.value), ['', '2'])
    select.props.onVisibleChange(true)
    select.props.onVisibleChange(false)
    assert.equal(overlayOpens, 1, 'opening the overlay selector requests fresh channel tasks')
    diagnosticButtons()[0].props.onClick({ stopPropagation() {} })
    await flush()
    assert.equal(view.all(node => hasClass(node, 'live-diagnostics')).length, 1, 'diagnostics opens only in the selected player')
    assert.equal(diagnosticButtons()[0].props['aria-expanded'], true)
    assert.equal(diagnosticButtons()[1].props['aria-expanded'], false)
    assert.ok(media().every((node, index) => node === originalVideo[index]), 'opening diagnostics retains live video elements')
    diagnosticButtons()[0].props.onClick({ stopPropagation() {} })
    await flush()
    assert.equal(view.all(node => hasClass(node, 'live-diagnostics')).length, 0, 'diagnostics closes without touching another player')
    const heartbeatCount = calls.heartbeat.length
    assert.deepEqual(media().map(node => node.props.style.objectFit), ['contain', 'contain'], 'an omitted fitMode and an explicit full-frame choice both retain the entire image')
    assert.ok(fitButtons().every(button => button.props['aria-label'] === 'event.fitContain'))
    assert.ok(fitButtons().every(button => button.children.find(node => node.type === 'span')?.text === 'event.fitContain'))
    assert.ok(fitButtons().every(button => button.props.title === 'event.fitCoverHint'), 'the label describes the current mode while the hint describes the next action')
    for (const index of [0, 0, 0, 1, 1]) {
      fitButtons()[index].props.onClick()
      await flush()
    }
    assert.deepEqual([...fits], ['cover', 'contain'], 'each window owns its display choice')
    assert.deepEqual(media().map(node => node.props.style.objectFit), ['cover', 'contain'])
    assert.equal(fitButtons()[0].props['aria-label'], 'event.fitCover')
    assert.equal(fitButtons()[0].children.find(node => node.type === 'span')?.text, 'event.fitCover')
    assert.equal(fitButtons()[0].props.title, 'event.fitContainHint')
    assert.equal(fullscreen.value, null, 'fit control does not request fullscreen')
    const fullButton = view.all(node => node.type === 'button' && node.props['aria-label'] === 'event.fullscreen')[0]
    fullButton.props.onClick()
    await flush()
    assert.equal(fullscreen.value, 0)
    assert.deepEqual([...fits], ['cover', 'contain'], 'fullscreen keeps the chosen display mode')
    view.all(node => node.type === 'button' && node.props['aria-label'] === 'event.exitFullscreen')[0].props.onClick()
    await flush()
    assert.equal(fullscreen.value, null)
    assert.ok(media().every((node, index) => node === originalVideo[index]), 'presentation retains the actual video element')
    assert.equal(calls.request.length, 2, 'fit and fullscreen never acquire replacement viewers')
    assert.equal(calls.stop.length, 0, 'fit and fullscreen never release live viewers')
    assert.equal(calls.heartbeat.length, heartbeatCount, 'fit and fullscreen do not restart heartbeat')
    assert.equal(transports.length, 2, 'fit and fullscreen do not create transports')
    assert.ok(transports.every(player => player.starts === 1 && player.stops === 0 && player.destroys === 0))
    firstOverlay.value = '2'
    await flush()
    assert.equal(calls.stop.length, 1, 'selecting the overlay retires the first raw viewer')
    failRaw = true
    tasks.value = []
    firstOverlay.value = ''
    await flush()
    assert.equal(firstOverlay.value, '', 'a failed raw fallback cannot restore the retired overlay')
    assert.equal(calls.stop.length, 2, 'the disabled overlay viewer is released once')
  } finally {
    view.unmount()
    await flush()
  }
  assert.equal(calls.stop.length, 3, 'normal unmount releases exactly the remaining viewer')
  assert.equal(timers.size, 0, 'normal cleanup clears all viewer timers')
}
// Mount the actual wall as well: the parent supplies fitMode explicitly, so a
// safe player prop default alone would not prevent the original regression.
const channels = Array.from({ length: 4 }, (_, index) => ({
  videoChannelId: `fixture-channel-${index}`, channelName: `Camera ${index}`,
  channelStatus: 1, channelType: 1, taskList: []
}))
const task = (algorithmId, enableStatus) => ({ algorithmId, algorithmName: algorithmId, enableStatus })
const taskA = task('A', 1)
const taskB = task('B', 1)
channels[0].taskList = [taskA, taskB]
const savedWindows = [channels[0], channels[0], channels[0], channels[3]].map((channel, index) => ({
  id: channel.videoChannelId, name: 'Old name', taskList: [taskA], runAlgorithmId: ['A', 'B', '', ''][index]
}))
const storage = new Map([['playedCameraList', JSON.stringify(savedWindows)]])
const listeners = new Map()
const pageDocument = {
  hidden: false,
  addEventListener: (name, fn) => listeners.set(name, fn),
  removeEventListener(name, fn) { if (listeners.get(name) === fn) listeners.delete(name) }
}
let channelQueries = 0
let delayedResponse
let deferQuery = false
const noContent = { default: { render: () => null } }
const wallTimers = new Map()
let wallTimerId = 0
const scheduleWall = (callback, delay) => { const id = ++wallTimerId; wallTimers.set(id, { callback, delay }); return id }
const mountWall = () => mountComponent('views/box/bigScreen/warnningScreen/index.vue', {
  wrap: component => ({
    ...component,
    components: {
      ...component.components,
      'el-tree': {
        props: ['data'],
        methods: { setCurrentKey() {}, filter() {} },
        render() {
          return Vue.h('fixture-tree', this.data[0].children.map(data => this.$slots.default({ data })))
        }
      }
    }
  }),
  mocks: {
    '@/assets/beep.ogg': { default: '' },
    '@/assets/error-image.png': { default: '' },
    '@element-plus/icons-vue': Object.fromEntries(['Search', 'Setting', 'FullScreen', 'Back', 'Refresh', 'ArrowLeft', 'ArrowRight', 'VideoCamera', 'Bell'].map(name => [name, name])),
    '../components/flvVideo.vue': { default: {
      props: ['fitMode', 'index', 'isFullScreen', 'taskList', 'channelId', 'runAlgorithmId'], emits: ['update:fitMode', 'fullScreen', 'refresh-tasks', 'runAlgorithmIdChange', 'stop'],
      setup(props, { emit }) {
        return () => Vue.h('fixture-preview', { ...props,
          chooseFit: value => emit('update:fitMode', value),
          maximize: () => emit('fullScreen'),
          openOverlay: () => emit('refresh-tasks'),
          selectOverlay: id => emit('runAlgorithmIdChange', { index: props.index, channelId: props.channelId, runAlgorithmId: id }),
          close: () => emit('stop', props.index)
        })
      }
    } },
    '@/components/ThemeSwitcher.vue': noContent,
    '../components/detailDialog.vue': noContent,
    '../components/captureDialog.vue': noContent,
    '../components/TreeSelect.vue': noContent,
    '@/utils/i18nResource': { resolveResourceAlgorithmName: item => item.algorithmName },
    '@/utils/format': { formatSimilarity: value => value },
    moment: { default: moment }, lodash: { default: lodash }
  },
  globals: {
    localStorage: { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value) },
    window: { addEventListener() {}, removeEventListener() {} },
    document: pageDocument,
    WebSocket: class { close() {} },
    setTimeout: scheduleWall, clearTimeout: id => wallTimers.delete(id),
    setInterval: scheduleWall, clearInterval: id => wallTimers.delete(id),
    console: { log() {}, warn: (...args) => assert.fail(args.join(' ')), error: (...args) => assert.fail(args.join(' ')) }
  },
  api: {
    async queryPopUpParam() { return { resData: { popUpSwitch: 0, audioPlay: 0, popUpDuration: 2 } } },
    async getChannelList() {
      channelQueries++
      if (deferQuery) await new Promise(resolve => { delayedResponse = resolve })
      return { resData: { rows: structuredClone(channels) } }
    },
    async boxQueryEvent() { return { resData: { rows: [], total: 0 } } },
    async algorithmInquire() { return { resData: { rows: [] } } },
    bigScreenWs: () => 'ws://fixture.invalid/events'
  }
})
const wall = await mountWall()
const previews = () => wall.all(node => node.type === 'fixture-preview')
const wallFits = () => previews().map(node => node.props.fitMode)
try {
  await wall.settle()
  assert.deepEqual(wallFits(), ['contain', 'contain', 'contain', 'contain'], 'all four real wall slots open without cropping')
  const overlays = () => previews().slice(0, 3).map(node => node.props.runAlgorithmId)
  const taskLists = () => previews().slice(0, 3).map(node => Array.from(node.props.taskList, t => [t.algorithmId, t.enableStatus]))
  assert.deepEqual(overlays(), ['A', 'B', ''], 'restore valid choices and raw video independently')
  assert.deepEqual(taskLists(), Array(3).fill([['A', 1], ['B', 1]]), 'discard the stale task catalog from every cached window')

  channels[0].taskList = [task('A', 0), taskB, task('C', 1)]
  const poll = Array.from(wallTimers.values()).find(timer => timer.delay === 30000).callback
  pageDocument.hidden = true
  listeners.get('visibilitychange')()
  poll()
  assert.equal(channelQueries, 1, 'hidden pages do not poll channels')
  pageDocument.hidden = false
  deferQuery = true
  listeners.get('visibilitychange')()
  previews()[0].props.openOverlay()
  poll()
  assert.equal(channelQueries, 2, 'concurrent refresh triggers share one channel request')
  delayedResponse()
  await wall.settle()
  assert.deepEqual(taskLists(), Array(3).fill([['A', 0], ['B', 1], ['C', 1]]))
  assert.deepEqual(overlays(), ['', 'B', ''], 'only the disabled selection reverts to raw video')

  previews()[1].props.openOverlay()
  assert.equal(channelQueries, 3)
  previews()[1].props.selectOverlay('C')
  delayedResponse()
  await wall.settle()
  assert.deepEqual(overlays(), ['', 'C', ''], 'an in-flight response respects a newer selection')

  wall.all(node => node.props.id === 'onboarding-camera-toggle')[0].props.onClick({ stopPropagation() {} })
  await wall.settle()
  wall.all(node => node.type === 'button' && node.props.title === 'action.refresh')[0].props.onClick()
  assert.equal(channelQueries, 4, 'manual refresh rechecks the channel list')
  delayedResponse()
  await wall.settle()
  assert.deepEqual(overlays(), ['', 'C', ''])
  assert.ok(JSON.parse(storage.get('playedCameraList')).every(slot => !('taskList' in slot)), 'persist choices without a stale catalog')
  previews()[1].props.chooseFit('cover')
  await wall.settle()
  assert.deepEqual(wallFits(), ['contain', 'cover', 'contain', 'contain'], 'an explicit crop choice affects only its own window')
  previews()[1].props.maximize()
  await wall.settle()
  assert.equal(previews()[1].props.isFullScreen, 1)
  assert.deepEqual(wallFits(), ['contain', 'cover', 'contain', 'contain'], 'maximizing retains each window display choice')
  previews()[1].props.maximize()
  await wall.settle()
  assert.ok(previews().every(node => node.props.isFullScreen === null))
  const screenButtons = () => wall.all(node => hasClass(node, 'screen-btn'))
  screenButtons()[0].props.onClick()
  await wall.settle()
  assert.equal(previews().length, 1)
  assert.equal(previews()[0].props.index, 1, 'single-screen retains the selected window')
  assert.equal(previews()[0].props.fitMode, 'cover')
  screenButtons()[1].props.onClick()
  await wall.settle()
  assert.deepEqual(wallFits(), ['contain', 'cover', 'contain', 'contain'], 'four-screen restores the independent modes')
  poll()
  previews()[3].props.close()
  delayedResponse()
  await wall.settle()
  assert.equal(previews().length, 3, 'refresh must not reopen a window closed while the request was pending')
  poll()
} finally {
  wall.unmount()
}
const layoutAfterExit = storage.get('playedCameraList')
delayedResponse()
await wall.settle()
assert.equal(storage.get('playedCameraList'), layoutAfterExit, 'a late response cannot rewrite a closed wall')
assert.equal(listeners.has('visibilitychange'), false)
assert.equal(wallTimers.size, 0, 'wall cleanup clears its timers')
deferQuery = false
for (const { assignments, expected } of [
  { assignments: [], expected: 0 },
  { assignments: [{ id: 'removed-channel' }], expected: 0 },
  { assignments: savedWindows, expected: 3 }
]) {
  storage.set('screenStatus', JSON.stringify({ screenType: 4, currentSelectedIndex: 3 }))
  storage.set('playedCameraList', JSON.stringify(assignments))
  const display = await mountWall()
  const slots = () => display.all(node => hasClass(node, 'video-item'))
  const selectedSlot = () => slots().findIndex(node => hasClass(node, 'video-active'))
  const screenButtons = () => display.all(node => hasClass(node, 'screen-btn'))
  try {
    await display.settle()
    assert.equal(selectedSlot(), expected, 'restore selection only when an assigned preview still exists')

    slots()[2].props.onClick()
    await display.settle()
    assert.equal(selectedSlot(), 2, 'an empty wall still accepts explicit selection')
    screenButtons()[0].props.onClick()
    await display.settle()
    screenButtons()[1].props.onClick()
    await display.settle()
    const next = expected === 0 ? 0 : 2
    assert.equal(selectedSlot(), next, 'switching back to an empty wall selects the first slot')
    assert.equal(JSON.parse(storage.get('screenStatus')).currentSelectedIndex, next)
  } finally {
    display.unmount()
  }
}
for (const screenType of [1, 4]) {
  storage.set('screenStatus', JSON.stringify({ screenType, currentSelectedIndex: 0 }))
  storage.set('playedCameraList', '[]')
  const display = await mountWall()
  const cameraToggle = () => display.all(node => node.props.id === 'onboarding-camera-toggle')[0]
  const channelRows = () => display.all(node => hasClass(node, 'custom-tree-node'))
  const openOrCloseDrawer = () => cameraToggle().props.onClick({ stopPropagation() {} })
  try {
    await display.settle()
    openOrCloseDrawer()
    await display.settle()
    assert.equal(cameraToggle().props['aria-expanded'], true)
    channelRows()[0].props.onDblclick()
    await display.settle()
    assert.equal(display.all(node => node.type === 'fixture-preview')[0].props.channelId, channels[0].videoChannelId)
    assert.equal(cameraToggle().props['aria-expanded'], true, 'opening a preview keeps the channel list open')
    channelRows()[1].props.onDblclick()
    await display.settle()
    const previews = display.all(node => node.type === 'fixture-preview')
    const expected = screenType === 1 ? channels.slice(1, 2) : channels.slice(0, 2)
    assert.deepEqual(previews.map(node => node.props.channelId), expected.map(channel => channel.videoChannelId))
    const audio = display.all(node => node.type === 'audio')[0]
    audio.play = async () => {}
    audio.pause = () => {}
    const drawer = display.all(node => hasClass(node, 'select-area'))[0]
    drawer.contains = target => display.all(node => node === target, drawer).length > 0

    const lastPreview = previews.at(-1)
    lastPreview.props.close()
    listeners.get('click')?.({ target: lastPreview })
    await display.settle()
    assert.equal(display.all(node => node.type === 'fixture-preview').length, previews.length - 1)
    assert.equal(cameraToggle().props['aria-expanded'], true, 'closing a preview keeps the drawer open')
    listeners.get('click')?.({ target: display.root })
    await display.settle()
    assert.equal(cameraToggle().props['aria-expanded'], true, 'clicking elsewhere keeps the drawer open')

    channelRows()[1].props.onDblclick()
    await display.settle()
    assert.equal(display.all(node => hasClass(node, 'channel-window')).length, 0, 'playing channels show no window badge')
    assert.ok(hasClass(channelRows()[1], 'is-assigned'), 'playing channels retain their row styling')
    openOrCloseDrawer()
    await display.settle()
    assert.equal(cameraToggle().props['aria-expanded'], false, 'the drawer still closes from its toggle')
  } finally {
    display.unmount()
  }
}
console.log('Realtime display checks passed: full-frame defaults in the real player and wall, independent fit modes, retained video/FLV/WebRTC viewers and heartbeat, single/four/fullscreen presentation, normal cleanup. Transports are controlled; device layout remains a separate check.')
