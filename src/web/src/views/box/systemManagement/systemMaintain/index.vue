<template>
  <div class="system-maintain ui-admin-page">
    <el-tabs v-model="activeTab">
      <el-tab-pane :label="t('systemManage.softwareUpgrade')" name="upgrade">
        <div class="upgrade-container">
          <div class="upgrade-form">
            <div class="form-item">
              <span class="label">{{ t('systemManage.localUpgrade') }}</span>
              <el-input v-model="fileName" :placeholder="t('systemManage.selectUpgradeFile')" readonly class="file-input" size="small" @click="handleClickInput">
              </el-input>
              <el-upload ref="upload" class="upload-btn" action="#" :auto-upload="false" :show-file-list="false" :before-upload="beforeUpload" :on-change="handleFileChange" accept=".tar.gz">
                <el-button size="small">{{ t('systemManage.browse') }}</el-button>
              </el-upload>
              <el-button size="small" type="primary" @click="handleUpgrade" class="ui-secondary-button">{{ t('systemManage.upgrade') }}</el-button>
            </div>

            <div class="tips">
              <p class="tip-item">{{ t('systemManage.upgradeTip1') }}</p>
              <p class="tip-item">{{ t('systemManage.upgradeTip2') }}</p>
            </div>

          </div>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.restoreSettings')" name="reset">
        <div class="reset-container">
          <el-button type="primary" size="small" @click="handleReset" class="ui-secondary-button">{{ t('systemManage.restoreFactory') }}</el-button>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.deviceLog')" name="log">
        <div class="reset-container">
          <el-button type="primary" size="small" @click="downloadLog" class="ui-secondary-button">{{ t('systemManage.downloadDeviceLog') }}</el-button>
        </div>
      </el-tab-pane>
      <el-tab-pane v-if="authorization.supported" :label="t('systemManage.modelAuthorization')" name="authorization">
        <div class="authorization-container">
          <el-descriptions :column="1" border>
            <el-descriptions-item :label="t('systemManage.authorizationStatus')">
              <el-tag :type="authorization.authorized ? 'success' : 'warning'">
                {{ authorization.authorized ? t('systemManage.authorized') : t('systemManage.notAuthorized') }}
              </el-tag>
            </el-descriptions-item>
          </el-descriptions>
          <div class="authorization-actions">
            <el-button @click="downloadAuthorizationRequest">{{ t('systemManage.downloadAuthorizationRequest') }}</el-button>
            <el-upload action="#" :auto-upload="false" :show-file-list="false" :on-change="handleCertificateChange" accept=".bin">
              <el-button type="primary" class="ui-secondary-button">{{ t('systemManage.uploadAuthorizationFile') }}</el-button>
            </el-upload>
          </div>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.taskRunningDetail')" name="task">
        <running-detail v-if="activeTab==='task'" />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup>
import { ref, watch, onBeforeUnmount, onMounted, getCurrentInstance } from 'vue'
import { ElMessage, ElMessageBox, ElLoading } from 'element-plus'
import RunningDetail from './components/RunningDetail.vue'
import { t } from '@/i18n'
import { formatBytes, normalizeApiError } from '@/utils/apiError'
import { isSupportedUpgradePackageName } from '@/utils/upgradePackage'
import {
  uploadFileInChunks,
  UploadPurpose
} from '@/utils/chunkUpload'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const activeTab = ref('upgrade')
const uploadFile = ref(null)
const fileName = ref('')
const upload = ref(null)
const checkTimer = ref(null)
const upgradeStatusPollIntervalMs = 5000
const upgradeRecoveryTimeoutMs = 15 * 60 * 1000
let upgradeLoading = null
const authorization = ref({ supported: false, authorized: false, state: 'unsupported' })

const refreshAuthorization = async () => {
  try {
    const response = await $API.queryModelAuthorization()
    authorization.value = response?.resData?.resData || response?.resData || authorization.value
  } catch (_) {
    authorization.value = { supported: false, authorized: false, state: 'unsupported' }
  }
}

const downloadAuthorizationRequest = async () => {
  const response = await fetch('/gtw/cwai/System/DownloadModelAuthorizationRequest', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', token: localStorage.getItem('mtk') || '', mtk: localStorage.getItem('mtk') || '' },
    body: '{}'
  })
  if (!response.ok) throw new Error(t('systemManage.authorizationRequestFailed'))
  const blob = await response.blob()
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = 'device-request.cmpr'
  link.click()
  URL.revokeObjectURL(url)
}

