import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const parameter = await mountComponent('views/gam/countManagement/arrangeDetail/flow/ParameterSetting.vue', {
  props: { algorithmMetadata: { params: [{ key: 'threshold', name: 'Threshold', type: 'text', value: '1', level: '2' }] } }
})
try {
  const checkbox = () => parameter.all((n) => n.type === 'el-checkbox')[0]
  assert.ok(checkbox(), 'ownership checkbox must render')
  assert.equal(parameter.instance.saveParamConfig()[0].channelEditable, true, 'new parameters default editable')
  for (const checked of [false, true]) {
    checkbox().props.activate(checked)
    await parameter.settle()
    const [saved] = parameter.instance.saveParamConfig()
    assert.equal(saved.senior, checked ? 0 : 2)
    assert.equal(saved.channelEditable, checked)
  }
  parameter.all((n) => n.type === 'el-radio-group')[0].props.activate('detail')
  await parameter.settle()
  parameter.all((n) => n.type === 'button' && n.props.class === 'add-card')[0].props.onClick()
  await parameter.settle()
  const added = parameter.instance.saveParamConfig().at(-1)
  assert.equal(added.senior, 0, 'adding a parameter defaults to visible')
  assert.equal(added.channelEditable, true)
} finally { parameter.unmount() }

let resolveDevice
let requests = 0
const response = new Promise((resolve) => { resolveDevice = resolve })
const device = await mountComponent('views/box/systemManagement/systemConfig/components/DeviceInfo.vue', {
  api: { queryDeviceInfo: () => { requests++; return response } }
})
try {
  resolveDevice({ resData: { devInfoList: [
    { key: 'deviceType', name: 'Device', value: 'normal-device' },
    { key: 'rkllmAvailable', name: 'RKLLM', value: 'hidden-capability' },
    { key: 'futureExtension', name: 'Future', value: 'retained-extension' }
  ] } })
  await device.settle()
  const text = device.all(() => true).map((n) => n.text).join(' ')
  assert.ok(text.includes('normal-device'))
  assert.ok(text.includes('retained-extension'))
  assert.ok(!text.includes('hidden-capability'))
  assert.equal(requests, 1)
} finally { device.unmount() }

for (const platform of ['1', '15']) {
  for (const match of [true, false]) {
    const form = await mountComponent('views/gam/taskManager/editTask/dynamicForm.vue', {
      props: { params: [
        { key: 'mode', type: 'switch', name: 'Mode', value: match ? '1' : '0', senior: 0 },
        { key: 'child', type: 'text', name: 'Child', value: 'kept-value', senior: 0, dependsOn: { key: 'mode', value: '1' } }
      ] },
      mocks: { './distanceDialog.vue': { default: { render: () => null } }, '@/assets/CatchPhoto.png': { default: '' }, echarts: { number: Number } },
      globals: { localStorage: { getItem: () => platform }, window: { localStorage: { getItem: () => platform } }, setTimeout: () => 1, clearTimeout: () => {} }
    })
    try {
      const childForms = form.all((n) => n.type === 'el-form' && n.props.model?.key === 'child')
      assert.equal(childForms.length, platform === '15' || match ? 1 : 0)
      if (childForms.length) assert.equal(childForms[0].props.disabled, platform === '15' && !match)
      const data = form.instance.getAllFormData()
      assert.equal(data.find((p) => p.key === 'child')?.value, 'kept-value')
    } finally { form.unmount() }
  }
}

for (const scenario of [
  { search: '?channelId=outer', hash: '#/edit?channelId=hash-channel', expected: 'hash-channel' },
  { search: '?channelCode=legacy', hash: '#/edit', expected: 'resolved-channel', legacy: true },
  { search: '', hash: '#/edit', expected: 'prop-channel' }
]) {
  const calls = []
  const storage = { getItem: () => '15' }
  const service = await mountComponent('views/gam/taskManager/editTask/serviceConfig.vue', {
    props: { channelId: 'prop-channel' },
    mocks: {
      './areaSetting2.vue': { default: { render: () => null } }, './paramSetting.vue': { default: { render: () => null } },
      './BatchApplication.vue': { default: { render: () => null } }, '@/components/eventBus.js': { default: { $emit() {} } }, uuid: { v4: () => 'fixture-id' }
    },
    globals: { localStorage: storage, window: { location: scenario, localStorage: storage } },
    api: {
      algorithmInquire: async () => ({ resData: { rows: [] } }),
      selectConfigByAlgorithmId: () => new Promise(() => {}),
      selectAllAlgorithmInfo: async (params) => { calls.push(params); return { resData: {} } },
      channelCodeDetail: async (params) => { assert.equal(params.channelCode, 'legacy'); return { resData: { channelId: 'resolved-channel' } } },
      boxGetTimeTemplate: async () => ({ resData: { rows: [] } })
    }
  })
  try {
    await service.settle()
    assert.equal(calls.length, 1, 'one channel lookup per mount')
    assert.equal(calls[0].channelId, scenario.expected)
  } finally { service.unmount() }
}

console.log('Component behavior checks passed')
