<template>
  <div class="net-config ui-admin-page">
    <header class="network-heading">
      <h1>{{ t('nav.networkSettings') }}</h1>
    </header>
    <el-tabs v-model="activeName">
      <el-tab-pane :label="t('systemManage.networkPortSettings')" name="network">
        <div class="network-container" v-if="activeName === 'network'">
          <div class="network-list">
            <div class="network-card" v-for="(item, index) in netCardList" :key="index">
              <div class="card-header">
                <div class="title">
                  <svg class="ethernet-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                    <rect x="3" y="3" width="18" height="18" rx="2" />
                    <path d="M6.5 7.5h11v7h-2v2h-2v2h-3v-2h-2v-2h-2zM9 7.5v3m3-3v3m3-3v3" />
                  </svg>
                  <span class="title-content">{{ item.ethName }}</span>
                  <el-button :type="item.isEdit ? 'primary' : undefined" size="small" :class="item.isEdit ? 'network-save' : 'network-edit'" @click="handleEdit(item,index)">{{ item.isEdit ? t('action.save') : t('action.edit') }}</el-button>
                </div>
              </div>
              <div class="card-content">
                <el-form :model="item" :rules="rules" :ref="(el) => setEditFormRef(el, index)" label-position="right" :label-width="currentLocale === 'en-US' ? '140px' : '100px'" size="small">
                  <el-form-item :label="t('systemManage.macAddress')" prop="mac">
                    <el-input v-model="item.mac" disabled></el-input>
                  </el-form-item>
                  <el-form-item :label="t('systemManage.ipMethod')" prop="dhcp">
                    <el-select v-model="item.dhcp" :disabled="!item.isEdit" :placeholder="t('placeholder.select')">
                      <el-option :label="t('systemManage.staticIP')" :value="0"></el-option>
                      <el-option v-if="item.ethName !=='LAN'" label="DHCP" :value="1"></el-option>
                    </el-select>
                  </el-form-item>
                  <el-form-item :label="t('systemManage.ipAddress')" prop="ipAddr">
                    <div class="ip-input">
                      <el-input v-model="item.ipAddr" :disabled="!item.isEdit || item.dhcp == 1"></el-input>
                      <el-button size="small" @click="handleTest(item.ipAddr, index)">{{ t('systemManage.ipConflictCheck') }}</el-button>
                    </div>
                  </el-form-item>
                  <el-form-item :label="t('systemManage.subnetMask')" prop="netMask">
                    <el-input v-model="item.netMask" :disabled="!item.isEdit || item.dhcp == 1"></el-input>
                  </el-form-item>
                  <el-form-item v-if="item.ethName !=='LAN'" :label="t('systemManage.gateway')" prop="gateway">
                    <el-input v-model="item.gateway" :disabled="!item.isEdit || item.dhcp == 1"></el-input>
                  </el-form-item>
                </el-form>
              </div>
            </div>
          </div>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.dnsSettings')" name="dns">
        <div class="dns-container" v-if="activeName === 'dns'">
          <el-form ref="dnsFormRef" :model="dnsConfig" :rules="dnsRules" label-position="right" :label-width="currentLocale === 'en-US' ? '140px' : '100px'" size="small">
            <el-form-item :label="'DNS1' + localeColon" prop="dns1">
              <div class="dns-input">
                <el-input v-model="dnsConfig.dns1" :placeholder="t('systemManage.enterDNS1')"></el-input>
              </div>
            </el-form-item>
            <el-form-item :label="'DNS2' + localeColon" prop="dns2">
              <div class="dns-input">
                <el-input v-model="dnsConfig.dns2" :placeholder="t('systemManage.enterDNS2')"></el-input>
              </div>
            </el-form-item>
          </el-form>
          <div class="dns-footer">
            <el-button @click="handleCancel" size="small">{{ t('action.reset') }}</el-button>
            <el-button type="primary" @click="handleDnsSave" size="small">{{ t('action.save') }}</el-button>
          </div>
        </div>
      </el-tab-pane>
      <el-tab-pane :label="t('systemManage.networkDetection')" name="detect">
        <div class="detect-container" v-if="activeName === 'detect'">
          <el-form :model="detectForm" :rules="detectRules" ref="detectFormRef" label-position="right" :label-width="currentLocale === 'en-US' ? '200px' : '180px'" size="small">
            <el-form-item :label="t('systemManage.targetAddress')" prop="ip">
              <el-input v-model.trim="detectForm.ip" :placeholder="t('systemManage.enterTargetAddress')"></el-input>
            </el-form-item>
            <el-form-item prop="packetSize">
              <template #label>
                <span>{{ t('systemManage.packetSize') }}</span>
                <el-tooltip :content="t('systemManage.packetSizeTooltip')" placement="top">
                  <i class="el-icon-question"></i>
                </el-tooltip>
                {{ localeColon }}
              </template>
              <el-input v-model.number="detectForm.packetSize" type="number" :placeholder="t('systemManage.enterPacketSize')"></el-input>
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleDetect" class="ui-secondary-button">{{ t('systemManage.networkDetection') }}</el-button>
            </el-form-item>
          </el-form>
        </div>
      </el-tab-pane>
    </el-tabs>

    <el-dialog :title="t('systemManage.detectResult')" v-model="detectVisible" center width="400px" class="ui-admin-dialog">
      <div class="detect-result">
        <div class="result-item">
          <span class="label">{{ t('systemManage.lostRate') }}</span>
          <span class="value">{{ detectResult.lostRate }}%</span>
        </div>
        <div class="result-item">
          <span class="label">{{ t('systemManage.averageLatency') }}</span>
          <span class="value">{{ detectResult.averageLatency }}ms</span>
        </div>
      </div>
      <template #footer>
        <el-button @click="detectVisible = false" size="small">{{ t('action.cancel') }}</el-button>
      </template>
    </el-dialog>

    <el-dialog class="dialogtype nameinfo ui-admin-dialog" v-if="dialogVisible" width="368px" :title="t('common.notice')" v-model="dialogVisible" center>
      <div class="fd">
        <img v-if="imgState == 1" width="44px" :src="img1Url" />
        <img v-else-if="imgState == 2" width="44px" :src="img2Url" />
        <img v-else-if="imgState == 3" width="44px" :src="img3Url" />
        <img v-else class="uploading-icon" :src="uploadingUrl">
        <p>{{dialogContent}}</p>
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, watch, onMounted, getCurrentInstance } from 'vue'
import { ElLoading } from 'element-plus'
import { t, localeColon, currentLocale } from '@/i18n'
import { isValidOptionalDnsAddress } from '@/utils/networkValidation'

