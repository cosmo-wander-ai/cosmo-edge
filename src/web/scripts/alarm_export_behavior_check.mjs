import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import vm from 'node:vm'

const requests = []
const events = []
let response = { ok: true, headers: { get: () => 'text/csv' }, blob: async () => 'csv' }
const context = vm.createContext({
  localStorage: { getItem: () => 'test-token' },
  fetch: async (url, options) => { requests.push({ url, options }); return response },
  window: { URL: { createObjectURL: () => 'blob:test', revokeObjectURL: url => events.push(['revoke', url]) } },
  document: {
    createElement: () => ({ style: {}, click: () => events.push(['click']) }),
    body: { appendChild: () => events.push(['append']), removeChild: () => events.push(['remove']) }
  }
})
const source = await fs.readFile(new URL('../src/utils/alarmExport.js', import.meta.url), 'utf8')
const module = new vm.SourceTextModule(source, { context })
await module.link(() => { throw new Error('unexpected import') })
await module.evaluate()
const { downloadAlarmExport } = module.namespace
await downloadAlarmExport({ pageNum: 1 })
assert.equal(requests[0].url, '/gtw/cwai/Event/DownloadAlarm')
assert.equal(requests[0].options.method, 'POST')
assert.equal(requests[0].options.headers.token, 'test-token')
assert.equal(requests[0].options.body, '{"pageNum":1}')
assert.deepEqual(events.map(event => event[0]), ['append', 'click', 'remove', 'revoke'])
response = { ok: true, headers: { get: () => 'application/json' } }
await assert.rejects(downloadAlarmExport({}), /Event export unavailable/)
response = { ok: false, headers: { get: () => 'text/csv' } }
await assert.rejects(downloadAlarmExport({}), /Event export unavailable/)
console.log('Authenticated alarm export checks passed')
