<template>
  <el-dialog v-model="visible" :title="t('visualReview.title')" width="min(1180px, 94vw)" top="6vh" destroy-on-close class="ui-admin-dialog visual-review-dialog">
    <div class="review-content">
      <el-radio-group v-model="format" class="review-format" :aria-label="t('visualReview.recordType')">
        <el-radio-button value="typed-v1">{{ t('visualReview.typed') }}</el-radio-button>
        <el-radio-button value="legacy">{{ t('visualReview.legacy') }}</el-radio-button>
      </el-radio-group>
      <el-alert :title="t(format === 'typed-v1' ? 'visualReview.notice' : 'visualReview.legacyNotice')" type="info" :closable="false" show-icon />
      <div class="review-service-summary" :aria-label="t('visualReview.serviceStatus')">
        <div class="review-service-item">
          <span class="review-service-label">{{ t('visualReview.entry') }}</span>
          <span v-if="format === 'typed-v1'" class="review-service-value" :class="availabilityClass(state.runtime.available)">{{ availability(state.runtime.available) }}</span>
          <span v-else class="review-service-value">{{ legacyRuntime }}</span>
        </div>
        <div v-if="format === 'typed-v1'" class="review-service-item">
          <span class="review-service-label">{{ t('visualReview.audit') }}</span>
          <span class="review-service-value" :class="availabilityClass(state.audit.available)">{{ availability(state.audit.available) }}</span>
        </div>
        <div class="review-service-item">
          <span class="review-service-label">{{ t('visualReview.filtering') }}</span>
          <span class="review-service-value" :class="state.filtering === true ? 'is-available' : ''">{{ state.filtering === null ? t('visualReview.unknownState') : state.filtering ? t('visualReview.on') : t('visualReview.off') }}</span>
        </div>
      </div>
      <template v-if="format === 'typed-v1'">
        <details class="review-preparation">
          <summary>
            <span>{{ t('visualReview.preparation') }}</span>
            <span :class="availabilityClass(state.questions.available)">{{ availability(state.questions.available) }}</span>
            <span class="preparation-counts">{{ t('visualReview.queued') }} {{ state.questions.queued ?? '—' }} · {{ t('visualReview.retries') }} {{ state.questions.retried ?? '—' }}</span>
          </summary>
          <p v-if="state.questions.active">{{ t('visualReview.task') }} {{ state.questions.active.task_id }} · {{ t('visualReview.progress') }} {{ state.questions.active.prepared_questions }}/{{ state.questions.active.questions }}</p>
          <p v-else>{{ t('visualReview.progress') }}: —</p>
        </details>
        <div class="review-search">
          <el-input v-model="requestId" :placeholder="t('visualReview.requestId')" :aria-label="t('visualReview.requestId')" maxlength="256" clearable @keyup.enter="search" />
          <el-button @click="search">{{ t('action.search') }}</el-button>
        </div>
      </template>
      <el-alert v-if="state.failed" :title="t('visualReview.queryFailed')" type="error" :closable="false" show-icon />
      <div v-loading="state.loading" class="review-table-wrap">
        <VisualAuditTable v-if="format === 'typed-v1'" :rows="state.rows" :empty-text="state.failed ? t('visualReview.queryFailed') : t('visualReview.empty')" />
        <LegacyLayaReviewTable v-else :rows="state.rows" :empty-text="state.failed ? t('visualReview.queryFailed') : t('visualReview.empty')" />
      </div>
      <p class="review-retention">{{ format === 'typed-v1' ? t('visualReview.retention') : t('visualReview.legacyRetention', { count: state.retention ?? '—' }) }}</p>
    </div>
    <template #footer>
      <div class="review-footer">
        <el-pagination v-model:current-page="page" :page-size="20" :total="state.total" layout="total, prev, pager, next" @current-change="load" />
        <div class="review-footer-actions">
          <el-button @click="load">{{ t('action.refresh') }}</el-button>
          <el-button type="primary" @click="visible = false">{{ t('action.close') }}</el-button>
        </div>
      </div>
    </template>
  </el-dialog>
