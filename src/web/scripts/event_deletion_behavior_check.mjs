import assert from 'node:assert/strict'
import * as Vue from 'vue'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'

const { useEventRecordPage } = await loadBehaviorModule('views/box/eventQuery/useEventRecordPage.js', {
  mocks: { vue: Vue, '@/i18n': { t: (key, values) => `${key}:${JSON.stringify(values || {})}` } }
})
const plain = value => JSON.parse(JSON.stringify(value))

const makePage = ({ rows = [], grid = true, total = rows.length, pageNum = 1, pageSize = 10 } = {}) => {
  const pageData = Vue.reactive({ pageNum, pageSize, total: 0 })
  const tableData = Vue.ref([])
  const calls = { delete: [], query: [], messages: [], removed: [], confirm: [] }
  let serverRows = rows
  const proxy = {
    $confirm: async message => { calls.confirm.push(message); return true },
    $message: Object.fromEntries(['success', 'warning', 'error'].map(kind => [kind, message => calls.messages.push({ kind, message })])),
    $API: {
      boxQueryEvent: async query => {
        calls.query.push(plain(query))
        const offset = (query.pageNum - 1) * query.pageSize
        return { resData: { rows: serverRows.slice(offset, offset + query.pageSize), total: serverRows.length } }
      },
      boxDeleteEvent: async request => {
        calls.delete.push(plain(request))
        serverRows = serverRows.filter(row => !request.ids.includes(row.id))
        return { resData: { deletedIds: request.ids, missingIds: [], failedItems: [] } }
      }
    }
  }
  const page = useEventRecordPage({
    proxy, pageData, tableData, isGrid: () => grid,
    buildQuery: () => ({ pageNum: pageData.pageNum, pageSize: pageData.pageSize, categorys: ['1'], filter: 'kept' }),
    onRemoved: ids => calls.removed.push(plain(ids))
  })
  return { page, pageData, tableData, proxy, calls, setRows: value => { serverRows = value } }
}

{
  const fixture = makePage({ rows: [{ id: 'a' }, { id: 'b' }] })
  const { page, proxy, calls, tableData } = fixture
  await page.init()
  page.handleGridSelectAll(true)
  assert.deepEqual(plain(page.selectedIds.value), ['a', 'b'])
  assert.equal(page.allGridSelected.value, true)
  page.handleGridSelect(false, tableData.value[1])
  assert.equal(page.someGridSelected.value, true)
  proxy.$confirm = async () => { throw 'cancel' }
  await page.handleBatchDelete()
  assert.equal(calls.delete.length, 0, 'cancelling must not issue a delete')
  assert.deepEqual(plain(page.selectedIds.value), ['a'])
  assert.equal(page.deleting.value, false)
}

{
  const fixture = makePage({ rows: [{ id: 'a' }, { id: 'b' }] })
  const { page, proxy, calls, tableData, setRows } = fixture
  await page.init()
  page.handleGridSelectAll(true)
  proxy.$API.boxDeleteEvent = async request => {
    calls.delete.push(plain(request))
    setRows([{ id: 'b' }])
    return { resData: { deletedIds: ['a'], missingIds: [], failedItems: [{ id: 'b', reason: 'RECORDING' }] } }
  }
  await page.handleBatchDelete()
  assert.deepEqual(calls.delete, [{ ids: ['a', 'b'] }], 'batch payload uses event IDs')
  assert.match(calls.confirm[0], /^event.deleteConfirm:/)
  assert.deepEqual(plain(tableData.value), [{ id: 'b' }])
  assert.deepEqual(plain(page.selectedIds.value), ['b'], 'the remaining failed item is selected for retry')
  assert.deepEqual(calls.removed, [['a']])
  assert.equal(calls.messages[0].kind, 'warning')
  assert.match(calls.messages[0].message, /event.deleteRecording/)
}

{
  const fixture = makePage({ rows: [{ id: 'a' }, { id: 'b' }], pageNum: 2, pageSize: 1 })
  const { page, pageData, calls, tableData } = fixture
  await page.init()
  await page.handleDelete(tableData.value[0])
  assert.match(calls.confirm[0], /^event.deleteConfirmSingle:/)
  assert.equal(pageData.pageNum, 1, 'the last empty page moves to the last existing page')
  assert.deepEqual(plain(tableData.value), [{ id: 'a' }])
  assert.deepEqual(calls.delete, [{ ids: ['b'] }])
  assert.ok(calls.query.every(query => query.filter === 'kept'))
}

