<template>
  <el-dialog v-model="visible" :title="t('gbAccess.title')" width="min(1060px, calc(100vw - 48px))" :before-close="close" :close-on-click-modal="false" destroy-on-close class="ui-admin-dialog ui-scroll-dialog gb-dialog">
    <el-alert v-if="message" :title="message" :type="messageType" :closable="false" show-icon class="gb-message" />
    <el-tabs v-model="tab" class="gb-tabs">
      <el-tab-pane :label="t('gbAccess.platformTab')" name="platform">
        <el-form label-position="top" :disabled="busy || !ready" class="gb-platform-form">
          <div class="gb-service-row">
            <el-form-item :label="t('gbAccess.enabled')" class="gb-enable"><el-switch v-model="platform.enabled" :aria-label="t('gbAccess.enabled')" /></el-form-item>
            <span v-if="ready" class="gb-state" :class="listening ? 'gb-state--success' : 'gb-state--muted'"><i aria-hidden="true" />{{ listening ? t('gbAccess.listening') : t('gbAccess.notListening') }}</span>
          </div>
          <el-alert v-if="serviceError" :title="errorText(serviceError)" type="error" :closable="false" show-icon class="gb-message" />
          <div class="gb-form-grid">
            <el-form-item :label="t('gbAccess.platformId')"><el-input v-model.trim="platform.platformId" maxlength="20" class="gb-code-input" /></el-form-item>
            <el-form-item :label="t('gbAccess.realm')"><el-input v-model.trim="platform.realm" maxlength="10" class="gb-code-input" /></el-form-item>
            <el-form-item class="gb-span-all">
              <template #label><span class="gb-label-help">{{ t('gbAccess.address') }}<el-tooltip :content="t('gbAccess.addressTip')" placement="top" :show-after="200"><button type="button" class="gb-help" :aria-label="t('gbAccess.addressTip')">?</button></el-tooltip></span></template>
              <div class="gb-address">
                <el-select v-model="addressChoice" :loading="addressLoading" @change="chooseAddress">
                  <el-option value="auto" :label="t('gbAccess.autoAddress')" />
                  <el-option v-for="item in addresses" :key="item.address" :value="item.address" :label="addressLabel(item)" />
                  <el-option v-if="addressChoice === 'manual'" value="manual" :label="t('gbAccess.manualAddress')" />
                </el-select>
                <el-alert v-if="addressWarning" :title="addressWarning" type="warning" :closable="false" class="gb-inline-alert" />
                <el-collapse v-model="addressAdvanced" class="gb-advanced">
                  <el-collapse-item :title="t('gbAccess.addressAdvanced')" name="address">
                    <el-checkbox :model-value="addressChoice === 'manual'" @change="enableManualAddress">{{ t('gbAccess.manualAddress') }}</el-checkbox>
                    <el-input v-if="addressChoice === 'manual'" v-model.trim="platform.address" :placeholder="t('gbAccess.manualAddressPlaceholder')" />
                    <p class="gb-hint">{{ t('gbAccess.manualAddressTip') }}</p>
                  </el-collapse-item>
                </el-collapse>
              </div>
            </el-form-item>
            <el-form-item :label="t('gbAccess.sipPort')"><el-input-number v-model="platform.sipPort" :min="1024" :max="65535" :precision="0" /></el-form-item>
            <el-form-item :label="t('gbAccess.heartbeatSetting')"><el-input-number v-model="platform.heartbeatTimeout" :min="15" :max="3600" :precision="0" /></el-form-item>
          </div>
        </el-form>
      </el-tab-pane>
      <el-tab-pane :label="t('gbAccess.devicesTab')" name="devices">
        <div class="gb-tools">
          <span class="gb-device-count">{{ t('gbAccess.deviceCount', { count: devices.length }) }}</span>
          <div class="gb-tool-actions">
            <el-tooltip :content="t('gbAccess.deviceTip')" placement="top" :show-after="200"><button type="button" class="gb-help" :aria-label="t('gbAccess.deviceTip')">?</button></el-tooltip>
            <el-button :disabled="busy" @click="refresh">{{ t('action.refresh') }}</el-button>
            <el-button type="primary" :disabled="busy || !ready || deviceEditorVisible" @click="startDevice">{{ t('gbAccess.registerDevice') }}</el-button>
          </div>
        </div>
        <section v-if="deviceEditorVisible" ref="deviceEditor" class="gb-device-editor" :aria-label="editing ? t('gbAccess.editDevice') : t('gbAccess.registerDevice')">
          <h3>{{ editing ? t('gbAccess.editDevice') : t('gbAccess.registerDevice') }}</h3>
          <el-form label-position="top" :disabled="busy || !ready" class="gb-device-form" @submit.prevent="saveDevice">
            <div class="gb-form-grid">
              <el-form-item :label="t('gbAccess.deviceId')"><el-input v-model.trim="deviceForm.id" :disabled="editing" maxlength="20" class="gb-code-input" /></el-form-item>
              <el-form-item :label="t('gbAccess.password')"><el-input v-model="deviceForm.password" type="password" show-password autocomplete="new-password" :placeholder="editing ? t('gbAccess.keepPassword') : t('gbAccess.passwordHint')" /></el-form-item>
            </div>
            <el-collapse v-model="deviceAdvanced" class="gb-advanced">
              <el-collapse-item :title="t('gbAccess.advanced')" name="advanced">
                <div class="gb-form-grid">
                  <el-form-item :label="t('gbAccess.authId')"><el-input v-model.trim="deviceForm.username" :placeholder="t('gbAccess.sameAsId')" /></el-form-item>
                  <el-form-item :label="t('gbAccess.mediaTransport')"><el-select v-model="deviceForm.mediaTransport"><el-option value="tcp" label="TCP" /><el-option value="udp" label="UDP" /></el-select></el-form-item>
                </div>
                <p class="gb-hint">{{ t('gbAccess.transportTip') }}</p>
                <el-checkbox v-model="deviceForm.allowUnauthenticated">{{ t('gbAccess.noAuth') }}</el-checkbox>
                <el-alert v-if="deviceForm.allowUnauthenticated" :title="t('gbAccess.noAuthWarning')" type="warning" :closable="false" show-icon />
              </el-collapse-item>
            </el-collapse>
            <div class="gb-editor-actions"><el-button :disabled="busy" @click="resetDevice">{{ t('action.cancel') }}</el-button><el-button type="primary" :loading="busy" @click="saveDevice">{{ editing ? t('action.save') : t('action.add') }}</el-button></div>
          </el-form>
        </section>
        <el-table :data="devices" row-key="id" :expand-row-keys="expandedDevices" @expand-change="rememberExpanded" :empty-text="t('gbAccess.emptyDeviceList')" class="gb-device-table">
          <el-table-column type="expand" width="38">
            <template #default="scope">
              <div class="gb-channel-region">
                <div class="gb-channel-heading"><strong>{{ t('gbAccess.channelCount', { count: scope.row.channels.length }) }}</strong><el-tooltip :content="t('gbAccess.mediaNote')" placement="top" :show-after="200"><button type="button" class="gb-help" :aria-label="t('gbAccess.mediaNote')">?</button></el-tooltip></div>
                <el-table :data="scope.row.channels" :empty-text="t('gbAccess.emptyCatalog')" class="gb-channels">
                  <el-table-column :label="t('gbAccess.channelId')" min-width="215"><template #default="channel"><span class="gb-code">{{ channel.row.id }}</span></template></el-table-column>
                  <el-table-column :label="t('field.channelName')" min-width="180"><template #default="channel"><span v-if="channel.row.added" class="gb-channel-name">{{ channel.row.channelName }}</span><el-input v-else v-model="channel.row.channelName" :disabled="busy" maxlength="80" :aria-label="t('field.channelName')" /></template></el-table-column>
                  <el-table-column :label="t('field.channelStatus')" min-width="180"><template #default="channel"><span class="gb-state" :class="'gb-state--' + statusTone(channel.row.state)"><i aria-hidden="true" />{{ stateText(channel.row.state) }}</span><div v-if="channel.row.error" class="gb-error">{{ errorText(channel.row.error) }}</div></template></el-table-column>
                  <el-table-column :label="t('field.actions')" width="130"><template #default="channel"><span v-if="channel.row.added" class="gb-added">{{ t('gbAccess.alreadyAdded') }}</span><el-button v-else :disabled="busy || !scope.row.online || !!channel.row.error" link type="primary" @click="addChannel(scope.row, channel.row)">{{ t('gbAccess.addChannel') }}</el-button></template></el-table-column>
                </el-table>
              </div>
            </template>
          </el-table-column>
          <el-table-column :label="t('gbAccess.deviceId')" min-width="218"><template #default="scope"><span class="gb-code">{{ scope.row.id }}</span><span class="gb-channel-total">{{ t('gbAccess.channelCount', { count: scope.row.channels.length }) }}</span></template></el-table-column>
          <el-table-column :label="t('gbAccess.deviceAddress')" prop="ip" min-width="130" />
          <el-table-column :label="t('field.channelStatus')" min-width="165"><template #default="scope"><span class="gb-state" :class="'gb-state--' + statusTone(scope.row.state)"><i aria-hidden="true" />{{ stateText(scope.row.state) }}</span><div v-if="scope.row.error" class="gb-error">{{ errorText(scope.row.error) }}</div></template></el-table-column>
          <el-table-column :label="t('field.actions')" width="245"><template #default="scope"><div class="gb-row-actions"><el-button link type="primary" :disabled="busy || !scope.row.online" @click="queryCatalog(scope.row)">{{ t('gbAccess.queryCatalog') }}</el-button><el-button link type="primary" :disabled="busy" @click="editDevice(scope.row)" class="ui-action ui-action-edit">{{ t('action.edit') }}</el-button><el-button link type="danger" :disabled="busy" @click="removeDevice(scope.row)" class="ui-action ui-action-delete">{{ t('action.delete') }}</el-button></div></template></el-table-column>
        </el-table>
      </el-tab-pane>
    </el-tabs>
    <template #footer><div class="gb-footer"><span v-if="tab === 'platform'" class="gb-save-warning">{{ t('gbAccess.saveWarning') }}</span><div class="gb-footer-actions"><el-button :disabled="busy" @click="close">{{ t('action.close') }}</el-button><el-button v-if="tab === 'platform'" type="primary" :disabled="!ready" :loading="busy" @click="savePlatform">{{ t('action.save') }}</el-button></div></div></template>
  </el-dialog>
