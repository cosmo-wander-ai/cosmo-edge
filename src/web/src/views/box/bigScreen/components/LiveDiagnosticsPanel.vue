<template>
  <section class="live-diagnostics" :aria-label="t('event.diagnosticsTitle')" @click.stop>
    <header class="diagnostics-header">
      <svg class="diagnostics-pulse" viewBox="0 0 20 20" fill="none" aria-hidden="true"><path d="M1 10h4l2-5 3.1 10 2.6-7 1.5 2H19" /></svg>
      <h3>{{ t('event.diagnosticsTitle') }}</h3>
      <button type="button" class="diagnostics-close" :aria-label="t('action.close')" @click="$emit('close')">
        <svg viewBox="0 0 20 20" fill="none" aria-hidden="true"><path d="M5 5l10 10M15 5L5 15" /></svg>
      </button>
    </header>
    <div class="diagnostics-body">
      <p v-if="state === 'select'" class="diagnostics-message">{{ t('event.diagnosticsSelectOverlay') }}</p>
      <p v-else-if="state === 'loading'" class="diagnostics-message">{{ t('event.diagnosticsLoading') }}</p>
      <p v-else-if="state === 'empty'" class="diagnostics-message">{{ t('event.diagnosticsNoData') }}</p>
      <p v-else-if="state === 'ambiguous'" class="diagnostics-message">{{ t('event.diagnosticsAmbiguous') }}</p>
      <p v-else-if="state === 'error'" class="diagnostics-message">{{ t('event.diagnosticsError') }}</p>
      <template v-else>
        <div class="diagnostics-subheading">{{ t('event.diagnosticsNodeTitle') }}</div>
        <dl class="diagnostics-values">
          <div v-for="node in rows" :key="node.name" class="diagnostics-value">
            <dt :title="t(node.label)">{{ t(node.label) }}</dt>
            <dd><span v-if="node.value !== null">{{ node.value }}</span><span v-else class="diagnostics-empty" :title="t('event.diagnosticsNoSamples')">—</span><small v-if="node.value !== null">ms</small></dd>
          </div>
        </dl>
        <div class="diagnostics-subheading diagnostics-preview-heading">{{ t('event.diagnosticsPreviewTitle') }}</div>
        <dl class="diagnostics-values">
          <div v-for="node in previewRows" :key="node.name" class="diagnostics-value">
            <dt>{{ node.name === 'Encode' ? t('event.diagnosticsEncode') : 'OSD' }}</dt>
            <dd><span v-if="node.value !== null">{{ node.value }}</span><span v-else class="diagnostics-empty" :title="t('event.diagnosticsNoSamples')">—</span><small v-if="node.value !== null">ms</small></dd>
          </div>
        </dl>
        <button type="button" class="diagnostics-detail-toggle" :aria-expanded="showDetails" @click="showDetails = !showDetails">
          {{ t(showDetails ? 'event.diagnosticsHideDetails' : 'event.diagnosticsViewDetails') }}
          <svg :class="{ 'is-open': showDetails }" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m6 3 5 5-5 5" /></svg>
        </button>
        <div v-if="showDetails" class="diagnostics-detail">
          <p>{{ t('event.diagnosticsScope') }}</p>
          <p>{{ previewSupported ? t('event.diagnosticsPreviewScope') : t('event.diagnosticsPreviewUnavailable') }}</p>
          <dl v-if="extraRows.length" class="diagnostics-extra">
            <div v-for="(node, index) in extraRows" :key="index" class="diagnostics-extra-value">
              <dt :title="node.name">{{ node.name }}</dt><dd>{{ node.value === null ? '—' : `${node.value} ms` }}</dd>
            </div>
          </dl>
        </div>
      </template>
    </div>
  </section>
</template>

<script setup>
import { getCurrentInstance, onBeforeUnmount, ref, watch } from 'vue'
import { t } from '@/i18n'

defineEmits(['close'])
const props = defineProps({ channelId: String, algorithmId: String })
const { proxy } = getCurrentInstance()
const actionDefinitions = [
  { name: 'Decode', label: 'event.diagnosticsDecode' },
  { name: 'Detect', label: 'event.diagnosticsDetect' },
  { name: 'AreaAlarm', label: 'event.diagnosticsAreaAlarm' },
  { name: 'Alarm', label: 'event.diagnosticsAlarm' }
]
const state = ref('loading')
const rows = ref([])
const extraRows = ref([])
const previewSupported = ref(false)
const previewRows = ref([])
const showDetails = ref(false)
let generation = 0
let timer = null
let disposed = false
let matchedTaskId = ''

