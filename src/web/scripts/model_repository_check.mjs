import assert from 'node:assert/strict'
import { h } from 'vue'
import { i18n, mountComponent } from './helpers/mount_behavior_component.mjs'

const typeCases = [
  { modelCode: 'custom-joint', modelName: 'Safety Review', modelType: 'YOLO26_PERSON_ATTRIBUTES' },
  { modelCode: 'classifier', modelName: 'Detector Ranking', modelType: 'YOLOV8_Classify' },
  { modelCode: 'obb', modelName: 'Rotated Objects', modelType: 'YOLO26_OBB_DET' },
  { modelCode: 'qwen', modelName: 'Image Questions', modelType: 'QWEN3VL' },
  { modelCode: 'qwen35', modelName: 'Image Summary', modelType: 'qwen3_5' },
  { modelCode: 'laya', modelName: 'Visual Review', modelType: 'laya_v' },
  { modelCode: 'sam', modelName: 'Segmentation', modelType: 'SAM2' },
  { modelCode: 'legacy-detect', modelName: 'Legacy YOLO' },
  { modelCode: 'legacy-other', modelName: 'Legacy Package', modelType: '' },
  { modelCode: 'dino', modelName: 'Prompt Localization', modelType: 'dino' }
]
// The matching model is beyond both the display page and the API batch size.
const models = [
  ...typeCases.map(model => ({ version: 'V1.0.0', isExportable: true, ...model })),
  ...Array.from({ length: 100 - typeCases.length }, (_, index) => ({
    modelCode: `other-${index}`, modelName: `Classifier ${index}`, modelType: 'classify', version: 'V1.0.0', isExportable: true
  })),
  { modelCode: '3000003', modelName: 'MouseDetection', modelType: 'yolov5_det', version: 'V6.0.1',
    updateTime: '2026-10-15 12:00:00', inputDim: '1,3,640,640', isExportable: false },
  { modelCode: 'custom-abc', modelName: 'Detector 3000003 copy', modelType: 'yolov5_det',
    description: 'Customer supplied description', version: 'V2.0.0', isExportable: true }
]
const originalModels = JSON.parse(JSON.stringify(models))
let repositoryModels = models
const previousLocale = i18n.currentLocale.value
i18n.currentLocale.value = 'zh-CN'
const requests = []
let categoryRequests = 0
let delayedRefresh
const importedModel = { modelCode: 'new-detector', modelName: 'Imported Package', modelType: 'yolov8_det', isExportable: true }
const page = await mountComponent('views/gam/countManagement/atomicModel/index.vue', {
  mocks: {
    'vue-router': { useRouter: () => ({ push() {} }) },
    './AlgorithmTypeIcon.vue': { default: { render: () => h('model-icon') } },
    '@/utils/chunkUpload': { UploadPurpose: {}, uploadFileInChunks: async () => ({ uploadId: 'staged-model' }) }
  },
  globals: { setTimeout: callback => { delayedRefresh = callback } },
  api: {
    getAtomicModelPage: async params => {
      requests.push(params)
      const start = (params.pageNum - 1) * params.pageSize
      return { resData: { total: repositoryModels.length, rows: repositoryModels.slice(start, start + params.pageSize) } }
    },
    atomicModelList: async () => {
      categoryRequests += 1
      return { resData: { list: [] } }
    },
    importModel: async ({ uploadId }) => {
      assert.equal(uploadId, 'staged-model')
      repositoryModels = [...repositoryModels, importedModel]
      return { resCode: 1 }
    },
    deleteAtomicModel: async ({ modelCode }) => {
      assert.equal(modelCode, importedModel.modelCode)
      repositoryModels = repositoryModels.filter(model => model.modelCode !== modelCode)
      return { resCode: 1 }
    },
    algorithmInquire: async () => ({ resData: { rows: [] } })
  }
})
try {
  await page.settle()
  const cards = () => page.all(n => n.props.class === 'model-card')
  const texts = (root = page.root) => page.all(n => n.text, root).map(n => n.text).join(' ')
  const button = (label, root) => page.all(n => n.type === 'el-button' && texts(n).trim() === label, root)[0]
  const tabButton = label => page.all(n => n.type === 'button' && texts(n).includes(label))[0]
  const tabCount = label => Number(texts(page.all(n => n.props.class === 'tab-count', tabButton(label))[0]))
  const assertRefresh = (from, action) => {
    assert.deepEqual(requests.slice(from).map(({ pageNum, pageSize }) => [pageNum, pageSize]), [[1, 100], [2, 100]], action)
    assert.equal(categoryRequests, 0, 'the model repository never sends a category request')
  }
  const assertType = (code, label, icon) => {
    const card = cards().find(card => page.all(n => n.type === 'dd' && n.props.title === code, card).length)
    assert.ok(card, `${code} is in the selected category`)
    assert.equal(texts(page.all(n => n.props.class === 'model-type-tag', card)[0]), label)
    assert.equal(page.all(n => n.type === 'model-icon', card)[0].props.type, icon)
  }
  const search = async value => {
    const input = page.all(n => n.type === 'el-input' && n.props.placeholder === 'glossary.searchModelPlaceholder')[0]
    input.props.activate(value)
    input.props.onInput(value)
    await page.settle()
  }
  const tab = async label => {
    tabButton(label).props.onClick()
    await page.settle()
  }
  assertRefresh(0, 'load both API pages on first open')
  assert.equal(cards().length, 12)
  assert.equal(tabCount('common.all'), 102, 'include records without a recognized type')
  assertType('legacy-other', 'other', 'other')
  await tab('glossary.modelTypeDetect')
  assert.equal(cards().length, tabCount('glossary.modelTypeDetect'))
  assertType('custom-joint', 'yolo26_person_attributes', 'detect')
  assertType('obb', 'yolo26_obb_det', 'detect')
  assertType('legacy-detect', 'detect', 'other')
  await tab('glossary.modelTypeClassify')
  assert.equal(cards().length, tabCount('glossary.modelTypeClassify'))
  assertType('classifier', 'yolov8_classify', 'classify')
  await tab('glossary.modelTypeFoundation')
  assert.equal(cards().length, tabCount('glossary.modelTypeFoundation'))
  assertType('qwen', 'qwen3vl', 'multimodal')
  assertType('qwen35', 'qwen3_5', 'multimodal')
  assertType('laya', 'laya_v', 'multimodal')
  assertType('sam', 'sam2', 'segmentation')
  assertType('dino', 'dino', 'text-detection')
  await tab('common.all')
  await search('3000003')
  assert.equal(cards().length, 2, 'search matches model ID or model name, including numeric names')
  await tab('glossary.modelTypeDetect')
  assert.equal(cards().length, 2)
  assert.ok(texts().includes('V6.0.1'), 'category cards must retain version metadata')
  await search('Safety Review')
  assert.equal(cards().length, 1, 'person-attribute models are categorized as detectors regardless of model name')
  assert.ok(texts(cards()[0]).includes('yolo26_person_attributes'))
  await search('鼠类')
  assert.equal(cards().length, 1, 'Chinese preset names are searchable beyond the first API batch')
  const cardTitle = () => texts(page.all(n => n.props.class === 'card-title', cards()[0])[0])
  assert.equal(cardTitle(), '鼠类检测')
  assert.equal(texts(page.all(n => n.props.class === 'card-subtitle', cards()[0])[0]), 'MouseDetection')
  i18n.currentLocale.value = 'en-US'
  await page.settle()
  assert.equal(cards().length, 1, 'Chinese search remains available after switching the display locale')
  assert.equal(cardTitle(), 'MouseDetection')
  assert.equal(page.all(n => n.props.class === 'card-subtitle', cards()[0]).length, 0,
    'English display does not duplicate the original name')
  i18n.currentLocale.value = 'zh-CN'
  await page.settle()
  assert.equal(cardTitle(), '鼠类检测', 'locale changes update existing cards')
  await search('  mousedetection  ')
  assert.equal(cards().length, 1)
  const metadata = page.all(n => n.type === 'dd', cards()[0]).map(n => texts(n))
  assert.deepEqual(metadata, ['3000003', 'V6.0.1'], 'matching card retains its model ID and version')
  await tab('glossary.modelTypeClassify')
  assert.equal(cards().length, 0, 'category and keyword are combined')
  await tab('common.all')
  assert.equal(cards().length, 1, 'switching tabs retains the keyword')
  button('action.details').props.onClick()
  await page.settle()
  assert.equal(texts(page.all(n => n.type === 'h3')[0]), '鼠类检测')
  assert.equal(texts(page.all(n => n.props.class === 'detail-subtitle')[0]), 'MouseDetection')
  assert.ok(texts().includes('定位图像中的鼠类区域。'))
  assert.ok(texts().includes('2026-10-15 12:00:00'))
  assert.ok(texts().includes('1,3,640,640'))
  assert.equal(page.all(n => n.type === 'el-button' && texts(n) === 'action.delete').length, 0,
    'preset protection survives the category view')
  await search('')
  const pagination = page.all(n => n.type === 'el-pagination')[0]
  const changePage = pagination.props.onCurrentChange || pagination.props['onCurrent-change']
  assert.equal(typeof changePage, 'function')
  changePage(9)
  await page.settle()
  assert.equal(cards().length, 6)
  await search('custom-abc')
  assert.equal(cards().length, 1, 'search from a later page resets to page one')
  assert.equal(cardTitle(), 'Detector 3000003 copy', 'imported names are displayed verbatim')
  assert.equal(page.all(n => n.props.class === 'card-subtitle', cards()[0]).length, 0)
  button('action.details').props.onClick()
  await page.settle()
  assert.ok(texts().includes('Customer supplied description'), 'imported descriptions remain visible')
  assert.equal(requests.length, 2, 'search and category changes use the same complete records')

  const detectorCount = tabCount('glossary.modelTypeDetect')
  await search('')
  await tab('glossary.modelTypeDetect')
  button('action.importModel').props.onClick()
  await page.settle()
  const importDialog = page.all(n => n.type === 'el-dialog' && n.props.title === 'action.importModel')[0]
  page.all(n => n.type === 'el-upload', importDialog)[0].props['on-change']({ raw: { name: 'model.zip', size: 100 } })
  const beforeImport = requests.length
  await button('action.confirm', importDialog).props.onClick()
  await page.settle()
  assertRefresh(beforeImport, 'refresh reads both API pages after an import')
  assert.equal(tabCount('common.all'), 103)
  assert.equal(tabCount('glossary.modelTypeDetect'), detectorCount + 1)
  assertType(importedModel.modelCode, 'yolov8_det', 'detect')

  const beforeDelayedRefresh = requests.length
  delayedRefresh()
  await page.settle()
  assertRefresh(beforeDelayedRefresh, 'the delayed import refresh reads the same two API pages')
  assert.equal(tabCount('common.all'), 103)

  await search(importedModel.modelCode)
  button('action.details').props.onClick()
  await page.settle()
  button('action.delete').props.onClick()
  await page.settle()
  const beforeDelete = requests.length
  button('action.confirmDelete').props.onClick()
  await new Promise(setImmediate)
  await page.settle()
  assertRefresh(beforeDelete, 'refresh reads both API pages after a deletion')
  assert.equal(tabCount('common.all'), 102)
  assert.equal(tabCount('glossary.modelTypeDetect'), detectorCount)
  assert.equal(cards().length, 0)
  assert.deepEqual(models, originalModels, 'localized presentation never changes the source model records')
} finally {
  page.unmount()
  i18n.currentLocale.value = previousLocale
}
console.log('Model repository pagination, model types, bilingual search, and refresh counts passed')
