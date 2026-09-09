import assert from 'node:assert/strict'
import { loadBehaviorModule } from './helpers/load_behavior_module.mjs'

const calls = []
const result = Promise.resolve({ resCode: 1 })
const { default: api } = await loadBehaviorModule('api/index.js', {
  mocks: { '@/utils/request': { request: config => { calls.push(config); return result } } }
})

const cases = [
  ['dologin', '/gtw/cwai/login/dologin'],
  ['algorithmInquire', '/gtw/cwai/algorithm/page'],
  ['queryDocumentUrl', '/gtw/cwai/System/QueryDocumentUrl'],
  ['algorithmInfo', '/gtw/cwai/algorithm/version/info'],
  ['schedulePollingList', '/gtw/cwai/cust/schedule/polling/list'],
  ['uploadAtomicModelTemp', '/gtw/cwai/atomic/model/uploadTemp'],
  ['cancelAtomicModelUpload', '/gtw/cwai/atomic/model/cancelUpload', { silentError: true }],
  ['boxAlgorithmUpload', '/gtw/cwai/algorithm/Upload'],
  ['pTaskCreate', '/gtw/cwai/aihost/PTaskCreate', { timeout: 120000 }]
]
for (const [name, url, options = {}] of cases) {
  const data = { id: 0, name: '测试 "quoted"', values: [] }
  assert.equal(api[name](data), result, `${name} must return the request promise`)
  const config = calls.pop()
  assert.equal(config.data, data, `${name} must forward the original payload`)
  assert.deepEqual(JSON.parse(JSON.stringify(config)), { url, method: 'post', data, ...options })
}
assert.equal(api.getUploadCapabilities(), result)
assert.deepEqual(JSON.parse(JSON.stringify(calls.pop())), {
  url: '/gtw/cwai/atomic/model/uploadCapabilities', method: 'post', data: {}, silentError: true
})
assert.equal(api.exportAlgorithmLayout(), '/gtw/cwai/algorithm/layout/export')
assert.equal(api.exportSingleAlg(), '/gtw/cwai/algorithm/layout/exportSingleAlg')
assert.equal(api.exportModelConfig(), '/gtw/cwai/atomic/model/exportConfig')
assert.equal(calls.length, 0, 'URL helpers must not send a request')
console.log('API contract checks passed (merged exports, payloads, options and URL helpers)')
