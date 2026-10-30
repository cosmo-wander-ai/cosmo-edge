import assert from 'node:assert/strict'
import { i18n, mountComponent } from './helpers/mount_behavior_component.mjs'

// The matching model is beyond both the display page and the API batch size.
const models = Array.from({ length: 100 }, (_, index) => ({
  modelCode: `other-${index}`, modelName: `Classifier ${index}`, version: 'V1.0.0', isExportable: true
}))
models.push({ modelCode: '3000003', modelName: 'MouseDetection', version: 'V6.0.1',
  updateTime: '2026-10-15 12:00:00', inputDim: '1,3,640,640', isExportable: false })
models.push({ modelCode: 'custom-abc', modelName: 'Detector 3000003 copy', description: 'Customer supplied description',
  version: 'V2.0.0', isExportable: true })
const originalModels = JSON.parse(JSON.stringify(models))
const previousLocale = i18n.currentLocale.value
i18n.currentLocale.value = 'zh-CN'
const requests = []
const page = await mountComponent('views/gam/countManagement/atomicModel/index.vue', {
  mocks: {
    'vue-router': { useRouter: () => ({ push() {} }) },
    '@/utils/chunkUpload': { UploadPurpose: {}, uploadFileInChunks: async () => ({}) }
  },
  api: {
    getAtomicModelPage: async params => {
      requests.push(params)
      const start = (params.pageNum - 1) * params.pageSize
      return { resData: { total: models.length, rows: models.slice(start, start + params.pageSize) } }
    },
    atomicModelList: async ({ modelType }) => ({ resData: { list: models.filter(model =>
      modelType === (model.modelCode.startsWith('other-') ? 'classify' : 'yolov5_det'))
      .map(model => ({ atomicCode: model.modelCode, atomicName: model.modelName })) } }),
    algorithmInquire: async () => ({ resData: { rows: [] } })
  }
})
try {
  await page.settle()
  const cards = () => page.all(n => n.props.class === 'model-card')
  const texts = (root = page.root) => page.all(n => n.text, root).map(n => n.text).join(' ')
  const search = async value => {
    const input = page.all(n => n.type === 'el-input' && n.props.placeholder === 'glossary.searchModelPlaceholder')[0]
    input.props.activate(value)
    input.props.onInput(value)
    await page.settle()
  }
  const tab = async label => {
    page.all(n => n.type === 'button' && texts(n).includes(label))[0].props.onClick()
    await page.settle()
  }
  assert.equal(requests.length, 2, 'load all API pages, not only the first display page')
  assert.equal(cards().length, 12)
  await search('3000003')
  assert.equal(cards().length, 2, 'search matches model ID or model name, including numeric names')
  await tab('glossary.modelTypeDetect')
  assert.equal(cards().length, 2)
  assert.ok(texts().includes('V6.0.1'), 'category cards must retain version metadata')
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
  page.all(n => n.type === 'el-button' && texts(n) === 'action.details')[0].props.onClick()
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
  page.all(n => n.type === 'el-button' && texts(n) === 'action.details')[0].props.onClick()
  await page.settle()
  assert.ok(texts().includes('Customer supplied description'), 'imported descriptions remain visible')
  assert.equal(requests.length, 2, 'search and category changes use the same complete records')
  assert.deepEqual(models, originalModels, 'localized presentation never changes the source model records')
} finally {
  page.unmount()
  i18n.currentLocale.value = previousLocale
}
console.log('Model repository localized display, bilingual search, category and raw metadata checks passed')
