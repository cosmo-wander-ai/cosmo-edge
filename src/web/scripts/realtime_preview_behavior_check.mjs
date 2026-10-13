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
        return { resData: { stream: { protocol, flvUrl: '/fixture/live.flv', webrtcUrl: '/fixture/whep', httpPort: 8080, rtcApiPort: 1985, keepAliveInterval: 10 } } }
      },
      async boxStreamKeepAlive(params) { calls.heartbeat.push(params) },
      async boxStreamStop(params) { calls.stop.push(params) }
    },
    message: { error: message => assert.fail(message) },
    wrap: Player => ({ setup: () => () => Vue.h('fixture-wall', {}, fits.map((fit, index) => Vue.h(Player, {
      key: index, index, channelId: `channel-${index}`, cameraName: `Camera ${index}`, runAlgorithmId: '', taskList: [],
      ...(fit === undefined ? {} : { fitMode: fit }), 'onUpdate:fitMode': value => { fits[index] = value },
      isFullScreen: fullscreen.value === index ? index : null,
      onFullScreen: event => { fullscreen.value = event.isFullScreen ? index : null }
    }))) })
  })
  const flush = async () => { for (let step = 0; step < 12; step++) await view.settle() }
  const media = () => view.all(node => node.type === 'video')
  const fitButtons = () => view.all(node => hasClass(node, 'video-fit'))
  try {
    await flush()
    const originalVideo = media()
    assert.equal(originalVideo.length, 2)
    assert.equal(calls.request.length, 2)
    assert.equal(transports.length, 2)
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
  } finally {
    view.unmount()
    await flush()
  }
  assert.equal(calls.stop.length, 2, 'normal unmount releases each viewer once')
  assert.equal(timers.size, 0, 'normal cleanup clears all viewer timers')
}
// Mount the actual wall as well: the parent supplies fitMode explicitly, so a
// safe player prop default alone would not prevent the original regression.
const channels = Array.from({ length: 4 }, (_, index) => ({
  videoChannelId: `fixture-channel-${index}`, channelName: `Camera ${index}`,
  channelStatus: 1, channelType: 1, taskList: []
}))
const storage = new Map([['playedCameraList', JSON.stringify(channels.map(channel => ({
  id: channel.videoChannelId, name: channel.channelName, taskList: [], runAlgorithmId: ''
})))]])
const noContent = { default: { render: () => null } }
const wallTimers = new Map()
let wallTimerId = 0
const scheduleWall = callback => { const id = ++wallTimerId; wallTimers.set(id, callback); return id }
const wall = await mountComponent('views/box/bigScreen/warnningScreen/index.vue', {
  mocks: {
    '@/assets/beep.ogg': { default: '' },
    '@/assets/error-image.png': { default: '' },
    '@element-plus/icons-vue': Object.fromEntries(['Search', 'Setting', 'FullScreen', 'Back', 'Refresh', 'ArrowLeft', 'ArrowRight', 'VideoCamera', 'Monitor', 'Bell'].map(name => [name, name])),
    '../components/flvVideo.vue': { default: {
      props: ['fitMode', 'index', 'isFullScreen'], emits: ['update:fitMode', 'fullScreen'],
      setup(props, { emit }) {
        return () => Vue.h('fixture-preview', { ...props,
          chooseFit: value => emit('update:fitMode', value),
          maximize: () => emit('fullScreen')
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
    document: { addEventListener() {}, removeEventListener() {} },
    WebSocket: class { close() {} },
    setTimeout: scheduleWall, clearTimeout: id => wallTimers.delete(id),
    setInterval: scheduleWall, clearInterval: id => wallTimers.delete(id),
    console: { log() {}, warn: (...args) => assert.fail(args.join(' ')), error: (...args) => assert.fail(args.join(' ')) }
  },
  api: {
    async queryPopUpParam() { return { resData: { popUpSwitch: 0, audioPlay: 0, popUpDuration: 2 } } },
    async getChannelList() { return { resData: { rows: channels } } },
    async boxQueryEvent() { return { resData: { rows: [], total: 0 } } },
    async algorithmInquire() { return { resData: { rows: [] } } },
    bigScreenWs: () => 'ws://fixture.invalid/events'
  }
})
const previews = () => wall.all(node => node.type === 'fixture-preview')
const wallFits = () => previews().map(node => node.props.fitMode)
try {
  await wall.settle()
  assert.deepEqual(wallFits(), ['contain', 'contain', 'contain', 'contain'], 'all four real wall slots open without cropping')
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
} finally {
  wall.unmount()
}
assert.equal(wallTimers.size, 0, 'wall cleanup clears its timers')
console.log('Realtime display checks passed: full-frame defaults in the real player and wall, independent fit modes, retained video/FLV/WebRTC viewers and heartbeat, single/four/fullscreen presentation, normal cleanup. Transports are controlled; device layout remains a separate check.')
