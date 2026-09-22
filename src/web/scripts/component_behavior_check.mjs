import assert from 'node:assert/strict'
import { reactive } from 'vue'
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

for (const platform of ['1', '-1', null, '15']) {
  for (const match of [true, false]) {
    const form = await mountComponent('views/gam/taskManager/editTask/dynamicForm.vue', {
      props: { modelValue: [
        { key: 'mode', type: 'switch', name: 'Mode', value: match ? '1' : '0', senior: 0 },
        { key: 'child', type: 'text', name: 'Child', value: 'kept-value', senior: 0, dependsOn: { key: 'mode', value: '1' } }
      ] },
      mocks: { './distanceDialog.vue': { default: { render: () => null } }, '@/assets/CatchPhoto.png': { default: '' }, echarts: { number: Number } },
      globals: { localStorage: { getItem: () => platform }, window: { localStorage: { getItem: () => platform } }, setTimeout: () => 1, clearTimeout: () => {} }
    })
    try {
      const childForms = form.all((n) => n.type === 'el-form' && n.props.model?.key === 'child')
      assert.equal(childForms.length, 1)
      assert.equal(childForms[0].props.disabled, !match)
      const data = form.instance.collect()
      assert.equal(data.find((p) => p.key === 'child')?.value, 'kept-value')
    } finally { form.unmount() }
  }
}

// Exercise the real SFC across parent echoes, external replacements and resets.
const formEvents = []
const editing = await mountComponent('views/gam/taskManager/editTask/dynamicForm.vue', {
  props: {
    modelValue: [
      { key: 'radio', type: 'radio', name: 'Radio', value: '1', senior: 0, isColumn: true, options: [{ name: 'One', value: '1' }, { name: 'Two', value: '2' }] },
      { key: 'name', type: 'text', name: 'Name', value: 'initial', senior: 0, isColumn: true, regexpr: '^.+$' },
      { key: 'scene', type: 'text', value: 'must-stay-owned', senior: 2, channelEditable: false }
    ],
    'onUpdate:modelValue': value => formEvents.push(value)
  },
  mocks: { './distanceDialog.vue': { default: { render: () => null } }, '@/assets/CatchPhoto.png': { default: '' } }
})
try {
  assert.equal(formEvents.length, 0, 'hydrating a form does not emit an edit')
  editing.all(n => n.type === 'el-radio-group')[0].props.activate('2')
  await editing.settle()
  assert.equal(formEvents.length, 1, 'a radio edit emits one model update')
  assert.equal(formEvents[0].find(p => p.key === 'radio').value, '2')
  editing.instance.$.props.modelValue = reactive(formEvents[0])
  await editing.settle()
  assert.equal(formEvents.length, 1, 'parent echo does not emit again')
  editing.instance.$.props.modelValue.find(p => p.key === 'name').value = 'reset-in-place'
  await editing.settle()
  assert.equal(editing.instance.collect().find(p => p.key === 'name').value, 'reset-in-place')
  editing.all(n => n.type === 'el-input')[0].props.activate('')
  assert.equal((await editing.instance.validateAndCollect()).valid, false)
  editing.all(n => n.type === 'el-input')[0].props.activate('saved-before-blur')
  const result = await editing.instance.validateAndCollect()
  assert.equal(result.valid, true)
  assert.equal(result.params.find(p => p.key === 'name').value, 'saved-before-blur')
  assert.ok(!result.params.some(p => p.key === 'scene'), 'scene-owned fields never enter the draft')
  editing.instance.$.props.modelValue = []
  await editing.settle()
  assert.equal(editing.instance.collect().length, 0, 'empty replacement clears the draft')
} finally { editing.unmount() }

const parentConfig = reactive({
  channelId: 'channel-parent',
  taskParam: [{ key: 'name', type: 'text', name: 'Name', value: 'initial', senior: 0, isColumn: true }]
})
const parameterPanel = await mountComponent('views/gam/taskManager/editTask/paramSetting.vue', {
  props: { config: parentConfig, algorithmCode: 'algorithm-parent' },
  mocks: { './distanceDialog.vue': { default: { render: () => null } }, '@/assets/CatchPhoto.png': { default: '' } },
  globals: { document: { addEventListener() {}, removeEventListener() {} } }
})
try {
  const input = () => parameterPanel.all(n => n.type === 'el-input')[0]
  input().props.activate('through-parent')
  await parameterPanel.settle()
  assert.equal(parentConfig.taskParam[0].value, 'through-parent', 'real parent v-model receives the edit')
  parentConfig.taskParam[0].value = 'parent-reset'
  await parameterPanel.settle()
  assert.equal(input().props.modelValue, 'parent-reset', 'real parent in-place reset reaches the child draft')
  assert.equal((await parameterPanel.instance.validateAndCollect()).params[0].value, 'parent-reset')
} finally { parameterPanel.unmount() }

