<template>
  <div class="net-config">
    <el-tabs v-model="activeName">
      <el-tab-pane :label="t('systemManage.networkPortSettings')" name="network">
        <div class="network-container" v-if="activeName === 'network'">
          <div class="network-list">
            <div class="network-card" v-for="(item, index) in netCardList" :key="index">
              <div class="card-header">
                <div class="title">
                  <span class="title-label">{{ t('systemManage.nameLabel') }}</span>
                  <span class="title-content">{{ item.ethName }}</span>
                  <el-button type="primary" size="small" @click="handleEdit(item,index)">{{ item.isEdit ? t('action.save') : t('action.edit') }}</el-button>
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
                      <el-button type="primary" size="small" @click="handleTest(item.ipAddr, index)">{{ t('systemManage.ipConflictCheck') }}</el-button>
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
              <el-button type="primary" @click="handleDetect">{{ t('systemManage.networkDetection') }}</el-button>
            </el-form-item>
          </el-form>
        </div>
      </el-tab-pane>
    </el-tabs>

    <el-dialog :title="t('systemManage.detectResult')" v-model="detectVisible" center width="400px">
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

    <el-dialog class="dialogtype nameinfo" v-if="dialogVisible" width="368px" :title="t('common.notice')" v-model="dialogVisible" center>
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

const dnsRules = {
  dns1: [
    {
      validator: (rule, value, callback) => {
        if (!value) {
          callback()
          return
        }
        const parts = value.split('.')
        if (parts.length !== 4) {
          callback(new Error(t('systemManage.ipError')))
          return
        }
        const firstPart = parseInt(parts[0], 10)
        if (isNaN(firstPart) || firstPart < 1 || firstPart > 223) {
          callback(new Error(t('systemManage.dnsRangeError')))
          return
        }
        for (let i = 1; i < 4; i++) {
          const part = parseInt(parts[i], 10)
          if (isNaN(part) || part < 0 || part > 255) {
            callback(new Error(t('systemManage.ipError')))
            return
          }
        }
        callback()
      },
      trigger: 'blur'
    }
  ],
  dns2: [
    {
      validator: (rule, value, callback) => {
        if (!value) {
          callback()
          return
        }
        const parts = value.split('.')
        if (parts.length !== 4) {
          callback(new Error(t('systemManage.ipError')))
          return
        }
        const firstPart = parseInt(parts[0], 10)
        if (isNaN(firstPart) || firstPart < 1 || firstPart > 223) {
          callback(new Error(t('systemManage.dnsRangeError')))
          return
        }
        for (let i = 1; i < 4; i++) {
          const part = parseInt(parts[i], 10)
          if (isNaN(part) || part < 0 || part > 255) {
            callback(new Error(t('systemManage.ipError')))
            return
          }
        }
        callback()
      },
      trigger: 'blur'
    }
  ]
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
  padding: 20px;
  background: #fff;
  border-radius: 4px;

  .network-container {
    padding: 20px;

    .network-list {
      display: flex;
      flex-wrap: wrap;
      gap: 20px;
    }
  }

  .network-card {
    background: #fff;
    border-radius: 4px;
    padding: 20px;
    width: 400px;
    box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);

    .card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 20px;

      .title {
        display: flex;
        align-items: center;
        font-size: 16px;
      }

      span {
        display: inline-block;
        color: #606266;
      }

      .title-label {
        width: 90px;
        margin-right: 10px;
        text-align: right;
      }

      .title-content {
        width: 220px;
        margin-right: 10px;
        text-align: left;
      }
    }

    .card-content {
      :deep(.el-form-item) {
        margin-bottom: 18px;

        .el-form-item__label {
          color: #606266;
        }

        .el-input {
          width: 220px;
        }
      }

      .ip-input {
        display: flex;
        align-items: center;
        gap: 10px;
      }
    }
  }
}

.dns-container {
  padding: 20px;
  width: 400px;

  .dns-input {
    .el-input {
      width: 220px;
    }
  }

  .dns-footer {
    margin-top: 20px;
    text-align: center;

    .el-button {
      margin: 0 10px;
    }
  }
}

.detect-container {
  padding: 20px;
  width: 500px;

  :deep(.el-input) {
    width: 220px;
  }
}

.detect-result {
  padding: 20px;

  .result-item {
    display: flex;
    margin-bottom: 15px;

    .label {
      width: 80px;
      color: #606266;
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
