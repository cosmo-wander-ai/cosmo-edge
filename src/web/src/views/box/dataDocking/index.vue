<template>
  <div class="main-body">
    <!-- <div class="main-container-header">
      <div class="tips">{{ t('boxOther.runModeTip') }}</div>
      <span>{{ t('boxOther.runMode') }}</span>
      <el-radio-group v-model="tempRunMode" @change="handleRunModeChange">
        <el-radio :value="0">{{ t('boxOther.standalone') }}</el-radio>
        <el-radio :value="1">{{ t('boxOther.networked') }}</el-radio>
      </el-radio-group>
    </div> -->
    <div class="main-container-body">
      <div v-if="runMode === 0">
        <el-tabs v-model="activeName" @tab-click="handleClick">
          <el-tab-pane :label="t('boxOther.httpDocking')" name="http">
            <div class="item-content">
              <el-form ref="httpFormRef" :model="httpFormData" :rules="httpRules" :label-width="currentLocale === 'en-US' ? '150px' : '120px'">
                <el-form-item :label="t('boxOther.enablePush') + localeColon">
                  <el-switch v-model="httpFormData.switch" @change="handleSwitchChange('http')" />
                  <el-button link style="margin-left: 50px;" @click="downloadDocClick(0)">{{ t('boxOther.interfaceDoc') }}</el-button>
                </el-form-item>
                <el-form-item v-if="httpFormData.switch" :label="t('boxOther.serverAddress') + localeColon" prop="url">
                  <el-input v-model.trim="httpFormData.url" :placeholder="t('validate.enterField', { field: t('boxOther.serverAddress') })" size="small"
                    :disabled="!httpFormData.switch" />
                </el-form-item>
              </el-form>
              <div>
                <el-button type="primary" @click="submitFormHttp" size="small">{{ t('action.save') }}</el-button>
              </div>
            </div>
          </el-tab-pane>
          <el-tab-pane :label="t('boxOther.mqttDocking')" name="mqtt">
            <div class="item-content">
              <el-form ref="mqttFormRef" :model="mqttFormData" :rules="mqttRules" :label-width="currentLocale === 'en-US' ? '150px' : '120px'">
                <el-form-item :label="t('boxOther.enablePush') + localeColon">
                  <el-switch v-model="mqttFormData.switch" @change="handleSwitchChange('mqtt')" />
                  <el-button link style="margin-left: 50px;" @click="downloadDocClick(1)">{{ t('boxOther.interfaceDoc') }}</el-button>
                </el-form-item>
                <el-form-item :label="t('boxOther.serverAddress') + localeColon" prop="url" v-if="mqttFormData.switch">
                  <el-input v-model.trim="mqttFormData.url" :placeholder="t('validate.enterField', { field: t('boxOther.serverAddress') })" size="small"
                    :disabled="!mqttFormData.switch" />
                </el-form-item>
                <el-form-item :label="t('boxOther.port') + localeColon" prop="port" v-if="mqttFormData.switch">
                  <el-input v-model.trim="mqttFormData.port" :placeholder="t('boxOther.enterPort')" size="small"
                    :disabled="!mqttFormData.switch" @input="(e) => handleMqttInput(e, 'port')" />
                </el-form-item>
                <el-form-item :label="t('field.status') + localeColon" v-if="mqttFormData.switch">
                  <span v-if="mqttFormData.status" style="color:#0efe42;">{{ t('status.online') }}</span>
                  <span v-else>{{ t('status.offline') }}</span>
                </el-form-item>
              </el-form>
              <div>
                <el-button type="primary" @click="submitFormMqtt" size="small">{{ t('action.save') }}</el-button>
              </div>
            </div>
          </el-tab-pane>
        </el-tabs>
      </div>
      <div v-else>
        <div class="item-content">
          <el-form ref="netFormRef" :model="netFormData" :rules="netRules" :label-width="currentLocale === 'en-US' ? '200px' : '160px'">
            <el-form-item :label="t('boxOther.mqttServerAddress') + localeColon" prop="mqttIp">
              <el-input v-model.trim="netFormData.mqttIp" :placeholder="t('validate.enterField', { field: t('boxOther.serverAddress') })" size="small" />
            </el-form-item>
            <el-form-item :label="t('boxOther.port') + localeColon" prop="mqttPort">
              <el-input v-model.trim="netFormData.mqttPort" :placeholder="t('boxOther.enterPort')" size="small"
                @input="(e) => handleNetInput(e, 'mqttPort')" />
            </el-form-item>
            <el-form-item :label="t('boxOther.httpServerAddress') + localeColon" prop="httpUrl">
              <el-input v-model.trim="netFormData.httpUrl" :placeholder="t('validate.enterField', { field: t('boxOther.serverAddress') })" size="small" />
            </el-form-item>
            <el-form-item :label="t('field.status') + localeColon">
              <span v-if="netFormData.status" style="color:#0efe42;">{{ t('status.online') }}</span>
              <span v-else>{{ t('status.offline') }}</span>
            </el-form-item>
          </el-form>
          <div class="net-btn-tools">
            <el-button type="primary" @click="resetClick" size="small">{{ t('boxOther.default') }}</el-button>
            <el-button type="primary" @click="submitFormNet" size="small">{{ t('action.save') }}</el-button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, watch, onMounted, getCurrentInstance } from 'vue'