const clearTimer = () => {
  if (timer !== null) clearTimeout(timer)
  timer = null
}
const formatDuration = node => Number.isFinite(node?.durationAvgUs) && node.durationAvgUs >= 0 &&
  Number.isFinite(node?.durationCount) && node.durationCount > 0
  ? (node.durationAvgUs / 1000).toFixed(2) : null
const selectValue = (nodes, name) => {
  const matches = nodes.filter(node => node?.name === name)
  return matches.length === 1 ? formatDuration(matches[0]) : null
}
const clearValues = () => {
  rows.value = []
  extraRows.value = []
  previewRows.value = []
  previewSupported.value = false
}
const matchingStatus = (response, channelId, algorithmId) => {
  const statuses = Array.isArray(response?.resData?.status) ? response.resData.status : []
  return statuses.filter(item => String(item?.channelId || '') === channelId &&
    String(item?.algorithmId || '') === algorithmId)
}

// Discover the exact server-owned task once, then request old-OSD action stats
// only for that task. A generation token prevents stale async updates.
const refresh = async () => {
  const currentGeneration = ++generation
  clearTimer()
  const channelId = String(props.channelId || '')
  const algorithmId = String(props.algorithmId || '')
  if (!channelId || !algorithmId) {
    clearValues()
    state.value = 'select'
    return
  }
  if (state.value !== 'ready') state.value = 'loading'
  try {
    if (!matchedTaskId) {
      const discovery = await proxy.$API.queryRunningDetail({}, { silentError: true })
      if (disposed || currentGeneration !== generation) return
      const matches = matchingStatus(discovery, channelId, algorithmId)
      if (matches.length !== 1 || typeof matches[0].taskId !== 'string' || !matches[0].taskId) {
        clearValues()
        matchedTaskId = ''
        state.value = matches.length > 1 ? 'ambiguous' : 'empty'
      } else {
        matchedTaskId = matches[0].taskId
      }
    }
    if (matchedTaskId) {
      const response = await proxy.$API.queryRunningDetail({ tasks: [matchedTaskId], includeActionDurations: true }, { silentError: true })
      if (disposed || currentGeneration !== generation) return
      const matches = matchingStatus(response, channelId, algorithmId)
      if (matches.length !== 1 || matches[0].taskId !== matchedTaskId) {
        clearValues()
        matchedTaskId = ''
        state.value = matches.length > 1 ? 'ambiguous' : 'empty'
      } else {
        const actions = Array.isArray(matches[0].actionDurationInfos) ? matches[0].actionDurationInfos : []
        rows.value = actionDefinitions.map(node => ({ ...node, value: selectValue(actions, node.name) }))
        extraRows.value = actions.filter(node => typeof node?.name === 'string' && !actionDefinitions.some(def => def.name === node.name))
          .map(node => ({ name: node.name, value: formatDuration(node) }))
        previewSupported.value = Array.isArray(matches[0].previewDurationInfos)
        const preview = previewSupported.value ? matches[0].previewDurationInfos : []
        previewRows.value = ['OSD', 'Encode'].map(name => ({ name, value: selectValue(preview, name) }))
        state.value = 'ready'
      }
    }
  } catch {
    if (disposed || currentGeneration !== generation) return
    clearValues()
    state.value = 'error'
  }
  if (!disposed && currentGeneration === generation) timer = setTimeout(refresh, matchedTaskId ? 5000 : 30000)
}

watch(() => [props.channelId, props.algorithmId], () => {
  matchedTaskId = ''
  clearValues()
  showDetails.value = false
  state.value = 'loading'
  void refresh()
}, { immediate: true })

onBeforeUnmount(() => {
  disposed = true
  generation++
  clearTimer()
})
</script>

