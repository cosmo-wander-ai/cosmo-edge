<template>
  <div class="page ui-admin-page flow-theme">
    <div class="header">
      <div class="title">
        <div class="title-row">
          <span class="model-name">{{ modelName }}</span>
          <span class="model-code">{{ t('field.modelId') }}{{ localeColon }}{{ modelCode }}</span>
        </div>
        <div class="tags">
          <el-tag size="small" type="info">{{ modelType }}</el-tag>
          <el-tag size="small" type="info">{{ chipType }}</el-tag>
          <el-tag size="small" type="info">{{ version }}</el-tag>
        </div>
      </div>
      <div class="actions">
        <el-button type="primary" size="small" :disabled="!configReady" @click="handleSave">{{ t('action.save') }}</el-button>
        <el-tooltip :content="t('common.presetModelNotExportable')" :disabled="isExportable" placement="top">
          <span>
            <el-button size="small" :disabled="!configReady || !isExportable" @click="handleExport">{{ t('action.export') }}</el-button>
          </span>
        </el-tooltip>
        <el-button size="small" @click="handleReset">{{ t('action.restoreDefault') }}</el-button>
        <el-button size="small" @click="goBack">{{ t('action.goBack') }}</el-button>
      </div>
    </div>

    <div class="config-body">
      <el-alert v-if="configError" :title="t('common.modelConfigLoadFailed')" type="error" :closable="false" show-icon />
      <ParamsConfig
        v-model="paramsConfig"
        :model-type="modelType"
        :labels="labels"
        @update:labels="labels = $event"
      />

      <el-card class="card" shadow="never">
        <template #header>
          <div class="card-header" @click="showOther = !showOther">
            <span>
              <span>{{ t('glossary.inputOutputNodes') }}</span>
              <span class="title-span-tip">{{ t('glossary.viewEditNodeDef') }}</span>
            </span>
            <el-icon :class="['collapse-icon', { rotated: showOther }]">
              <ArrowDown />
            </el-icon>
          </div>
        </template>
        <div v-show="showOther">
          <ConfigFLow v-model:flowData="flowData" />
        </div>
      </el-card>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, getCurrentInstance } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ArrowDown } from '@element-plus/icons-vue'
import { t, localeColon } from '@/i18n'
import ConfigFLow from './configFLow.vue'
import ParamsConfig from './ParamsConfig.vue'

const route = useRoute()
const router = useRouter()
const { proxy } = getCurrentInstance()

const raw = ref(null)
const modelName = ref('')
const modelCode = ref('')
const modelType = ref('')
const chipType = ref('')
const version = ref('')
const isExportable = ref(true)
const configReady = ref(false)
const configError = ref(false)
const defaultConfigJson = ref('')
const showOther = ref(false)
const flowData = ref({})
const paramsConfig = ref({})
const labels = ref([])
const maxLabelCount = 80

const isGenerationModel = (type) => ['qwen3vl', 'qwen3_5'].includes(String(type || '').toLowerCase())

const parseModelConfig = (text) => {
  const cfg = JSON.parse(text)
  if (!cfg || Array.isArray(cfg) || typeof cfg !== 'object' ||
      !Array.isArray(cfg.models) || !cfg.models.length ||
      !cfg.models[0] || Array.isArray(cfg.models[0]) || typeof cfg.models[0] !== 'object') {
    throw new Error('Invalid model configuration')
  }
  return cfg
}

const hydrate = (cfg) => {
  raw.value = cfg
  const m = cfg?.models?.[0] || {}
  modelName.value = m?.name || ''
  modelCode.value = cfg?.algorithm_code || ''
  modelType.value = cfg?.model_type || ''
  chipType.value = cfg?.chip_type || ''
  version.value = cfg?.version || ''

  if (isGenerationModel(cfg?.model_type)) {
    paramsConfig.value = JSON.parse(JSON.stringify(cfg?.config?.generation || {}))
  } else {
    paramsConfig.value = JSON.parse(JSON.stringify(m?.params || {}))
  }

  labels.value = (cfg?.labels || []).slice(0, maxLabelCount).map((item) => ({
    id: item?.id != null ? String(item.id) : '',
    name: item?.name || '',
    threshold: Array.isArray(item?.threshold) ? [...item.threshold] : [0.25, 0.25]
  }))

  flowData.value = {
    inputs: JSON.parse(JSON.stringify(m?.inputs || [])),
    outputs: JSON.parse(JSON.stringify(m?.outputs || []))
  }
  configReady.value = true
  configError.value = false
}

