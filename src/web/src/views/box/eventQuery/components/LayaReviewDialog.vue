<template>
  <el-dialog v-model="visible" :title="t('visualReview.title')" width="90%" destroy-on-close class="ui-admin-dialog">
    <el-radio-group v-model="format" class="review-format" :aria-label="t('visualReview.recordType')">
      <el-radio-button value="typed-v1">{{ t('visualReview.typed') }}</el-radio-button>
      <el-radio-button value="legacy">{{ t('visualReview.legacy') }}</el-radio-button>
    </el-radio-group>
    <el-alert :title="t(format === 'typed-v1' ? 'visualReview.notice' : 'visualReview.legacyNotice')" type="info" :closable="false" />
    <template v-if="format === 'typed-v1'">
      <p>{{ t('visualReview.entry') }}: {{ availability(state.runtime.available) }} · {{ t('visualReview.audit') }}: {{ availability(state.audit.available) }} · {{ t('visualReview.filtering') }}: {{ state.filtering === null ? t('visualReview.unknownState') : state.filtering ? t('visualReview.on') : t('visualReview.off') }}</p>
      <p>{{ t('visualReview.retention') }}</p>
      <p>{{ t('visualReview.preparation') }}: {{ availability(state.questions.available) }} · {{ t('visualReview.queued') }}: {{ state.questions.queued ?? '—' }} · {{ t('visualReview.retries') }}: {{ state.questions.retried ?? '—' }}</p>
      <p v-if="state.questions.active">{{ t('visualReview.task') }} {{ state.questions.active.task_id }} · {{ t('visualReview.progress') }} {{ state.questions.active.prepared_questions }}/{{ state.questions.active.questions }}</p>
      <div class="review-search">
        <el-input v-model="requestId" :placeholder="t('visualReview.requestId')" :aria-label="t('visualReview.requestId')" maxlength="256" clearable @keyup.enter="search" />
        <el-button @click="search">{{ t('action.search') }}</el-button>
      </div>
    </template>
    <template v-else>
      <p>{{ t('visualReview.entry') }}: {{ legacyRuntime }} · {{ t('visualReview.filtering') }}: {{ state.filtering === null ? t('visualReview.unknownState') : state.filtering ? t('visualReview.on') : t('visualReview.off') }}</p>
      <p>{{ t('visualReview.legacyRetention', { count: state.retention ?? '—' }) }}</p>
    </template>
    <el-alert v-if="state.failed" :title="t('visualReview.queryFailed')" type="error" :closable="false" />
    <div v-loading="state.loading">
      <VisualAuditTable v-if="format === 'typed-v1'" :rows="state.rows" />
      <LegacyLayaReviewTable v-else :rows="state.rows" />
    </div>
    <el-pagination v-model:current-page="page" :page-size="20" :total="state.total" layout="prev, pager, next, total" @current-change="load" />
    <template #footer><el-button @click="load">{{ t('action.refresh') }}</el-button><el-button @click="visible = false">{{ t('action.close') }}</el-button></template>
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
.review-format { margin-bottom: 16px; }
.review-search { display: flex; gap: 8px; max-width: 540px; margin-bottom: 16px; }
</style>
