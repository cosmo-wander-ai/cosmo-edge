<template>
  <section class="toolbar-section audio-out-settings" aria-labelledby="local-audio-out-title">
    <header class="audio-out-header">
      <h2 id="local-audio-out-title">{{ t('localAudioOut.title') }}</h2>
      <span role="status">{{ statusText }}</span>
      <el-button link size="small" :disabled="busy" @click="refresh">{{ t('action.refresh') }}</el-button>
    </header>
    <el-form v-if="settings" inline class="audio-out-form" @submit.prevent="save">
      <el-form-item :label="t('localAudioOut.enabled')">
        <el-switch v-model="form.enabled" :aria-label="t('localAudioOut.enabled')"
          :disabled="busy || (!settings.available && !settings.enabled)" />
      </el-form-item>
      <el-form-item :label="t('localAudioOut.recording')">
        <el-select v-model="form.fileId" size="small" filterable class="recording-select"
          :aria-label="t('localAudioOut.recording')" :disabled="busy || !settings.available || !files?.length">
          <el-option v-if="form.fileId && !selectedFile" :value="form.fileId"
            :label="files ? t('localAudioOut.missingRecording') : form.fileId" disabled />
          <el-option v-for="file in files" :key="file.fileId" :value="file.fileId" :label="fileLabel(file)" />
        </el-select>
      </el-form-item>
      <el-button type="primary" size="small" native-type="submit" :disabled="!canSave" :loading="saving">
        {{ t('action.save') }}
      </el-button>
    </el-form>
  </section>
</template>

<script setup>
import { computed, getCurrentInstance, onMounted, reactive, ref } from 'vue'
import { t, translateApiMessage } from '@/i18n'
import { normalizeApiError } from '@/utils/apiError'

defineProps({ fileLabel: { type: Function, required: true } })
const emit = defineEmits(['saved-file'])
const { proxy } = getCurrentInstance()
const settings = ref(null)
const form = reactive({ enabled: false, fileId: '' })
const files = ref([])
const loading = ref(false)
const saving = ref(false)
let catalogRequest = 0

const busy = computed(() => loading.value || saving.value)
const selectedFile = computed(() => files.value?.find(file => file.fileId === form.fileId))
const canSave = computed(() => {
  const saved = settings.value
  if (!saved || busy.value) return false
  if (saved.enabled && !form.enabled && form.fileId === saved.fileId) return true
  return saved.available && !!selectedFile.value &&
    (form.enabled !== saved.enabled || form.fileId !== saved.fileId || !!saved.reason)
})

const reasonText = (reason, fallback) => translateApiMessage('localAudioOut.reasons.' + reason, fallback)
const statusText = computed(() => {
  if (!settings.value) return loading.value ? '' : t('localAudioOut.loadFailed')
  if (settings.value.reason) return reasonText(settings.value.reason, t('localAudioOut.unavailable'))
  if (!settings.value.available) return t('localAudioOut.unavailable')
  return t(files.value ? 'localAudioOut.ready' : 'localAudioOut.libraryUnavailable')
})

async function refreshFiles() {
  const requestId = ++catalogRequest
  let rows = null
  try {
    // The library holds up to 1000 files. Table search and pagination are independent.
    const { resData } = await proxy.$API.queryAudioFile({ pageNum: 1, pageSize: 1000 })
    if (Array.isArray(resData?.audioFileList)) rows = resData.audioFileList
  } catch {
    // The form can still disable an active output when the library is unavailable.
  }
  if (requestId === catalogRequest) files.value = rows
}

async function readSettings() {
  try {
    const { resData } = await proxy.$API.queryLocalAudioOut()
    if (typeof resData?.enabled !== 'boolean' || typeof resData.available !== 'boolean' ||
        typeof resData.fileId !== 'string') {
      throw new Error('Invalid local audio settings')
    }
    settings.value = resData
    form.enabled = resData.enabled
    form.fileId = resData.fileId
    emit('saved-file', resData.fileId)
    return true
  } catch {
    settings.value = null
    return false
  }
}

async function refresh() {
  if (busy.value) return
  loading.value = true
  try {
    await Promise.all([readSettings(), refreshFiles()])
  } finally {
    loading.value = false
  }
}

async function save() {
  if (!canSave.value) return
  const requested = { enabled: form.enabled, fileId: form.fileId }
  saving.value = true
  try {
    await proxy.$API.setLocalAudioOut(requested)
    const verified = await readSettings()
    if (!verified) emit('saved-file', requested.fileId)
    if (verified && form.enabled === requested.enabled && form.fileId === requested.fileId) {
      proxy.$message.success(t('common.saveSucceeded'))
    }
  } catch (error) {
    const { code, status, message, messageKey } = normalizeApiError(error)
    if (code !== '10005' && status !== 401) {
      proxy.$message.error(reasonText(message, translateApiMessage(messageKey, message)))
    }
    await readSettings()
  } finally {
    saving.value = false
  }
}

onMounted(refresh)
defineExpose({ refresh, refreshFiles })
</script>

<style scoped>
.audio-out-settings {
  padding: 16px;
  margin-bottom: 16px;
}
.audio-out-header, .audio-out-form {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px 16px;
}
.audio-out-header h2 {
  margin: 0 auto 0 0;
  color: var(--text-primary);
  font-size: 15px;
  font-weight: 600;
}
.audio-out-header [role='status'] {
  color: var(--text-secondary);
  font-size: 13px;
}
.audio-out-form { margin-top: 12px; }
.audio-out-form :deep(.el-form-item) { margin: 0; }
.recording-select { width: min(260px, 55vw); }
</style>
