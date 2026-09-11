<template>
  <el-dialog v-model="visible" :title="t('gbAccess.title')" width="min(1060px, 95vw)" :before-close="close" :close-on-click-modal="false" destroy-on-close>
    <el-alert v-if="message" :title="message" :type="messageType" :closable="false" show-icon class="gb-message" />
    <el-tabs v-model="tab">
      <el-tab-pane :label="t('gbAccess.platformTab')" name="platform">
        <p>{{ t('gbAccess.platformTip') }}</p>
        <el-form label-width="180px" :disabled="busy || !ready">
          <el-form-item :label="t('gbAccess.enabled')"><el-switch v-model="platform.enabled" /></el-form-item>
          <el-form-item :label="t('gbAccess.platformId')"><el-input v-model.trim="platform.platformId" maxlength="20" /></el-form-item>
          <el-form-item :label="t('gbAccess.realm')"><el-input v-model.trim="platform.realm" maxlength="10" /></el-form-item>
          <el-form-item :label="t('gbAccess.address')">
            <div class="gb-address">
              <el-select v-model="addressChoice" :loading="addressLoading" @change="chooseAddress">
                <el-option value="auto" :label="t('gbAccess.autoAddress')" />
                <el-option v-for="item in addresses" :key="item.address" :value="item.address" :label="addressLabel(item)" />
                <el-option v-if="addressChoice === 'manual'" value="manual" :label="t('gbAccess.manualAddress')" />
              </el-select>
              <p>{{ t('gbAccess.addressTip') }}</p>
              <el-alert v-if="addressWarning" :title="addressWarning" type="warning" :closable="false" />
              <el-collapse v-model="addressAdvanced">
                <el-collapse-item :title="t('gbAccess.addressAdvanced')" name="address">
                  <el-checkbox :model-value="addressChoice === 'manual'" @change="enableManualAddress">{{ t('gbAccess.manualAddress') }}</el-checkbox>
                  <el-input v-if="addressChoice === 'manual'" v-model.trim="platform.address" :placeholder="t('gbAccess.manualAddressPlaceholder')" />
                  <p>{{ t('gbAccess.manualAddressTip') }}</p>
                </el-collapse-item>
              </el-collapse>
            </div>
          </el-form-item>
          <el-form-item :label="t('gbAccess.sipPort')"><el-input-number v-model="platform.sipPort" :min="1024" :max="65535" :precision="0" /></el-form-item>
          <el-form-item :label="t('gbAccess.heartbeatSetting')"><el-input-number v-model="platform.heartbeatTimeout" :min="15" :max="3600" :precision="0" /></el-form-item>
          <el-form-item><el-button type="primary" :loading="busy" @click="savePlatform">{{ t('action.save') }}</el-button></el-form-item>
        </el-form>
        <el-alert :title="listening ? t('gbAccess.listening') : t('gbAccess.notListening')" :type="listening ? 'success' : 'warning'" :closable="false" />
        <p v-if="serviceError" class="gb-error">{{ errorText(serviceError) }}</p>
      </el-tab-pane>
      <el-tab-pane :label="t('gbAccess.devicesTab')" name="devices">
        <p>{{ t('gbAccess.deviceTip') }}</p>
        <el-form inline :disabled="busy || !ready" class="gb-device-form" @submit.prevent="saveDevice">
          <el-form-item :label="t('gbAccess.deviceId')"><el-input v-model.trim="deviceForm.id" :disabled="editing" maxlength="20" /></el-form-item>
          <el-form-item :label="t('gbAccess.password')"><el-input v-model="deviceForm.password" type="password" show-password autocomplete="new-password" :placeholder="editing ? t('gbAccess.keepPassword') : t('gbAccess.passwordHint')" /></el-form-item>
          <el-form-item><el-button type="primary" :loading="busy" @click="saveDevice">{{ editing ? t('action.save') : t('action.add') }}</el-button><el-button v-if="editing" @click="resetDevice">{{ t('action.cancel') }}</el-button></el-form-item>
          <el-collapse>
            <el-collapse-item :title="t('gbAccess.advanced')" name="advanced">
              <el-form-item :label="t('gbAccess.authId')"><el-input v-model.trim="deviceForm.username" :placeholder="t('gbAccess.sameAsId')" /></el-form-item>
              <el-checkbox v-model="deviceForm.allowUnauthenticated">{{ t('gbAccess.noAuth') }}</el-checkbox>
              <el-alert v-if="deviceForm.allowUnauthenticated" :title="t('gbAccess.noAuthWarning')" type="warning" :closable="false" />
            </el-collapse-item>
          </el-collapse>
        </el-form>
        <div class="gb-tools"><el-button :disabled="busy" @click="refresh">{{ t('action.refresh') }}</el-button><span>{{ t('gbAccess.deviceListTip') }}</span></div>
        <el-table :data="devices" row-key="id" default-expand-all :empty-text="t('gbAccess.empty')">
          <el-table-column type="expand">
            <template #default="scope">
              <el-table :data="scope.row.channels" class="gb-channels" :empty-text="t('gbAccess.emptyCatalog')">
                <el-table-column :label="t('gbAccess.channelId')" prop="id" min-width="195" />
                <el-table-column :label="t('field.channelName')" min-width="190"><template #default="channel"><el-input v-model="channel.row.channelName" :disabled="channel.row.added || busy" maxlength="80" /></template></el-table-column>
                <el-table-column :label="t('field.channelStatus')" min-width="160"><template #default="channel"><span>{{ stateText(channel.row.state) }}</span><div v-if="channel.row.error" class="gb-error">{{ errorText(channel.row.error) }}</div></template></el-table-column>
                <el-table-column :label="t('field.actions')" width="130"><template #default="channel"><el-button :disabled="busy || channel.row.added || !scope.row.online || !!channel.row.error" link type="primary" @click="addChannel(scope.row, channel.row)">{{ channel.row.added ? t('gbAccess.alreadyAdded') : t('gbAccess.addChannel') }}</el-button></template></el-table-column>
              </el-table>
            </template>
          </el-table-column>
          <el-table-column :label="t('gbAccess.deviceId')" prop="id" min-width="195" />
          <el-table-column :label="t('gbAccess.deviceAddress')" prop="ip" min-width="130" />
          <el-table-column :label="t('field.channelStatus')" min-width="180"><template #default="scope"><span>{{ stateText(scope.row.state) }}</span><div v-if="scope.row.error" class="gb-error">{{ errorText(scope.row.error) }}</div></template></el-table-column>
          <el-table-column :label="t('field.actions')" width="220"><template #default="scope"><el-button link type="primary" :disabled="busy || !scope.row.online" @click="queryCatalog(scope.row)">{{ t('gbAccess.queryCatalog') }}</el-button><el-button link type="primary" :disabled="busy" @click="editDevice(scope.row)">{{ t('action.edit') }}</el-button><el-button link type="danger" :disabled="busy" @click="removeDevice(scope.row)">{{ t('action.delete') }}</el-button></template></el-table-column>
        </el-table>
        <p>{{ t('gbAccess.mediaNote') }}</p>
      </el-tab-pane>
    </el-tabs>
    <template #footer><el-button :disabled="busy" @click="close">{{ t('action.close') }}</el-button></template>
  </el-dialog>
