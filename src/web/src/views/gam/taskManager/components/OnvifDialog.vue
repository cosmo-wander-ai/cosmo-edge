<template>
  <el-dialog v-model="visible" :title="t('onvif.title')" width="960px" :close-on-click-modal="!busy" :before-close="close">
    <el-tabs v-if="!editing" v-model="mode">
      <el-tab-pane :label="t('onvif.manualTab')" name="manual" :disabled="busy" />
      <el-tab-pane :label="t('onvif.autoTab')" name="auto" :disabled="busy" />
    </el-tabs>
    <el-alert :title="mode === 'manual' ? t('onvif.manualTip') : t('onvif.autoTip')" type="info" :closable="false" />
    <el-form v-if="mode === 'manual'" label-width="160px" class="onvif-form" :disabled="busy" @submit.prevent="submit">
      <template v-if="mode === 'manual'">
        <el-form-item :label="t('onvif.channelName')" required><el-input v-model="manual.channelName" :placeholder="t('onvif.namePlaceholder')" /></el-form-item>
        <el-form-item :label="t('onvif.ipAddress')" required><el-input v-model.trim="manual.endpoint" placeholder="192.168.1.100" /></el-form-item>
      </template>
      <el-form-item :label="t('onvif.username')" :required="mode === 'manual'"><el-input v-model.trim="credentials.username" autocomplete="off" /></el-form-item>
      <el-form-item :label="t('onvif.password')" :required="mode === 'manual' && !editing"><el-input v-model="credentials.password" type="password" show-password autocomplete="new-password" :placeholder="editing ? t('onvif.keepPassword') : ''" /></el-form-item>
      <el-form-item><el-checkbox v-model="credentials.separateRtspCredentials">{{ t('onvif.separateRtsp') }}</el-checkbox></el-form-item>
      <template v-if="credentials.separateRtspCredentials">
        <el-form-item :label="t('onvif.rtspUsername')" :required="mode === 'manual'"><el-input v-model.trim="credentials.rtspUsername" autocomplete="off" /></el-form-item>
        <el-form-item :label="t('onvif.rtspPassword')" :required="mode === 'manual' && !editing"><el-input v-model="credentials.rtspPassword" type="password" show-password autocomplete="new-password" :placeholder="editing ? t('onvif.keepPassword') : ''" /></el-form-item>
      </template>
    </el-form>
    <template v-if="mode === 'auto'">
      <div class="discovery-toolbar">
        <el-button type="primary" :loading="busy" @click="discover">{{ t('onvif.discover') }}</el-button>
        <span class="form-hint">{{ t('onvif.searchHint') }}</span>
      </div>
      <el-table v-if="rows.length" ref="table" :data="rows" row-key="key" @selection-change="selection = $event" max-height="330">
        <el-table-column type="selection" width="45" :selectable="row => !row.saved && !busy" />
        <el-table-column type="expand" width="40">
          <template #default="{ row }">
            <el-form class="rtsp-options" label-width="160px" :disabled="busy || row.saved">
              <el-form-item><el-checkbox v-model="row.credentials.separateRtspCredentials" @change="clearRowError(row)">{{ t('onvif.separateRtsp') }}</el-checkbox></el-form-item>
              <template v-if="row.credentials.separateRtspCredentials">
                <el-form-item :label="t('onvif.rtspUsername')" required><el-input v-model.trim="row.credentials.rtspUsername" autocomplete="off" @input="clearRowError(row)" /></el-form-item>
                <el-form-item :label="t('onvif.rtspPassword')" required><el-input v-model="row.credentials.rtspPassword" type="password" show-password autocomplete="new-password" @input="clearRowError(row)" /></el-form-item>
              </template>
            </el-form>
          </template>
        </el-table-column>
        <el-table-column :label="t('onvif.channelName')" min-width="220"><template #default="{ row }">
          <el-input v-model="row.channelName" :disabled="busy || row.saved" @input="clearRowError(row)" />
          <div v-if="row.error" class="error" role="alert">{{ errorText(row.error) }}</div>
          <span v-else-if="row.saved" class="form-hint">{{ t('onvif.added') }}</span>
        </template></el-table-column>
        <el-table-column :label="t('onvif.ipAddress')" min-width="140"><template #default="{ row }">{{ deviceAddress(row.endpoint) }}</template></el-table-column>
        <el-table-column :label="t('onvif.username')" min-width="160"><template #default="{ row }"><el-input v-model.trim="row.credentials.username" :disabled="busy || row.saved" autocomplete="off" @input="clearRowError(row)" /></template></el-table-column>
        <el-table-column :label="t('onvif.password')" min-width="180"><template #default="{ row }"><el-input v-model="row.credentials.password" :disabled="busy || row.saved" type="password" show-password autocomplete="new-password" @input="clearRowError(row)" /></template></el-table-column>
      </el-table>
      <p v-else class="form-hint">{{ t('onvif.searchEmpty') }}</p>
      <p v-if="rows.length" class="form-hint">{{ t('onvif.selectionHint') }}</p>
    </template>
    <el-alert v-if="message" :title="message" :type="messageType" :closable="false" show-icon />
    <template #footer>
      <el-button :disabled="busy" @click="close">{{ t('action.close') }}</el-button>
      <el-button type="primary" :disabled="busy || !formReady || (mode === 'auto' && !selection.length)" :loading="busy" @click="submit">
        {{ editing ? t('onvif.saveChanges') : mode === 'manual' ? t('onvif.addChannel') : t('onvif.saveSelected') }}
      </el-button>
      <el-button v-if="busy" @click="cancelPending">{{ t('onvif.cancelPending') }}</el-button>
    </template>
  </el-dialog>
