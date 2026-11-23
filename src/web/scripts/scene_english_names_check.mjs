import assert from 'node:assert/strict'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const locale = { value: 'en-US' }
const i18n = { global: { locale, te: key => key === 'resource.algorithm.preferred.algorithmname',
  t: () => 'Explicit translated name' } }
const { resolveResourceAlgorithmName } = await loadBehaviorModule('utils/i18nResource.js', {
  mocks: { '@/i18n': { i18n } }
})
const names = [
  ['鼠患检测', 'Rodent Detection'],
  ['头肩检测', 'Head and Shoulder Detection'],
  ['人员聚集', 'People Gathering'],
  ['烟雾检测', 'Smoke Detection'],
  ['火焰检测', 'Flame Detection'],
  ['垃圾堆检测', 'Trash Pile Detection']
]
for (const [raw, translated] of names) {
  assert.equal(resolveResourceAlgorithmName({ algorithmUsage: '1', algorithmName: raw }), translated)
  locale.value = 'zh-CN'
  assert.equal(resolveResourceAlgorithmName({ algorithmUsage: '1', algorithmName: raw }), raw)
  locale.value = 'en-US'
}
// A real resource translation remains authoritative; custom and non-video
// names do not acquire a translation based on an unrelated algorithm ID.
assert.equal(resolveResourceAlgorithmName({ algorithmUsage: '1', algorithmName: '鼠患检测',
  algorithmId: 'preferred' }), 'Explicit translated name')
assert.equal(resolveResourceAlgorithmName({ algorithmUsage: '1', algorithmName: 'QA_custom' }), 'QA_custom')
assert.equal(resolveResourceAlgorithmName({ algorithmUsage: '2', algorithmName: '人员聚集' }), '人员聚集')

const rows = names.map(([name], index) => ({ algorithmId: `qa${index}`,
  algorithmName: name, algorithmCategory: '2', algorithmUsage: '1' }))
const page = await mountComponent('views/gam/countManagement/algorithmicManagement/algorithmicIndex.vue', {
  mocks: {
    '@/components/TopBar.vue': { default: { render() { return null } } },
    '@/components/SceneTaskIcon.vue': { default: { render() { return null } } },
    '@/utils/chunkUpload': { uploadFileInChunks() {}, UploadPurpose: {} },
    moment: { default: () => ({ format: () => 'time' }) }
  },
  globals: { localStorage: { getItem: () => null, removeItem() {} } },
  api: {
    getChannelList: async () => ({ resData: { rows: [], total: 0 } }),
    algorithmInquire: async () => ({ resData: { rows, total: rows.length } })
  }
})
try {
  await page.settle()
  const text = page.all(node => node.text).map(node => node.text).join(' ')
  for (const [, translated] of names) assert.ok(text.includes(translated), `${translated} missing from list`)
} finally {
  page.unmount()
}
console.log('English video scene names and fallback boundaries passed')
