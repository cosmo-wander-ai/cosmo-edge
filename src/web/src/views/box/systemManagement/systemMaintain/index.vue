<template>
  <div class="system-maintain">
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
              <el-button size="small" type="primary" @click="handleUpgrade">{{ t('systemManage.upgrade') }}</el-button>
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
          <el-button type="primary" size="small" @click="handleReset">{{ t('systemManage.restoreFactory') }}</el-button>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.deviceLog')" name="log">
        <div class="reset-container">
          <el-button type="primary" size="small" @click="downloadLog">{{ t('systemManage.downloadDeviceLog') }}</el-button>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.taskRunningDetail')" name="task">
        <running-detail v-if="activeTab==='task'" />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup>
import { ref, watch, onBeforeUnmount, getCurrentInstance } from 'vue'
import { ElMessage, ElMessageBox, ElLoading } from 'element-plus'
import RunningDetail from './components/RunningDetail.vue'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const activeTab = ref('upgrade')
const uploadFile = ref(null)
const fileName = ref('')
const upload = ref(null)
const checkTimer = ref(null)
const upgradePackagePattern = /^cosmo-[Vv]\d+\.\d+\.\d+-[0-9a-fA-F]{32}\.tar\.gz$/

watch(activeTab, (newVal) => {
  if (newVal === 'task') {
    // Task tab logic if needed
  }
}, { immediate: true })

const beforeUpload = (file) => {
  if (!upgradePackagePattern.test(file.name)) {
    ElMessage.error(t('systemManage.invalidUpgradeFile'))
    return false
  }
  fileName.value = file.name
  uploadFile.value = file
  return true
}

const handleFileChange = (file) => {
  if (file) {
    if (!upgradePackagePattern.test(file.name)) {
      ElMessage.error(t('systemManage.invalidUpgradeFile'))
      fileName.value = ''
      uploadFile.value = null
      upload.value?.clearFiles()
      return
    }
    fileName.value = file.name
    uploadFile.value = file.raw
  }
}

const handleUpgrade = () => {
  if (!uploadFile.value) {
    ElMessage.warning(t('systemManage.selectUpgradeFile'))
    return
  }
  const loading = ElLoading.service({
    lock: true,
    text: t('systemManage.fileTransferring'),
    background: 'rgba(0, 0, 0, 0.7)'
  })
  const formData = new FormData()
  formData.append('file', uploadFile.value)
  $API
    .boxSystemUpgrade(formData)
    .then(() => {
      loading.setText(t('systemManage.fileTransferComplete'))
      setTimeout(() => {
        loading.setText(t('systemManage.upgradeInProgress'))
        checkDeviceStatus(loading)
      }, 1000)
    })
    .catch((err) => {
      loading.close()
      const msg = err?.resMsg?.[0]?.msgText || t('systemManage.fileTransferFailed')
      ElMessage.error(msg)
    })
}

const checkDeviceStatus = (loading) => {
  checkTimer.value = setInterval(() => {
    $API
      .boxCheckDeviceStatus({})
      .then(() => {
        loading.close()
        clearInterval(checkTimer.value)
        localStorage.removeItem('token')
        localStorage.removeItem('mtk')
        window.location.href = '/box/#/boxLogin'
      })
      .catch(() => {
        console.log('测试主机状态中。。。')
      })
  }, 30000)
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
  if (checkTimer.value) {
    clearInterval(checkTimer.value)
    checkTimer.value = null
  }
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
    border: 1px solid #dcdfe6;
    padding: 15px;
    margin-top: 20px;

    .status-title {
      font-weight: bold;
      margin-bottom: 10px;
    }

    .status-msg {
      color: #409eff;
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