let resolveLibrary
const library = await mountComponent('views/gam/taskManager/editTask/dynamicForm.vue', {
  props: { modelValue: [{ key: 'library', type: 'commoditySet', name: 'Library', value: '', senior: 0, isColumn: true }] },
  mocks: { './distanceDialog.vue': { default: { render: () => null } }, '@/assets/CatchPhoto.png': { default: '' } },
  api: { boxQueryThingsLibInfo: () => new Promise(resolve => { resolveLibrary = resolve }) }
})
try {
  library.instance.$.props.modelValue = [{ key: 'new', type: 'text', name: 'New', value: 'new-model', senior: 0, isColumn: true }]
  library.instance.$.props.algorithmCode = 'changed'
  await library.settle()
  resolveLibrary({ resData: { thingsLibList: [{ id: 'old-id', name: 'Old' }] } })
  await library.settle()
  assert.deepEqual(JSON.parse(JSON.stringify(library.instance.collect().map(p => [p.key, p.value]))), [['new', 'new-model']], 'late library reply cannot modify a new model')
} finally { library.unmount() }

for (const platform of ['1', '-1', null, '15']) for (const scenario of [
  { search: '?channelId=outer', hash: '#/edit?channelId=hash-channel', expected: 'hash-channel' },
  { search: '?channelCode=legacy', hash: '#/edit', expected: 'resolved-channel', legacy: true },
  { search: '', hash: '#/edit', expected: 'prop-channel' }
]) {
  const calls = []
  const storage = { getItem: key => key === 'platformType' ? platform : null }
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

const storedLayout = JSON.stringify([
  { id: 'camera-1', name: 'Camera', taskList: [], runAlgorithmId: 'disabled-algorithm' }
])
const storage = new Map([['playedCameraList', storedLayout]])
const pending = () => new Promise(() => {})
const assetMocks = Object.fromEntries([
  'beep.ogg', 'big_screen_btn_bg.png', 'big_screen_no_camera.png', 'check-circle.png',
  'close-circle.png', 'error-image.png', 'screen-camera.png', 'screen-exit.png'
].map(name => [`@/assets/${name}`, { default: '' }]))
const screen = await mountComponent('views/box/bigScreen/warnningScreen/index.vue', {
  mocks: {
    '../components/flvVideo.vue': { default: { render: () => null } },
    '../components/detailDialog.vue': { default: { render: () => null } },
    '../components/captureDialog.vue': { default: { render: () => null } },
    '../components/TreeSelect.vue': { default: { render: () => null } },
    ...assetMocks,
    '@/components/eventBus': { default: { $emit() {} } },
    '@/utils/i18nResource': { resolveResourceAlgorithmName: () => '' },
    '@/utils/format': { formatSimilarity: value => value },
    moment: { default: () => ({ format: () => '', startOf() { return this }, endOf() { return this }, valueOf: () => 0 }) },
    lodash: { default: { find: (items, matcher) => items.find(
      typeof matcher === 'function'
        ? matcher
        : item => Object.entries(matcher).every(([key, value]) => item[key] === value)
    ) } }
  },
  globals: {
    localStorage: { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value) },
    document: { addEventListener() {}, removeEventListener() {}, body: {} },
    window: { addEventListener() {}, removeEventListener() {} },
    requestAnimationFrame: () => 1,
    cancelAnimationFrame() {}
  },
  api: {
    queryPopUpParam: pending,
    boxQueryCameraList: async () => ({ resData: { rows: [{
      videoChannelId: 'camera-1', channelName: 'Camera', channelStatus: 1, channelType: 0,
      taskList: [{ algorithmId: 'disabled-algorithm', enableStatus: 0 }]
    }] } }),
    boxQueryEvent: pending,
    boxAllAlgorithmInfo: pending, bigScreenWs: () => ''
  }
})
try {
  await screen.settle()
  assert.equal(JSON.parse(storage.get('playedCameraList'))[0].runAlgorithmId, '', 'disabled algorithms restore the raw stream')
} finally { screen.unmount() }

console.log('Component behavior checks passed')

await import('./edge_behavior_check.mjs')