</template>

<script setup>
import { reactive, ref, watch } from 'vue'
import { request } from '@/utils/request'
import { t } from '@/i18n'
import { deviceAddress, deviceName, saveDevice, runDeviceBatch } from './onvifAccess.js'

const emit = defineEmits(['saved'])
const visible = ref(false)
const editing = ref(false)
const mode = ref('manual')
const busy = ref(false)
const formReady = ref(false)
const rows = ref([])
const selection = ref([])
const table = ref()
const message = ref('')
const messageType = ref('error')
const manual = ref({})
const credentials = reactive({ username: '', password: '', separateRtspCredentials: false, rtspUsername: '', rtspPassword: '' })
let generation = 0
let sequence = 0
const makeRow = item => reactive({ key: ++sequence, channelName: '', endpoint: '', profile: null, profileToken: '', error: '', saved: false, pending: false, ...item })
const errorText = code => {
  const errors = {
    unauthorized: t('onvif.unauthorized'), timeout: t('onvif.timeout'), network_error: t('onvif.networkError'),
    invalid_endpoint: t('onvif.invalidEndpoint'), no_interface: t('onvif.noInterface'), no_profiles: t('onvif.noProfiles'),
    no_profile_selected: t('onvif.noProfiles'), duplicate_source: t('onvif.duplicate'), busy: t('onvif.busy'),
    storage_error: t('onvif.storageError'), source_not_found: t('onvif.sourceNotFound'),
    service_unavailable: t('onvif.unsupported'), soap_fault: t('onvif.unsupported'), invalid_xml: t('onvif.unsupported'),
    unsupported_stream: t('onvif.unsupportedStream'), invalid_parameter: t('onvif.invalidParameter'), channel_save_failed: t('onvif.saveFailed'),
    credentials_required: t('onvif.credentialsRequired'), rtsp_credentials_required: t('onvif.rtspCredentialsRequired')
  }
  return errors[code] || t('onvif.failed')
}
const api = async (operation, data = {}) => {
  const response = await request({ url: `/gtw/cwai/Camera/${operation}`, method: 'post', data, timeout: 25000 })
  if (!response.resData?.success) throw new Error(response.resData?.error || 'failed')
  return response.resData
}
function showError(code) { messageType.value = 'error'; message.value = errorText(code) }
function clearRowError(row) { row.error = ''; row.profile = null; message.value = '' }
function clearSecrets(row) {
  if (row.credentials) { row.credentials.password = ''; row.credentials.rtspPassword = '' }
}
watch(() => [credentials.username, credentials.password, credentials.separateRtspCredentials, credentials.rtspUsername, credentials.rtspPassword], () => {
  // Never authenticate on each keystroke: partial passwords can lock camera accounts.
  message.value = ''
})
async function open(camera) {
  const token = ++generation
  editing.value = !!camera
  formReady.value = !camera
  mode.value = 'manual'
  rows.value.forEach(clearSecrets)
  rows.value = []; selection.value = []; message.value = ''
  manual.value = makeRow({})
  Object.assign(credentials, { username: '', password: '', separateRtspCredentials: false, rtspUsername: '', rtspPassword: '' })
  visible.value = true
  busy.value = true
  try {
    if (camera) {
      const config = await api('OnvifGet', { source: camera.url })
      if (token !== generation) return
      Object.assign(credentials, { username: config.username, rtspUsername: config.rtspUsername, separateRtspCredentials: config.separateRtspCredentials })
      manual.value = makeRow({ ...config, videoChannelId: camera.videoChannelId, channelName: camera.channelName })
      formReady.value = true
    }
  } catch (error) { showError(error.message) }
  finally { busy.value = false }
}
function close(done) {
  if (busy.value) return
  ++generation
  visible.value = false
  Object.assign(credentials, { username: '', password: '', separateRtspCredentials: false, rtspUsername: '', rtspPassword: '' })
  rows.value.forEach(clearSecrets)
  rows.value = []; selection.value = []; manual.value = makeRow({})
  if (typeof done === 'function') done()
}
function cancelPending() { ++generation; messageType.value = 'info'; message.value = t('onvif.cancelNotice') }
function credentialError(account, row) {
  if (!account.username || ((!row.source || !row.hasPassword) && !account.password)) return 'credentials_required'
  if (account.separateRtspCredentials && (!account.rtspUsername || ((!row.source || !row.hasRtspPassword) && !account.rtspPassword))) return 'rtsp_credentials_required'
  return ''
}
async function discover() {
  if (busy.value || !formReady.value) return
  const token = generation
  const active = () => token === generation
  busy.value = true; message.value = ''
  try {
    const result = await api('OnvifDiscover', { interfaceAddress: '', timeoutMs: 2500 })
    if (!active()) return
    for (const item of result.rows) {
      let row = rows.value.find(row => (item.uuid && row.uuid === item.uuid) || row.endpoint === item.endpoint)
      if (!row) {
        row = makeRow({ ...item, channelName: deviceName(item), credentials: {
          username: 'admin', password: 'admin', separateRtspCredentials: false, rtspUsername: '', rtspPassword: ''
        } })
        rows.value.push(row)
      } else if (!row.saved) {
        // Refresh addresses after DHCP changes without discarding an edited name.
        row.endpoint = item.endpoint
        row.endpoints = item.endpoints || []
      }
    }
    if (!result.rows.length) { messageType.value = 'info'; message.value = t('onvif.noDevices') }
    // Never try the prefilled credentials during discovery; only Add authenticates.
  } catch (error) { if (active()) showError(error.message) }
  finally { busy.value = false }
}
async function submit() {
  if (busy.value || !formReady.value) return
  const selected = mode.value === 'manual' ? [manual.value] : [...selection.value].filter(row => !row.saved)
  if (!selected.length) return
  const token = generation
  const active = () => token === generation
  busy.value = true; message.value = ''
  try {
    await runDeviceBatch(selected, async row => {
      const account = mode.value === 'manual' ? credentials : row.credentials
      const error = credentialError(account, row)
      if (error) throw new Error(error)
      if (!row.channelName.trim() || !row.endpoint) throw new Error('invalid_parameter')
      await saveDevice(api, row, { ...account }, active)
      if (row.saved) { clearSecrets(row); table.value?.toggleRowSelection(row, false); emit('saved') }
    }, active)
    if (!active()) return
    if (mode.value === 'manual') {
      if (manual.value.error) showError(manual.value.error)
      else if (manual.value.saved) { busy.value = false; close() }
    } else {
      const added = selected.filter(row => row.saved).length
      const failed = selected.filter(row => row.error).length
      messageType.value = failed ? 'error' : 'success'
      message.value = t('onvif.batchSummary', { added, failed })
    }
  } finally { busy.value = false }
}
defineExpose({ open })
</script>

<style scoped>
.onvif-form { margin-top: 20px; }
.onvif-form .el-input { max-width: 540px; }
.form-hint { color: var(--el-text-color-secondary); font-size: 13px; line-height: 1.6; }
.onvif-form .form-hint { margin-left: 12px; }
.error { color: var(--el-color-danger); }
.discovery-toolbar { display: flex; align-items: center; gap: 12px; margin: 20px 0; }
.rtsp-options { margin: 12px; max-width: 600px; }
</style>
