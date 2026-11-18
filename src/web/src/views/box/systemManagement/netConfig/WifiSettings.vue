<template>
  <section class="wifi-settings">
    <el-alert :title="t('wifi.accessNotice')" type="info" :closable="false" show-icon />
    <el-alert v-if="!status.serviceReady" :title="t('wifi.serviceUnavailable')" :description="reasonText(status.reason)" type="warning" :closable="false" show-icon />
    <div class="wifi-summary" aria-live="polite">
      <span><strong>{{ t('wifi.interface') }}</strong> {{ status.interfaceName || '—' }}</span>
      <span><strong>{{ t('wifi.connectionStatus') }}</strong> {{ stateLabel }}</span>
      <span v-if="status.ssid"><strong>SSID</strong> {{ status.ssid }}</span>
      <span><strong>{{ t('wifi.autoConnect') }}</strong> {{ status.autoConnect ? t('wifi.enabled') : t('wifi.disabled') }}</span>
      <span><strong>{{ t('wifi.currentEgress') }}</strong> {{ status.currentInterface || t('wifi.localOnly') }}</span>
      <span><strong>{{ t('wifi.actualAddress') }}</strong> <a v-if="currentUrl" :href="currentUrl" target="_blank" rel="noopener noreferrer">{{ status.currentAddress }}</a><template v-else>—</template></span>
      <span v-if="wifiAddress"><strong>{{ t('wifi.wifiAddress') }}</strong> {{ wifiAddress }}</span>
    </div>
    <el-alert v-if="status.localOnly" :title="t('wifi.localOnly')" type="warning" :closable="false" />
    <div v-if="operation.operationId" class="wifi-operation" aria-live="polite" role="status">
      <strong>{{ operationLabel }}</strong>
      <span v-if="operation.ipAddr">{{ t('wifi.wifiAddress') }} {{ operation.ipAddr }}</span>
      <span v-if="operation.error">{{ reasonText(operation.error) }}</span>
      <span v-if="operation.rollback">{{ t('wifi.rollbackResult') }} {{ reasonText(operation.rollback) }}</span>
      <template v-if="operation.stage === 'awaitingConfirmation'">
        <p>{{ t('wifi.confirmNotice', { seconds: operation.confirmSeconds ?? 90 }) }}</p>
        <el-button type="primary" :loading="submitting" :disabled="submitting" @click="confirmConfig">{{ t('wifi.keepConfig') }}</el-button>
      </template>
    </div>
    <el-alert v-if="errorMessage" :title="errorMessage" type="error" :closable="false" show-icon />
    <div class="wifi-actions">
      <el-button :disabled="busy" :loading="operation.stage === 'scanning' && !operation.done" @click="runOperation('scanWifi')">{{ t('wifi.scan') }}</el-button>
      <el-button :disabled="busy" @click="manualNetwork">{{ t('wifi.hiddenNetwork') }}</el-button>
      <el-button :disabled="busy || !status.hasCredentials" @click="runOperation('disconnectWifi')">{{ t('wifi.disconnect') }}</el-button>
      <el-button type="danger" plain :disabled="busy || !status.hasCredentials" @click="forgetNetwork">{{ t('wifi.forget') }}</el-button>
    </div>
    <el-table v-if="scanResults.length" :data="scanResults" :row-key="scanRowKey" class="wifi-scan-table">
      <el-table-column prop="ssid" label="SSID" min-width="160"><template #default="{ row }">{{ row.ssid || t('wifi.hiddenSsid') }}</template></el-table-column>
      <el-table-column prop="security" :label="t('wifi.security')" min-width="100" />
      <el-table-column :label="t('wifi.signal')" min-width="100"><template #default="{ row }">{{ row.signal }} dBm</template></el-table-column>
      <el-table-column :label="t('wifi.frequency')" min-width="120"><template #default="{ row }">{{ row.frequency }} MHz</template></el-table-column>
      <el-table-column :label="t('wifi.selectNetwork')" min-width="140"><template #default="{ row }"><el-button size="small" :disabled="busy || row.security !== 'wpa2' || !row.ssid" @click="selectNetwork(row)">{{ row.security === 'wpa2' ? t('wifi.selectNetwork') : t('wifi.unsupportedSecurity') }}</el-button></template></el-table-column>
    </el-table>
    <el-form class="wifi-form" :model="form" :disabled="busy" label-width="var(--settings-label-width)" label-position="right" size="small" @submit.prevent="connectNetwork">
      <el-form-item label="SSID" required><el-input v-model="form.ssid" autocomplete="off" :placeholder="t('wifi.ssidPlaceholder')" /></el-form-item>
      <el-form-item :label="t('wifi.hidden')"><el-switch v-model="form.hidden" /></el-form-item>
      <el-form-item :label="t('wifi.password')" :required="!canReusePassword">
        <el-input v-model="form.password" type="password" show-password autocomplete="new-password" :placeholder="canReusePassword ? t('wifi.passwordSaved') : t('wifi.passwordPlaceholder')" />
      </el-form-item>
      <el-form-item :label="t('systemManage.ipMethod')"><el-select v-model="form.dhcp"><el-option label="DHCP" :value="true" /><el-option :label="t('systemManage.staticIP')" :value="false" /></el-select></el-form-item>
      <template v-if="!form.dhcp">
        <el-form-item :label="t('systemManage.ipAddress')" required><el-input v-model="form.ipAddr" /></el-form-item>
        <el-form-item :label="t('systemManage.subnetMask')" required><el-input v-model="form.netMask" /></el-form-item>
        <el-form-item :label="t('systemManage.gateway')"><el-input v-model="form.gateway" /></el-form-item>
      </template>
      <el-form-item :label="t('wifi.autoDns')"><el-switch v-model="form.autoDns" :disabled="!form.dhcp" /><span v-if="!form.dhcp" class="wifi-dns-note">{{ t('wifi.staticDnsRequired') }}</span></el-form-item>
      <template v-if="!form.autoDns">
        <el-form-item label="DNS1" required><el-input v-model="form.dns1" /></el-form-item>
        <el-form-item label="DNS2"><el-input v-model="form.dns2" /></el-form-item>
      </template>
      <el-form-item><el-button type="primary" native-type="submit" :loading="submitting" @click.prevent="connectNetwork">{{ t('wifi.connect') }}</el-button></el-form-item>
    </el-form>
  </section>
