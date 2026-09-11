import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { test } from 'node:test'
import vm from 'node:vm'
import * as vue from 'vue'
import { parse, compileScript, compileTemplate } from '@vue/compiler-sfc'

const filename = new URL('../src/views/gam/taskManager/components/Gb28181Dialog.vue', import.meta.url)
const { descriptor, errors } = parse(await readFile(filename, 'utf8'), { filename: filename.pathname })
assert.deepEqual(errors, [])
const script = compileScript(descriptor, { id: 'gb-test' })
const template = compileTemplate({ source: descriptor.template.content, filename: filename.pathname, id: 'gb-test', compilerOptions: { bindingMetadata: script.bindings } })
assert.deepEqual(template.errors, [])
const access = await readFile(new URL('../src/views/gam/taskManager/components/gb28181Access.js', import.meta.url), 'utf8')
const device = { id: '34020000001110000001', username: 'auth-account', hasPassword: true, online: true, state: 'registered', channels: [{ id: '34020000001320000001', name: 'Entrance', added: false, state: 'idle', error: '' }] }
const config = { enabled: true, listening: true, platformId: '34020000002000000001', realm: '3402000000', address: '', sipPort: 5060, heartbeatTimeout: 180, devices: [device] }

test('GB access exposes only platform settings and devices/channels tabs', () => {
  const tabs = [...descriptor.template.content.matchAll(/<el-tab-pane\b[^>]*\bname="([^"]+)"/g)].map(match => match[1])
  assert.deepEqual(tabs, ['platform', 'devices'])
  assert.doesNotMatch(descriptor.template.content, /gbAccess\.(guideTab|step[1-4]|cameraServer|cameraUser|userMapping|channelMapping|cameraAutoAddress)/)
})
test('GB access stays in add-channel flow without a standalone button or trial banner', async () => {
  const page = await readFile(new URL('../src/views/gam/taskManager/index.vue', import.meta.url), 'utf8')
  const { descriptor: pageDescriptor } = parse(page)
  assert.doesNotMatch(pageDescriptor.template.content, /@click="gb28181Dialog\.open\(\)"/)
  assert.match(pageDescriptor.scriptSetup.content, /if \(val === 7\)\s*\{\s*channelDialogVisible\.value = false\s*nextTick\(\(\) => gb28181Dialog\.value\.open\(\)\)/)
  assert.doesNotMatch(descriptor.template.content, /gbAccess\.scope/)
})
async function dialog(handler = () => config, network = async () => ({ resData: { netCardList: [] } })) {
  const calls = [], events = [], timers = [], networkCalls = []
  const context = vm.createContext({ console, window: { location: { hostname: '192.0.2.10' } }, setInterval: fn => { timers.push(fn); return timers.length }, clearInterval() {} })
  const mocks = {
    vue, '@/i18n': { t: (key, values) => values ? `${key}:${values.code}` : key },
    'element-plus': { ElMessageBox: { confirm: async () => {} } },
    '@/utils/request': { request: async request => {
      if (request.url === '/gtw/cwai/network/QueryNetCard') { networkCalls.push(request); return network() }
      calls.push(request.data); return { resData: { success: true, ...await handler(request.data) } }
    } }
  }
  const cache = new Map()
  const linker = async specifier => {
    if (cache.has(specifier)) return cache.get(specifier)
    if (specifier === './gb28181Access.js') {
      const module = new vm.SourceTextModule(access, { context }); cache.set(specifier, module); await module.link(linker); return module
    }
    const values = mocks[specifier]; assert.ok(values, specifier)
    const module = new vm.SyntheticModule(Object.keys(values), function () { for (const [name, value] of Object.entries(values)) this.setExport(name, value) }, { context })
    cache.set(specifier, module); return module
  }
  const module = new vm.SourceTextModule(script.content, { context }); await module.link(linker); await module.evaluate()
  const scope = vue.effectScope()
  const state = scope.run(() => module.namespace.default.setup({}, { expose() {}, emit: event => events.push(event) }))
  return { state, calls, events, timers, networkCalls, stop: () => scope.stop() }
}

const networkFixture = async () => ({ resData: { netCardList: [
  { ipAddr: '192.0.2.10', ethName: 'eth0' }, { ipAddr: '192.0.2.10', ethName: 'eth1' },
  { ipAddr: '192.0.2.11', ethName: 'eth2' }, { ipAddr: '0.0.0.0' }, { ipAddr: '127.0.0.1' },
  { ipAddr: '224.0.0.1' }, { ipAddr: '256.1.1.1' }, { ipAddr: '::1' }, { ipAddr: '' }
] } })
test('GB automatic addressing stays automatic and offers only actual usable interfaces', async () => {
  const d = await dialog(undefined, networkFixture); await d.state.open()
  assert.equal(d.state.platform.address, ''); assert.equal(d.state.addressChoice.value, 'auto')
  assert.match(descriptor.template.content, /<el-option value="auto"/)
  assert.equal(d.state.addresses.value.length, 2)
  assert.equal(d.state.addresses.value[0].name, 'eth0 / eth1')
  assert.match(d.state.addressLabel(d.state.addresses.value[0]), /gbAccess.loginAddress/)
  assert.doesNotMatch(d.state.addressLabel(d.state.addresses.value[1]), /gbAccess.loginAddress/)
  assert.equal(d.networkCalls[0].method, 'get')
  d.state.addressChoice.value = '192.0.2.11'; d.state.chooseAddress('192.0.2.11')
  await d.state.refresh(); assert.equal(d.state.platform.address, '192.0.2.11')
  await d.state.savePlatform()
  const save = d.calls.find(c => c.action === 'savePlatform')
  assert.equal(save.address, '192.0.2.11'); assert.equal(Object.hasOwn(save, 'addressChoice'), false)
  d.state.addressChoice.value = 'auto'; d.state.chooseAddress('auto'); await d.state.savePlatform()
  assert.equal(d.calls.filter(c => c.action === 'savePlatform').at(-1).address, '')
  d.state.close(); d.stop()
})
test('GB preserves existing custom addresses and supports explicit automatic reset', async () => {
  const d = await dialog(() => ({ ...config, address: '198.51.100.8' }), networkFixture); await d.state.open()
  assert.equal(d.state.addressChoice.value, 'manual'); assert.equal(d.state.addressAdvanced.value[0], 'address')
  await d.state.savePlatform(); assert.equal(d.calls.find(c => c.action === 'savePlatform').address, '198.51.100.8')
  d.state.enableManualAddress(false); await d.state.savePlatform()
  assert.equal(d.calls.filter(c => c.action === 'savePlatform').at(-1).address, '')
  d.state.close(); d.stop()
})
test('GB interface failures do not overwrite saved settings or block valid saves', async () => {
  const d = await dialog(() => ({ ...config, address: '192.0.2.10' }), async () => { throw new Error('offline') }); await d.state.open()
  assert.equal(d.state.ready.value, true); assert.equal(d.state.addressWarning.value, 'gbAccess.addressLoadFailed')
  assert.equal(d.state.addresses.value.length, 0); assert.equal(d.state.platform.address, '192.0.2.10')
  await d.state.savePlatform(); assert.equal(d.calls.find(c => c.action === 'savePlatform').address, '192.0.2.10')
  d.state.close(); d.stop()
})
test('GB manual addresses require one usable IPv4 address', async () => {
  const d = await dialog(); await d.state.open(); d.state.enableManualAddress(true)
  for (const value of ['', '127.0.0.1', '0.0.0.0', '224.1.1.1', '255.255.255.255', '01.2.3.4', 'host.local', '192.0.2.10:5060', '192.0.2.10,192.0.2.11']) {
    d.state.platform.address = value; await d.state.savePlatform(); assert.equal(d.state.message.value, 'gbAccess.invalidAddress')
  }
  assert.equal(d.calls.length, 1)
  d.state.platform.address = '192.0.2.10'; await d.state.savePlatform()
  assert.equal(d.calls.find(c => c.action === 'savePlatform').address, '192.0.2.10')
  d.state.close(); d.stop()
})
test('GB page loads configuration and does not authenticate devices during list refresh', async () => {
  const d = await dialog(); await d.state.open()
  assert.equal(d.state.ready.value, true); assert.equal(d.state.deviceForm.password, '')
  assert.deepEqual(d.calls.map(c => c.action), ['list']); assert.equal(d.state.tab.value, 'devices')
  d.state.close(); d.stop()
})
test('GB disabled platform opens settings and unavailable load blocks writes', async () => {
  let d = await dialog(() => ({ ...config, listening: false, enabled: false })); await d.state.open()
  assert.equal(d.state.tab.value, 'platform'); d.state.close(); d.stop()
  d = await dialog(() => { throw new Error('service_unavailable') }); await d.state.open(); await d.state.savePlatform()
  assert.equal(d.state.ready.value, false); assert.equal(d.calls.length, 1); d.state.close(); d.stop()
})
test('GB new devices require an explicit SIP password; saves clear credentials', async () => {
  const d = await dialog(); await d.state.open(); d.state.deviceForm.id = device.id
  await d.state.saveDevice(); assert.equal(d.calls.length, 1); assert.equal(d.state.message.value, 'gbAccess.passwordRequired')
  d.state.deviceForm.password = 'fixture-password'; await d.state.saveDevice()
  const save = d.calls.find(c => c.action === 'saveDevice')
  assert.equal(save.username, device.id); assert.equal(save.allowUnauthenticated, false)
  assert.equal(d.state.deviceForm.password, ''); d.state.close(); d.stop()
})
test('GB editing preserves omitted password and supports a different authentication ID', async () => {
  const d = await dialog(); await d.state.open(); d.state.editDevice(device); await d.state.saveDevice()
  const save = d.calls.find(c => c.action === 'saveDevice')
  assert.equal(save.username, 'auth-account'); assert.equal(Object.hasOwn(save, 'password'), false)
  d.state.close(); d.stop()
})
test('GB catalog refresh preserves edited names and adds the channel ID not the device ID', async () => {
  const d = await dialog(); await d.state.open()
  d.state.devices.value[0].channels[0].channelName = 'Custom name'; await d.state.refresh()
  const row = d.state.devices.value[0], channel = row.channels[0]
  assert.equal(channel.channelName, 'Custom name')
  await d.state.addChannel(row, channel)
  const save = d.calls.find(c => c.action === 'addChannel')
  assert.equal(save.deviceId, device.id); assert.equal(save.channelId, device.channels[0].id); assert.equal(save.channelName, 'Custom name')
  assert.deepEqual(d.events, ['saved']); d.state.close(); d.stop()
})
test('GB compatibility mode is explicit and closing clears unfinished credentials', async () => {
  const d = await dialog(); await d.state.open(); Object.assign(d.state.deviceForm, { id: device.id, allowUnauthenticated: true })
  await d.state.saveDevice(); assert.equal(d.calls.find(c => c.action === 'saveDevice').allowUnauthenticated, true)
  d.state.deviceForm.password = 'unsaved'; d.state.close(); assert.equal(d.state.deviceForm.password, ''); d.stop()
})
