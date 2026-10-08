import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const cases = [
  ['face', 'param.faceSet', 'faceSet', 'Face library'],
  ['workwear', 'param.workClothesSet', 'workClothesSet', 'Workwear library'],
  ['legacy-face', 'param.faceSet', 'text', 'Face library'],
  ['legacy-workwear', 'param.workClothesSet', 'text', 'Workwear library']
]
const requests = []
const errors = []
const page = await mountComponent('views/gam/imageAnalysis/index.vue', {
  mocks: {
    uuid: { v4: () => 'test-request' },
    '@element-plus/icons-vue': { Upload: 'Upload', VideoPlay: 'VideoPlay', Delete: 'Delete' },
    'element-plus': { ElMessage: { success() {}, warning() {}, error: error => errors.push(error) } },
    '@/utils/chunkUpload': { UploadPurpose: { IMAGE: 'image' }, uploadFileInChunks: async () => ({ uploadId: 'test-upload' }) }
  },
  globals: { URL: { createObjectURL: () => 'blob:test', revokeObjectURL() {} } },
  api: {
    algorithmInquire: async () => ({ resData: { rows: cases.map(([id]) => ({ algorithmId: id, algorithmName: id })) } }),
    algorithmLayoutDetail: async ({ id }) => {
      const [, key, type] = cases.find(([code]) => code === id)
      return { resData: { algorithmMetadata: JSON.stringify({ params: [
        { key, type, name: type, defaultValue: '12' },
        { key: 'score', type: 'text', name: 'Score', defaultValue: 0 },
        { key: 'mode', type: 'select', name: 'Mode', defaultValue: 'yes', options: [{ value: 'yes', name: 'Yes' }] }
      ] }) } }
    },
    boxQueryFaceLibInfo: async () => ({ resData: { faceLibList: [{ id: 12, name: 'Face library' }, { id: '34', name: 'Second face library' }] } }),
    boxQueryPersonLibInfo: async () => ({ resData: { personLibList: [{ id: '12', name: 'Workwear library' }, { id: 34, name: 'Second workwear library' }] } }),
    pTaskCreate: async () => ({}), pTaskCancle: async () => ({}),
    imageAnalysis: async params => { requests.push(JSON.parse(JSON.stringify(params))); return { resData: {} } }
  }
})
try {
  page.all(n => n.type === 'el-upload')[0].props['on-change']({ name: 'test.png', raw: { type: 'image/png', size: 1 } })
  await page.settle()
  for (const [id, key, , label] of cases) {
    page.all(n => n.type === 'el-select' && n.props.class === 'algorithm-select')[0].props.activate(id)
    await page.settle()
    await page.settle()
    const selector = () => page.all(n => n.type === 'el-select' && n.props.multiple !== undefined)[0]
    assert.ok(selector(), `${id}: image analysis parameters must render a library selector`)
    assert.ok(page.all(n => n.type === 'el-option' && n.props.label === label).length, `${id}: library names must come from the appropriate API`)
    assert.deepEqual(Array.from(selector().props.modelValue), ['12'], 'scene bindings must be restored on selection')
    assert.equal(page.all(n => n.type === 'el-input')[0].props.modelValue, '0', 'numeric zero defaults must survive')
    selector().props.activate(['12', '34'])
    await page.settle()
    const analyze = () => page.all(n => n.type === 'el-button' && n.props.type === 'success')[0].props.onClick()
    await analyze()
    await page.settle()
    assert.equal(requests.at(-1).algorithmCode, id)
    assert.equal(requests.at(-1).taskConfig.params.find(p => p.key === key)?.value, '12,34', 'actual image API must receive selected IDs as CSV')
    assert.equal(requests.at(-1).taskConfig.params.find(p => p.key === 'score')?.value, '0')
    assert.equal(requests.at(-1).taskConfig.params.find(p => p.key === 'mode')?.value, 'yes')
    selector().props.activate([])
    await page.settle()
    await analyze()
    await page.settle()
    assert.equal(requests.at(-1).taskConfig.params.find(p => p.key === key)?.value, '', 'clearing must override the stored scene binding instead of omitting it')
  }
  assert.deepEqual(errors, [])
} finally { page.unmount() }
console.log('Image analysis page library checks passed')