import { ElLoading } from 'element-plus'
import { currentLocale, t, localeColon } from '@/i18n'

const { proxy } = getCurrentInstance()

const runMode = ref(0)
const tempRunMode = ref(0)
const activeName = ref('http')

const httpFormRef = ref(null)
const mqttFormRef = ref(null)
const netFormRef = ref(null)

const httpFormData = reactive({
  switch: false,
  url: ''
})

const httpRules = computed(() => ({
  url: [
    { required: true, message: t('validate.enterField', { field: t('boxOther.serverAddress') }), trigger: 'blur' },
    { max: 256, message: t('boxOther.serverAddressMaxChars', { n: 256 }), trigger: 'blur' }
  ]
}))

const mqttFormData = reactive({
  switch: false,
  url: '',
  port: '',
  status: false
})

const mqttRules = computed(() => ({
  url: [
    { required: true, message: t('validate.enterField', { field: t('boxOther.serverAddress') }), trigger: 'blur' },
    { max: 256, message: t('boxOther.serverAddressMaxChars', { n: 256 }), trigger: 'blur' }
  ],
  port: [
    { required: true, message: t('boxOther.enterPort'), trigger: 'blur' }
  ],
  clientId: [
    { required: true, message: t('boxOther.enterClientId'), trigger: 'blur' },
    { max: 256, message: t('boxOther.clientIdMaxChars', { n: 256 }), trigger: 'blur' }
  ],
  clientSecret: [
    { required: true, message: t('boxOther.enterClientSecret'), trigger: 'blur' },
    { max: 256, message: t('boxOther.clientSecretMaxChars', { n: 256 }), trigger: 'blur' }
  ]
}))

const netFormData = reactive({
  mqttIp: '',
  mqttPort: '1883',
  httpUrl: '',
  status: false
})

const netRules = computed(() => ({
  mqttIp: [
    {
      max: 256,
      message: t('boxOther.mqttServerAddressMaxChars', { n: 256 }),
      trigger: 'blur'
    }
  ],
  mqttPort: [
    { required: true, message: t('boxOther.enterMqttPort'), trigger: 'blur' }
  ],
  httpUrl: [
    {
      max: 256,
      message: t('boxOther.httpServerAddressMaxChars', { n: 256 }),
      trigger: 'blur'
    }
  ]
}))

// Watch runMode
watch(runMode, (newVal) => {
  tempRunMode.value = newVal
})

// Methods
const handleClick = (tab) => {
  activeName.value = tab.props.name
  if (tab.props.name === 'http') {
    queryHttpInterfaceParam()
  } else if (tab.props.name === 'mqtt') {
    queryMqttAdapterParam()
  }
}

const normalizeSwitchValue = (data) => {
  const value = data?.switch ?? data?.enable
  return value === true || value === 1 || value === '1'
}

// 修改运行模式切换的处理函数
const handleRunModeChange = (newVal) => {
  // 如果新值与当前值相同，不做处理
  if (newVal === runMode.value) {
    return
  }

  // 显示确认对话框
  proxy.$confirm(
    t('boxOther.changeRunModeConfirm'),
    t('common.notice'),
    {
      confirmButtonText: t('action.confirm'),
      cancelButtonText: t('action.cancel'),
      type: 'warning'
    }
  )
    .then(() => {
      runMode.value = newVal
      proxy.$API
        .modifyRunModeParam({ runMode: runMode.value })
        .then(() => {
          let countdown = 60
          const loading = ElLoading.service({
            lock: true,
            text: t('boxOther.rebootCountdown', { n: countdown }),
            background: 'rgba(0, 0, 0, 0.7)'
          })

          const timer = setInterval(() => {
            countdown--
            loading.setText(t('boxOther.rebootCountdown', { n: countdown }))
            if (countdown <= 0) {
              clearInterval(timer)
              loading.close()
              localStorage.removeItem('mtk')
              localStorage.removeItem('token')
              window.location.href = '/#/boxLogin'
            }
          }, 1000)
        })
    })
    .catch(() => {
      // 用户取消，恢复到原来的值
      tempRunMode.value = runMode.value
    })
}