</template>

<script setup>
import { onBeforeUnmount, reactive, ref } from 'vue'
import { ElMessageBox } from 'element-plus'
import { request } from '@/utils/request'
import { t } from '@/i18n'
import { deviceRequest, errorText, isServerAddress, mergeDevices, serverAddresses, stateText } from './gb28181Access.js'
const emit = defineEmits(['saved'])
const visible = ref(false), busy = ref(false), ready = ref(false), tab = ref('devices'), devices = ref([])
const message = ref(''), messageType = ref('info'), listening = ref(false), serviceError = ref(''), editing = ref(false)
const platform = reactive({ enabled: false, platformId: '', realm: '', address: '', sipPort: 5060, heartbeatTimeout: 180 })
const deviceForm = reactive({ id: '', username: '', password: '', hasPassword: false, allowUnauthenticated: false })
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
function resetDevice() { editing.value = false; Object.assign(deviceForm, { id: '', username: '', password: '', hasPassword: false, allowUnauthenticated: false }) }
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
  ++generation; refreshing = false; visible.value = true; ready.value = false; message.value = ''; devices.value = []; resetDevice()
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
function editDevice(row) { editing.value = true; Object.assign(deviceForm, { id: row.id, username: row.username, password: '', hasPassword: row.hasPassword, allowUnauthenticated: row.allowUnauthenticated }) }
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
.gb-message, .gb-tools { margin: 12px 0; }
.gb-tools { display: flex; gap: 16px; align-items: center; }
.gb-error { color: var(--el-color-danger); font-size: 12px; white-space: normal; }
.gb-channels { padding: 0 18px 14px 38px; }
.gb-device-form .el-collapse { width: 100%; margin-bottom: 16px; }
.gb-device-form .el-input { width: 230px; }
.gb-address { width: 100%; }
.gb-address .el-select { width: 100%; }
.gb-address p { margin: 6px 0; color: var(--el-text-color-secondary); line-height: 1.6; }
</style>