const handleCertificateChange = async file => {
  const rawFile = file?.raw || file
  if (!rawFile || rawFile.size !== 236) {
    ElMessage.error(t('systemManage.invalidAuthorizationFile'))
    return
  }
  try {
    const staged = await uploadFileInChunks(rawFile, {
      purpose: UploadPurpose.MODEL_AUTHORIZATION_CERTIFICATE,
      uploadChunk: formData => $API.uploadAtomicModelTemp(formData),
      cancelUpload: data => $API.cancelAtomicModelUpload(data),
      getCapabilities: () => $API.getUploadCapabilities()
    })
    await $API.installModelAuthorization({ uploadId: staged.uploadId })
    ElMessage.success(t('systemManage.authorizationInstalled'))
    await refreshAuthorization()
  } catch (_) {
    ElMessage.error(t('systemManage.authorizationInstallFailed'))
  }
}

onMounted(refreshAuthorization)

const extractDeviceStatus = response =>
  response?.resData?.resData || response?.resData || {}

const checkUpgradeSpace = async cleanupEventMedia => {
  const response = await $API.boxCheckUpgradeSpace({
    packageSizeBytes: uploadFile.value.size,
    cleanupEventMedia
  })
  return extractDeviceStatus(response)
}

const delay = milliseconds =>
  new Promise(resolve => setTimeout(resolve, milliseconds))

const clearCheckTimer = () => {
  if (checkTimer.value) {
    clearTimeout(checkTimer.value)
    checkTimer.value = null
  }
}

const closeUpgradeLoading = () => {
  if (upgradeLoading) {
    upgradeLoading.close()
    upgradeLoading = null
  }
}

const finishUpgradeRecovery = async (loading) => {
  clearCheckTimer()
  loading.setText(t('systemManage.upgradeComplete'))
  await delay(1000)
  closeUpgradeLoading()
  // Element Plus removes the full-screen mask after its leave transition.
  // Let that finish before replacing the document so the login page cannot
  // inherit a stale upgrade mask.
  await delay(400)
  localStorage.removeItem('token')
  localStorage.removeItem('mtk')
  // A hash-only navigation keeps the old document and its module graph alive.
  // Changing the query forces a fresh index.html request after the upgrade.
  window.location.replace(`/?upgrade=${Date.now()}#/boxLogin`)
}

watch(activeTab, (newVal) => {
  if (newVal === 'task') {
    // Task tab logic if needed
  }
}, { immediate: true })

const beforeUpload = (file) => {
  if (!isSupportedUpgradePackageName(file.name)) {
    ElMessage.error(t('systemManage.invalidUpgradeFile'))
    return false
  }
  if (!Number.isSafeInteger(file.size) || file.size <= 0) {
    ElMessage.error(t('api.error.UpLoadDataEmpty'))
    return false
  }
  fileName.value = file.name
  uploadFile.value = file
  return true
}

const handleFileChange = (file) => {
  if (file) {
    const rawFile = file.raw || file
    if (!isSupportedUpgradePackageName(rawFile.name)) {
      ElMessage.error(t('systemManage.invalidUpgradeFile'))
      fileName.value = ''
      uploadFile.value = null
      upload.value?.clearFiles()
      return
    }
    if (!Number.isSafeInteger(rawFile.size) || rawFile.size <= 0) {
      ElMessage.error(t('api.error.UpLoadDataEmpty'))
      fileName.value = ''
      uploadFile.value = null
      upload.value?.clearFiles()
      return
    }
    fileName.value = rawFile.name
    uploadFile.value = rawFile
  }
}

