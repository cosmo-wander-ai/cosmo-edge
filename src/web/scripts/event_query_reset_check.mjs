import assert from 'node:assert/strict'
import { getCurrentInstance, h } from 'vue'
import moment from 'moment'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

// The lightweight renderer creates plain objects in place of DOM elements.
Object.defineProperty(Object.prototype, 'getBoundingClientRect', {
  configurable: true, value: () => ({ top: 0 })
})

// Exercise the real page handlers with a small TopBar fixture that sends the
// same reset event even when the generic form reset leaves some fields intact.
const topBar = {
  props: ['formData'], emits: ['search', 'reset'],
  setup(props, { emit }) {
    return () => h('fixture-topbar', {
      form: props.formData,
      search: () => emit('search'),
      reset: () => {
        props.formData.times = null
        props.formData.algorithmCode = ''
        props.formData.videoChannelName = ''
        emit('reset')
      }
    })
  }
}
const empty = { default: { render: () => null } }
const globals = {
  window: { innerHeight: 900, addEventListener() {}, removeEventListener() {} },
  localStorage: { getItem: () => null, setItem() {} },
  document: { documentElement: {}, addEventListener() {}, removeEventListener() {} }
}

for (const page of ['alarmRecord', 'faceBody', 'carStatistics']) {
  const requests = []
  const exports = []
  let finishOld
  const api = {
    algorithmInquire: async () => ({ resData: { rows: [] } }),
    getChannelList: async () => ({ resData: { rows: [] } }),
    queryFaceLibInfo: async () => ({ resData: { faceLibList: [] } }),
    getCarDict: async () => ({ resData: {} }),
    boxQueryEvent: (params) => {
      requests.push(JSON.parse(JSON.stringify(params)))
      if (params.algorithmCodes?.length) return new Promise(resolve => { finishOld = resolve })
      return Promise.resolve({ resData: { rows: [{ id: 'today' }], total: 432 } })
    }
  }
  const view = await mountComponent(`views/box/eventQuery/${page}/index.vue`, {
    globals, api,
    mocks: {
      moment: { default: moment },
      '@/components/TopBar.vue': { default: topBar },
      '@/utils/eventImage': { eventImageUrl: () => '', loadEventImageUrl: async () => '' },
      '@/utils/alarmExport': { downloadAlarmExport: async params => { exports.push(params) } },
      '@/utils/i18nResource': { resolveResourceAlgorithmName: x => x?.algorithmName || '' },
      '../components/LayaReviewDialog.vue': empty,
      '../components/detailDialog.vue': empty,
      '../components/rukuDialog.vue': empty,
      '../components/captureDialog.vue': empty,
      '../components/carDetailDialog.vue': empty,
      '@/components/videoPlaying265.vue': empty
    },
    wrap: Component => ({
      setup() {
        const app = getCurrentInstance().appContext.app
        app.component('el-table', { props: ['data'], setup(props, { expose }) {
          expose({ clearSelection() {}, toggleRowSelection() {} })
          return () => h('fixture-table', { rows: props.data })
        } })
        app.component('el-table-column', { render: () => null })
        return () => h(Component)
      }
    })
  })
  try {
    await view.settle()
    const bar = view.all(node => node.type === 'fixture-topbar')[0]
    assert.ok(bar, `${page}: top bar mounted`)
    assert.equal(requests.length, 1, `${page}: initial query`)
    if (page === 'faceBody') {
      bar.props.form.reportStatus = 0
      await view.settle()
      const exportButton = view.all(node => node.type === 'el-button' && node.children.some(child => child.text === 'event.dataExport'))[0]
      assert.ok(exportButton, 'faceBody: export button mounted')
      exportButton.props.onClick()
      await view.settle()
      assert.equal(exports.at(-1).reportStatus, 0, 'export preserves status zero')
    }
    const pagination = view.all(node => node.type === 'el-pagination')[0]
    assert.ok(pagination, `${page}: pagination mounted`)
    pagination.props.onCurrentChange(3)
    await view.settle()
    assert.equal(requests.at(-1).pageNum, 3)
    bar.props.form.algorithmCode = 'fire'
    bar.props.form.videoChannelName = 'channel'
    bar.props.form.reportStatus = 1
    if (page === 'carStatistics') bar.props.form.propType = 'sedan'
    if (page === 'faceBody') bar.props.form.personName = 'test person'
    bar.props.search()
    await view.settle()
    assert.deepEqual(requests.at(-1).algorithmCodes, ['fire'])
    assert.equal(requests.at(-1).pageNum, 1, `${page}: search returns to page one`)
    bar.props.reset()
    await view.settle()
    const reset = requests.at(-1)
    assert.deepEqual(reset.algorithmCodes, [], `${page}: reset removes scene`)
    assert.equal(reset.pageNum, 1, `${page}: reset returns to page one`)
    assert.equal(reset.timeBegin, moment().startOf('day').valueOf())
    assert.equal(reset.timeEnd, moment().endOf('day').startOf('second').add(1, 'second').valueOf())
    assert.equal(bar.props.form.times.length, 2, `${page}: today remains visible`)
    assert.equal(bar.props.form.algorithmCode, '')
    assert.equal(bar.props.form.reportStatus, page === 'faceBody' ? null : '')
    if (page === 'carStatistics') assert.equal(bar.props.form.propType, '')
    if (page === 'faceBody') assert.equal(bar.props.form.personName, '')
    const table = () => view.all(node => node.type === 'fixture-table')[0]
    assert.ok(table(), `${page}: event table mounted`)
    assert.equal(table().props.rows[0].id, 'today')
    finishOld({ resData: { rows: [{ id: 'stale' }], total: 10 } })
    await view.settle()
    assert.equal(table().props.rows[0].id, 'today', `${page}: late filtered result is ignored`)
  } finally { view.unmount() }
}
console.log('Event query reset restores today, reloads first page and ignores stale results')