{
  const fixture = makePage({ rows: [{ id: 'a' }, { id: 'b' }], grid: false })
  const { page, tableData, calls } = fixture
  const selection = []
  page.eventTableRef.value = {
    clearSelection() { selection.length = 0 },
    toggleRowSelection(row) { selection.push(row.id) }
  }
  await page.init()
  page.handleSelectionChange([tableData.value[1]])
  assert.deepEqual(plain(page.selectedIds.value), ['b'], 'table row selection stores IDs')
  await page.handleBatchDelete()
  assert.deepEqual(calls.delete, [{ ids: ['b'] }])
}

{
  const fixture = makePage({ rows: [{ id: 'a' }] })
  const { page, proxy, calls } = fixture
  await page.init()
  page.handleGridSelectAll(true)
  proxy.$API.boxDeleteEvent = async () => ({ resData: { deletedIds: [], missingIds: [], failedItems: [] } })
  await page.handleBatchDelete()
  assert.equal(calls.messages[0].kind, 'error', 'an incomplete response cannot masquerade as success')
  assert.deepEqual(plain(page.selectedIds.value), ['a'])
}

{
  const fixture = makePage({ rows: [{ id: 'a' }] })
  const { page, proxy, calls, tableData, setRows } = fixture
  await page.init()
  page.handleGridSelectAll(true)
  proxy.$API.boxDeleteEvent = async () => {
    setRows([])
    throw new Error('connection lost after submission')
  }
  await page.handleBatchDelete()
  assert.equal(calls.messages[0].kind, 'error')
  assert.equal(calls.messages.some(message => message.kind === 'success'), false)
  assert.deepEqual(plain(tableData.value), [], 'a failed request refreshes the authoritative list')
  assert.deepEqual(plain(page.selectedIds.value), [])
}

{
  const fixture = makePage({ rows: [{ id: 'a' }, { id: 'b' }] })
  const { page, proxy, calls } = fixture
  await page.init()
  page.handleGridSelectAll(true)
  let finish
  proxy.$API.boxDeleteEvent = request => {
    calls.delete.push(plain(request))
    return new Promise(resolve => { finish = resolve })
  }
  const first = page.handleBatchDelete()
  await Promise.resolve()
  assert.equal(page.deleting.value, true)
  await page.handleDelete({ id: 'a' })
  assert.equal(calls.delete.length, 1, 'a pending deletion blocks another submission')
  finish({ resData: { deletedIds: [], missingIds: [], failedItems: [
    { id: 'a', reason: 'RECORDING' }, { id: 'b', reason: 'RECORDING' }
  ] } })
  await first
  assert.equal(page.deleting.value, false)
  assert.equal(calls.messages[0].kind, 'error', 'all-failed batches are not reported as successful')
}

{
  const fixture = makePage({ rows: [{ id: 'a' }, { id: 'b' }], pageSize: 1 })
  const { page, pageData } = fixture
  await page.init()
  page.handleGridSelectAll(true)
  page.handleCurrentChange(2)
  assert.deepEqual(plain(page.selectedIds.value), [], 'page changes clear the selection')
  assert.equal(pageData.pageNum, 2)
  page.handleSizeChange(20)
  assert.equal(pageData.pageNum, 1, 'page-size changes return to the first page')
}

{
  const fixture = makePage()
  const { page, proxy, tableData } = fixture
  const pending = []
  proxy.$API.boxQueryEvent = () => new Promise(resolve => pending.push(resolve))
  const older = page.init()
  const newer = page.init()
  pending[1]({ resData: { rows: [{ id: 'new' }], total: 1 } })
  await newer
  pending[0]({ resData: { rows: [{ id: 'stale' }], total: 1 } })
  await older
  assert.deepEqual(plain(tableData.value), [{ id: 'new' }], 'a stale response cannot restore old records')
}

console.log('Event deletion behavior checks passed')
