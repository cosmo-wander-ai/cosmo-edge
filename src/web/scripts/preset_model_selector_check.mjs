import assert from 'node:assert/strict'
import { getCurrentInstance, h, onErrorCaptured, provide, ref } from 'vue'
import lodash from 'lodash'
import { mountComponent, i18n } from './helpers/mount_behavior_component.mjs'
import { loadModelDisplaySources, withModelDisplaySource } from '../src/utils/modelDisplaySources.js'
import { flowEditorKey } from '../src/views/gam/countManagement/arrangeDetail/flow/flowEditorContext.js'

const plain = value => JSON.parse(JSON.stringify(value))
const preset = Object.freeze({ modelCode: '3000003', modelName: 'MouseDetection', isExportable: false })
const filler = Array.from({ length: 100 }, (_, index) => Object.freeze({
  modelCode: `user-${index}`, modelName: `Custom ${index}`, isExportable: true
}))
const snapshot = [...filler, preset]
const pagedSource = (rows, requests = []) => async params => {
  requests.push(plain(params))
  const start = (params.pageNum - 1) * params.pageSize
  return { resData: { rows: rows.slice(start, start + params.pageSize), total: rows.length } }
}
const label = { label: '0', class_name: 'mouse', nameCN: 'mouse', threshold: [0.25] }
const atomic = Object.freeze({
  atomicCode: preset.modelCode, atomicName: preset.modelName,
  label: JSON.stringify([label]), labelList: Object.freeze([Object.freeze({ ...label })])
})

// A complete source snapshot is required, and no successful snapshot is cached.
const sourceRequests = []
const sourceApi = { getAtomicModelPage: pagedSource(snapshot, sourceRequests) }
const sources = await loadModelDisplaySources(sourceApi)
assert.equal(sources.size, 101)
assert.deepEqual(sourceRequests.map(request => request.pageNum), [1, 2])
assert.equal(withModelDisplaySource(atomic, sources).isExportable, false)
sourceApi.getAtomicModelPage = pagedSource([{ ...preset, isExportable: true }], sourceRequests)
assert.equal((await loadModelDisplaySources(sourceApi)).get(preset.modelCode).isExportable, true,
  'a later import must replace an earlier preset provenance snapshot')
assert.equal(withModelDisplaySource({ ...atomic, isExportable: true }, sources).isExportable, true)
assert.equal(withModelDisplaySource({ ...atomic, atomicName: 'My mouse model' }, sources).isExportable, true)
assert.equal((await loadModelDisplaySources({})).size, 0)

for (const getAtomicModelPage of [
  async () => { throw new Error('source unavailable') },
  async ({ pageNum }) => pageNum === 1
    ? { resData: { rows: [preset, ...filler.slice(0, 99)], total: 101 } }
    : Promise.reject(new Error('second page unavailable')),
  async ({ pageNum }) => ({ resData: { rows: [preset], total: pageNum === 1 ? 2 : 1 } }),
  async () => ({ resData: { rows: [preset], total: 2 } }),
  async () => ({ resData: { rows: [], total: 1 } })
]) {
  assert.equal((await loadModelDisplaySources({ getAtomicModelPage })).size, 0,
    'partial, changing, repeated or failed pages cannot authorize display aliases')
}
let current = true
let finishRead
let cancelledReads = 0
const cancelled = loadModelDisplaySources({
  getAtomicModelPage: () => {
    cancelledReads++
    return new Promise(resolve => { finishRead = resolve })
  }
}, () => current)
current = false
finishRead({ resData: { rows: [preset], total: 2 } })
assert.equal((await cancelled).size, 0)
assert.equal(cancelledReads, 1, 'stale reads cannot continue paginating')

