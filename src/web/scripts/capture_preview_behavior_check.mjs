import assert from 'node:assert/strict'
import * as Vue from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const plain = value => JSON.parse(JSON.stringify(value))
const text = node => [node.text, ...node.children.map(text)].join(' ')
const hasClass = (node, name) => String(node.props.class || '').split(/\s+/).includes(name)
const channel = { videoChannelId: 'existing-channel', channelName: 'Existing test channel', channelStatus: 1, channelType: 0 }

// Only Element Plus rendering, ROI drawing and transport are controlled. Both
// Capture parents and the shared flvjs SFC execute their production handlers.
// No real streams, library creation, capture or save requests are permitted.
const Tree = {
  inheritAttrs: false,
  props: ['data'],
  methods: { filter() {}, setCurrentKey() {} },
  render() {
    const rows = list => list.flatMap(data => [
      ...(this.$slots.default?.({ node: { label: data.label }, data }) || []),
      ...rows(data.children || [])
    ])
    return Vue.h('fixture-tree', this.$attrs, rows(this.data || []))
  }
}
const Transition = { setup: (_, { slots }) => () => slots.default?.() }

for (const library of ['workClothes', 'item']) {
  for (const protocol of ['httpflv', 'webrtc']) {
    const visible = Vue.ref(false)
    const appearance = Vue.shallowRef({ preference: 'light', resolved: 'light' })
    const timers = new Map()
    const transports = []
    const calls = { cameraList: [], request: [], stop: [], heartbeat: [] }
    const warnings = []
    let timerId = 0
    const schedule = (callback, delay, repeat) => {
      const id = ++timerId
      timers.set(id, { callback, delay, repeat })
      return id
    }
    const runTimeouts = delay => {
      const pending = [...timers].filter(([, timer]) => !timer.repeat && timer.delay === delay)
      assert.ok(pending.length, `expected a scheduled ${delay}ms callback`)
      for (const [id, timer] of pending) { timers.delete(id); timer.callback() }
    }
    const document = { hidden: false, onvisibilitychange: null, documentElement: { dataset: { theme: 'light' } } }
    const window = { location: { hostname: 'fixture.invalid', protocol: 'http:' }, RTCPeerConnection: class {} }
    const flv = {
      isSupported: () => true,
      Events: { ERROR: 'error', SCRIPTDATA_ARRIVED: 'metadata' },
      createPlayer(options) {
        assert.equal(protocol, 'httpflv')
        assert.match(options.url, /^http:\/\/fixture\.invalid:8080\//)
        const listeners = new Map()
        const events = new Map()
        const player = Vue.markRaw({
          kind: 'httpflv', starts: 0, stops: 0, unloaded: 0, detached: 0, destroyed: 0, video: null,
          attachMediaElement(element) {
            this.video = Vue.toRaw(element)
            element.buffered = { length: 0 }
            element.addEventListener = (name, callback) => listeners.set(name, callback)
            element.removeEventListener = (name, callback) => {
              if (listeners.get(name) === callback) listeners.delete(name)
            }
          },
          load() { listeners.get('canplay')?.() },
          play() { this.starts++ },
          pause() { this.stops++ },
          unload() { this.unloaded++ },
          detachMediaElement() { this.detached++ },
          destroy() { this.destroyed++ },
          on(name, callback) { events.set(name, callback) },
          off(name) { events.delete(name) },
          emit(name) { events.get(name)?.() }
        })
        transports.push(player)
        return player
      }
    }
    const mocks = {
      vue: { ...Vue, Transition },
      moment: { default: () => { throw new Error('unexpected date conversion') } },
      '@element-plus/icons-vue': Object.fromEntries(['DArrowRight', 'Search', 'Close', 'CopyDocument', 'FullScreen'].map(name => [name, name])),
      './mVCanvas.vue': { default: { render: () => Vue.h('fixture-roi-canvas') } },
      'flv.js': { default: flv },
      '@/utils/whepPlayer': {
        createWhepPlayer(options) {
          assert.equal(protocol, 'webrtc')
          assert.match(options.url, /^http:\/\/fixture\.invalid:1985\//)
          const player = Vue.markRaw({
            kind: 'webrtc', starts: 0, stops: 0, video: Vue.toRaw(options.video),
            start() { this.starts++; options.onConnected() },
            stop() { this.stops++ }
          })
          transports.push(player)
          return player
        }
      },
      '@/composables/useAppearance': { useAppearance: () => ({ appearance: Vue.readonly(appearance), setAppearance() {} }) }
    }
    for (const name of ['close-circle.png', 'check-circle.png', 'zanwu1.png', 'auth_fail.png', 'video_preview.png']) {
      mocks[`@/assets/${name}`] = { default: `fixture-asset:${name}` }
    }
    const view = await mountComponent(`views/box/basePicManagement/${library}/components/Capture.vue`, {
      mocks,
      globals: {
        window, document,
        console: { log() {}, warn: (...args) => warnings.push(args), error: (...args) => warnings.push(args) },
        setTimeout: (fn, delay) => schedule(fn, delay, false),
        clearTimeout: id => timers.delete(id),
        setInterval: (fn, delay) => schedule(fn, delay, true),
        clearInterval: id => timers.delete(id),
        fetch() { assert.fail('network access is forbidden in this component check') }
      },
      api: new Proxy({
        async boxQueryCameraList(params) { calls.cameraList.push(plain(params)); return { resData: { rows: [channel] } } },
        async boxRequestLiveStream(params) {
          calls.request.push(plain(params))
          assert.equal(params.channelId, channel.videoChannelId)
          return { resData: { stream: { protocol, flvUrl: '/fixture/live.flv', webrtcUrl: '/fixture/whep', httpPort: 8080, rtcApiPort: 1985, keepAliveInterval: 10 } } }
        },
        async boxStreamStop(params) { calls.stop.push(plain(params)); return { resCode: 1 } },
        async boxStreamKeepAlive(params) { calls.heartbeat.push(plain(params)); return { resCode: 1 } }
      }, { get(target, key) { assert.ok(key in target, `unexpected business API: ${String(key)}`); return target[key] } }),
      message: { error: message => assert.fail(message), warning: message => assert.fail(message) },
      wrap: Capture => ({
        setup() {
          // Test-local Element registration; shared mounting helpers stay intact.
          Vue.getCurrentInstance().appContext.components['el-tree'] = Tree
          return () => Vue.h('fixture-theme-host', { 'data-theme': appearance.value.resolved }, [
            Vue.h(Capture, {
              visible: visible.value, personLibId: 'existing-library', thingsLibId: 'existing-library',
              'onUpdate:visible': value => { visible.value = value }
            })
          ])
        }
      })
    })
    const flush = async () => { for (let i = 0; i < 4; i++) await view.settle() }
    const status = () => view.all(node => node.props.role === 'status')
    const media = () => view.all(node => node.type === 'video')
    const selectChannel = async () => {
      view.all(node => hasClass(node, 'arrow-toggle'))[0].props.onClick()
      await flush()
      const row = view.all(node => hasClass(node, 'custom-tree-node') && text(node).includes(channel.channelName))[0]
      assert.ok(row, 'the parent renders an existing API channel')
      row.props.onDblclick()
      await flush()
      runTimeouts(100)
      await flush()
      if (protocol === 'httpflv') {
        transports.at(-1).emit(flv.Events.SCRIPTDATA_ARRIVED)
        runTimeouts(1000)
        await flush()
      }
    }
    const assertReleased = player => {
      assert.equal(player.stops, 1, 'one transport stop per viewer lifetime')
      if (protocol === 'httpflv') {
        assert.equal(player.unloaded, 1)
        assert.equal(player.detached, 1)
        assert.equal(player.destroyed, 1)
      }
    }
    let unmounted = false
    try {
      visible.value = true
      await flush()
      assert.equal(calls.cameraList.length, 1)
      assert.equal(calls.request.length, 0, 'opening Capture does not start an unselected stream')
      await selectChannel()
      assert.equal(calls.request.length, 1)
      assert.equal(transports.length, 1)
      const first = transports[0]
      const originalVideo = media()[0]
      assert.ok(originalVideo)
      assert.equal(first.video, originalVideo)
      assert.equal(first.starts, 1)
      assert.equal(status().length, 0)
      assert.equal(view.all(node => hasClass(node, 'loading-spinner')).length, 0)
      const timerIdentity = [...timers.keys()]
      for (const [preference, resolved] of [['dark', 'dark'], ['system', 'dark'], ['system', 'light'], ['light', 'light']]) {
        document.documentElement.dataset.theme = resolved
        appearance.value = { preference, resolved }
        await flush()
        assert.equal(view.all(node => node.type === 'fixture-theme-host')[0].props['data-theme'], resolved)
        assert.equal(media()[0], originalVideo, 'a theme-only host update retains the media element')
        assert.equal(transports.length, 1, 'theme switching must not create another transport')
        assert.equal(first.stops, 0)
        assert.equal(calls.request.length, 1)
        assert.equal(calls.stop.length, 0)
        assert.deepEqual([...timers.keys()], timerIdentity, 'theme switching retains heartbeat/chase timers')
      }
      for (const timer of timers.values()) if (timer.repeat && timer.delay === 10000) timer.callback()
      await flush()
      assert.deepEqual(calls.heartbeat, [{ channelId: channel.videoChannelId, algorithmId: '' }])

      const container = view.all(node => hasClass(node, 'video-container'))[0]
      container.props.onMouseover()
      await flush()
      const stopButton = view.all(node => node.type === 'el-icon' && hasClass(node.parent, 'video-top-control'))[0]
      assert.ok(stopButton, 'real player exposes its stop action')
      stopButton.props.onClick()
      await flush()
      assert.equal(status().length, 1)
      assert.match(text(status()[0]), /common.stopPreview/)
      assert.equal(status()[0].parent, container, 'stopped status belongs to the same preview viewport')
      assertReleased(first)
      assert.equal(timers.size, 0, 'stop clears viewer heartbeat and playback timers')
      assert.deepEqual(calls.stop, [{ channelId: channel.videoChannelId, algorithmId: '' }])

      await selectChannel()
      assert.equal(calls.cameraList.length, 1, 'replay reuses the selected existing channel list')
      assert.equal(calls.request.length, 2, 'same-channel double click acquires a fresh viewer')
      assert.equal(transports.length, 2)
      assert.equal(transports[1].starts, 1)
      assert.notEqual(media()[0], originalVideo, 'same-channel replay mounts a fresh media instance')
      assert.equal(status().length, 0, 'replay removes the stopped status')
      assert.equal(calls.stop.length, 1, 'unmounting the already stopped player does not double-release')
      const dialog = view.all(node => node.type === 'el-dialog')[0]
      dialog.props.onClose()
      await flush()
      assert.equal(visible.value, false, 'real Capture close emits its parent visibility update')
      assert.equal(view.all(node => node.type === 'el-dialog').length, 0)
      assert.equal(media().length, 0)
      assert.equal(status().length, 0)
      assertReleased(transports[1])
      assert.deepEqual(calls.stop, [
        { channelId: channel.videoChannelId, algorithmId: '' },
        { channelId: channel.videoChannelId, algorithmId: '' }
      ])
      assert.equal(timers.size, 0)
      assert.equal(document.onvisibilitychange, null, 'viewer visibility listener is removed on unmount')
      assert.equal(warnings.length, 0, 'no hidden stream or playback failures')
      view.unmount()
      unmounted = true
      await flush()
      assert.equal(calls.stop.length, 2)
      console.log(`Capture preview passed: ${library} / ${protocol}`)
    } finally {
      if (!unmounted) view.unmount()
    }
  }
}
console.log('Controlled component checks passed: two real Capture parents and shared player; existing-channel select, theme-preserved video/transport/timers, stop status, same-channel replay, close cleanup. Transport/Element/ROI are stubs; this is not physical-device acceptance.')