const collect = () => {
  const cfg = JSON.parse(JSON.stringify(raw.value || {}))
  if (!cfg.models || !cfg.models.length) {
    cfg.models = [{}]
  }

  const m = cfg.models[0]
  if (isGenerationModel(cfg.model_type)) {
    if (!cfg.config) cfg.config = {}
    cfg.config.generation = JSON.parse(JSON.stringify(paramsConfig.value || {}))
  } else {
    m.params = JSON.parse(JSON.stringify(paramsConfig.value || {}))
  }

  m.inputs = JSON.parse(JSON.stringify(flowData.value?.inputs || []))
  m.outputs = JSON.parse(JSON.stringify(flowData.value?.outputs || []))
  cfg.labels = labels.value.slice(0, maxLabelCount).map((item) => ({
    id: item?.id != null ? String(item.id) : '',
    name: item?.name || '',
    threshold: Array.isArray(item?.threshold) ? [...item.threshold] : [0.25, 0.25]
  }))

  return cfg
}

const handleSave = () => {
  if (!configReady.value) return
  const cfg = collect()
  const params = {
    modelCode: modelCode.value,
    configJson: JSON.stringify(cfg)
  }
  proxy.$API.saveModelConfig(params).then(() => {
    raw.value = cfg
    proxy.$message.success(t('common.operationSucceeded'))
  })
}

const handleExport = () => {
  if (!configReady.value || !isExportable.value) return
  const cfg = collect()
  const saveParams = {
    modelCode: modelCode.value,
    configJson: JSON.stringify(cfg)
  }
  proxy.$API.saveModelConfig(saveParams).then(() => {
    raw.value = cfg
    const fileName = `${modelName.value || 'model'}_${modelCode.value}.tar.gz`
    const params = {
      modelCode: modelCode.value,
      modelName: modelName.value
    }
    const x = new XMLHttpRequest()
    x.open('post', proxy.$API.exportModelConfig(), true)
    x.responseType = 'blob'
    x.setRequestHeader('Content-Type', 'application/json;charset=UTF-8')
    x.setRequestHeader('token', localStorage.getItem('token'))
    x.setRequestHeader('lang', localStorage.getItem('language'))
    x.setRequestHeader('mtk', localStorage.getItem('token'))
    x.onload = function () {
      if (this.status === 200) {
        const blob = this.response
        const a = document.createElement('a')
        const url = window.URL.createObjectURL(blob)
        a.href = url
        a.download = fileName
        a.click()
        window.URL.revokeObjectURL(url)
      } else {
        proxy.$message.error(t('common.exportFailed'))
      }
    }
    x.onerror = function () {
      proxy.$message.error(t('common.exportNetworkError'))
    }
    x.send(JSON.stringify(params))
  }).catch(() => {
    proxy.$message.error(t('common.saveFailedCannotExport'))
  })
}

const handleReset = () => {
  proxy.$confirm(t('validate.confirmResetDefault'), t('common.notice'), {
    type: 'warning'
  }).then(() => {
    const text = defaultConfigJson.value
    if (!text) {
      proxy.$message.warning(t('common.noDefaultConfig'))
      return
    }
    try {
      hydrate(parseModelConfig(text))
      proxy.$message.success(t('common.defaultRestored'))
    } catch {
      proxy.$message.error(t('validate.editFailed'))
    }
  })
}

const goBack = () => {
  router.back()
}

const queryModelConfig = () => {
  const routeModelCode = route.query?.modelCode || ''
  proxy.$API.getModelConfig({ modelCode: routeModelCode }).then((res) => {
    const { resData } = res || {}
    const text = resData?.configJson || ''
    isExportable.value = resData?.isExportable !== false
    defaultConfigJson.value = resData?.defaultConfigJson || ''
    try {
      hydrate(parseModelConfig(text))
    } catch {
      configError.value = true
    }
  }).catch(() => {
    configError.value = true
  })
}

onMounted(() => {
  queryModelConfig()
})
</script>

<style lang="scss" src="../arrangeDetail/flow/flow-palette.scss"></style>

<style scoped lang="scss">
.page {
  height: calc(100vh - 100px);
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  padding: 0 0 12px;
}

.title {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.title-span-tip {
  margin-left: 8px;
  font-size: 12px;
  color: var(--text-secondary);
}

.title-row {
  display: flex;
  align-items: baseline;
}

.model-name {
  color: var(--text-primary);
  font-size: 18px;
  font-weight: 600;
}

.model-code {
  margin-left: 8px;
  color: var(--text-secondary);
}

.tags {
  display: flex;
  gap: 8px;
}

.actions {
  display: flex;
  gap: 6px;
  align-items: center;

  :deep(.el-button + .el-button) {
    margin-left: 6px;
  }
}

.config-body {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  overflow-x: hidden;
  -webkit-overflow-scrolling: touch;
}

.card {
  margin-top: 12px;
  border-color: var(--border-color);
  box-shadow: none;

  :deep(.el-card__header) {
    background: var(--bg-white);
    border-bottom-color: var(--border-light);
    color: var(--text-primary);
  }
}

:deep(.el-card__header) {
  cursor: pointer;
}

:deep(.el-card__body) {
  padding: 0 !important;
}

.card-header {
  font-weight: 600;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.collapse-icon {
  cursor: pointer;
  transition: transform 0.2s ease;
}

.collapse-icon.rotated {
  transform: rotate(180deg);
}
</style>