const { proxy } = getCurrentInstance()

const activeName = ref('network')
const netCardList = ref([])
const oldCardList = ref([])
const dnsFormRef = ref(null)
const detectFormRef = ref(null)
const editFormRefs = reactive({})

// 图片资源
const img1Url = new URL('@/assets/img1.png', import.meta.url).href
const img2Url = new URL('@/assets/img2.png', import.meta.url).href
const img3Url = new URL('@/assets/img3.png', import.meta.url).href
const uploadingUrl = new URL('@/assets/uploading.png', import.meta.url).href

const dnsConfig = reactive({
  dns1: '',
  dns2: ''
})

const rules = {
  ipAddr: [
    { required: true, message: t('systemManage.enterIP'), trigger: 'blur' },
    {
      pattern:
        /^([1-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])$/,
      message: t('systemManage.ipFormatError'),
      trigger: 'blur'
    }
  ],
  netMask: [
    { required: true, message: t('systemManage.enterSubnetMask'), trigger: 'blur' },
    {
      pattern:
        /^([1-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])$/,
      message: t('systemManage.subnetMaskFormatError'),
      trigger: 'blur'
    }
  ],
  gateway: [
    {
      pattern:
        /^([1-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])\.([0-9]|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5])$/,
      message: t('systemManage.gatewayFormatError'),
      trigger: 'blur'
    }
  ]
}

// 校验 DNS 表单中的可选 IPv4 地址；DNS1 和 DNS2 共用该规则，空值保持允许。
const validateDnsAddress = (rule, value, callback) => {
  if (isValidOptionalDnsAddress(value)) {
    callback()
    return
  }
  callback(new Error(t('systemManage.dnsFormatError')))
}

const dnsRules = {
  dns1: [{ validator: validateDnsAddress, trigger: 'blur' }],
  dns2: [{ validator: validateDnsAddress, trigger: 'blur' }]
}

const detectForm = reactive({
  ip: '',
  packetSize: 64
})

const detectRules = {
  ip: [
    { required: true, message: t('systemManage.enterTargetAddress'), trigger: 'blur' },
    {
      max: 256,
      message: t('systemManage.targetAddressMaxLength'),
      trigger: 'blur'
    }
  ],
  packetSize: [
    { required: true, message: t('systemManage.enterPacketSize'), trigger: 'blur' },
    { type: 'number', message: t('systemManage.packetSizeMustBeNumber'), trigger: 'blur' },
    {
      type: 'number',
      min: 64,
      max: 10000,
      message: t('systemManage.packetSizeRange'),
      trigger: 'blur'
    }
  ]
}

const detectVisible = ref(false)
const detectResult = reactive({
  lostRate: 0,
  averageLatency: 1
})

const dialogVisible = ref(false)
const imgState = ref(1)
const dialogContent = ref('')

// 设置表单 ref
const setEditFormRef = (el, index) => {
  if (el) {
    editFormRefs[`editForm${index}`] = el
  }
}

