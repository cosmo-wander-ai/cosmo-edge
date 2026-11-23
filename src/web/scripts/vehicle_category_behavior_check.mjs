import assert from 'node:assert/strict'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const rows = [
  { algorithmId: 'v1', algorithmName: 'vehicle one', algorithmCategory: '10', algorithmUsage: '1' },
  { algorithmId: 'v2', algorithmName: 'vehicle two', algorithmCategory: '10', algorithmUsage: '1' },
  { algorithmId: 'p1', algorithmName: 'person', algorithmCategory: '2', algorithmUsage: '1' }
]
const requests = []
const exports = []
class FakeXHR {
  open() {}
  setRequestHeader() {}
  send(value) { exports.push(JSON.parse(value)) }
}
const storage = { getItem: () => null, removeItem() {} }
const component = await mountComponent('views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue', {
  mocks: {
    '@/components/TopBar.vue': { default: { render() { return null } } },
    '@/components/SceneTaskIcon.vue': { default: { render() { return null } } },
    '@/utils/i18nResource': { resolveResourceAlgorithmName: row => row.algorithmName, resolveResourceAlgorithmRemark: () => '' },
    '@/utils/chunkUpload': { uploadFileInChunks() {}, UploadPurpose: {} },
    moment: { default: () => ({ format: () => 'time' }) }
  },
  globals: { localStorage: storage, XMLHttpRequest: FakeXHR },
  api: {
    getChannelList: async () => ({ resData: { rows: [], total: 0 } }),
    algorithmInquire: async params => {
      requests.push({ ...params })
      const selected = rows.filter(row => (!params.algorithmCategory || row.algorithmCategory === params.algorithmCategory) && (!params.algorithmName || row.algorithmName.includes(params.algorithmName)))
      const start = (params.pageNum - 1) * params.pageSize
      return { resData: { rows: selected.slice(start, start + params.pageSize), total: selected.length } }
    },
    exportAlgorithmLayout: () => '/mock/export'
  }
})
try {
  const vm = component.instance
  const options = vm.topBarData.formList.find(item => item.model === 'algorithmCategory').dataList
  assert.deepEqual(Array.from(options, item => item.value), ['', 'face', 'detect', 'count', '10'])
  assert.equal(options.find(item => item.value === '10').label, 'glossary.vehicleAnalysis')
  vm.formData.algorithmCategory = '10'
  vm.formData.algorithmName = 'vehicle'
  vm.pageData.pageSize = 1
  vm.searchList()
  await component.settle()
  assert.equal(requests.at(-1).algorithmCategory, '10')
  assert.equal(requests.at(-1).algorithmName, 'vehicle')
  assert.equal(vm.pageData.total, 2)
  assert.equal(vm.tableData[0].algorithmId, 'v1')
  vm.handleCurrentChange(2)
  await component.settle()
  assert.equal(vm.tableData[0].algorithmId, 'v2')
  vm.exportAlgorithmic()
  assert.equal(exports.at(-1).algorithmCategory, '10')
  assert.equal(exports.at(-1).algorithmName, 'vehicle')
  vm.formData.algorithmCategory = ''
  vm.formData.algorithmName = ''
  vm.searchList()
  await component.settle()
  assert.equal(requests.at(-1).algorithmCategory, '')
  assert.equal(vm.pageData.total, 3)
  assert.equal(vm.pageData.pageNum, 1)
  console.log('vehicle category candidate behavior: PASS')
} finally {
  component.unmount()
}
