import assert from 'node:assert/strict'
import { h } from 'vue'
import { createVisualReviewLoader, visualReviewScores, visualReviewLabel } from '../src/utils/visualReview.js'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const typed = (id = 'request-new') => ({ resCode: 1, resData: { schema: 1, format: 'typed-v1', rows: [{ request_id: id, request: { task_id: 'task-2', roi_id: 'roi-2' }, items: [] }], total: 1, automatic_filtering: false, question_runtime: { available: true, queued: 2 }, audit_runtime: { available: true } } })
const legacy = { resCode: 1, resData: { rows: [{ event_id: 'legacy' }], total: 1, runtime: { automatic_filtering: false }, retention_limit: 10000 } }
let state
const pending = []
const loader = createVisualReviewLoader(query => new Promise((resolve, reject) => pending.push({ query, resolve, reject })), value => { state = value })
const old = loader.load({ eventId: 'old', format: 'typed-v1' })
const current = loader.load({ eventId: 'new', format: 'typed-v1' })
pending[1].resolve(typed())
await current
assert.equal(state.rows[0].request_id, 'request-new')
pending[0].resolve(typed('request-old'))
await old
assert.equal(state.rows[0].request_id, 'request-new', 'late alarm responses must not replace the current view')
assert.equal(state.filtering, false)
assert.equal(state.questions.queued, 2)
const closing = loader.load({ format: 'typed-v1' })
loader.invalidate()
const invalidatedState = state
pending[2].reject(new Error('closed'))
await closing
assert.equal(state, invalidatedState, 'closing or unmounting ignores pending failures')

for (const response of [legacy, { resCode: 0 }, { resCode: 1, resData: { ...typed().resData, rows: [{}] } }, { resCode: 1, resData: { ...typed().resData, rows: [{ request_id: 'bad-item', request: {}, items: [null] }] } }]) {
  const incompatible = loader.load({ format: 'typed-v1' })
  assert.equal(state.rows.length, 0)
  assert.equal(state.total, 0)
  pending.at(-1).resolve(response)
  await incompatible
  assert.equal(state.failed, true, 'legacy or malformed responses cannot masquerade as typed reviews')
  assert.equal(state.rows.length, 0)
}
const historical = loader.load({ eventId: 'alarm', pageNum: 1, pageSize: 20 })
pending.at(-1).resolve(legacy)
await historical
assert.equal(state.rows[0].event_id, 'legacy')
assert.equal(state.retention, 10000)
assert.equal(state.filtering, false)
assert.equal(visualReviewScores({ status: 'unknown', ordered_options: ['a'], probabilities: [1] }), '—')
assert.equal(visualReviewScores({ status: 'completed', ordered_options: ['no', 'yes'], probabilities: [0.8, 0.2] }), 'no: 80.0% / yes: 20.0%')
assert.equal(visualReviewScores({ status: 'completed', ordered_options: ['no', 'yes'], probabilities: [1] }), '—')
assert.equal(visualReviewScores({ status: 'completed', ordered_options: ['bad'], probabilities: [NaN] }), 'bad: —')

// Mount the real dialog/controller. Tables are separate render-only children;
// inspect the selected child's props to verify format and result isolation.
const table = name => ({ default: { props: ['rows'], render() { return h(name, { rows: this.rows }) } } })
const calls = []
const dialog = await mountComponent('views/box/eventQuery/components/LayaReviewDialog.vue', {
  props: { modelValue: true, eventId: 'alarm-a' },
  mocks: { './VisualAuditTable.vue': table('typed-table'), './LegacyLayaReviewTable.vue': table('legacy-table') },
  api: { boxQueryLayaReview: query => new Promise(resolve => calls.push({ query, resolve })) }
})
try {
  assert.equal(calls.length, 1, 'an already-open dialog fetches immediately')
  assert.equal(calls[0].query.format, 'typed-v1')
  assert.equal(calls[0].query.eventId, 'alarm-a')
  dialog.instance.$.props.eventId = 'alarm-b'
  await dialog.settle()
  assert.equal(calls.length, 2)
  calls[1].resolve(typed('alarm-b-request'))
  await dialog.settle()
  assert.equal(dialog.all(n => n.type === 'typed-table')[0].props.rows[0].request_id, 'alarm-b-request')
  calls[0].resolve(typed('alarm-a-request'))
  await dialog.settle()
  assert.equal(dialog.all(n => n.type === 'typed-table')[0].props.rows[0].request_id, 'alarm-b-request')
  dialog.all(n => n.type === 'el-radio-group')[0].props.activate('legacy')
  await dialog.settle()
  assert.equal(calls.length, 3)
  assert.equal(calls[2].query.format, undefined, 'legacy access preserves the existing API default')
  assert.equal(calls[2].query.eventId, 'alarm-b')
  assert.equal(dialog.all(n => n.type === 'typed-table').length, 0)
  assert.equal(dialog.all(n => n.type === 'legacy-table')[0].props.rows.length, 0)
  dialog.instance.$.props.modelValue = false
  await dialog.settle()
  calls[2].resolve(legacy)
  await dialog.settle()
  assert.equal(dialog.all(n => n.type === 'legacy-table')[0].props.rows.length, 0, 'closing clears stale results')
} finally { dialog.unmount() }
console.log('Visual review behavior: request isolation, typed/legacy routing, strict results, mounted dialog PASS')

assert.equal(visualReviewLabel(key => key, 'delivery', 'filtered'), 'visualReview.delivery.filtered')
assert.equal(visualReviewLabel(key => key, 'reason', 'unqualified_filtering_disabled'), 'visualReview.reason.unqualified_filtering_disabled')
assert.equal(visualReviewLabel(key => key, 'decision', 'reject'), 'visualReview.decision.reject')
