import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import lodash from 'lodash'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { collectNodeMetadata, updatePictureMatchDefaults } from '../src/views/gam/countManagement/arrangeDetail/flow/nodeState.js'
import { mergeTaskParamSchemasByKey } from '../src/utils/taskParamOwnership.js'

const repositoryRoot = process.env.COSMO_REPO_ROOT || fileURLToPath(new URL('../../../', import.meta.url))
const actions = JSON.parse(await readFile(path.join(repositoryRoot, 'data/resource/aiboxresource_bm1688/layout/actions.json'), 'utf8'))
const action = actions.find(item => item.id === 'PB_00006')
const empty = { default: { render: () => null } }
const mocks = {
  './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty,
  'tree-transfer-vue3': empty, lodash: { default: lodash },
  uuid: { v4: () => 'fixture-id' },
  '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
  '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
}

for (const [kind, key, name] of [
  ['body', 'param.workClothesSet', 'Workwear library'],
  ['face', 'param.faceSet', 'Face library']
]) {
  const changes = []
  const panel = await mountComponent('views/gam/countManagement/arrangeDetail/flow/NodeDetailPanel.vue', {
    props: {
      nodeId: 'match',
      onPictureMatchParamChange: param => changes.push(param),
      nodeData: {
        actionDetail: { ...action, actionId: action.id, flowActionId: 'match' },
        configObject: { params: [{ key: 'match.libraryType', value: kind }, { key, value: '12' }], webConfig: {} }
      }
    },
    mocks,
    globals: { document: { addEventListener() {}, removeEventListener() {} } },
    api: {
      boxQueryPersonLibInfo: async () => ({ resData: { personLibList: [{ id: 12, name: 'Workwear library' }] } }),
      boxQueryFaceLibInfo: async () => ({ resData: { faceLibList: [{ id: '12', name: 'Face library' }] } })
    }
  })
  try {
    const selector = () => panel.all(n => n.type === 'el-select' && n.props.multiple !== undefined)[0]
    assert.ok(panel.all(n => n.type === 'el-option' && n.props.label === name).length, `${kind}: the actual picture match node panel must offer its library`)
    assert.equal(panel.all(n => n.type === 'el-select' && n.props.multiple !== undefined).length, 1, 'only the selected library type is shown')
    assert.deepEqual(Array.from(selector().props.modelValue), ['12'])
    assert.equal(changes.length, 0, 'hydration must not overwrite existing scene defaults')
    selector().props.activate(['12', '34'])
    await panel.settle()
    const saved = panel.instance.submitForm()
    const sceneParams = [{ key, defaultValue: '99', value: '99', senior: 2, channelEditable: false }]
    const synced = updatePictureMatchDefaults(sceneParams, changes.at(-1))
    assert.equal(synced[0].defaultValue, '12,34', 'an explicit selection must replace an older scene default')
    assert.equal(synced[0].value, '12,34')
    assert.equal(synced[0].channelEditable, false)
    assert.equal(sceneParams[0].defaultValue, '99', 'do not mutate the previous scene snapshot')
    const legacy = { key, type: kind === 'body' ? 'workClothesSet' : 'faceSet', defaultValue: '', name: 'Legacy binding', level: '2' }
    const oldGraph = [
      { data: { actionId: 'PA_00005', configObject: { webConfig: { metaDataParams: [legacy] } } } },
      { data: { actionId: 'PB_00006', configObject: { webConfig: {} } } }
    ]
    assert.equal(collectNodeMetadata(oldGraph).length, 1, 'unopened match nodes must not remove legacy bindings')
    const graph = [oldGraph[0], { data: { actionId: 'PB_00006', configObject: saved } }]
    const merged = mergeTaskParamSchemasByKey(synced, collectNodeMetadata(graph))
    assert.deepEqual(merged.conflictKeys, [], 'converted feature-node descriptors must not conflict with the match node')
    assert.equal(merged.params.find(p => p.key === key).defaultValue, '12,34')
    assert.equal(saved.params.find(p => p.key === key)?.value, '12,34', 'node execution must receive selected libraries')
    assert.equal(saved.webConfig.metaDataParams.find(p => p.key === key)?.defaultValue, '12,34', 'scene parameter defaults must stay in sync')
    panel.instance.$.props.nodeData = { ...panel.instance.$.props.nodeData, templateVersion: 1, configObject: saved }
    await panel.settle()
    assert.deepEqual(Array.from(selector().props.modelValue), ['12', '34'], 'reopening the node must restore saved selections')
    const otherKind = kind === 'body' ? 'face' : 'body'
    const kindSelector = () => panel.all(n => n.type === 'el-select' && n.props.multiple === undefined && ['body', 'face'].includes(n.props.modelValue))[0]
    kindSelector().props.activate(otherKind)
    await panel.settle()
    assert.ok(panel.all(n => n.type === 'el-option' && n.props.label === (otherKind === 'body' ? 'Workwear library' : 'Face library')).length)
    kindSelector().props.activate(kind)
    await panel.settle()
    assert.deepEqual(Array.from(selector().props.modelValue), ['12', '34'], 'switching library type must preserve the other binding')
    panel.all(n => n.type === 'el-input' && n.props.modelValue === '80')[0].props.activate('0')
    await panel.settle()
    assert.equal(panel.instance.submitForm().params.find(p => p.key === 'param.limitScore').value, '0')
    selector().props.activate([])
    await panel.settle()
    const cleared = panel.instance.submitForm()
    assert.equal(cleared.params.find(p => p.key === key)?.value, '', 'clearing must overwrite a prior node binding')
    assert.equal(cleared.webConfig.metaDataParams.find(p => p.key === key)?.defaultValue, '')
    assert.equal(updatePictureMatchDefaults(synced, changes.at(-1))[0].defaultValue, '', 'clearing must clear scene defaults too')
    panel.instance.$.props.nodeData = {
      ...panel.instance.$.props.nodeData, templateVersion: 2,
      configObject: {
        params: [{ key: 'match.libraryType', value: kind }],
        webConfig: { metaDataParams: [{ key, defaultValue: '34' }] }
      }
    }
    await panel.settle()
    assert.deepEqual(Array.from(selector().props.modelValue), ['34'], 'legacy metadata defaults must also load')
    selector().props.activate([])
    await panel.settle()
    assert.equal(panel.instance.submitForm().params.find(p => p.key === key)?.value, '', 'clearing must not restore the legacy default')
    const eventCount = changes.length
    panel.instance.$.props.sceneParams = [{ key, defaultValue: '34' }]
    panel.instance.$.props.nodeData = {
      ...panel.instance.$.props.nodeData, templateVersion: 3,
      configObject: { params: [{ key: 'match.libraryType', value: kind }, { key, value: '12' }], webConfig: {} }
    }
    await panel.settle()
    assert.deepEqual(Array.from(selector().props.modelValue), ['34'], 'show the effective scene binding when it overrides the node')
    assert.equal(changes.length, eventCount, 'scene hydration must not emit an edit')
  } finally { panel.unmount() }
}
console.log('Picture match node panel library checks passed')