</template>

<script setup>
import { computed, reactive, ref, watch, onBeforeUnmount, getCurrentInstance } from 'vue'
import { ElMessageBox } from 'element-plus'
import { t } from '@/i18n'
import { isWifiIpv4, wifiValidationError, wifiConnectionRequest } from './wifiValidation.js'
import { wifiReasonText, wifiRequestErrorText } from './wifiMessages.js'

const props = defineProps({ status: { type: Object, required: true } })
const emit = defineEmits(['busy', 'refresh'])
const { proxy } = getCurrentInstance()
const form = reactive({ ssid: '', password: '', hidden: false, dhcp: true, ipAddr: '', netMask: '255.255.255.0', gateway: '', autoDns: true, dns1: '', dns2: '' })
const operation = ref({})
const scanResults = ref([])
const submitting = ref(false)
const errorMessage = ref('')
let stopped = false
let querying = false
let initialized = false
let latestOperationId = ''
const busy = computed(() => submitting.value || Boolean(props.status.busy) || Boolean(operation.value.operationId && !operation.value.done))
const canReusePassword = computed(() => Boolean(props.status.hasCredentials && props.status.savedConfig?.ssid === form.ssid))
const wifiAddress = computed(() => props.status.interfaces?.find(item => item.interfaceName === props.status.interfaceName)?.ipAddr || '')
const currentUrl = computed(() => isWifiIpv4(props.status.currentAddress) ? `${window.location.protocol}//${props.status.currentAddress}${window.location.port ? ':' + window.location.port : ''}/#/boxLogin` : '')
const stageKeys = {
  scanning: 'wifi.scanning', connecting: 'wifi.connecting', addressing: 'wifi.addressing',
  awaitingConfirmation: 'wifi.awaitingConfirmation', succeeded: 'wifi.succeeded', failed: 'wifi.failed', rolledBack: 'wifi.rolledBack'
}
const operationLabel = computed(() => stageKeys[operation.value.stage] ? t(stageKeys[operation.value.stage]) : t('wifi.processing'))
const stateKeys = {
  disconnected: 'wifi.disconnected', connecting: 'wifi.connecting', addressing: 'wifi.addressing',
  awaitingConfirmation: 'wifi.awaitingConfirmation', standby: 'wifi.standby',
  connected: 'wifi.connected', unavailable: 'wifi.serviceUnavailable', serviceUnavailable: 'wifi.serviceUnavailable',
  disabled: 'wifi.disconnected', noAddress: 'wifi.addressing', noModule: 'wifi.noModule'
}
const stateLabel = computed(() => {
  if (!props.status.serviceReady) return t('wifi.serviceUnavailable')
  if (props.status.state === 'connected' && props.status.currentInterface !== props.status.interfaceName) return t('wifi.standby')
  return stateKeys[props.status.state] ? t(stateKeys[props.status.state]) : props.status.state
})
const reasonText = reason => wifiReasonText(reason, t)
const scanRowKey = row => `${row.bssid}:${row.ssid}:${row.security}`

