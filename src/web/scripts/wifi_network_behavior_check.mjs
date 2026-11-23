import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { test } from 'node:test'
import vm from 'node:vm'
import * as vue from 'vue'
import { parse, compileScript } from '@vue/compiler-sfc'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'
import * as wifiMessages from '../src/views/box/systemManagement/netConfig/wifiMessages.js'
import zhCN from '../src/i18n/locales/zh-CN.js'
import enUS from '../src/i18n/locales/en-US.js'

const validation = await loadBehaviorModule('views/box/systemManagement/netConfig/wifiValidation.js', { globals: { TextEncoder } })
// Vue's nextTick only drains its render queue; the VM transport also adopts
// promises across contexts. Wait a task turn for those continuations to settle.
const settle = async () => { await vue.nextTick(); await new Promise(setImmediate); await vue.nextTick() }
const blankForm = () => ({ ssid: 'test network', password: 'test-password', hidden: false, dhcp: true, ipAddr: '', netMask: '255.255.255.0', gateway: '', autoDns: true, dns1: '', dns2: '' })

test('SSID uses byte length and preserves spaces and special characters', () => {
  const form = blankForm()
  for (const ssid of [' 中文 $ " \\ ', 'a'.repeat(32), '中'.repeat(10)]) {
    form.ssid = ssid
    assert.equal(validation.wifiValidationError(form, false), '')
    assert.equal(validation.wifiConnectionRequest(form, false).ssid, ssid)
  }
  for (const ssid of ['', 'a'.repeat(33), '中'.repeat(11), 'bad\0ssid']) {
    form.ssid = ssid
    assert.equal(validation.wifiValidationError(form, false), 'wifi.invalidSsid')
  }
})

test('saved credentials are omitted, new credentials and static settings validate', () => {
  const form = { ...blankForm(), password: '' }
  assert.equal(validation.wifiValidationError(form, true), '')
  assert.equal('password' in validation.wifiConnectionRequest(form, true), false)
  assert.equal(validation.wifiValidationError(form, false), 'wifi.invalidPassword')
  form.password = 'a'.repeat(64)
  assert.equal(validation.wifiValidationError(form, false), '')
  for (const password of ['z'.repeat(64), '中文密码测试', 'with\ttab', 'seven!!']) {
    form.password = password
    assert.equal(validation.wifiValidationError(form, false), 'wifi.invalidPassword')
  }
  for (const password of [' !@#$%^&()', '~'.repeat(63)]) {
    form.password = password
    assert.equal(validation.wifiValidationError(form, false), '')
  }
  Object.assign(form, { password: 'test-password', dhcp: false, ipAddr: '192.0.2.10', gateway: '192.0.2.1', autoDns: false, dns1: '192.0.2.53' })
  assert.equal(validation.wifiValidationError(form, false), '')
  for (const netMask of ['255.0.255.0', '0.0.0.0', '255.255.255.254', '255.255.255.255']) {
    form.netMask = netMask
    assert.equal(validation.wifiValidationError(form, false), 'systemManage.subnetMaskFormatError')
  }
  for (const netMask of ['128.0.0.0', '255.255.255.252']) {
    form.netMask = netMask
    assert.equal(validation.wifiValidationError(form, false), '')
  }
  form.autoDns = true
  assert.equal(validation.wifiValidationError(form, false), 'wifi.staticDnsRequired')
  form.autoDns = false
  form.netMask = '255.255.255.0'; form.dns1 = ''
  assert.equal(validation.wifiValidationError(form, false), 'systemManage.dnsFormatError')
})


test('Wi-Fi failure reasons and recovery results are translated without hiding unknown codes', async () => {
  for (const locale of [zhCN, enUS]) {
    const translate = (key, values = {}) => {
      const text = key.split('.').reduce((value, part) => value?.[part], locale)
      assert.equal(typeof text, 'string', key)
      return text.replace(/\{(\w+)\}/g, (_, name) => values[name] ?? `{${name}}`)
    }
    for (const code of Object.keys(locale.wifi.reason).filter(code => code !== 'unknown')) {
      assert.equal(wifiMessages.wifiReasonText(code, translate), locale.wifi.reason[code])
    }
    assert.match(wifiMessages.wifiReasonText('futureDriverError', translate), /futureDriverError/)
    assert.equal(wifiMessages.wifiReasonText('', translate), '')
    assert.equal(wifiMessages.wifiRequestErrorText({ resMsg: [{ msgText: 'authenticationFailed' }] }, translate), locale.wifi.reason.authenticationFailed)
    assert.equal(wifiMessages.wifiRequestErrorText(new Error('offline'), translate), locale.wifi.requestFailed)
  }
})

