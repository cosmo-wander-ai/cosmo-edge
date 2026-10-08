import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const requests = []
const storage = new Map()
const page = await mountComponent('views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue', {
  mocks: { moment: { default: () => ({ format: () => '' }) } },
  globals: {
    localStorage: { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value), removeItem: key => storage.delete(key) },
    setTimeout: () => 0
  },
  api: {
    algorithmInquire: async params => { requests.push({ ...params }); return { resData: { rows: [], total: 0 } } },
    boxCameraPage: async () => ({ resData: { rows: [] } })
  }
})
try {
  const text = node => [node.text, ...node.children.map(text)].join(' ')
  const search = () => page.all(n => n.type === 'el-button' && text(n).includes('action.search'))[0].props.onClick()
  const sourceSelector = page.all(n => n.type === 'el-select' && n.props.class === 'el-form')
    .find(n => page.all(option => option.type === 'el-option' && option.props.label === 'glossary.imageAnalysis', n).length)
  assert.ok(sourceSelector, 'scene task search must offer a data source type selector')
  for (const value of ['2', '1', '']) {
    page.instance.pageData.pageNum = 3
    sourceSelector.props.activate(value)
    await page.settle()
    search()
    await page.settle()
    assert.equal(requests.at(-1).algorithmUsage, value)
    assert.equal(requests.at(-1).pageNum, 1, 'changing a search must reset pagination')
  }
  sourceSelector.props.activate('2')
  await page.settle()
  page.instance.formData.algorithmCategory = 'detect'
  search()
  await page.settle()
  assert.equal(requests.at(-1).algorithmUsage, '2', 'category filtering must preserve the source filter')
  page.all(n => n.type === 'el-button' && text(n).includes('action.reset'))[0].props.onClick()
  await page.settle()
  search()
  await page.settle()
  assert.equal(requests.at(-1).algorithmUsage, '', 'reset restores all data sources')
} finally { page.unmount() }
console.log('Scene source type filter checks passed')