const queryHttpInterfaceParam = () => {
  proxy.$API.queryHttpInterfaceParam({}).then((res) => {
    const { resData } = res
    httpFormData.switch = normalizeSwitchValue(resData)
    httpFormData.url = resData.url
  })
}

const queryMqttAdapterParam = () => {
  proxy.$API.queryMqttAdapterParam({}).then((res) => {
    const { resData } = res
    mqttFormData.switch = normalizeSwitchValue(resData)
    mqttFormData.url = resData.url
    mqttFormData.port = resData.port
    mqttFormData.status = resData.status
  })
}

const queryIotNetworkParam = () => {
  proxy.$API.queryIotNetworkParam({}).then((res) => {
    const { resData } = res
    netFormData.mqttIp = resData.mqttIp
    netFormData.mqttPort = resData.mqttPort
    netFormData.httpUrl = resData.httpUrl
    netFormData.status = resData.status
  })
}

const handleSwitchChange = (type) => {
  if (type === 'http' && !httpFormData.switch) {
    httpFormRef.value?.clearValidate()
  } else if (type === 'mqtt' && !mqttFormData.switch) {
    mqttFormRef.value?.clearValidate()
  }
}

const submitFormHttp = () => {
  httpFormRef.value?.validate((valid) => {
    if (valid) {
      proxy.$API
        .setHttpInterfaceParam({
          switch: httpFormData.switch,
          url: httpFormData.url
        })
        .then(() => {
          proxy.$message.success(t('common.operationSucceeded'))
          queryHttpInterfaceParam()
        })
    }
  })
}

const submitFormMqtt = () => {
  mqttFormRef.value?.validate((valid) => {
    if (valid) {
      const params = {
        switch: mqttFormData.switch,
        url: mqttFormData.url,
        port: Number(mqttFormData.port)
      }
      proxy.$API.setMqttAdapterParam(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        queryMqttAdapterParam()
      })
    }
  })
}

const submitFormNet = () => {
  netFormRef.value?.validate((valid) => {
    if (valid) {
      const params = {
        mqttIp: netFormData.mqttIp,
        mqttPort: Number(netFormData.mqttPort),
        httpUrl: netFormData.httpUrl
      }
      proxy.$API.modifyIotNetworkParam(params).then(() => {
        proxy.$message.success(t('common.operationSucceeded'))
        queryIotNetworkParam()
      })
    }
  })
}

const downloadDocClick = (val) => {
  proxy.$API.queryDocumentUrl({ type: val }).then((res) => {
    const { resData } = res
    if (resData?.url) {
      const baseUrl =
        import.meta.env.MODE === 'development'
          ? import.meta.env.VITE_APP_API_URL + resData.url
          : resData.url
      const url = new URL(baseUrl, window.location.origin)
      url.searchParams.set('locale', currentLocale.value)
      const openUrl = url.toString()

      console.log('===openUrl======', openUrl)
      window.open(openUrl, '_blank')
    }
  })
}

const handleMqttInput = (val, key) => {
  mqttFormData[key] = val.replace(/[^\d]/g, '')
}

const handleNetInput = (val, key) => {
  netFormData[key] = val.replace(/[^\d]/g, '')
}

const resetClick = () => {
  netFormData.mqttIp = ''
  netFormData.mqttPort = '1883'
  netFormData.httpUrl = ''
  submitFormNet()
}

// Lifecycle
onMounted(() => {
  if (runMode.value === 0) {
    queryHttpInterfaceParam()
    queryMqttAdapterParam()
  } else {
    queryIotNetworkParam()
  }
})
</script>

<style lang="scss" scoped>
.main-body {
  display: flex;
  flex-direction: column;
  padding: 10px 15px;
  background-color: #fff;
  border-radius: 2px;
}

.main-container-header {
  .tips {
    color: red;
    font-size: 14px;
    margin-bottom: 10px;
  }

  span {
    display: inline-block;
    margin-right: 20px;
    color: #409eff;
  }
}

.main-container-body {
  margin-top: 15px;
}

.item-content {
  display: flex;

  .el-form {
    width: 550px;
  }

  :deep(.el-input) {
    width: 300px;
  }
}

.net-btn-tools {
  margin-top: 3px;
}
</style>
