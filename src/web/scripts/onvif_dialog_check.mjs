import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { test } from 'node:test'
import vm from 'node:vm'
import * as vue from 'vue'
import { parse, compileScript, compileTemplate } from '@vue/compiler-sfc'
import * as access from '../src/views/gam/taskManager/components/onvifAccess.js'

const filename = new URL('../src/views/gam/taskManager/components/OnvifDialog.vue', import.meta.url)
const source = await readFile(filename, 'utf8')
const { descriptor, errors } = parse(source, { filename: filename.pathname })
assert.equal(errors.length, 0)
const script = compileScript(descriptor, { id: 'onvif-test' })
const template = compileTemplate({ source: descriptor.template.content, filename: filename.pathname, id: 'onvif-test', compilerOptions: { bindingMetadata: script.bindings } })
assert.deepEqual(template.errors, [])
const profiles = [{ token: 'main', codec: 'H264', width: 1920, height: 1080, fps: 25 }]

async function dialog(handler) {
  const calls = []; const events = []; const context = vm.createContext({ console })
  const mocks = {
    vue,
    '@/i18n': { t: key => key },
    './onvifAccess.js': access,
    '@/utils/request': { request: async config => { calls.push(config); return { resData: { success: true, ...await handler(config) } } } }
  }
  const module = new vm.SourceTextModule(script.content, { context })
  await module.link(specifier => {
    const values = mocks[specifier]
    assert.ok(values, specifier)
    return new vm.SyntheticModule(Object.keys(values), function () {
      for (const [name, value] of Object.entries(values)) this.setExport(name, value)
    }, { context })
  })
  await module.evaluate()
  const scope = vue.effectScope()
  const state = scope.run(() => module.namespace.default.setup({}, { expose() {}, emit: event => events.push(event) }))
  return { state, calls, events, stop: () => scope.stop() }
}
function fill(state) {
  Object.assign(state.credentials, { username: 'fixture', password: 'fixture-password' })
  Object.assign(state.manual.value, { channelName: 'Entrance', endpoint: '192.0.2.10' })
}

test('manual add uses four fields, queries and saves automatically, then clears secrets', async () => {
  const d = await dialog(() => ({ profiles }))
  await d.state.open(); fill(d.state); await vue.nextTick(); await d.state.submit()
  assert.deepEqual(d.calls.map(c => c.url.split('/').pop()), ['OnvifProbe', 'OnvifSave'])
  assert.equal(d.state.visible.value, false); assert.equal(d.state.credentials.password, '')
  assert.deepEqual(d.events, ['saved']); d.stop()
})
test('query error keeps manual input, displays error and sends no save', async () => {
  const d = await dialog(() => ({ success: false, error: 'unauthorized' }))
  await d.state.open(); fill(d.state); await vue.nextTick(); await d.state.submit()
  assert.equal(d.calls.length, 1); assert.equal(d.state.message.value, 'onvif.unauthorized')
  assert.equal(d.state.manual.value.channelName, 'Entrance'); assert.equal(d.state.visible.value, true)
  d.stop()
})
test('discovery never authenticates and repeated searches preserve edited credentials', async () => {
  const device = { uuid: 'fixture-uuid', endpoint: 'http://192.0.2.10/onvif/device_service', scopes: 'onvif://www.onvif.org/name/Entrance' }
  const d = await dialog(config => config.url.endsWith('OnvifDiscover') ? { rows: [device, device] } : { profiles })
  await d.state.open(); fill(d.state); d.state.mode.value = 'auto'; await vue.nextTick()
  await d.state.discover()
  assert.equal(d.state.rows.value.length, 1); assert.equal(d.state.rows.value[0].profile, null)
  assert.equal(d.state.rows.value[0].channelName, 'Entrance')
  assert.deepEqual(d.calls.map(c => c.url.split('/').pop()), ['OnvifDiscover'])
  assert.equal(d.calls[0].data.interfaceAddress, '')
  const row = d.state.rows.value[0]
  Object.assign(row.credentials, { username: 'operator', password: 'changed-password' }); row.channelName = 'Edited name'
  await d.state.discover()
  assert.equal(row.credentials.username, 'operator'); assert.equal(row.credentials.password, 'changed-password')
  assert.equal(row.channelName, 'Edited name'); assert.equal(d.state.rows.value.length, 1)
  assert.deepEqual(d.calls.map(c => c.url.split('/').pop()), ['OnvifDiscover', 'OnvifDiscover'])
  d.stop()
})
test('failed edit load cannot create a new channel', async () => {
  const d = await dialog(() => ({ success: false, error: 'source_not_found' }))
  await d.state.open({ url: 'onvif://fixture', videoChannelId: 'fixture', channelName: 'Existing' })
  fill(d.state); await vue.nextTick(); await d.state.submit()
  assert.equal(d.calls.length, 1); assert.equal(d.state.formReady.value, false)
  d.stop()
})

