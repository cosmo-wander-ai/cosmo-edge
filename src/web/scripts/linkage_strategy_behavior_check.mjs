import assert from 'node:assert/strict'
import * as Vue from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'

let enabled = false
let rejectSwitch = false
const switches = []
const { default: api } = await loadBehaviorModule('api/index.js', {
  mocks: { '@/utils/request': { request: async ({ url, data }) => {
    if (url.endsWith('/Storages')) return { resData: { strages: [] } }
    if (url.endsWith('/Page')) return { resData: { tasks: [{ id: 'strategy-1', name: 'Test', status: enabled, workFlow: '[]' }] } }
    if (url.endsWith('/Switch')) {
      switches.push(JSON.parse(JSON.stringify(data)))
      if (rejectSwitch) throw new Error('Failed to persist strategy')
      // Match MsgSwitchRecv: the canonical switch field defaults to false.
      enabled = data.switch ?? false
      return { resCode: 1 }
    }
    if (url.endsWith('/Update')) return { resCode: 1 }
    throw new Error(`Unexpected request: ${url}`)
  } } }
})
const mount = () => mountComponent('views/box/strategyManagement/linkageStrategy/index.vue', {
  api,
  mocks: {
    // Canvas sizing is unrelated to persistence and has no layout in this renderer.
    vue: { ...Vue, nextTick: () => Promise.resolve() },
    '@element-plus/icons-vue': { Plus: 'Plus', EditPen: 'EditPen', Delete: 'Delete' },
    '../components/ArrangeFlow.vue': { default: {
      methods: { saveFlowData: () => ({ workFlow: '[]' }), clearFlow() {} },
      render: () => null
    } }
  },
  globals: { window: { innerHeight: 900, addEventListener() {}, removeEventListener() {} } }
})
const control = page => page.all(n => n.type === 'el-switch')[0]
const save = async page => {
  page.all(n => n.type === 'el-button' && n.props.onClick)[0].props.onClick()
  await page.settle()
  await page.settle()
}

let page = await mount()
try {
  assert.equal(control(page).props.modelValue, false)
  control(page).props.activate(true)
  await page.settle()
  assert.deepEqual(switches.at(-1), { id: 'strategy-1', switch: true })
  assert.equal(enabled, true)
  await save(page)
  assert.equal(control(page).props.modelValue, true, 'saving workflow must retain enabled state')
  page.unmount()
  page = await mount()
  assert.equal(control(page).props.modelValue, true, 'returning to the page must show persisted state')

  rejectSwitch = true
  control(page).props.activate(false)
  await page.settle()
  assert.equal(control(page).props.modelValue, true, 'failed switch must restore its previous state')
  assert.equal(enabled, true)
  rejectSwitch = false
  control(page).props.activate(false)
  await page.settle()
  assert.deepEqual(switches.at(-1), { id: 'strategy-1', switch: false })
  assert.equal(enabled, false)
  await save(page)
  assert.equal(control(page).props.modelValue, false)
  page.unmount()
  page = await mount()
  assert.equal(control(page).props.modelValue, false, 'disabled state must also survive navigation')
} finally { page.unmount() }
console.log('Linkage strategy switch/save/navigation checks passed')
