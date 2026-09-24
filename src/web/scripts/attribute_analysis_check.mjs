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

const repositoryRoot = process.env.COSMO_REPO_ROOT || fileURLToPath(new URL('../../../', import.meta.url))
const empty = { default: { render: () => null } }
const components = { 'el-table-column': { render() { return h('el-table-column', this.$attrs) } } }
for (const platform of ['bm1688', 'cv186x', 'x86']) {
  const actions = JSON.parse(await readFile(`${repositoryRoot}/data/resource/aiboxresource_${platform}/layout/actions.json`, 'utf8'))
  const action = actions.find(a => a.id === 'BA_20003')
  const form = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
    props: { actionDetail: { ...action, flowActionId: 'accumulate' }, configObject: { params: flow[2].configObject.params, webConfig: {} }, attributeSources: [{ position: 'classify', atomicCode: 'model', atomicName: 'Classifier', labelList: [] }] },
    components, globals: { TextEncoder },
    mocks: { '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default])), './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty, 'tree-transfer-vue3': empty, uuid: { v4: () => 'fixture' }, lodash: { default: lodash }, '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } } }
  })
  try {
    const saved = form.instance.submitForm()
    assert.equal(saved.params.find(p => p.key === 'inputMode').value, 'attributes')
    assert.deepEqual(JSON.parse(saved.params.find(p => p.key === 'attributeSchema').value), schema)
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
    algorithmLayoutDetail: async request => { assert.equal(request.id, '42'); return { resData: { algorithmProcessdata: JSON.stringify(flow), attributeSchemaId: 'current-version' } } },
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
} finally { eventPage.unmount() }
console.log('Attribute schema, scene form, historical display and multi-channel query checks passed')
