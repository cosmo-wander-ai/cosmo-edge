import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import lodash from 'lodash'
import moment from 'moment'
import { h } from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { attributeDisplay, attributeClassifierSources, validAttributeSchema, validAttributeFlow, parseAttributeSchema } from '../src/utils/attributeAnalysis.js'

const schema = { objectType: 'person', attributes: [{ key: 'hat', name: 'Hat', sourceNode: 'classify', modelCode: 'model', type: 'single', threshold: 0.5, minRatio: 0.6, options: [{ label: 'yes', value: 'yes', name: 'Wearing a hat' }, { label: 'no', value: 'no', name: 'No hat' }] }] }
assert.equal(validAttributeSchema(schema), true)
assert.equal(validAttributeSchema({ ...schema, attributes: [...schema.attributes, ...schema.attributes] }), false)
assert.deepEqual(parseAttributeSchema('null').attributes, [])
const node = (actionId, flowActionId, preFlowActionId, params = []) => ({ actionId, flowActionId, preFlowActionId, configObject: { params } })
const flow = [node('AA_00003', 'track', '-1'), node('AA_00002', 'classify', 'track', [{ key: 'atomicCode', value: 'model' }]), node('BA_20003', 'accumulate', 'classify', [{ key: 'inputMode', value: 'attributes' }, { key: 'attributeSchema', value: JSON.stringify(schema) }]), node('BA_00004', 'report', 'accumulate')]
assert.equal(validAttributeFlow(flow), true)
assert.equal(validAttributeFlow(flow.filter(n => n.flowActionId !== 'track')), false)
assert.equal(validAttributeFlow(flow.map(n => n.flowActionId === 'classify' ? { ...n, preFlowActionId: '-1' } : n)), false)
assert.equal(validAttributeFlow(flow.map(n => n.flowActionId === 'accumulate' ? { ...n, preFlowActionId: 'classify,track' } : n)), false)
const original = { schema, attributes: [{ key: 'hat', status: 'valid', values: ['yes'] }] }
assert.equal(attributeDisplay(original, { ...schema.attributes[0], options: [{ value: 'yes', name: 'Changed' }] }, 'unknown', 'failed', 'n/a'), 'Wearing a hat')
assert.equal(attributeDisplay({ ...original, attributes: [{ key: 'hat', status: 'unknown' }] }, schema.attributes[0], 'unknown', 'failed', 'n/a'), 'unknown')
const canvas = flow.map(n => ({ id: n.flowActionId, data: { ...n, configObject: { webConfig: { atomic: { atomicCode: 'model' } } } } }))
assert.deepEqual(attributeClassifierSources(canvas, flow.map(n => ({ source: n.preFlowActionId, target: n.flowActionId })), 'accumulate').map(s => s.position), ['classify'])

const binarySchema = JSON.parse(JSON.stringify(schema))
binarySchema.attributes[0].type = 'binary'
binarySchema.attributes[0].options = [{ label: 'yes', value: 'yes', name: 'Present' }, { label: '', value: 'no', name: 'Absent' }]
assert.equal(validAttributeSchema(binarySchema), true)
for (const options of [binarySchema.attributes[0].options.slice(0, 1), [{ label: '', value: 'yes', name: 'Present' }, { label: '', value: 'no', name: 'Absent' }], [{ label: 'yes', value: 'yes', name: 'Present' }, { label: 'no', value: 'no', name: 'Absent' }], [{ label: 'yes', value: 'yes', name: 'Present' }, { label: '', value: 'yes', name: 'Absent' }]]) {
  assert.equal(validAttributeSchema({ ...binarySchema, attributes: [{ ...binarySchema.attributes[0], options }] }), false)
}
const negativeRecord = { schema: binarySchema, attributes: [{ key: 'hat', status: 'valid', values: ['no'] }] }
assert.equal(attributeDisplay(negativeRecord, binarySchema.attributes[0], 'unknown', 'failed', 'n/a'), 'Absent')
assert.equal(attributeDisplay({ ...original, attributes: [{ key: 'hat', status: 'unknown' }] }, binarySchema.attributes[0], 'unknown', 'failed', 'n/a'), 'unknown', 'historical unknowns must not become negative')
let savedBinary
const editor = await mountComponent('views/gam/countManagement/arrangeDetail/flow/AttributeSchemaEditor.vue', {
  props: { modelValue: JSON.stringify(schema), atomicList: [{ position: 'classify', atomicCode: 'model', atomicName: 'Classifier', labelList: [{ class_name: 'yes', nameCN: 'Present' }, { class_name: 'other', nameCN: 'Other' }] }], 'onUpdate:modelValue': value => { savedBinary = JSON.parse(value) } },
  globals: { TextEncoder },
  components: { 'el-table-column': { render() { return h('el-table-column', this.$attrs) } } }
})
try {
  editor.all(n => n.type === 'el-select')[1].props.activate('binary')
  await editor.settle()
  assert.equal(validAttributeSchema(savedBinary), true)
  assert.equal(savedBinary.attributes[0].options[1].label, '')
  editor.all(n => n.type === 'el-input' && n.props.modelValue === 'attributeAnalysis.no')[0].props.activate('Absent')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options[1].name, 'Absent')
  editor.all(n => n.type === 'el-select')[0].props.activate('classify')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options.length, 2, 'changing source keeps binary outcomes')
  assert.equal(savedBinary.attributes[0].options[1].name, 'Absent')
  assert.equal(validAttributeSchema(savedBinary), true)
  editor.all(n => n.type === 'el-select')[1].props.activate('single')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options.length, 1)
  assert.equal(validAttributeSchema(savedBinary), true)
} finally { editor.unmount() }
const binaryFlow = JSON.parse(JSON.stringify(flow))
binaryFlow[2].configObject.params.find(p => p.key === 'attributeSchema').value = JSON.stringify(binarySchema)
assert.equal(validAttributeFlow(binaryFlow), true)