async function component(filename, props, api = {}) {
  const file = new URL(`../src/views/box/systemManagement/netConfig/${filename}`, import.meta.url)
  const { descriptor, errors } = parse(await readFile(file, 'utf8'), { filename: file.pathname })
  assert.equal(errors.length, 0)
  const script = compileScript(descriptor, { id: 'wifi-test' })
  const hooks = { unmount: [], mounted: [], activated: [], deactivated: [] }
  const events = []; const calls = []
  const timers = new Map()
  let sequence = 0
  const proxy = { $API: new Proxy(api, { get(target, name) { return async (...args) => { calls.push({ name, args }); return target[name]?.(...args) || { resData: {} } } } }) }
  const context = vm.createContext({
    console, TextEncoder, URL,
    window: { location: { protocol: 'https:', port: '8443' } },
    setTimeout: (callback, delay) => { const id = ++sequence; timers.set(id, { callback, delay }); return id },
    clearTimeout: id => timers.delete(id)
  })
  const mocks = {
    vue: {
      ...vue, getCurrentInstance: () => ({ proxy }),
      onBeforeUnmount: fn => hooks.unmount.push(fn), onMounted: fn => hooks.mounted.push(fn),
      onActivated: fn => hooks.activated.push(fn), onDeactivated: fn => hooks.deactivated.push(fn)
    },
    'element-plus': { ElMessageBox: { confirm: async () => true }, ElLoading: {} },
    '@/i18n': { t: key => key, currentLocale: vue.ref('zh-CN'), localeColon: '：' },
    '@/utils/networkValidation': { isValidOptionalDnsAddress: value => !value || validation.isWifiIpv4(value) },
    './wifiValidation.js': validation,
    './wifiMessages.js': wifiMessages,
    './WifiSettings.vue': { default: {} }
  }
  const module = new vm.SourceTextModule(script.content, { context, initializeImportMeta(meta) { meta.url = file.href } })
  await module.link(specifier => {
    assert.ok(mocks[specifier], specifier)
    const entries = mocks[specifier]
    return new vm.SyntheticModule(Object.keys(entries), function () {
      for (const [name, value] of Object.entries(entries)) this.setExport(name, value)
    }, { context })
  })
  await module.evaluate()
  const scope = vue.effectScope()
  const state = scope.run(() => module.namespace.default.setup(props, { expose() {}, emit: (...args) => events.push(args) }))
  return { state, calls, events, hooks, timers, stop() { hooks.unmount.forEach(fn => fn()); scope.stop() } }
}
const status = (extra = {}) => ({ available: true, serviceReady: true, state: 'disconnected', busy: false, interfaces: [], ...extra })

test('connect is asynchronous, clears password, blocks duplicate writes and requires explicit confirmation', async () => {
  let stage = 'addressing'
  let submit
  const props = vue.reactive({ status: status() })
  const c = await component('WifiSettings.vue', props, {
    connectWifi: () => new Promise(resolve => { submit = resolve }),
    queryWifiOperation: ({ operationId }) => ({ resData: { operationId, type: 'connect', stage, done: stage === 'succeeded', success: stage === 'succeeded' } }),
    confirmWifiConfig: () => { stage = 'succeeded'; return { resData: {} } }
  })
  Object.assign(c.state.form, blankForm())
  const pending = c.state.connectNetwork()
  assert.equal(c.state.form.password, '')
  assert.equal(c.state.busy.value, true)
  await c.state.connectNetwork()
  assert.equal(c.calls.filter(call => call.name === 'connectWifi').length, 1)
  assert.equal(c.calls[0].args[0].password, 'test-password')
  submit({ resData: { operationId: 'trial-1' } }); await pending
  assert.equal(c.state.operation.value.stage, 'addressing')
  assert.equal(c.state.operation.value.success, false)
  assert.equal('password' in c.calls[0].args[0], false)
  stage = 'awaitingConfirmation'; props.status = status({ operationId: 'trial-1', busy: true })
  await settle()
  assert.equal(c.calls.filter(call => call.name === 'queryWifiOperation').length, 2)
  assert.equal(c.state.operation.value.stage, 'awaitingConfirmation')
  assert.equal(c.calls.some(call => call.name === 'confirmWifiConfig'), false)
  await c.state.confirmConfig()
  assert.equal(c.state.operation.value.stage, 'succeeded')
  assert.equal(c.calls.filter(call => call.name === 'confirmWifiConfig').length, 1)
  c.stop()
})