const handleUpgrade = async () => {
  if (!uploadFile.value) {
    ElMessage.warning(t('systemManage.selectUpgradeFile'))
    return
  }

  try {
    await ElMessageBox.confirm(
      t('systemManage.upgradeConfirm', { fileName: fileName.value }),
      t('common.notice'),
      {
        confirmButtonText: t('action.confirm'),
        cancelButtonText: t('action.cancel'),
        type: 'warning'
      }
    )
  } catch (_) {
    return
  }

  let spaceStatus
  try {
    spaceStatus = await checkUpgradeSpace(false)
  } catch (_) {
    ElMessage.error(t('systemManage.upgradeSpaceCheckFailed'))
    return
  }

  if (!spaceStatus.sufficient) {
    try {
      await ElMessageBox.confirm(
        t('systemManage.upgradeSpaceCleanupConfirm', {
          available: formatBytes(spaceStatus.availableBytes),
          required: formatBytes(spaceStatus.requiredBytes),
          reclaimable: formatBytes(spaceStatus.eventMediaBytes)
        }),
        t('common.notice'),
        {
          confirmButtonText: t('action.deleteAndContinue'),
          cancelButtonText: t('action.cancel'),
          type: 'warning'
        }
      )
    } catch (_) {
      return
    }

    try {
      spaceStatus = await checkUpgradeSpace(true)
    } catch (_) {
      ElMessage.error(t('systemManage.upgradeEventCleanupFailed'))
      return
    }
    if (!spaceStatus.sufficient) {
      ElMessage.error(t('systemManage.upgradeSpaceStillInsufficient', {
        available: formatBytes(spaceStatus.availableBytes),
        required: formatBytes(spaceStatus.requiredBytes)
      }))
      return
    }
  }

  clearCheckTimer()
  let baselineBootId = ''
  try {
    const status = await $API.boxCheckDeviceStatus({})
    baselineBootId = String(extractDeviceStatus(status).bootId || '')
  } catch (_) {
    ElMessage.error(t('systemManage.deviceStatusUnavailable'))
    return
  }

  const loading = ElLoading.service({
    lock: true,
    text: t('systemManage.fileTransferring'),
    background: 'rgba(0, 0, 0, 0.7)'
  })
  upgradeLoading = loading
  let stagedUpload
  let upgradeRequestStarted = false
  try {
    stagedUpload = await uploadFileInChunks(uploadFile.value, {
      purpose: UploadPurpose.UPGRADE,
      uploadChunk: formData => $API.uploadAtomicModelTemp(formData),
      cancelUpload: data => $API.cancelAtomicModelUpload(data),
      getCapabilities: () => $API.getUploadCapabilities(),
      onProgress: ({ percent }) => {
        loading.setText(t('systemManage.fileTransferringProgress', { percent }))
      }
    })
    if (!stagedUpload.uploadId) {
      throw new Error(t('validate.missingUploadId'))
    }
    loading.setText(t('systemManage.upgradePreparing'))
    upgradeRequestStarted = true
    await $API.boxSystemUpgrade({
      uploadId: stagedUpload.uploadId
    })
    loading.setText(t('systemManage.upgradeInProgress'))
    checkDeviceStatus(loading, baselineBootId)
  } catch (error) {
    const structuredResponse =
      error?.resCode !== undefined ||
      error?.response?.data?.resCode !== undefined
    if (upgradeRequestStarted && !structuredResponse) {
      loading.setText(t('systemManage.upgradeOutcomeUncertain'))
      checkDeviceStatus(loading, baselineBootId)
      return
    }
    if (stagedUpload?.uploadId) {
      try {
        await $API.cancelAtomicModelUpload({ uploadId: stagedUpload.uploadId })
      } catch (_) {
        // The staging service also expires abandoned sessions by TTL.
      }
    }
    closeUpgradeLoading()
    const fallbackMessage = upgradeRequestStarted
      ? t('systemManage.upgradeFailed')
      : t('systemManage.fileTransferFailed')
    const msg = error?.resMsg?.[0]?.msgText || fallbackMessage
    ElMessage.error(msg)
  }
}