test('empty credentials discover devices and generate names without authentication', async () => {
  const d = await dialog(() => ({ rows: [{ endpoint: 'http://192.0.2.10/onvif/device_service', scopes: 'onvif://www.onvif.org/name/Entrance' }] }))
  await d.state.open(); d.state.mode.value = 'auto'; await d.state.discover()
  assert.deepEqual(d.calls.map(c => c.url.split('/').pop()), ['OnvifDiscover'])
  assert.equal(d.state.message.value, '')
  assert.equal(d.state.rows.value[0].channelName, 'Entrance')
  assert.equal(d.state.rows.value[0].credentials.username, 'admin')
  assert.equal(d.state.rows.value[0].credentials.password, 'admin')
  assert.equal(d.state.rows.value[0].credentials.separateRtspCredentials, false)
  d.stop()
})

test('row credentials validate on add, typing does not authenticate, and corrected add queries automatically', async () => {
  const d = await dialog(config => config.url.endsWith('OnvifDiscover')
    ? { rows: [{ endpoint: 'http://192.0.2.10/onvif/device_service' }] } : { profiles })
  await d.state.open(); d.state.mode.value = 'auto'; d.state.credentials.username = 'fixture'
  await vue.nextTick(); await d.state.discover()
  assert.equal(d.calls.length, 1)
  d.state.selection.value = [...d.state.rows.value]
  const row = d.state.rows.value[0]
  row.credentials.password = ''
  await d.state.submit()
  assert.equal(row.error, 'credentials_required')
  assert.equal(d.calls.length, 1)
  d.state.rows.value[0].channelName = 'Edited entrance'
  row.credentials.password = 'fixture-password'; await vue.nextTick()
  assert.equal(d.calls.length, 1, 'typing credentials must not authenticate')
  await d.state.submit()
  assert.deepEqual(d.calls.map(c => c.url.split('/').pop()), ['OnvifDiscover', 'OnvifProbe', 'OnvifSave'])
  assert.equal(d.calls[2].data.channelName, 'Edited entrance')
  assert.equal(d.calls[2].data.username, 'admin'); assert.equal(d.calls[2].data.password, 'fixture-password')
  assert.equal(row.credentials.password, '', 'clear secrets after successful save'); d.stop()
})

test('per-row separate RTSP credentials validate independently without blocking discovery', async () => {
  const d = await dialog(config => config.url.endsWith('OnvifDiscover')
    ? { rows: [{ endpoint: 'http://192.0.2.10/onvif/device_service' }] } : { profiles })
  await d.state.open(); fill(d.state); d.state.mode.value = 'auto'
  d.state.credentials.separateRtspCredentials = true; await vue.nextTick()
  await d.state.discover()
  assert.equal(d.calls.length, 1)
  const row = d.state.rows.value[0]
  row.credentials.separateRtspCredentials = true; d.state.selection.value = [row]
  await d.state.submit()
  assert.equal(row.error, 'rtsp_credentials_required'); assert.equal(d.calls.length, 1)
  Object.assign(row.credentials, { rtspUsername: 'media', rtspPassword: 'media-password' })
  await d.state.submit()
  assert.equal(d.calls[2].data.rtspUsername, 'media'); assert.equal(d.calls[2].data.rtspPassword, 'media-password')
  assert.equal(row.credentials.rtspPassword, ''); d.stop()
})