test('refresh resumes pending confirmation and service status uses real egress and addresses', async () => {
  const c = await component('WifiSettings.vue', vue.reactive({ status: status({
    state: 'standby', interfaceName: 'wlP1p1s0', currentInterface: 'eth0', currentAddress: '192.0.2.10',
    hasCredentials: true, savedConfig: { ssid: 'saved network', dhcp: true, autoDns: true }, operationId: 'pending-1',
    interfaces: [{ interfaceName: 'wlP1p1s0', ipAddr: '192.0.2.20' }]
  }) }), {
    queryWifiOperation: ({ operationId }) => ({ resData: { operationId, stage: 'awaitingConfirmation', done: false, confirmSeconds: 30 } })
  })
  await settle()
  assert.equal(c.state.operation.value.stage, 'awaitingConfirmation')
  assert.equal(c.state.stateLabel.value, 'wifi.standby')
  assert.equal(c.state.wifiAddress.value, '192.0.2.20')
  assert.equal(c.state.currentUrl.value, 'https://192.0.2.10:8443/#/boxLogin')
  assert.equal(c.state.canReusePassword.value, true)
  c.state.form.ssid = 'different network'
  assert.equal(c.state.canReusePassword.value, false)
  c.stop()
})

test('scans run only on explicit requests and results include unsupported security', async () => {
  const props = vue.reactive({ status: status() })
  const c = await component('WifiSettings.vue', props, {
    scanWifi: () => ({ resData: { operationId: 'scan-1' } }),
    queryWifiOperation: ({ operationId }) => ({ resData: { operationId, type: 'scan', stage: 'succeeded', done: true, success: true,
      scanResults: [{ ssid: 'same name', security: 'wpa2', bssid: 'a' }, { ssid: 'same name', security: 'wpa3', bssid: 'b' }] } })
  })
  assert.equal(c.calls.length, 0)
  await c.state.runOperation('scanWifi')
  assert.equal(c.state.scanResults.value.length, 2)
  assert.notEqual(c.state.scanRowKey(c.state.scanResults.value[0]), c.state.scanRowKey(c.state.scanResults.value[1]))
  props.status = status({ operationId: 'scan-1' }); await vue.nextTick()
  assert.equal(c.calls.filter(call => call.name === 'scanWifi').length, 1)
  c.stop()
})

test('rejected connect clears secrets and stopped component ignores late responses', async () => {
  const c = await component('WifiSettings.vue', vue.reactive({ status: status() }), {
    connectWifi: async () => { throw new Error('offline') }
  })
  Object.assign(c.state.form, blankForm())
  await c.state.connectNetwork()
  assert.equal(c.state.form.password, '')
  assert.equal('password' in c.calls[0].args[0], false)
  assert.equal(c.state.errorMessage.value, 'wifi.requestFailed')
  c.stop()
  await c.state.runOperation('scanWifi')
  assert.equal(c.calls.length, 1)
})

test('network page polls status without scanning and cancels on leave', async () => {
  let resolveStatus
  const c = await component('index.vue', {}, {
    queryNetworkStatus: () => new Promise(resolve => { resolveStatus = resolve })
  })
  c.hooks.mounted.forEach(fn => fn())
  assert.equal(c.calls.filter(call => call.name === 'queryNetworkStatus').length, 1)
  c.hooks.activated.forEach(fn => fn())
  assert.equal(c.calls.filter(call => call.name === 'queryNetworkStatus').length, 1)
  resolveStatus({ resData: status() }); await settle()
  assert.equal(c.state.networkStatus.value.available, true)
  assert.equal(c.timers.size, 1)
  assert.equal(c.calls.some(call => call.name === 'scanWifi'), false)
  c.hooks.deactivated.forEach(fn => fn())
  assert.equal(c.timers.size, 0)
  await c.state.refreshNetworkStatus()
  assert.equal(c.calls.filter(call => call.name === 'queryNetworkStatus').length, 1)
  c.stop()
})


test('static IPv4 selects manual DNS and preserves initialized saved DNS values', async () => {
  const c = await component('WifiSettings.vue', vue.reactive({ status: status({
    hasCredentials: true, savedConfig: { ssid: 'saved static', dhcp: false, ipAddr: '192.0.2.10', netMask: '255.255.255.0', gateway: '192.0.2.1', autoDns: false, dns: ['192.0.2.53', '192.0.2.54'] }
  }) }))
  await settle()
  assert.equal(c.state.form.autoDns, false)
  assert.equal(c.state.form.dns1, '192.0.2.53')
  assert.equal(c.state.form.dns2, '192.0.2.54')
  c.state.form.dhcp = true
  await settle()
  c.state.form.autoDns = true
  c.state.form.dhcp = false
  await settle()
  assert.equal(c.state.form.autoDns, false)
  assert.equal(c.state.form.dns1, '192.0.2.53')
  assert.equal(validation.wifiValidationError(c.state.form, true), '')
  c.stop()
})