// 查询网卡
const queryNetCard = () => {
  proxy.$API.queryNetCard().then((res) => {
    const { resData } = res
    const list = resData.netCardList || []
    oldCardList.value = JSON.parse(JSON.stringify(list))
    netCardList.value = list.map((item) => {
      return {
        ...item,
        isEdit: false
      }
    })
  })
}

// 查询 DNS
const queryNetDns = () => {
  proxy.$API.queryNetDns().then((res) => {
    const { resData } = res
    if (resData?.dns1) dnsConfig.dns1 = resData.dns1
    if (resData?.dns2) dnsConfig.dns2 = resData.dns2
  })
}

// 检查是否更新
const isUpdate = (item, index) => {
  let updateFlag = false
  for (const key in item) {
    if (item.hasOwnProperty(key)) {
      if (key === 'isEdit') {
        continue
      }
      if (item[key] != oldCardList.value[index][key]) {
        updateFlag = true
      }
    }
  }
  return updateFlag
}

// 提交 API
const submitApi = (item, params, index) => {
  if (!isUpdate(params.netCard, index)) {
    item.isEdit = false
    dialogContent.value = t('systemManage.notModified')
    imgState.value = 1
    dialogVisible.value = true
    return
  }

  const EditorIp = params.netCard.ipAddr
  proxy.$API.modifyNetCard(params).then(() => {
    const loading = ElLoading.service({
      lock: true,
      text: t('systemManage.modifySuccessRestarting', { n: 60 }),
      background: 'rgba(0, 0, 0, 0.7)'
    })

    let countdown = 60
    const timer = setInterval(() => {
      countdown--
      loading.setText(t('systemManage.modifySuccessRestarting', { n: countdown }))
      if (countdown <= 0) {
        clearInterval(timer)
        loading.close()
        localStorage.removeItem('mtk')
        localStorage.removeItem('token')
        window.location.href = 'http://' + EditorIp + '/#/boxLogin'
      }
    }, 1000)
  })
}

// 编辑
const handleEdit = (item, index) => {
  if (netCardList.value[index].isEdit) {
    const params = {
      netCard: {}
    }
    let obj = {}
    if (item.dhcp) {
      obj.dhcp = item.dhcp
      obj.ethName = item.ethName
      params.netCard = obj
      submitApi(item, params, index)
    } else {
      const ruleRef = editFormRefs[`editForm${index}`]
      ruleRef?.validate((valid) => {
        if (!valid) {
          return
        }
        obj = { ...item }
        delete obj.isEdit
        params.netCard = obj
        submitApi(item, params, index)
      })
    }
  } else {
    netCardList.value[index].isEdit = !netCardList.value[index].isEdit
  }
}

// 检测 IP 是否已被占用
const handleTest = (ip, index) => {
  const formRef = editFormRefs[`editForm${index}`]
  formRef?.validateField('ipAddr', (valid) => {
    if (valid) {
      dialogContent.value = t('systemManage.ipConflictChecking')
      imgState.value = 0
      dialogVisible.value = true

      const oldIp = oldCardList.value[index].ipAddr
      if (oldIp === ip) {
        setTimeout(() => {
          dialogContent.value = t('systemManage.ipNotUsed')
          imgState.value = 1
        }, 1500)
      } else {
        setTimeout(() => {
          proxy.$API.ipAccessibleCheck({ ip })
            .then((res) => {
              if (res.resData.accessible) {
                dialogContent.value = t('systemManage.ipConflict')
                imgState.value = 3
              } else {
                dialogContent.value = t('systemManage.ipNotUsed')
                imgState.value = 1
              }
            })
            .catch(() => {
              dialogContent.value = t('systemManage.ipConflictCheckFailed')
              imgState.value = 3
            })
        }, 1500)
      }
    } else {
      dialogContent.value = t('systemManage.ipFormatError')
      imgState.value = 3
      dialogVisible.value = true
    }
  })
}

// DNS 默认
const handleCancel = () => {
  dnsConfig.dns1 = '114.114.114.114'
  dnsConfig.dns2 = ''
  handleDnsSave()
}

// DNS 保存
const handleDnsSave = () => {
  dnsFormRef.value?.validate((valid) => {
    if (valid) {
      const params = {
        dns1: dnsConfig.dns1,
        dns2: dnsConfig.dns2
      }
      proxy.$API.modifyNetDns(params).then(() => {
        proxy.$message.success(t('common.saveSucceeded'))
        queryNetDns()
      })
    }
  })
}

// 网络检测
const handleDetect = () => {
  detectFormRef.value?.validate((valid) => {
    if (valid) {
      const params = {
        ip: detectForm.ip,
        packetSize: detectForm.packetSize
      }
      proxy.$API.networkQualityCheck(params).then((res) => {
        const { resData } = res
        detectResult.lostRate = resData.lostRate
        detectResult.averageLatency = resData.averageLatency
        detectVisible.value = true
      })
    }
  })
}

