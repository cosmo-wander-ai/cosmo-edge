// Keep request identity across paging, format changes and dialog closure. A late
// response must never replace a different alarm's review records.
export const emptyVisualReview = () => ({ rows: [], total: 0, runtime: {}, audit: {}, questions: {}, filtering: null, retention: null, loading: false, failed: false })

export function createVisualReviewLoader(request, publish) {
  let generation = 0
  return {
    invalidate() { generation++ },
    async load(query) {
      const current = ++generation
      publish({ ...emptyVisualReview(), loading: true })
      try {
        const response = await request({ ...query })
        if (current !== generation) return
        const data = response?.resData
        if (response?.resCode !== 1 || !Array.isArray(data?.rows) || !Number.isInteger(data.total) || data.total < 0) throw new Error('invalid_review_page')
        const object = value => value !== null && typeof value === 'object' && !Array.isArray(value)
        if (query.format === 'typed-v1' && (data.format !== 'typed-v1' || data.schema !== 1 ||
            data.rows.some(row => typeof row?.request_id !== 'string' || !object(row.request) || !Array.isArray(row.items) ||
              row.items.some(item => !object(item?.identity) || (item.result != null && !object(item.result)))))) throw new Error('invalid_typed_review_page')
        const filtering = query.format === 'typed-v1' ? data.automatic_filtering : data.runtime?.automatic_filtering
        publish({ ...emptyVisualReview(), rows: data.rows, total: data.total, runtime: data.runtime || {}, audit: data.audit_runtime || {}, questions: data.question_runtime || {}, filtering: typeof filtering === 'boolean' ? filtering : null, retention: data.retention_limit })
      } catch {
        if (current === generation) publish({ ...emptyVisualReview(), failed: true })
      }
    }
  }
}

export const reviewStateCodes = {
  result: new Set(['completed', 'partial', 'unknown', 'pending']),
  delivery: new Set(['pending', 'alarm_linked', 'returned', 'cancelled', 'no_alarm', 'alarm_store_failed', 'interrupted']),
  reason: new Set(['configuration_preparing', 'compiler_queue_full', 'compiler_queue_bytes_full', 'compile_deadline_exceeded', 'compiler_not_configured', 'process_spawn_failed', 'process_io_failed', 'sequence_budget_exceeded', 'invalid_compiler_receipt', 'stale_task_run', 'service_stopped', 'worker_unavailable', 'deadline_exceeded', 'queue_full', 'audit_store_unavailable', 'audit_result_write_failed', 'engine_restarted', 'missing_prepared_question', 'unsupported_or_empty_roi', 'business_qualification_pending'])
}
export function visualReviewLabel(t, group, code) {
  return reviewStateCodes[group]?.has(code) ? t(`visualReview.${group}.${code}`) : code || '—'
}
export function visualReviewScores(result) {
  if (result?.status !== 'completed' || !Array.isArray(result.ordered_options) || !Array.isArray(result.probabilities) || result.ordered_options.length !== result.probabilities.length) return '—'
  return result.ordered_options.map((label, i) => {
    const value = result.probabilities[i]
    return `${label}: ${Number.isFinite(value) && value >= 0 && value <= 1 ? `${(value * 100).toFixed(1)}%` : '—'}`
  }).join(' / ')
}