const empty = { default: { render: () => null } }
const drain = async view => {
  for (let turn = 0; turn < 5; turn++) await view.settle()
  if (view.errors?.length) throw view.errors[0]
}
const mountSelector = async ({ source, models = [atomic], type = 'modelSelect_detector' } = {}) => {
  const locale = ref('zh-CN')
  const updates = []
  const typedRequests = []
  const errors = []
  let form
  const track = type === 'trackSelect'
  const params = [{ key: 'model', type, name: 'Model', level: '1', defaultValue: atomic.atomicCode }]
  if (track) params.push({ key: 'motionStatus', type: 'text', name: 'Motion', level: '1', defaultValue: '1' })
  const view = await mountComponent('views/gam/countManagement/arrangeDetail/flow/DynamicForm.vue', {
    props: {
      actionDetail: { actionId: 'AA_00001', flowActionId: 'model-node', inputParamConfig: JSON.stringify(params) },
      configObject: { params: [{ key: 'model', value: atomic.atomicCode }], webConfig: { labelList: [label] } },
      atomicList: models
    },
    mocks: {
      '@/i18n': { ...i18n, currentLocale: locale, t: (key, values) => values?.name ? `${key}: ${values.name}` : key },
      './flowEditorContext.js': { flowEditorKey },
      './ConditionView.vue': empty, './TreeSelectMultiple.vue': empty,
      './LibrarySelect.vue': empty, '@/components/VisualQuestionEditor.vue': empty,
      'tree-transfer-vue3': empty, lodash: { default: lodash }, uuid: { v4: () => 'fixture-id' },
      '@element-plus/icons-vue': Object.fromEntries(['QuestionFilled', 'ArrowDown', 'ArrowRight', 'CirclePlus', 'CircleClose'].map(name => [name, empty.default]))
    },
    api: {
      ...(source ? { getAtomicModelPage: source } : {}),
      atomicModelList: async params => {
        typedRequests.push(plain(params))
        return { resData: { list: models } }
      }
    },
    wrap: Component => ({
      setup(_, { attrs }) {
        onErrorCaptured(error => { errors.push(error); return false })
        // Observe label data without invoking Element Plus's scoped table slots.
        getCurrentInstance().appContext.app.component('el-table', {
          props: ['data'], setup: props => () => h('table-data-fixture', { rows: props.data })
        })
        provide(flowEditorKey, { updateAtomic: value => updates.push(plain(value)) })
        return () => h(Component, { ...attrs, ref: value => { form = value } })
      }
    })
  })
  await drain(view)
  if (errors.length) { view.unmount(); throw errors[0] }
  return {
    ...view, locale, updates, typedRequests, errors,
    collect: () => plain(form.submitForm()),
    selector: () => view.all(node => node.type === 'el-select' && typeof (node.props.filterMethod || node.props['filter-method']) === 'function')[0],
    modelOptions: () => view.all(node => node.type === 'el-option' && node.props.class === 'model-select-option')
  }
}
const assertRawSelection = async view => {
  view.selector().props.activate(atomic.atomicCode)
  await drain(view)
  assert.ok(view.updates.length > 0)
  assert.ok(view.updates.every(update => update.atomicName === atomic.atomicName),
    'editor updates must carry the original atomic name')
  const saved = view.collect()
  assert.equal(saved.webConfig.atomic.atomicName, atomic.atomicName)
  assert.equal(saved.webConfig.atomic.atomicCode, atomic.atomicCode)
  assert.equal(saved.params.find(item => item.key === 'model').value, atomic.atomicCode)
  assert.equal(JSON.stringify(saved).includes('鼠类检测'), false, 'display text must not leak into stored configuration')
}
const filter = async (view, value) => {
  const props = view.selector().props
  ;(props.filterMethod || props['filter-method'])(value)
  await drain(view)
}
const visible = async (view, open) => {
  view.selector().props.onVisibleChange(open)
  await drain(view)
}