test('different devices use independent credentials; failure does not block other rows or retry saved rows', async () => {
  const devices = ['192.0.2.10', '192.0.2.11'].map(ip => ({ endpoint: `http://${ip}/onvif/device_service` }))
  const d = await dialog(config => config.url.endsWith('OnvifDiscover') ? { rows: devices } :
    config.data.password === 'bad-password' ? { success: false, error: 'unauthorized' } : { profiles })
  await d.state.open(); d.state.mode.value = 'auto'; await d.state.discover()
  const [first, second] = d.state.rows.value
  Object.assign(first.credentials, { username: 'first', password: 'bad-password' })
  Object.assign(second.credentials, { username: 'second', password: 'second-password' })
  assert.notEqual(first.credentials, second.credentials)
  d.state.selection.value = [first, second]; await d.state.submit()
  assert.equal(first.error, 'unauthorized'); assert.equal(second.saved, true)
  assert.equal(d.calls.filter(c => c.url.endsWith('OnvifSave')).length, 1)
  const firstProbe = d.calls.find(c => c.url.endsWith('OnvifProbe') && c.data.endpoint === first.endpoint)
  const secondSave = d.calls.find(c => c.url.endsWith('OnvifSave'))
  assert.equal(firstProbe.data.username, 'first'); assert.equal(secondSave.data.username, 'second')
  first.credentials.password = 'corrected-password'; await d.state.submit()
  assert.equal(first.saved, true); assert.equal(d.calls.filter(c => c.url.endsWith('OnvifSave')).length, 2)
  assert.equal(d.events.length, 2); d.stop()
})

test('missing credentials do not block other rows; closing clears secrets', async () => {
  const d = await dialog(config => config.url.endsWith('OnvifDiscover')
    ? { rows: ['192.0.2.10', '192.0.2.11'].map(ip => ({ endpoint: `http://${ip}/onvif/device_service` })) } : { profiles })
  await d.state.open(); d.state.mode.value = 'auto'; await d.state.discover()
  const [first, second] = d.state.rows.value
  first.credentials.username = ''; first.credentials.password = 'retained-secret'
  d.state.selection.value = [first, second]; await d.state.submit()
  assert.equal(first.error, 'credentials_required'); assert.equal(second.saved, true)
  assert.equal(d.calls.filter(c => c.url.endsWith('OnvifProbe')).length, 1)
  d.state.close(); assert.equal(first.credentials.password, ''); assert.equal(d.state.rows.value.length, 0)
  d.stop()
})

test('automatic template has masked row credentials but no manual form, profile or result columns', () => {
  assert.match(descriptor.template.content, /<el-form v-if="mode === 'manual'"/)
  assert.match(descriptor.template.content, /v-model\.trim="row.credentials.username"/)
  assert.match(descriptor.template.content, /v-model="row.credentials.password"[^>]*type="password"/)
  assert.doesNotMatch(descriptor.template.content, /t\('onvif\.(autoProfile|status)'\)/)
})
test('editing preserves stored password and original channel identity', async () => {
  const d = await dialog(config => config.url.endsWith('OnvifGet')
    ? { source: 'onvif://fixture', endpoint: '192.0.2.10', username: 'fixture', hasPassword: true, profileToken: 'main', separateRtspCredentials: false }
    : { profiles })
  await d.state.open({ url: 'onvif://fixture', videoChannelId: 'existing', channelName: 'Existing' })
  await vue.nextTick(); await d.state.submit()
  const save = d.calls.find(c => c.url.endsWith('OnvifSave'))
  assert.equal(save.data.videoChannelId, 'existing'); assert.equal('password' in save.data, false)
  assert.equal(save.data.profileToken, 'main'); d.stop()
})
