import assert from 'node:assert/strict'
import lodash from 'lodash'
import { mergeTaskParamSchemasByKey } from '../src/utils/taskParamOwnership.js'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { readCatalog, validCatalog, questionsFromParams, readRoiQuestions, regionParams } from '../src/utils/visualQuestions.js'

const plain = value => JSON.parse(JSON.stringify(value))
const text = n => [n.text, ...n.children.map(text)].join(' ')
let saved = ''
let editor
editor = await mountComponent('components/VisualQuestionEditor.vue', {
  props: { modelValue: saved, 'onUpdate:modelValue': value => { saved = value; editor.instance.$.props.modelValue = value } }
})
try {
  const add = () => editor.all(n => n.type === 'el-button' && text(n).includes('visualQuestions.add') && !text(n).includes('addOption'))[0].props.onClick()
  add(); await editor.settle()
  editor.all(n => n.type === 'el-input')[0].props.activate('Is a person wearing a helmet?')
  await editor.settle()
  add(); await editor.settle()
  editor.all(n => n.type === 'el-input')[1].props.activate('What color is the helmet?')
  await editor.settle()
  editor.all(n => n.type === 'el-select')[1].props.activate('choice')
  await editor.settle()
  let inputs = editor.all(n => n.type === 'el-input')
  inputs[2].props.activate('red'); await editor.settle()
  editor.all(n => n.type === 'el-input')[4].props.activate('blue'); await editor.settle()
  assert.equal(validCatalog(saved), true)
  const catalog = readCatalog(saved)
  assert.deepEqual(catalog.questions.map(q => q.id), ['question-1', 'question-2'])
  assert.deepEqual(catalog.default, ['question-1', 'question-2'])
  assert.deepEqual(catalog.questions[1].criteria.map(o => o.label), ['red', 'blue'])
  assert.ok(catalog.questions[1].version > 1)
} finally { editor.unmount() }
const reopened = await mountComponent('components/VisualQuestionEditor.vue', { props: { modelValue: saved } })
assert.equal(reopened.all(n => n.type === 'el-input')[1].props.modelValue, 'What color is the helmet?')
reopened.unmount()
const taskParams = [{ key: 'param.visual.catalog', value: saved }]
const questions = questionsFromParams(taskParams)
assert.equal(questions.length, 2)
let roiParams = [{ key: 'name', value: 'door' }, { key: 'existing', value: 'preserved' }]
const roi = await mountComponent('components/VisualRoiEditor.vue', { props: { params: roiParams, questions } })
try {
  roi.all(n => n.type === 'el-select')[0].props.activate('select'); await roi.settle()
  roi.all(n => n.type === 'el-checkbox-group')[0].props['onUpdate:modelValue'](['question-2', 'question-1'])
  await roi.settle()
  const result = plain(roi.instance.collect())
  assert.equal(result.valid, true)
  roiParams = regionParams({ name: 'renamed door', params: result.params })
  assert.deepEqual(readRoiQuestions(roiParams).selected, ['question-2', 'question-1'])
  assert.equal(roiParams.find(p => p.key === 'existing').value, 'preserved')
} finally { roi.unmount() }
const promptRoi = await mountComponent('components/VisualRoiEditor.vue', { props: { params: roiParams, questions } })
try {
  assert.deepEqual(plain(promptRoi.all(n => n.type === 'el-checkbox-group')[0].props.modelValue), ['question-2', 'question-1'])
  promptRoi.all(n => n.type === 'el-select')[0].props.activate('prompt'); await promptRoi.settle()
  promptRoi.all(n => n.type === 'el-input')[0].props.activate('Is a person entering this door?'); await promptRoi.settle()
  const custom = plain(promptRoi.instance.collect())
  assert.equal(custom.valid, true)
  assert.equal(custom.params.some(p => p.key === 'visual.questions'), false)
  assert.equal(readRoiQuestions(custom.params).prompt, 'Is a person entering this door?')
  promptRoi.all(n => n.type === 'el-select')[0].props.activate('inherit'); await promptRoi.settle()
  const inherited = plain(promptRoi.instance.collect())
  assert.equal(readRoiQuestions(inherited.params).mode, 'inherit')
  assert.equal(inherited.params.find(p => p.key === 'existing').value, 'preserved')
} finally { promptRoi.unmount() }
console.log('Visual question editors PASS: multiple questions, choice order, saved values, ROI selection/prompt/inheritance and parameter preservation')