</template>

<script setup>
import { nextTick, onBeforeUnmount, reactive, ref } from 'vue'
import { ElMessageBox } from 'element-plus'
import { request } from '@/utils/request'
import { t } from '@/i18n'
import { deviceRequest, errorText, isServerAddress, mergeDevices, serverAddresses, stateText } from './gb28181Access.js'
const emit = defineEmits(['saved'])
const visible = ref(false), busy = ref(false), ready = ref(false), tab = ref('devices'), devices = ref([])
const deviceEditor = ref(null), deviceEditorVisible = ref(false), deviceAdvanced = ref([]), expandedDevices = ref([])
const message = ref(''), messageType = ref('info'), listening = ref(false), serviceError = ref(''), editing = ref(false)
const platform = reactive({ enabled: false, platformId: '', realm: '', address: '', sipPort: 5060, heartbeatTimeout: 180 })
const deviceForm = reactive({ id: '', username: '', password: '', hasPassword: false, allowUnauthenticated: false, mediaTransport: 'tcp' })
const browserHost = window.location.hostname
const addresses = ref([]), addressChoice = ref('auto'), addressLoading = ref(false), addressWarning = ref(''), addressAdvanced = ref([])
let timer, generation = 0, refreshing = false
function addressLabel(item) { return `${item.address}${item.name ? ` (${item.name})` : ''}${item.address === browserHost ? ` — ${t('gbAccess.loginAddress')}` : ''}` }
function chooseAddress(value) { platform.address = value === 'manual' ? platform.address : value === 'auto' ? '' : value }
function enableManualAddress(enabled) {
  addressChoice.value = enabled ? 'manual' : 'auto'
  if (!enabled) platform.address = ''
}
async function loadAddresses() {
  const token = generation
  addressLoading.value = true
  try {
    const result = await request({ url: '/gtw/cwai/network/QueryNetCard', method: 'get', timeout: 10000 })
    if (token !== generation) return
    if (!Array.isArray(result.resData?.netCardList)) throw new Error('network_unavailable')
    addresses.value = serverAddresses(result.resData.netCardList)
    if (!addresses.value.length) addressWarning.value = t('gbAccess.noAddresses')
  } catch { if (token === generation) addressWarning.value = t('gbAccess.addressLoadFailed') }
  finally { if (token === generation) addressLoading.value = false }
}
async function api(data) {
  const response = await request({ url: '/gtw/cwai/Camera/Gb28181Manage', method: 'post', data, timeout: 30000 })
  if (!response.resData?.success) throw new Error(response.resData?.error || 'service_unavailable')
  return response.resData
}
function fail(error) { messageType.value = 'error'; message.value = errorText(error.message) }
function resetDevice() { deviceEditorVisible.value = false; deviceAdvanced.value = []; editing.value = false; Object.assign(deviceForm, { id: '', username: '', password: '', hasPassword: false, allowUnauthenticated: false, mediaTransport: 'tcp' }) }
function revealDeviceEditor() { nextTick(() => deviceEditor.value?.scrollIntoView({ block: 'nearest', behavior: 'smooth' })) }
function startDevice() { resetDevice(); deviceEditorVisible.value = true; revealDeviceEditor() }
function rememberExpanded(_row, rows) { expandedDevices.value = rows.map(row => row.id) }
function statusTone(state) {
  if (['registered', 'receiving'].includes(state)) return 'success'
  if (['catalog_query', 'inviting', 'waiting_media'].includes(state)) return 'progress'
  if (['auth_failed', 'catalog_failed', 'stream_failed', 'invite_failed', 'unsupported_transport', 'channel_not_found'].includes(state)) return 'error'
  return 'muted'
}
async function refresh(loadPlatform = false) {
  if (refreshing || !visible.value) return
  const token = generation
  refreshing = true
  try {
    const result = await api({ action: 'list' })
    if (token !== generation) return
    if (loadPlatform === true) for (const key of Object.keys(platform)) platform[key] = result[key]
    devices.value = mergeDevices(devices.value, result.devices)
    listening.value = result.listening; serviceError.value = result.error; ready.value = true
  } catch (error) { if (token === generation) fail(error) }
  finally { if (token === generation) refreshing = false }
}
async function open() {
  ++generation; refreshing = false; visible.value = true; ready.value = false; message.value = ''; devices.value = []; expandedDevices.value = []; resetDevice()
  addresses.value = []; addressWarning.value = ''; addressChoice.value = 'auto'; addressAdvanced.value = []
  const token = generation
  tab.value = 'devices'; busy.value = true
  try {
    await Promise.all([refresh(true), loadAddresses()])
    if (token !== generation) return
    addressChoice.value = !platform.address ? 'auto' : addresses.value.some(item => item.address === platform.address) ? platform.address : 'manual'
    if (addressChoice.value === 'manual') addressAdvanced.value = ['address']
    if (!listening.value) tab.value = 'platform'
  }
  finally { if (token === generation) busy.value = false }
  if (token !== generation) return
  clearInterval(timer)
  timer = setInterval(() => { if (!busy.value) refresh() }, 3000)
}
function close(done) {
  if (busy.value) return
  ++generation; clearInterval(timer); visible.value = false; resetDevice(); devices.value = []
  if (typeof done === 'function') done()
}
onBeforeUnmount(() => { ++generation; clearInterval(timer); resetDevice() })
async function perform(data, after = () => {}) {
  if (busy.value || !ready.value) return
  busy.value = true; message.value = ''
  try { await api(data); after(); messageType.value = 'success'; message.value = t('gbAccess.saved'); await refresh() }
  catch (error) { fail(error) }
  finally { busy.value = false }
}
async function savePlatform() {
  if (busy.value || !ready.value) return
  if ((addressChoice.value === 'manual' && !platform.address) || (platform.address && !isServerAddress(platform.address))) {
    messageType.value = 'error'; message.value = t('gbAccess.invalidAddress'); return
  }
  await perform({ action: 'savePlatform', ...platform })
}
async function saveDevice() {
  try { const data = deviceRequest(deviceForm); await perform(data, resetDevice) }
  catch (error) { fail(error) }
}
function editDevice(row) { deviceEditorVisible.value = true; revealDeviceEditor(); deviceAdvanced.value = row.allowUnauthenticated ? ['advanced'] : []; editing.value = true; Object.assign(deviceForm, { id: row.id, username: row.username, password: '', hasPassword: row.hasPassword, allowUnauthenticated: row.allowUnauthenticated, mediaTransport: row.mediaTransport || 'tcp' }) }
async function queryCatalog(row) { await perform({ action: 'catalog', id: row.id }) }
async function addChannel(device, channel) {
  if (!channel.channelName.trim()) { fail(new Error('invalid_parameter')); return }
  await perform({ action: 'addChannel', deviceId: device.id, channelId: channel.id, channelName: channel.channelName.trim() }, () => { channel.added = true; emit('saved') })
}
async function removeDevice(row) {
  try { await ElMessageBox.confirm(t('gbAccess.removeConfirm'), t('action.delete'), { confirmButtonText: t('action.confirm'), cancelButtonText: t('action.cancel'), type: 'warning' }) }
  catch { return }
  await perform({ action: 'removeDevice', id: row.id }, () => { if (deviceForm.id === row.id) resetDevice() })
}
defineExpose({ open })
</script>