</template>
<script setup>
import { computed, getCurrentInstance, onBeforeUnmount, ref, watch } from 'vue'
import { t } from '@/i18n'
import { createVisualReviewLoader, emptyVisualReview } from '@/utils/visualReview'
import VisualAuditTable from './VisualAuditTable.vue'
import LegacyLayaReviewTable from './LegacyLayaReviewTable.vue'
const props = defineProps({ modelValue: Boolean, eventId: { type: String, default: '' } })
const emit = defineEmits(['update:modelValue'])
const visible = computed({ get: () => props.modelValue, set: value => emit('update:modelValue', value) })
const { proxy } = getCurrentInstance()
const format = ref('typed-v1'), page = ref(1), requestId = ref('')
const state = ref(emptyVisualReview())
const loader = createVisualReviewLoader(query => proxy.$API.boxQueryLayaReview(query), value => { state.value = value })
const legacyRuntime = computed(() => {
  const code = state.value.runtime.state
  return ['ready', 'loading', 'stopped', 'unavailable'].includes(code) ? t(`visualReview.worker.${code}`) : code || t('visualReview.unknownState')
})
const availabilityClass = value => value === true ? 'is-available' : value === false ? 'is-unavailable' : 'is-unknown'
const availability = value => value === true ? t('visualReview.available') : value === false ? t('visualReview.unavailable') : t('visualReview.unknownState')
function load() {
  if (!props.modelValue) return
  const query = { eventId: props.eventId, pageNum: page.value, pageSize: 20 }
  if (format.value === 'typed-v1') { query.format = 'typed-v1'; query.requestId = requestId.value.trim() }
  return loader.load(query)
}
function search() { page.value = 1; load() }
watch(() => [props.modelValue, props.eventId, format.value], () => {
  loader.invalidate()
  state.value = emptyVisualReview()
  page.value = 1
  requestId.value = ''
  if (props.modelValue) load()
}, { immediate: true, flush: 'sync' })
onBeforeUnmount(() => loader.invalidate())
</script>
<style scoped>
.visual-review-dialog { display: flex; flex-direction: column; max-height: 88vh; }
.visual-review-dialog :deep(.el-dialog__body) { min-height: 0; overflow-y: auto; }
.visual-review-dialog :deep(.el-dialog__header), .visual-review-dialog :deep(.el-dialog__footer) { flex: none; }
.review-content { display: flex; flex-direction: column; gap: 12px; min-width: 0; }
.review-format { align-self: flex-start; }
.review-content :deep(.el-alert) { padding: 10px 12px; }
.review-content :deep(.el-alert__title) { font-size: 12px; line-height: 1.6; }
.review-service-summary { display: flex; flex-wrap: wrap; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-subtle); padding: 12px 0; }
.review-service-item { flex: 1 1 230px; display: flex; align-items: baseline; justify-content: center; gap: 16px; padding: 2px 16px; min-width: 0; }
.review-service-item + .review-service-item { border-left: 1px solid var(--border-color); }
.review-service-label { color: var(--text-secondary); font-size: 12px; }
.review-service-value { color: var(--text-secondary); font-size: 13px; font-weight: 600; }
.review-service-value::before { content: ''; display: inline-block; width: 6px; height: 6px; margin-right: 7px; border-radius: 50%; background: currentColor; vertical-align: 1px; }
.is-available { color: var(--success-color); }
.is-unavailable { color: var(--danger-color); }
.is-unknown { color: var(--text-muted); }
.review-preparation { font-size: 12px; color: var(--text-secondary); }
.review-preparation summary { cursor: pointer; line-height: 1.7; }
.review-preparation summary > span { margin-right: 12px; }
.review-preparation p { margin: 6px 0 0 16px; overflow-wrap: anywhere; }
.preparation-counts { font-variant-numeric: tabular-nums; }
.visual-review-dialog .review-content .review-search { display: flex; gap: 8px; width: 100%; max-width: 470px; padding: 0; border: 0; background: none; }
.review-search :deep(.el-input) { min-width: 0; }
.review-table-wrap { min-width: 0; border: 1px solid var(--border-color); border-radius: 6px; overflow: hidden; }
.review-retention { margin: 0; padding: 9px 12px; font-size: 12px; line-height: 1.6; color: var(--text-secondary); background: var(--bg-subtle); border-radius: 4px; }
.review-footer { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; }
.review-footer :deep(.el-pagination) { overflow-x: auto; max-width: 100%; }
.review-footer-actions { display: flex; gap: 8px; margin-left: auto; }
.review-footer-actions :deep(.el-button + .el-button) { margin-left: 0; }
@media (max-width: 800px) {
  .review-service-item { flex-basis: 100%; justify-content: space-between; padding: 5px 14px; }
  .review-service-item + .review-service-item { border-left: 0; }
  .preparation-counts { display: block; margin-left: 16px; }
}
</style>
