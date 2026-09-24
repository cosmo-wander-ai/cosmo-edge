import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import lodash from 'lodash'
import moment from 'moment'
import { h, inject, provide } from 'vue'
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
  props: { modelValue: JSON.stringify(schema), atomicList: [{ position: 'classify', atomicCode: 'model', atomicName: 'Classifier', labelList: [{ class_name: 'yes', nameCN: 'Present', threshold: [0.5, 0.4] }, { class_name: 'other', nameCN: 'Other', threshold: [0.8, 0.7] }] }], 'onUpdate:modelValue': value => { savedBinary = JSON.parse(value) } },
  globals: { TextEncoder },
  components: { 'el-table-column': { render() { return h('el-table-column', this.$attrs) } } }
})
try {
  const control = field => editor.all(n => n.type === 'el-select' && n.props['data-field'] === field)[0]
  control('type').props.activate('binary')
  await editor.settle()
  assert.equal(validAttributeSchema(savedBinary), true)
  assert.equal(savedBinary.attributes[0].options[1].label, '')
  assert.equal(editor.all(n => n.type === 'el-select' && n.props['data-field'] === 'binary-label').length, 1, 'model labels must be selected instead of typed')
  assert.equal(editor.all(n => n.type === 'el-input').length, 0, 'raw identifiers and model labels must not require text inputs')
  control('negative-name').props.activate('Absent')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options[1].name, 'Absent')
  control('source').props.activate('classify')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options.length, 2, 'changing source keeps binary outcomes')
  assert.equal(savedBinary.attributes[0].options[1].name, 'Absent')
  assert.equal(validAttributeSchema(savedBinary), true)
  control('binary-label').props.activate('other')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options[0].label, 'other')
  assert.equal(savedBinary.attributes[0].options[0].value, 'yes', 'existing outcome identities remain stable')
  assert.equal(savedBinary.attributes[0].threshold, 0.8, 'model threshold follows the selected behavior')
  const recommended = editor.all(n => n.type === 'el-option' && n.props.value === 0.7 && String(n.props.label).includes('attributeAnalysis.recommendedThreshold'))[0]
  assert.ok(recommended)
  control('threshold').props.activate(recommended.props.value)
  await editor.settle()
  assert.equal(savedBinary.attributes[0].threshold, 0.7)
  control('type').props.activate('single')
  await editor.settle()
  assert.equal(savedBinary.attributes[0].options.length, 1)
  assert.equal(validAttributeSchema(savedBinary), true)
} finally { editor.unmount() }
// Exercise the customer path through real label selects, including table slots.
const choiceTables = {
  'el-table': { props: ['data'], setup(props, { slots }) { provide('attributeRows', () => props.data || []); return () => h('el-table', {}, slots.default?.()) } },
  'el-table-column': { setup(_, { slots, attrs }) { const rows = inject('attributeRows', () => []); return () => h('el-table-column', attrs, rows().flatMap((row, $index) => slots.default?.({ row, $index }) || [])) } }
}
let selectedSchema
const choiceEditor = await mountComponent('views/gam/countManagement/arrangeDetail/flow/AttributeSchemaEditor.vue', {
  props: {
    modelValue: JSON.stringify({ objectType: 'custom-object', attributes: [] }),
    atomicList: [{ position: 'behavior', atomicCode: 'behavior-model', atomicName: 'Behavior', labelList: [{ class_name: 'phone', nameCN: 'Phone', threshold: [0.8, 0.6] }, { class_name: 'with bag', nameCN: 'Bag', threshold: [0.7] }, { class_name: 'disabled', nameCN: 'Disabled', used: false }] }],
    'onUpdate:modelValue': value => { selectedSchema = JSON.parse(value) }
  },
  components: choiceTables, globals: { TextEncoder },
  mocks: { '@/i18n': { t: (key, values) => key === 'attributeAnalysis.notLabel' ? `Not ${values.name}` : key } }
})
try {
  const controls = field => choiceEditor.all(n => n.type === 'el-select' && n.props['data-field'] === field)
  const click = field => choiceEditor.all(n => n.type === 'el-button' && n.props['data-field'] === field)[0].props.onClick()
  assert.ok(choiceEditor.all(n => n.type === 'el-option' && n.props.value === 'custom-object').length, 'legacy object types remain selectable')
  click('add-attribute')
  await choiceEditor.settle()
  controls('source')[0].props.activate('behavior')
  await choiceEditor.settle()
  assert.equal(validAttributeSchema(selectedSchema), true, 'a source selection generates a complete categorical mapping')
  assert.equal(selectedSchema.attributes[0].name, 'Behavior')
  assert.deepEqual(selectedSchema.attributes[0].options.map(option => option.label), ['phone', 'with bag'])
  assert.deepEqual(selectedSchema.attributes[0].options.map(option => option.value), ['phone', 'value'])
  assert.equal(choiceEditor.all(n => n.type === 'el-input').length, 0)
  controls('option-label')[0].props.activate('with bag')
  await choiceEditor.settle()
  assert.equal(selectedSchema.attributes[0].options[0].label, 'phone', 'duplicate label choices are rejected')
  choiceEditor.all(n => n.type === 'el-button' && Object.hasOwn(n.props, 'link') && n.props.type === 'danger')[0].props.onClick()
  await choiceEditor.settle()
  assert.equal(selectedSchema.attributes[0].options.length, 1)
  controls('type')[0].props.activate('binary')
  await choiceEditor.settle()
  assert.equal(selectedSchema.attributes[0].name, 'Bag')
  assert.equal(selectedSchema.attributes[0].options[1].name, 'Not Bag')
  controls('binary-label')[0].props.activate('phone')
  await choiceEditor.settle()
  assert.equal(selectedSchema.attributes[0].name, 'Phone')
  assert.equal(selectedSchema.attributes[0].options[1].name, 'Not Phone')
  assert.equal(selectedSchema.attributes[0].threshold, 0.8)
  assert.equal(validAttributeSchema(selectedSchema), true)
  click('add-attribute')
  await choiceEditor.settle()
  choiceEditor.all(n => n.type === 'el-button' && Object.hasOwn(n.props, 'plain') && n.props.type === 'danger')[0].props.onClick()
  await choiceEditor.settle()
  click('add-attribute')
  await choiceEditor.settle()
  assert.equal(new Set(selectedSchema.attributes.map(attribute => attribute.key)).size, 2, 'generated keys stay unique after deletion and insertion')
} finally { choiceEditor.unmount() }

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
