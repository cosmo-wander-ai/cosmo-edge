import assert from 'node:assert/strict'
import { reactive } from 'vue'
import { randomUUID } from 'node:crypto'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const deferred = () => {
  let resolve, reject
  const promise = new Promise((yes, no) => { resolve = yes; reject = no })
  return { promise, resolve, reject }
}
const flush = async screen => { for (let i = 0; i < 15; i++) await screen.settle() }
async function createScreen({ stopResponse, createResponse } = {}) {
  const calls = [], events = [], acquisitions = [], requests = [], intervals = new Map()
  let timerId = 0
  let nextHeartbeat = null
  let componentProps
  const props = reactive({
    channelId: 'camera-1', runAlgorithmId: 'a', index: 0, cameraName: 'Camera',
    taskList: [{ algorithmId: 'a', enableStatus: 1 }, { algorithmId: 'b', enableStatus: 1 }],
    onRunAlgorithmIdChange(event) {
      events.push(event)
      componentProps.channelId = event.channelId
      componentProps.runAlgorithmId = event.runAlgorithmId
    }
  })
  const screen = await mountComponent('views/box/bigScreen/components/flvVideo.vue', {
    props,
    mocks: {
      uuid: { v4: randomUUID },
      'flv.js': { default: { isSupported: () => false } },
      '@/utils/whepPlayer': { createWhepPlayer({ onConnected }) { return {
        start() { queueMicrotask(onConnected); return Promise.resolve() }, stop() { return Promise.resolve() }
      } } },
      '@/utils/i18nResource': { resolveResourceAlgorithmName: task => task.algorithmId },
      ...Object.fromEntries(['video_zoom_out.png', 'video_zoom_in.png', 'video_close.png', 'big_screen_no_camera.png']
        .map(name => [`@/assets/${name}`, { default: '' }]))
    },
    globals: {
      localStorage: { getItem: () => null },
      window: { location: { hostname: 'unit.test', protocol: 'http:' }, RTCPeerConnection() {} },
      setInterval(callback, ms) { const id = ++timerId; intervals.set(id, { callback, ms }); return id },
      clearInterval(id) { intervals.delete(id) },
      setTimeout(callback, ms) { throw new Error(`Unexpected timer ${ms}`) }, clearTimeout() {}
    },
    api: {
      async boxRequestLiveStream(params) {
        calls.push(['request', params.algorithmId]); acquisitions.push({ ...params }); requests.push(['request', { ...params }])
        if (createResponse && acquisitions.length === 1) return createResponse.promise
        return { resData: { stream: { previewSessionId: params.previewSessionId, protocol: 'webrtc', webrtcUrl: '/rtc', rtcApiPort: 1985, keepAliveInterval: 5 } } }
      },
      boxStreamKeepAlive(params) {
        calls.push(['heartbeat', params.algorithmId]); requests.push(['heartbeat', { ...params }])
        if (nextHeartbeat) { const response = nextHeartbeat; nextHeartbeat = null; return response.promise }
        return Promise.resolve({ resCode: 1 })
      },
      async boxStreamStop(params) { calls.push(['stop', params.algorithmId]); requests.push(['stop', { ...params }]); return stopResponse ? stopResponse.promise : { resCode: 1 } }
    }
  })
  // createApp clones top-level root props; echo parent events into its reactive
  // component props to model the real parent binding without rewriting the SFC.
  componentProps = screen.instance.$.props
  await flush(screen)
  return { screen, props: componentProps, calls, events, acquisitions, requests,
    heartbeat(response) {
      nextHeartbeat = response
      const timer = [...intervals.values()].find(value => value.ms === 5000)
      assert.ok(timer, 'active viewer heartbeat exists')
      timer.callback()
    }
  }
}

{
  const fixture = await createScreen()
  try {
    assert.deepEqual(fixture.calls.filter(([kind]) => kind === 'request'), [['request', 'a']])
    const failure = deferred()
    fixture.heartbeat(failure)
    failure.reject({ resCode: 0, resMsg: [{ msgCode: '12290' }] })
    await flush(fixture.screen)
    assert.equal(fixture.events.length, 1)
    assert.equal(fixture.props.runAlgorithmId, '')
    assert.deepEqual(fixture.calls.filter(([kind]) => kind === 'request'), [['request', 'a'], ['request', '']])
    assert.deepEqual(fixture.calls.filter(([kind]) => kind === 'stop'), [['stop', 'a']])
    console.log('PASS failed active OSD heartbeat emits raw selection, retires OSD once, requests raw once')
  } finally { fixture.screen.unmount() }
}