const originalAtomic = plain(atomic)
const pagedRequests = []
const localized = await mountSelector({ source: pagedSource(snapshot, pagedRequests) })
try {
  assert.deepEqual(pagedRequests.map(request => request.pageNum), [1, 2])
  assert.deepEqual(localized.typedRequests, [{ modelName: '', modelType: 'detector', filePath: '' }],
    'provenance does not change the existing typed model query')
  assert.equal(localized.modelOptions()[0].props.label, '鼠类检测（3000003）')
  assert.equal(localized.modelOptions()[0].props.value, atomic.atomicCode)
  assert.ok(localized.all(node => node.text === atomic.atomicName).length, 'English subtitle remains visible')
  assert.equal(localized.collect().webConfig.labelList[0].class_name, label.class_name)
  const updateCount = localized.updates.length
  for (const query of ['鼠类', '  mOuSeDeTeCtIoN  ', '3000003']) {
    await filter(localized, query)
    assert.equal(localized.modelOptions().length, 1, `search matches ${query}`)
  }
  await filter(localized, 'not-a-model')
  assert.equal(localized.modelOptions().length, 0)
  await filter(localized, '')
  localized.locale.value = 'en-US'
  await drain(localized)
  assert.equal(localized.modelOptions()[0].props.label, 'MouseDetection（3000003）')
  await filter(localized, '鼠类')
  assert.equal(localized.modelOptions().length, 1, 'Chinese search still works in English locale')
  localized.locale.value = 'zh-CN'
  await drain(localized)
  assert.equal(localized.modelOptions()[0].props.label, '鼠类检测（3000003）')
  assert.equal(localized.updates.length, updateCount, 'search and locale changes do not emit configuration edits')
  await assertRawSelection(localized)
} finally { localized.unmount() }

for (const [reason, source] of [
  ['user override with the same ID and original name', pagedSource([{ ...preset, isExportable: true }])],
  ['missing provenance flag', pagedSource([{ modelCode: preset.modelCode, modelName: preset.modelName }])],
  ['different repository name', pagedSource([{ ...preset, modelName: 'Other model' }])],
  ['missing source API', undefined],
  ['failed source API', async () => { throw new Error('offline') }],
  ['failed later source page', async ({ pageNum }) => pageNum === 1
    ? { resData: { rows: [preset, ...filler.slice(0, 99)], total: 101 } }
    : Promise.reject(new Error('offline'))]
]) {
  const view = await mountSelector({ source })
  try {
    assert.equal(view.modelOptions()[0].props.label, 'MouseDetection（3000003）', reason)
    await filter(view, '鼠类')
    assert.equal(view.modelOptions().length, 0, `${reason}: preset search aliases must also be disabled`)
    await filter(view, 'MouseDetection')
    await assertRawSelection(view)
  } finally { view.unmount() }
}

// A late preset response must not overwrite newer user-model provenance.
let resolveOldSource
let sourceRead = 0
const racing = await mountSelector({ source: () => {
  sourceRead++
  if (sourceRead === 1) return new Promise(resolve => { resolveOldSource = resolve })
  return Promise.resolve({ resData: { rows: [{ ...preset, isExportable: sourceRead === 2 }], total: 1 } })
} })
try {
  await visible(racing, true)
  resolveOldSource({ resData: { rows: [preset], total: 1 } })
  await drain(racing)
  assert.equal(racing.modelOptions()[0].props.label, 'MouseDetection（3000003）')
  await visible(racing, false)
  await visible(racing, true)
  assert.equal(racing.modelOptions()[0].props.label, '鼠类检测（3000003）', 'reopening fetches fresh provenance')
  assert.equal(sourceRead, 3)
  await assertRawSelection(racing)
} finally { racing.unmount() }

const tracker = await mountSelector({ source: pagedSource([preset]), type: 'trackSelect' })
try {
  assert.equal(tracker.modelOptions()[0].props.label, '鼠类检测（3000003）')
  await filter(tracker, 'mousedetection')
  assert.equal(tracker.modelOptions().length, 1)
  const names = tracker.collect().webConfig.metaDataParams.map(item => item.name)
  assert.equal(names.length, 3)
  assert.ok(names.every(name => name.endsWith(': MouseDetection')),
    'generated tracking parameter names retain the original model name')
} finally { tracker.unmount() }

assert.deepEqual(plain(atomic), originalAtomic, 'source and display operations never mutate the model DTO or labels')
console.log('Preset model selector checks passed: provenance pagination/races, bilingual display/search, user overrides and raw-name preservation')