// Watch activeName
watch(activeName, (val) => {
  if (val === 'network') {
    queryNetCard()
  } else if (val === 'dns') {
    queryNetDns()
  } else if (val === 'detect') {
    detectForm.ip = ''
    detectForm.packetSize = 64
  }
})

// 生命周期
onMounted(() => {
  queryNetCard()
  queryNetDns()
})
</script>

<style lang="scss" scoped>
.net-config {
  padding: 0;
  background: transparent;

  .network-heading {
    margin-bottom: 18px;

    h1 {
      margin: 0;
      color: var(--text-primary);
      font-size: 24px;
      line-height: 1.4;
      font-weight: 650;
    }
  }

  :deep(> .el-tabs > .el-tabs__header) { margin-bottom: 18px; }
  :deep(> .el-tabs > .el-tabs__header .el-tabs__item) { height: 42px; }

  .network-container {
    padding: 0;

    .network-list {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 16px;
    }
  }

  .network-card {
    background: var(--bg-white);
    border-radius: 6px;
    padding: 24px;
    min-width: 0;
    border: 1px solid var(--border-color);
    box-shadow: none;

    .card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 24px;

      .title {
        display: flex;
        align-items: center;
        width: 100%;
        min-width: 0;
        gap: 12px;
        font-size: 15px;
      }

      .el-button {
        flex-shrink: 0;
        margin-left: auto;
        min-width: 68px;
        height: 32px;
      }

      span {
        display: inline-block;
        color: var(--text-secondary);
      }

      .ethernet-icon {
        flex: 0 0 28px;
        width: 28px;
        height: 28px;
        color: var(--text-secondary);
      }

      .title-content {
        flex: 1;
        min-width: 0;
        margin-right: 0;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: normal;
        overflow-wrap: anywhere;
        text-align: left;
        color: var(--text-primary);
        font-size: 20px;
        font-weight: 650;
      }
    }

    .card-content {
      :deep(.el-form-item) {
        margin-bottom: 16px;

        .el-form-item__label {
          color: var(--text-secondary);
          line-height: 36px;
        }

        .el-form-item__content {
          min-width: 0;
        }

        .el-input,
        .el-select {
          width: 100%;
          min-width: 0;
        }
      }

      :deep(.el-input__wrapper),
      :deep(.el-select__wrapper) { min-height: 36px; }

      :deep(.el-input.is-disabled .el-input__wrapper),
      :deep(.el-select__wrapper.is-disabled) {
        background: var(--bg-subtle);
        box-shadow: 0 0 0 1px var(--border-light) inset;
      }

      :deep(.el-input.is-disabled .el-input__inner) {
        color: var(--text-secondary);
        -webkit-text-fill-color: var(--text-secondary);
      }

      :deep(.el-select__wrapper.is-disabled .el-select__selected-item) { color: var(--text-secondary); }

      .ip-input {
        display: flex;
        flex-wrap: wrap;
        align-items: center;
        gap: 8px;
        width: 100%;
        min-width: 0;

        .el-input {
          flex: 1 1 130px;
          width: auto;
          min-width: 0;
        }

        .el-button {
          flex: 0 0 auto;
          margin-left: 0;
          min-height: 34px;
        }
      }
    }
  }

  @media (max-width: 1120px) {
    .network-container .network-list { grid-template-columns: 1fr; }
    .network-card { max-width: 720px; }
  }
}

.dns-container {
  box-sizing: border-box;
  padding: 24px;
  width: 580px;
  max-width: 100%;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  background: var(--bg-white);

  .dns-input {
    width: 100%;
    .el-input {
      width: 100%;
    }
  }

  .dns-footer {
    margin-top: 20px;
    display: flex;
    justify-content: flex-end;
    gap: 8px;

    .el-button {
      margin: 0;
    }
  }
}

.detect-container {
  box-sizing: border-box;
  padding: 24px;
  width: 660px;
  max-width: 100%;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  background: var(--bg-white);

  :deep(.el-input) {
    width: 100%;
  }
}

.detect-result {
  padding: 20px;

  .result-item {
    display: flex;
    margin-bottom: 15px;

    .label {
      width: 80px;
      color: var(--text-secondary);
      text-align: right;
      margin-right: 10px;
    }

    .value {
      flex: 1;
    }
  }
}

.fd {
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  font-size: 14px;
}

.uploading-icon {
  width: 44px;
  height: 44px;
  margin-bottom: 12px;
  animation: rotate 2s linear infinite;
}

@keyframes rotate {
  from {
    transform: rotate(0deg);
  }
  to {
    transform: rotate(360deg);
  }
}
</style>
