import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import lodash from 'lodash'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { isPairPictureWorkflow, filterPictureLibraryParams, updatePictureMatchDefaults } from '../src/views/gam/countManagement/arrangeDetail/flow/nodeState.js'

const graph = [{ actionId: 'PB_00007', configObject: { params: [{ key: 'pair.featureType', value: 'face' }] } }]
assert.ok(isPairPictureWorkflow(JSON.stringify(graph)))
assert.equal(isPairPictureWorkflow('broken'), false)
assert.deepEqual(filterPictureLibraryParams([{ key: 'param.faceSet' }, { key: 'param.workClothesSet' }, { key: 'pair.threshold' }], graph), [{ key: 'pair.threshold' }])
assert.equal(updatePictureMatchDefaults([{ key: 'pair.threshold', defaultValue: '80' }], { key: 'pair.threshold', value: '' })[0].defaultValue, '')

let sequence = 0, failure = false, score = 0
const requests = [], cancelled = [], notices = []
const page = await mountComponent('views/gam/imageAnalysis/index.vue', {
  mocks: {
    uuid: { v4: () => 'pair-request' },
    '@element-plus/icons-vue': { Upload: 'Upload', VideoPlay: 'VideoPlay', Delete: 'Delete' },
    'element-plus': { ElMessage: { warning: message => notices.push(message), error: message => notices.push(message), success() {} } },
    '@/utils/chunkUpload': { UploadPurpose: { IMAGE: 'image' }, uploadFileInChunks: async () => ({ uploadId: `upload-${++sequence}` }) }
  },
  globals: { document: { querySelector() { return null } }, URL: { createObjectURL: () => 'blob:pair', revokeObjectURL() {} } },
  api: {
    algorithmInquire: async () => ({ resData: { rows: [{ algorithmId: 'pair', algorithmName: 'Pair' }, { algorithmId: 'single', algorithmName: 'Single' }] } }),
    algorithmLayoutDetail: async ({ id }) => ({ resData: {
      algorithmProcessdata: JSON.stringify(id === 'pair' ? graph : []),
      algorithmMetadata: JSON.stringify({ params: [{ key: 'pair.threshold', defaultValue: '80', name: 'Threshold', type: 'text' }] })
    } }),
    pTaskCreate: async () => ({}), pTaskCancle: async () => ({}),
    cancelAtomicModelUpload: async ({ uploadId }) => { cancelled.push(uploadId) },
    imageAnalysis: async params => {
      requests.push(params)
      if (failure) throw { resCode: 1, resMsg: [{ messageKey: 'api.error.PicturePairNoTarget' }], resData: { errorSide: 'B' } }
      return { resData: { comparison: { score, featureType: 'face', decision: params.taskConfig.params[0]?.value === '' ? 'score_only' : 'not_matched' },
        targetList: [{ box: { x: 0, y: 0, width: 30, height: 30 } }], referenceTargetList: [{ box: { x: 0, y: 0, width: 30, height: 30 } }] } }
    }
  }
})
try {
  const select = () => page.all(n => n.type === 'el-select' && n.props.class === 'algorithm-select')[0]
  select().props.activate('pair')
  await page.settle(); await page.settle()
  assert.equal(page.all(n => n.type === 'el-upload').length, 2)
  const analyze = () => page.all(n => n.type === 'el-button' && n.props.type === 'success')[0]
  assert.equal(analyze().props.disabled, true)
  const file = name => ({ name, raw: { type: 'image/png', size: 20 } })
  page.all(n => n.type === 'el-upload')[0].props['on-change'](file('A.png'))
  await page.settle()
  assert.equal(analyze().props.disabled, true, 'a pair cannot run with only A')
  page.all(n => n.type === 'el-upload')[1].props['on-change'](file('B.png'))
  await page.settle()
  assert.equal(analyze().props.disabled, false)
  await analyze().props.onClick(); await page.settle()
  assert.equal(requests.length, 1, 'both images must use one request')
  assert.equal(requests[0].uploadId, 'upload-1')
  assert.equal(requests[0].referenceImage.uploadId, 'upload-2')
  assert.ok(page.all(n => n.text.includes('0.00 / 100')).length, 'valid zero must be shown')
  assert.ok(page.all(n => n.text.includes('imageAnalysis.pairBelow')).length)
  const threshold = page.all(n => n.type === 'el-input')[0]
  threshold.props.activate('')
  await page.settle()
  score = 100
  await analyze().props.onClick(); await page.settle()
  assert.equal(requests.at(-1).taskConfig.params.find(p => p.key === 'pair.threshold').value, '', 'empty explicitly clears scene threshold')
  assert.ok(page.all(n => n.text.includes('imageAnalysis.pairScoreOnly')).length)
  failure = true
  await analyze().props.onClick(); await page.settle()
  assert.ok(page.all(n => n.text.includes('imageAnalysis.imageB') && n.text.includes('api.error.PicturePairNoTarget')).length)
  assert.equal(page.all(n => n.text.includes('/ 100')).length, 0, 'error must not show a score from the previous request')
  assert.deepEqual(cancelled, ['upload-1', 'upload-2', 'upload-3', 'upload-4', 'upload-5', 'upload-6'])
  assert.deepEqual(notices, [])
  select().props.activate('single')
  await page.settle(); await page.settle()
  assert.equal(page.all(n => n.type === 'el-upload').length, 1, 'ordinary image workflows retain batch upload')
} finally { page.unmount() }
console.log('Dual-image comparison UI checks passed')

const actions = JSON.parse(await readFile(new URL('../../../data/resource/aiboxresource_bm1688/layout/actions.json', import.meta.url), 'utf8'))
const action = actions.find(a => a.id === 'PB_00007')
const empty = { default: { render: () => null } }
const changes = []
const panel = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
  props: {
    actionDetail: { ...action, actionId: action.id, flowActionId: 'pair' },
    configObject: { params: [{ key: 'pair.featureType', value: 'face' }, { key: 'pair.threshold', value: '80' }], webConfig: {} },
    sceneParams: [{ key: 'pair.threshold', defaultValue: '' }],
    flowData: [], atomicList: [],
    onPictureMatchParamChange: value => changes.push(value)
  },
  mocks: {
    './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty,
    'tree-transfer-vue3': empty, lodash: { default: lodash }, uuid: { v4: () => 'pair-node' },
    '@/components/eventBus.js': { default: { $emit() {}, $on() {}, $off() {} } },
    '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
  },
  globals: { document: { addEventListener() {}, removeEventListener() {} } }
})
try {
  const input = () => panel.all(n => n.type === 'el-input')[0]
  assert.ok(input(), 'pair threshold must be editable in the node panel')
  assert.equal(input().props.modelValue, '', 'an explicitly cleared scene threshold overrides the stored node threshold')
  input().props.activate('70')
  await panel.settle()
  assert.equal(changes.at(-1).defaultValue, '70')
  let saved = panel.instance.submitForm()
  assert.equal(saved.params.find(p => p.key === 'pair.threshold').value, '70')
  input().props.activate('')
  await panel.settle()
  saved = panel.instance.submitForm()
  assert.equal(saved.params.find(p => p.key === 'pair.threshold').value, '')
  assert.equal(saved.webConfig.metaDataParams.find(p => p.key === 'pair.threshold').defaultValue, '')
  assert.equal(saved.webConfig.metaDataParams.some(p => p.key === 'param.faceSet' || p.key === 'param.workClothesSet'), false)
} finally { panel.unmount() }
console.log('Dual-image comparison node threshold checks passed')