watch(busy, value => emit('busy', value), { immediate: true })
watch(() => form.dhcp, dhcp => { if (!dhcp) form.autoDns = false }, { immediate: true })
watch(() => props.status, async status => {
  if (!initialized && status.savedConfig?.ssid) {
    const saved = status.savedConfig
    Object.assign(form, {
      ssid: saved.ssid, hidden: saved.hidden ?? false, dhcp: saved.dhcp ?? true,
      ipAddr: saved.ipAddr || '', netMask: saved.netMask || '255.255.255.0', gateway: saved.gateway || '',
      autoDns: saved.autoDns ?? true, dns1: saved.dns?.[0] || '', dns2: saved.dns?.[1] || ''
    })
    initialized = true
  }
  if (submitting.value) return
  const id = status.operationId || operation.value.operationId
  if (id && (id !== operation.value.operationId || !operation.value.done)) {
    latestOperationId = id
    await queryOperation(id)
  }
}, { immediate: true })

async function queryOperation(operationId) {
  if (querying || stopped) return
  querying = true
  try {
    const { resData } = await proxy.$API.queryWifiOperation({ operationId })
    if (stopped || operationId !== latestOperationId) return
    operation.value = resData
    if (resData.type === 'scan' && resData.done && resData.success) scanResults.value = resData.scanResults || []
  } catch (error) {
    if (!stopped) errorMessage.value = wifiRequestErrorText(error, t, 'wifi.statusQueryFailed')
  } finally {
    querying = false
  }
}

function selectNetwork(row) {
  form.ssid = row.ssid
  form.hidden = false
  form.password = ''
  errorMessage.value = ''
}
function manualNetwork() {
  form.ssid = ''
  form.password = ''
  form.hidden = true
  errorMessage.value = ''
}

async function runOperation(method, data) {
  if (busy.value || stopped) return false
  submitting.value = true
  errorMessage.value = ''
  try {
    const { resData } = await proxy.$API[method](data)
    if (stopped) return false
    latestOperationId = resData.operationId
    operation.value = { operationId: resData.operationId, type: method === 'scanWifi' ? 'scan' : method, stage: method === 'scanWifi' ? 'scanning' : method === 'connectWifi' ? 'connecting' : 'processing', done: false }
    await queryOperation(resData.operationId)
    emit('refresh')
    return true
  } catch (error) {
    if (!stopped) errorMessage.value = wifiRequestErrorText(error, t)
    return false
  } finally {
    submitting.value = false
  }
}
async function connectNetwork() {
  if (busy.value) return
  const error = wifiValidationError(form, canReusePassword.value)
  if (error) {
    errorMessage.value = t(error)
    return
  }
  const data = wifiConnectionRequest(form, canReusePassword.value)
  form.password = ''
  try {
    await runOperation('connectWifi', data)
  } finally {
    delete data.password
  }
}
async function confirmConfig() {
  if (submitting.value || operation.value.stage !== 'awaitingConfirmation') return
  submitting.value = true
  errorMessage.value = ''
  try {
    await proxy.$API.confirmWifiConfig({ operationId: operation.value.operationId })
    await queryOperation(operation.value.operationId)
    emit('refresh')
  } catch (error) {
    if (!stopped) errorMessage.value = wifiRequestErrorText(error, t)
  } finally {
    submitting.value = false
  }
}
async function forgetNetwork() {
  if (busy.value) return
  try {
    await ElMessageBox.confirm(t('wifi.forgetConfirm'), t('common.notice'), {
      confirmButtonText: t('wifi.forget'), cancelButtonText: t('action.cancel'), type: 'warning'
    })
    const accepted = await runOperation('forgetWifi')
    if (accepted && !stopped) {
      form.ssid = ''
      form.password = ''
    }
  } catch {}
}
onBeforeUnmount(() => { stopped = true; form.password = '' })
</script>

<style scoped lang="scss">
.wifi-settings { display: grid; gap: 16px; }
.wifi-summary, .wifi-operation, .wifi-form {
  padding: 20px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 6px;
}
.wifi-summary { display: grid; gap: 10px; overflow-wrap: anywhere; }
.wifi-summary strong { margin-right: 12px; color: var(--text-secondary); font-weight: 500; }
.wifi-summary a { color: var(--el-color-primary); }
.wifi-actions { display: flex; flex-wrap: wrap; gap: 8px; }
.wifi-actions .el-button { margin: 0; }
.wifi-form { max-width: 620px; }
.wifi-form :deep(.el-select) { width: 100%; }
.wifi-operation { display: grid; justify-items: start; gap: 10px; overflow-wrap: anywhere; }
.wifi-operation p { margin: 0; }
.wifi-dns-note { margin-left: 10px; color: var(--text-secondary); }
@media (max-width: 640px) {
  .wifi-summary, .wifi-operation, .wifi-form { padding: 16px; }
  .wifi-form :deep(.el-form-item) { flex-direction: column; }
  .wifi-form :deep(.el-form-item__label) { width: 100% !important; justify-content: flex-start; padding: 0 0 5px; }
}
</style>