const repositoryRoot = process.env.COSMO_REPO_ROOT || fileURLToPath(new URL('../../../', import.meta.url))
const empty = { default: { render: () => null } }
const components = { 'el-table-column': { render() { return h('el-table-column', this.$attrs) } } }
for (const platform of ['bm1688', 'cv186x', 'x86']) {
  const actions = JSON.parse(await readFile(`${repositoryRoot}/data/resource/aiboxresource_${platform}/layout/actions.json`, 'utf8'))
  const action = actions.find(a => a.id === 'BA_20003')
  const formFlow = platform === 'bm1688' ? binaryFlow : flow
  const formSchema = platform === 'bm1688' ? binarySchema : schema
  const form = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
    props: { actionDetail: { ...action, flowActionId: 'accumulate' }, configObject: { params: formFlow[2].configObject.params, webConfig: {} }, attributeSources: [{ position: 'classify', atomicCode: 'model', atomicName: 'Classifier', labelList: [] }] },
    components, globals: { TextEncoder },
    mocks: { '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default])), './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': empty, uuid: { v4: () => 'fixture' }, lodash: { default: lodash }, '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } } }
  })
  try {
    const saved = form.instance.submitForm()
    assert.equal(saved.params.find(p => p.key === 'inputMode').value, 'attributes')
    assert.deepEqual(JSON.parse(saved.params.find(p => p.key === 'attributeSchema').value), formSchema)
    const purpose = form.all(n => n.type === 'el-radio-group')[0]
    purpose.props.activate(false)
    await form.settle()
    assert.equal(form.instance.submitForm().params.find(p => p.key === 'inputMode').value, 'auto')
  } finally { form.unmount() }
}

const requests = []
const sceneRequests = []
const eventPage = await mountComponent('views/box/eventQuery/attributes/index.vue', {
  components, globals: { TextEncoder }, mocks: { moment: { default: moment }, 'element-plus': { ElMessage: { error(message) { throw new Error(message) } } } },
  api: {
    algorithmInquire: async request => { sceneRequests.push(request); return { resData: { rows: [{ algorithmCategory: '12', algorithmId: '42', algorithmName: 'Scene' }] } } },
    getChannelList: async () => ({ resData: { rows: [{ videoChannelId: 'a', channelName: 'A' }, { videoChannelId: 'b', channelName: 'B' }] } }),
    algorithmLayoutDetail: async request => { assert.equal(request.id, '42'); return { resData: { algorithmProcessdata: JSON.stringify(binaryFlow), attributeSchemaId: 'current-version' } } },
    boxQueryEvent: async request => { requests.push(request); return { resData: { total: 0, rows: [], attributeSummary: { schemas: [], statistics: [] } } } }
  }
})
try {
  assert.equal(sceneRequests[0].algorithmUsage, '1', 'algorithm page API requires a string usage filter')
  const sceneOption = eventPage.all(n => n.type === 'el-option' && n.props.label === 'Scene')[0]
  assert.equal(sceneOption.props.value, '42', 'scene choices must use algorithmId from the page API')
  eventPage.all(n => n.type === 'el-select')[0].props.activate(sceneOption.props.value)
  for (let i = 0; i < 8; i++) await eventPage.settle()
  assert.equal(requests[0].attributeSchemaId, 'current-version', 'current definition is queryable before any record exists')
  assert.deepEqual(JSON.parse(JSON.stringify(requests[0].algorithmCodes)), ['42'])
  const controls = eventPage.all(n => n.type === 'el-select')
  controls[1].props.activate(['a', 'b'])
  controls.at(-1).props.activate('value:yes')
  await eventPage.settle()
  eventPage.all(n => n.type === 'el-button' && n.props.type === 'primary')[0].props.onClick()
  for (let i = 0; i < 4; i++) await eventPage.settle()
  assert.deepEqual(JSON.parse(JSON.stringify(requests.at(-1).channelIds)), ['a', 'b'])
  assert.deepEqual(JSON.parse(JSON.stringify(requests.at(-1).attributeFilters)), [{ key: 'hat', value: 'yes', status: 'valid' }])
  assert.equal(requests.at(-1).includeAttributeSummary, true)
  const negativeOption = eventPage.all(n => n.type === 'el-option' && n.props.label === 'Absent')[0]
  controls.at(-1).props.activate(negativeOption.props.value)
  await eventPage.settle()
  eventPage.all(n => n.type === 'el-button' && n.props.type === 'primary')[0].props.onClick()
  for (let i = 0; i < 4; i++) await eventPage.settle()
  assert.deepEqual(JSON.parse(JSON.stringify(requests.at(-1).attributeFilters)), [{ key: 'hat', value: 'no', status: 'valid' }])
} finally { eventPage.unmount() }
console.log('Attribute schema, scene form, historical display and multi-channel query checks passed')