<style scoped>
.gb-tabs :deep(.el-tabs__header) { margin: 0 0 16px; }
.gb-tabs :deep(.el-tabs__item) { font-size: 14px; }
.gb-message { margin-bottom: 14px; }
.gb-service-row { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 0 0 16px; margin-bottom: 16px; border-bottom: 1px solid var(--border-light); }
.gb-enable.el-form-item { display: flex; align-items: center; gap: 12px; margin-bottom: 0; }
.gb-enable :deep(.el-form-item__label) { margin: 0; height: auto; line-height: 32px; }
.gb-enable :deep(.el-form-item__content) { flex: none; }
.gb-form-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); column-gap: 24px; }
.gb-form-grid :deep(.el-form-item) { min-width: 0; margin-bottom: 16px; }
.gb-form-grid :deep(.el-form-item__label) { height: auto; margin-bottom: 6px; padding: 0; color: var(--text-secondary); font-size: 13px; line-height: 20px; }
.gb-form-grid :deep(.el-input), .gb-form-grid :deep(.el-select), .gb-form-grid :deep(.el-input-number) { width: 100%; }
.gb-span-all { grid-column: 1 / -1; }
.gb-code, .gb-code-input :deep(input) { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; font-variant-numeric: tabular-nums; }
.gb-code { font-size: 12px; white-space: nowrap; color: var(--text-primary); }
.gb-label-help, .gb-channel-heading { display: flex; align-items: center; gap: 8px; }
.gb-help { display: inline-flex; align-items: center; justify-content: center; width: 18px; height: 18px; border: 1px solid var(--border-color); border-radius: 50%; background: transparent; color: var(--text-secondary); font-family: inherit; font-size: 11px; line-height: 1; cursor: help; padding: 0; flex-shrink: 0; }
.gb-help:hover, .gb-help:focus-visible { color: var(--primary-color); border-color: var(--primary-color); }
.gb-address { width: 100%; }
.gb-inline-alert { margin-top: 8px; }
.gb-advanced { margin-top: 8px; border-bottom: 0; }
.gb-advanced :deep(.el-collapse-item__header) { height: 36px; color: var(--text-secondary); font-size: 12px; background: transparent; }
.gb-advanced :deep(.el-collapse-item__wrap) { background: transparent; }
.gb-advanced :deep(.el-collapse-item__content) { padding: 8px 0 12px; }
.gb-hint { margin: 6px 0 10px; color: var(--text-secondary); font-size: 12px; line-height: 1.6; }
.gb-tools, .gb-tool-actions, .gb-editor-actions, .gb-footer, .gb-footer-actions { display: flex; align-items: center; gap: 10px; }
.gb-tools { justify-content: space-between; margin-bottom: 14px; }
.gb-device-count { color: var(--text-primary); font-size: 13px; font-weight: 600; }
.gb-tool-actions :deep(.el-button + .el-button), .gb-editor-actions :deep(.el-button + .el-button), .gb-footer-actions :deep(.el-button + .el-button) { margin-left: 0; }
.gb-device-editor { margin-bottom: 16px; padding: 16px; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-subtle); }
.gb-device-editor h3 { margin: 0 0 12px; color: var(--text-primary); font-size: 14px; font-weight: 600; }
.gb-device-editor .gb-form-grid :deep(.el-form-item) { margin-bottom: 10px; }
.gb-editor-actions { justify-content: flex-end; padding-top: 12px; }
.gb-device-table { --el-table-header-bg-color: var(--bg-subtle); font-size: 13px; }
.gb-device-table :deep(.el-table__cell) { padding: 10px 0; }
.gb-device-table :deep(.el-table__expanded-cell) { padding: 0; }
.gb-device-table :deep(.el-table__expanded-cell:hover) { background: var(--bg-subtle) !important; }
.gb-channel-region { padding: 12px 14px 14px 30px; background: var(--bg-subtle); border-left: 2px solid var(--border-color); }
.gb-channel-heading { justify-content: space-between; margin: 0 0 8px; font-size: 12px; color: var(--text-secondary); }
.gb-channel-heading strong { font-weight: 500; }
.gb-channels { --el-table-header-bg-color: var(--bg-subtle); font-size: 12px; }
.gb-channels :deep(.el-table__cell) { padding: 8px 0; }
.gb-channels :deep(.el-input__wrapper) { padding: 0 8px; }
.gb-channel-total { display: block; margin-top: 2px; color: var(--text-secondary); font-size: 12px; line-height: 18px; }
.gb-channel-name { color: var(--text-primary); overflow-wrap: anywhere; }
.gb-added { color: var(--text-secondary); font-size: 12px; }
.gb-state { display: inline-flex; align-items: center; gap: 6px; line-height: 20px; font-size: 12px; }
.gb-state i { width: 6px; height: 6px; flex-shrink: 0; border-radius: 50%; background: currentColor; }
.gb-state--success { color: var(--theme-success, var(--el-color-success)); }
.gb-state--muted { color: var(--text-secondary); }
.gb-state--progress { color: var(--primary-color); }
.gb-state--error, .gb-error { color: var(--el-color-danger); }
.gb-error { margin-top: 3px; font-size: 12px; line-height: 18px; white-space: normal; overflow-wrap: anywhere; }
.gb-row-actions { display: flex; align-items: center; gap: 12px; white-space: nowrap; }
.gb-row-actions :deep(.el-button) { margin: 0; padding: 2px 0; font-size: 12px; }
.gb-footer { justify-content: flex-end; text-align: left; }
.gb-save-warning { margin-right: auto; color: var(--text-secondary); font-size: 12px; line-height: 18px; }
.gb-footer-actions { flex-shrink: 0; }
@media (max-width: 700px) {
  .gb-form-grid { grid-template-columns: minmax(0, 1fr); }
  .gb-service-row, .gb-tools, .gb-footer { flex-wrap: wrap; }
  .gb-save-warning { width: 100%; }
  .gb-channel-region { padding-left: 12px; }
}
</style>