const checkDeviceStatus = (loading, baselineBootId) => {
  const startedAt = Date.now()
  let observedUnavailable = false

  const poll = async () => {
    if (Date.now() - startedAt >= upgradeRecoveryTimeoutMs) {
      clearCheckTimer()
      closeUpgradeLoading()
      ElMessage.warning(t('systemManage.upgradeRecoveryTimeout'))
      return
    }

    try {
      const response = await $API.boxCheckDeviceStatus({})
      const currentBootId = String(extractDeviceStatus(response).bootId || '')
      const rebootConfirmed =
        (currentBootId && currentBootId !== baselineBootId) ||
        (!currentBootId && observedUnavailable)
      if (rebootConfirmed) {
        await finishUpgradeRecovery(loading)
        return
      }
      loading.setText(t('systemManage.waitingForDeviceRestart'))
    } catch (error) {
      const normalized = normalizeApiError(error)
      const authenticationBoundary =
        Number(normalized.status) === 401 ||
        String(normalized.code) === '10005'
      const recoveredAtLoginBoundary =
        observedUnavailable && authenticationBoundary
      if (recoveredAtLoginBoundary) {
        await finishUpgradeRecovery(loading)
        return
      }
      const status = Number(normalized.status)
      const serviceUnavailable =
        (!normalized.status && !normalized.code) ||
        status === 502 ||
        status === 503 ||
        status === 504
      if (serviceUnavailable) {
        observedUnavailable = true
        loading.setText(t('systemManage.deviceRestarting'))
      } else {
        // In particular, do not treat an authentication response by itself as
        // proof of reboot. It is recovery evidence only after an actual
        // network/service outage has been observed.
        loading.setText(t('systemManage.waitingForDeviceRestart'))
      }
    }
    checkTimer.value = setTimeout(poll, upgradeStatusPollIntervalMs)
  }

  checkTimer.value = setTimeout(poll, 1000)
}

const handleClickInput = () => {
  upload.value.$el.querySelector('input').click()
}

const handleReset = () => {
  ElMessageBox.confirm(t('systemManage.restoreFactoryConfirm'), t('common.notice'), {
    confirmButtonText: t('action.confirm'),
    cancelButtonText: t('action.cancel'),
    type: 'warning'
  }).then(() => {
    const params = {
      resetOperation: 1
    }
    $API.boxResetSystem(params).then(() => {
      const loading = ElLoading.service({
        lock: true,
        text: t('systemManage.restoringFactory'),
        background: 'rgba(0, 0, 0, 0.7)'
      })
      setTimeout(() => {
        loading.close()
        window.location.href = '/box/#/boxLogin'
      }, 180000)
    })
  })
}

const downloadLog = () => {
  const params = {
    exportType: 1
  }
  $API.boxExportFile(params).then((res) => {
    const { resData } = res
    if (resData && resData.fileUrl) {
      downloadFile(resData)
    }
  })
}

const downloadFile = async (resData) => {
  const response = await fetch(resData.fileUrl)
  if (!response.ok) throw new Error(t('api.fileFetchFailed'))
  const blob = await response.blob()
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.style.display = 'none'
  link.href = url
  link.download = resData.fileName
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.URL.revokeObjectURL(url)
}

onBeforeUnmount(() => {
  clearCheckTimer()
  closeUpgradeLoading()
})
</script>

<style lang="scss" scoped>
.system-maintain {
  padding: 20px;
  background: #fff;
  border-radius: 4px;

  .upgrade-container {
    padding: 20px;
  }

  .authorization-container { padding: 20px; max-width: 720px; }
  .authorization-actions { display: flex; gap: 12px; margin-top: 20px; }

  .form-item {
    display: flex;
    align-items: center;
    margin-bottom: 20px;

    .label {
      width: 80px;
    }

    .file-input {
      width: 300px;
      margin-right: 20px;
    }

    .upload-btn {
      height: 100%;
      margin-right: 10px;
    }
  }

  .tips {
    margin-bottom: 20px;
    color: #f00;
    font-size: 14px;

    .tip-item {
      margin-bottom: 5px;
    }
  }

  .status-box {
    border: 1px dashed #f00;
    padding: 15px;
    margin-bottom: 20px;

    .status-title {
      font-weight: bold;
      margin-bottom: 10px;
    }

    .status-content {
      color: #f00;
      font-size: 14px;
      line-height: 1.8;
    }
  }

  .reset-container {
    padding: 20px;

    .tips {
      margin-top: 20px;
      color: #666;
      font-size: 14px;
      line-height: 1.8;
    }
  }
  .upgrade-status {
    border: 1px solid var(--border-color);
    padding: 15px;
    margin-top: 20px;

    .status-title {
      font-weight: bold;
      margin-bottom: 10px;
    }

    .status-msg {
      color: var(--primary-color);
    }
  }
}

.task-container {
  padding: 20px;
  .search-form {
    margin-bottom: 20px;
  }

  .refresh-btn {
    margin-bottom: 10px;
  }
}
</style>