// The scene form normally hides channel-owned fields; question catalogs are
// intentionally edited here as defaults and remain overridable in each task.
const inert = { default: { render: () => null } }
const scene = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
  props: {
    actionDetail: { actionId: 'DA_00003', flowActionId: 'visual-node', inputParamConfig: JSON.stringify([
      { key: 'vlmProvider', type: 'select', level: '1', defaultValue: 'laya_v', options: [] },
      { key: 'visual.catalog', type: 'visualQuestions', level: '2', defaultValue: '', dependsOn: { key: 'vlmProvider', value: 'laya_v' } }
    ]) },
    configObject: { params: [{ key: 'vlmProvider', value: 'laya_v' }], webConfig: { metaDataParams: taskParams.map(p => ({ ...p, key: 'visual.catalog' })) } }
  },
  mocks: { '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, inert.default])), lodash: { default: lodash }, uuid: { v4: () => 'id' }, './ConditionView.vue': inert, './TreeSelectMultiple.vue': inert,
    'tree-transfer-vue3': inert, '@/components/eventBus.js': { default: { $on() {}, $off() {}, $emit() {} } } }
})
try {
  assert.equal(scene.all(n => n.type === 'el-input' && n.props.modelValue === 'What color is the helmet?').length, 1)
  const catalogParam = plain(scene.instance.submitForm()).webConfig.metaDataParams.find(p => p.key === 'visual.catalog')
  assert.deepEqual(readCatalog(catalogParam.value), readCatalog(saved))
} finally { scene.unmount() }
console.log('Scene question defaults are visible, hydrated and retained in task metadata PASS')

const revised = readCatalog(saved)
revised.questions[1].instructions = 'Is a person at the entrance?'
const latest = JSON.stringify(revised)
const merged = mergeTaskParamSchemasByKey(
  [{ key: 'visual.catalog', type: 'visualQuestions', value: saved, senior: 2, channelEditable: false }],
  [{ key: 'visual.catalog', type: 'visualQuestions', value: latest, senior: 0, channelEditable: true }]
).params[0]
assert.equal(merged.value, latest)
assert.equal(merged.defaultValue, latest)
assert.equal(merged.channelEditable, false)
console.log('Updated scene catalog becomes the task default while metadata visibility is preserved PASS')

let policySaved = saved
let policyEditor
policyEditor = await mountComponent('components/VisualQuestionEditor.vue', {
  props: { modelValue: saved, 'onUpdate:modelValue': value => { policySaved = value; policyEditor.instance.$.props.modelValue = value } },
  api: { boxQueryLayaReview: async () => ({ resCode: 1, resData: { runtime: { accepted_profiles: [{ id: 'entrance' }] } } }) }
})
try {
  await policyEditor.settle()
  policyEditor.all(n => n.type === 'el-select' && n.props['aria-label'] === 'visualQuestions.decisionMode')[0].props.activate('filter')
  await policyEditor.settle()
  assert.deepEqual(readCatalog(policySaved).decision, { mode: 'filter', profile_id: 'entrance' })
  assert.equal(validCatalog(policySaved), true)
  assert.deepEqual(readCatalog(policySaved).questions, readCatalog(saved).questions)
  policyEditor.all(n => n.type === 'el-select' && n.props['aria-label'] === 'visualQuestions.decisionMode')[0].props.activate('review')
  await policyEditor.settle()
  assert.equal(readCatalog(policySaved).decision.mode, 'review')
} finally { policyEditor.unmount() }
assert.equal(validCatalog(JSON.stringify({ ...readCatalog(saved), decision: { mode: 'filter', profile_id: '' } })), false)
console.log('Accepted filtering selection and return to review preserve question configuration PASS')