<style lang="scss" scoped>
.live-diagnostics {
  position: absolute;
  z-index: 3;
  left: 12px;
  bottom: 12px;
  width: min(270px, calc(100% - 24px));
  max-height: calc(100% - 24px);
  overflow: auto;
  box-sizing: border-box;
  color: #f2f4fb;
  background: rgba(38, 37, 53, .76);
  border: 1px solid rgba(205, 214, 232, .44);
  border-radius: 9px;
  box-shadow: 0 8px 25px rgba(0, 0, 0, .24);
  font-size: 12px;
  line-height: 1.4;
  scrollbar-width: thin;
  scrollbar-color: #798397 transparent;
}
.diagnostics-header { display: flex; align-items: center; gap: 8px; padding: 13px 14px 11px; border-bottom: 1px solid rgba(222, 228, 243, .1); }
.diagnostics-header h3 { flex: 1; margin: 0; font-size: 14px; font-weight: 650; line-height: 1.3; }
.diagnostics-pulse { width: 17px; height: 17px; flex: none; stroke: #8b7aff; stroke-width: 1.7; stroke-linecap: round; stroke-linejoin: round; }
.diagnostics-close { display: grid; place-items: center; width: 24px; height: 24px; margin: -4px; padding: 2px; border: 0; border-radius: 4px; color: #dce3f2; background: transparent; cursor: pointer; }
.diagnostics-close:hover { background: rgba(255, 255, 255, .1); }
.diagnostics-close:focus-visible, .diagnostics-detail-toggle:focus-visible { outline: 2px solid #a8a4ff; outline-offset: 2px; }
.diagnostics-close svg { width: 18px; height: 18px; stroke: currentColor; stroke-width: 1.4; stroke-linecap: round; }
.diagnostics-body { padding: 8px 14px 12px; }
.diagnostics-subheading { color: #a5aec0; font-size: 11px; }
.diagnostics-preview-heading { margin-top: 9px; }
.diagnostics-values { margin: 2px 0 0; }
.diagnostics-value { display: flex; justify-content: space-between; align-items: baseline; gap: 10px; min-height: 31px; padding: 6px 0; box-sizing: border-box; border-bottom: 1px solid rgba(224, 231, 244, .10); }
.diagnostics-value dt { min-width: 0; color: #e5eaf4; }
.diagnostics-value dd { flex: none; margin: 0; font-size: 13px; font-weight: 650; font-variant-numeric: tabular-nums; white-space: nowrap; }
.diagnostics-value small { margin-left: 4px; color: #b1bacb; font-size: 10px; font-weight: 400; }
.diagnostics-empty { color: #b1bacb; font-weight: 400; }
.diagnostics-message { margin: 8px 0; color: #c6cfde; }
.diagnostics-detail-toggle { display: inline-flex; align-items: center; gap: 3px; margin-top: 10px; padding: 0; border: 0; color: #a99aff; background: transparent; cursor: pointer; font: inherit; font-weight: 600; }
.diagnostics-detail-toggle svg { width: 13px; height: 13px; stroke: currentColor; stroke-width: 1.6; transition: transform .15s ease; }
.diagnostics-detail-toggle svg.is-open { transform: rotate(90deg); }
.diagnostics-detail { margin-top: 8px; color: #bac4d4; font-size: 11px; }
.diagnostics-detail p { margin: 5px 0; }
.diagnostics-extra { margin: 6px 0 0; }
.diagnostics-extra-value { display: flex; justify-content: space-between; gap: 8px; padding: 3px 0; }
.diagnostics-extra-value dt { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.diagnostics-extra-value dd { flex: none; margin: 0; font-variant-numeric: tabular-nums; }
@container (max-width: 600px) {
  .live-diagnostics { left: 8px; bottom: 8px; width: min(260px, calc(100% - 16px)); max-height: calc(100% - 16px); }
  .diagnostics-header { padding: 7px 11px; }
  .diagnostics-header h3 { font-size: 13px; }
  .diagnostics-body { padding: 5px 11px 7px; }
  .diagnostics-preview-heading { margin-top: 5px; }
  .diagnostics-values { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); column-gap: 10px; }
  .diagnostics-value { min-width: 0; min-height: 23px; gap: 3px; padding: 3px 0; }
  .diagnostics-value dt { overflow: hidden; font-size: 11px; text-overflow: ellipsis; white-space: nowrap; }
  .diagnostics-value dd { font-size: 11px; }
  .diagnostics-value small { margin-left: 2px; font-size: 9px; }
  .diagnostics-detail-toggle { margin-top: 6px; }
}
</style>