{
  const fixture = await createScreen()
  try {
    const late = deferred()
    fixture.heartbeat(late)
    fixture.props.runAlgorithmId = 'b'
    await flush(fixture.screen)
    assert.deepEqual(fixture.calls.filter(([kind]) => kind === 'request'), [['request', 'a'], ['request', 'b']])
    late.reject({ resCode: 0, resMsg: [{ msgCode: '12290' }] })
    await flush(fixture.screen)
    assert.equal(fixture.props.runAlgorithmId, 'b')
    assert.equal(fixture.events.length, 0)
    assert.deepEqual(fixture.calls.filter(([kind]) => kind === 'stop'), [['stop', 'a']])
    console.log('PASS late old OSD heartbeat failure preserves committed replacement selection/viewer')
  } finally { fixture.screen.unmount() }
}

// A hung old Stop must not consume the channel's five-second handoff grace.
{
  const oldStop = deferred()
  const fixture = await createScreen({ stopResponse: oldStop })
  try {
    const failure = deferred()
    fixture.heartbeat(failure)
    failure.reject({ resCode: 0, resMsg: [{ msgCode: '10027' }] })
    await flush(fixture.screen)
    assert.equal(fixture.props.runAlgorithmId, '')
    assert.deepEqual(fixture.acquisitions.map(p => p.algorithmId), ['a', ''])
    assert.notEqual(fixture.acquisitions[0].previewSessionId, fixture.acquisitions[1].previewSessionId)
    assert.equal(fixture.events[0].retiredAlgorithmId, 'a')
    fixture.props.runAlgorithmId = 'b'
    await flush(fixture.screen)
    oldStop.resolve({ resCode: 1 })
    await flush(fixture.screen)
    assert.equal(fixture.props.runAlgorithmId, 'b')
    for (const [kind, request] of fixture.requests) {
      assert.ok(request.previewSessionId, `${kind} must retain its acquisition identity`)
      assert.ok(fixture.acquisitions.some(a => a.previewSessionId === request.previewSessionId && a.algorithmId === request.algorithmId))
    }
    console.log('PASS hung old Stop does not delay raw fallback or overwrite a later user selection')
  } finally { oldStop.resolve({ resCode: 1 }); fixture.screen.unmount() }
}

for (const error of [new Error('network timeout'), { resCode: 0, resMsg: [{ msgCode: '10005' }] }]) {
  const fixture = await createScreen()
  try {
    const failure = deferred()
    fixture.heartbeat(failure)
    failure.reject(error)
    await flush(fixture.screen)
    assert.equal(fixture.props.runAlgorithmId, 'a')
    assert.equal(fixture.acquisitions.length, 1)
    assert.equal(fixture.events.length, 0)
    assert.equal(fixture.calls.filter(([kind]) => kind === 'stop').length, 0)
    console.log('PASS transient/auth heartbeat failure does not erase algorithm selection')
  } finally { fixture.screen.unmount() }
}

{
  const first = await createScreen()
  const second = await createScreen()
  try {
    assert.notEqual(first.acquisitions[0].previewSessionId, second.acquisitions[0].previewSessionId)
    first.screen.unmount()
    await flush(second.screen)
    assert.equal(second.calls.filter(([kind]) => kind === 'stop').length, 0)
    assert.equal(second.props.runAlgorithmId, 'a')
    console.log('PASS separate tiles acquire distinct session identities')
  } finally { second.screen.unmount() }
}
{
  const create = deferred(), oldStop = deferred()
  const fixture = await createScreen({ createResponse: create, stopResponse: oldStop })
  try {
    fixture.props.runAlgorithmId = ''
    await flush(fixture.screen)
    create.resolve({ resData: { stream: { previewSessionId: fixture.acquisitions[0].previewSessionId,
      protocol: 'webrtc', webrtcUrl: '/rtc', rtcApiPort: 1985, keepAliveInterval: 5 } } })
    await flush(fixture.screen)
    assert.deepEqual(fixture.acquisitions.map(p => p.algorithmId), ['a', ''])
    assert.equal(fixture.props.runAlgorithmId, '')
    console.log('PASS superseded Create response releases its scoped lease without waiting for old Stop')
  } finally { oldStop.resolve({ resCode: 1 }); fixture.screen.unmount() }
}
console.log('Preview session behavior passed (8 cases; production SFC, mocked transport/player readiness)')
