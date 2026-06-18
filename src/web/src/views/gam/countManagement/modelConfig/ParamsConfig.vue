<template>
  <div class="params-config">
    <el-card v-if="commonFields.length" class="card" shadow="never">
      <template #header>
        <div class="card-header" @click="showCommon = !showCommon">
          <span>{{ t('glossary.commonConfig') }}</span>
          <el-icon :class="['collapse-icon', { rotated: showCommon }]">
            <ArrowDown />
          </el-icon>
        </div>
      </template>
      <div v-show="showCommon" class="card-body">
        <el-form label-width="160px">
          <template v-for="field in commonFields" :key="field.key">
            <el-form-item v-if="isFieldVisible(field)" :label="field.label">
              <el-input-number
                v-if="field.type === 'number'"
                v-model="form[field.key]"
                :disabled="isReadonlyField(field)"
                v-bind="field.numProps || {}"
              />

              <div v-else-if="field.type === 'slider01'" class="slider-row">
                <el-slider
                  v-model="form[field.key]"
                  :min="0"
                  :max="1"
                  :step="0.01"
                  :disabled="isReadonlyField(field)"
                  class="slider"
                />
                <el-input-number
                  v-model="form[field.key]"
                  :min="0"
                  :max="1"
                  :step="0.01"
                  :disabled="isReadonlyField(field)"
                  class="slider-input"
                />
              </div>

              <el-select
                v-else-if="field.type === 'select'"
                v-model="form[field.key]"
                :disabled="isReadonlyField(field)"
                class="field-control"
              >
                <el-option
                  v-for="option in field.options || []"
                  :key="String(option.value)"
                  :label="option.label"
                  :value="option.value"
                />
              </el-select>

              <el-switch
                v-else-if="field.type === 'switch'"
                v-model="form[field.key]"
                :disabled="isReadonlyField(field)"
              />

              <el-input
                v-else-if="field.type === 'textarea'"
                v-model="arrayText[field.key]"
                type="textarea"
                :rows="4"
                :disabled="isReadonlyField(field)"
                class="textarea-control"
                @change="parseArray(field.key)"
              />

              <el-input
                v-else
                v-model="arrayText[field.key]"
                :disabled="isReadonlyField(field)"
                class="field-control"
                @change="parseArray(field.key)"
              />

              <span v-if="field.desc" class="tip">{{ field.desc }}</span>
            </el-form-item>
          </template>
        </el-form>

        <div v-if="showLabels" class="labels-header">
          <div>{{ t('glossary.categoryLabels') }}</div>
          <el-button
            link
            class="add-label-btn"
            :disabled="localLabels.length >= maxLabelCount"
            @click="addLabel"
          >
            {{ t('action.addLabel') }}
          </el-button>
        </div>
        <div v-if="showLabels" class="labels-desc">{{ t('glossary.labelThresholdIdName') }}</div>
        <div v-for="(lab, idx) in localLabels" :key="idx" class="label-row">
          <el-input
            v-model="labelThresholdText[idx]"
            placeholder="[min,max]"
            class="label-threshold"
            @change="parseLabelThreshold(idx)"
          />
          <el-input v-model="lab.id" placeholder="ID" class="label-id" />
          <el-input v-model="lab.name" :placeholder="t('field.name')" class="label-name" />
          <el-button circle type="danger" size="small" @click="removeLabel(idx)">
            <el-icon>
              <CircleClose />
            </el-icon>
          </el-button>
        </div>
      </div>
    </el-card>

    <el-card v-if="advancedFields.length" class="card" shadow="never">
      <template #header>
        <div class="card-header" @click="showAdvanced = !showAdvanced">
          <span>
            <span>{{ t('glossary.advancedConfig') }}</span>
            <span class="title-span-tip">{{ t('glossary.advancedConfigTip') }}</span>
          </span>
          <el-icon :class="['collapse-icon', { rotated: showAdvanced }]">
            <ArrowDown />
          </el-icon>
        </div>
      </template>
      <el-form v-show="showAdvanced" label-width="160px" class="card-body">
        <template v-for="field in advancedFields" :key="field.key">
          <el-form-item v-if="isFieldVisible(field)" :label="field.label">
            <el-input-number
              v-if="field.type === 'number'"
              v-model="form[field.key]"
              :disabled="isReadonlyField(field)"
              v-bind="field.numProps || {}"
            />

            <div v-else-if="field.type === 'slider01'" class="slider-row">
              <el-slider
                v-model="form[field.key]"
                :min="0"
                :max="1"
                :step="0.01"
                :disabled="isReadonlyField(field)"
                class="slider"
              />
              <el-input-number
                v-model="form[field.key]"
                :min="0"
                :max="1"
                :step="0.01"
                :disabled="isReadonlyField(field)"
                class="slider-input"
              />
            </div>

            <el-select
              v-else-if="field.type === 'select'"
              v-model="form[field.key]"
              :disabled="isReadonlyField(field)"
              class="field-control"
            >
              <el-option
                v-for="option in field.options || []"
                :key="String(option.value)"
                :label="option.label"
                :value="option.value"
              />
            </el-select>

            <el-radio-group
              v-else-if="field.key === 'is_bgr'"
              v-model="form[field.key]"
              :disabled="isReadonlyField(field)"
            >
              <el-radio :value="false">RGB</el-radio>
              <el-radio :value="true">BGR</el-radio>
            </el-radio-group>

            <el-switch
              v-else-if="field.type === 'switch'"
              v-model="form[field.key]"
              :disabled="isReadonlyField(field)"
            />

            <el-input
              v-else-if="field.type === 'textarea'"
              v-model="arrayText[field.key]"
              type="textarea"
              :rows="4"
              :disabled="isReadonlyField(field)"
              class="textarea-control"
              @change="parseArray(field.key)"
            />

            <el-input
              v-else
              v-model="arrayText[field.key]"
              :disabled="isReadonlyField(field)"
              class="field-control"
              @change="parseArray(field.key)"
            />

            <span v-if="field.desc" class="tip">{{ field.desc }}</span>
          </el-form-item>
        </template>
      </el-form>
    </el-card>
  </div>
</template>

<script setup>
import { computed, nextTick, reactive, ref, watch } from 'vue'
import { ArrowDown, CircleClose } from '@element-plus/icons-vue'
import { t } from '@/i18n'

const FIELD_DEFS = {
  input_size: { label: t('glossary.modelInputSize'), type: 'array', desc: '[H, W]' },
  gravity: {
    label: t('glossary.scaleMode'),
    type: 'select',
    options: [
      { value: 0, label: t('glossary.scaleStretch') },
      { value: 1, label: t('glossary.scaleFitCenter') },
      { value: 2, label: t('glossary.scaleFitTopLeft') }
    ]
  },
  padding_color: { label: t('glossary.paddingColor'), type: 'array', desc: '[R, G, B]' },
  normalize_mean: { label: t('glossary.normalizeMean'), type: 'array', desc: '[R, G, B]' },
  normalize_scale: {
    label: t('glossary.normalizeScale'),
    type: 'number',
    numProps: { step: 0.000001, precision: 8 }
  },
  normalize_std: { label: t('glossary.normalizeStd'), type: 'array', desc: '[R, G, B]' },
  is_bgr: { label: t('glossary.colorChannel'), type: 'switch' },
  confidence_threshold: { label: t('glossary.confidenceThreshold'), type: 'slider01' },
  nms_threshold: { label: t('glossary.nmsThreshold'), type: 'slider01' },
  top_k: { label: t('glossary.maxRetained'), type: 'number', numProps: { min: 1, max: 10000 } },
  use_npu_postprocess: { label: t('glossary.npuPostProcess'), type: 'switch' },
  anchors: { label: 'Anchors', type: 'textarea', showWhen: 'use_npu_postprocess' },
  stride: { label: 'Stride', type: 'array', showWhen: 'use_npu_postprocess' },
  crop: {
    label: t('glossary.enableCropExpand'),
    type: 'switch',
    desc: t('glossary.enableCropExpandDesc')
  },
  crop_h_top: {
    label: t('glossary.top'),
    type: 'number',
    showWhen: 'crop',
    numProps: { precision: 3, step: 0.01, min: -1, max: 1 },
    desc: t('glossary.cropRatioDesc')
  },
  crop_h_bottom: {
    label: t('glossary.bottom'),
    type: 'number',
    showWhen: 'crop',
    numProps: { precision: 3, step: 0.01, min: -1, max: 1 }
  },
  crop_w_left: {
    label: t('glossary.left'),
    type: 'number',
    showWhen: 'crop',
    numProps: { precision: 3, step: 0.01, min: -1, max: 1 }
  },
  crop_w_right: {
    label: t('glossary.right'),
    type: 'number',
    showWhen: 'crop',
    numProps: { precision: 3, step: 0.01, min: -1, max: 1 }
  },
  square: { label: t('glossary.forceSquare'), type: 'switch', showWhen: 'crop' },
  square_mode: {
    label: t('glossary.squareMode'),
    type: 'select',
    showWhen: 'square',
    options: [
      { value: 0, label: 'max' },
      { value: 1, label: 'min' },
      { value: 2, label: 'avg' }
    ]
  },
  input_width: { label: t('glossary.inputWidth'), type: 'number', numProps: { min: 1, max: 4096 } },
  input_height: { label: t('glossary.inputHeight'), type: 'number', numProps: { min: 1, max: 4096 } },
  text_threshold: { label: t('glossary.textMatchThreshold'), type: 'slider01' },
  box_threshold: { label: t('glossary.boxConfidenceThreshold'), type: 'slider01' },
  use_affine_crop: { label: t('glossary.affineCrop'), type: 'switch' },
  norm_ratio: {
    label: t('glossary.normalizeRatio'),
    type: 'number',
    showWhen: 'use_affine_crop',
    numProps: { precision: 2, step: 0.1 }
  },
  norm_mode: {
    label: t('glossary.normalizeMode'),
    type: 'select',
    showWhen: 'use_affine_crop',
    options: [
      { value: 0, label: t('glossary.modeN', { n: 0 }) },
      { value: 1, label: t('glossary.modeN', { n: 1 }) },
      { value: 2, label: t('glossary.modeN', { n: 2 }) }
    ]
  },
  output_hw: { label: t('glossary.outputSize'), type: 'array', showWhen: 'use_affine_crop', desc: '[H, W]' },
  center_index: { label: t('glossary.centerPointIndex'), type: 'array', showWhen: 'use_affine_crop' },
  do_sample: { label: t('glossary.samplingMode'), type: 'switch' },
  temperature: {
    label: t('glossary.generationTemp'),
    type: 'number',
    numProps: { precision: 2, min: 0, max: 2, step: 0.1 }
  },
  top_p: {
    label: 'Top P',
    type: 'number',
    numProps: { precision: 2, min: 0, max: 1, step: 0.05 }
  },
  repetition_penalty: {
    label: t('glossary.repetitionPenalty'),
    type: 'number',
    numProps: { precision: 2, min: 0.1, max: 5, step: 0.1 }
  },
  eos_token_id: { label: 'EOS Token ID', type: 'array' },
  bos_token_id: { label: 'BOS Token ID', type: 'number', numProps: { min: 0, step: 1 } },
  pad_token_id: { label: 'PAD Token ID', type: 'number', numProps: { min: 0, step: 1 } }
}

const TYPE_SCHEMA = {
  yolov5_det: {
    common: ['input_size', 'gravity', 'confidence_threshold', 'nms_threshold', 'top_k'],
    advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr', 'use_npu_postprocess', 'anchors', 'stride']
  },
  yolov8_det: {
    common: ['input_size', 'gravity', 'confidence_threshold', 'nms_threshold', 'top_k'],
    advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr']
  },
  yolo26_det: {
    common: ['input_size', 'gravity', 'confidence_threshold', 'top_k'],
    advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr']
  },
  classify: {
    common: ['input_size', 'gravity', 'crop', 'crop_h_top', 'crop_h_bottom', 'crop_w_left', 'crop_w_right', 'square', 'square_mode'],
    advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr']
  },
  keypoints: {
    common: ['input_size', 'gravity', 'crop', 'crop_h_top', 'crop_h_bottom', 'crop_w_left', 'crop_w_right', 'square', 'square_mode'],
    advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'is_bgr']
  },
  feature: {
    common: ['use_affine_crop', 'norm_ratio', 'norm_mode', 'output_hw', 'center_index'],
    advanced: ['normalize_mean', 'normalize_scale', 'is_bgr']
  },
  dino: {
    common: ['input_width', 'input_height', 'text_threshold', 'box_threshold'],
    advanced: ['is_bgr', 'normalize_mean', 'normalize_std']
  },
  sam2: {
    common: ['input_size', 'gravity'],
    advanced: ['padding_color', 'normalize_mean', 'normalize_scale', 'normalize_std', 'is_bgr']
  },
  qwen3vl: {
    common: [
      'do_sample',
      'temperature',
      { key: 'top_k', label: t('glossary.topKSampling'), type: 'number', numProps: { min: 1, max: 200, step: 1 } },
      'top_p',
      'repetition_penalty'
    ],
    advanced: ['eos_token_id', 'bos_token_id', 'pad_token_id']
  }
}

TYPE_SCHEMA.yolov9_det = TYPE_SCHEMA.yolov8_det
TYPE_SCHEMA.yolov11_det = TYPE_SCHEMA.yolov8_det
TYPE_SCHEMA.yolov12_det = TYPE_SCHEMA.yolov8_det
TYPE_SCHEMA.qwen3_5 = TYPE_SCHEMA.qwen3vl

const props = defineProps({
  modelValue: {
    type: Object,
    default: () => ({})
  },
  modelType: {
    type: String,
    default: ''
  },
  labels: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['update:modelValue', 'update:labels'])

const showCommon = ref(true)
const showAdvanced = ref(false)
const form = reactive({})
const arrayText = reactive({})
const localLabels = ref([])
const labelThresholdText = reactive({})
let syncing = false
let syncingLabels = false

const normalizeType = computed(() => String(props.modelType || '').toLowerCase())
const schema = computed(() => TYPE_SCHEMA[normalizeType.value] || null)

const resolveField = (entry) => {
  if (typeof entry === 'string') {
    return { key: entry, ...(FIELD_DEFS[entry] || { label: entry, type: 'array' }) }
  }
  return { ...(FIELD_DEFS[entry.key] || {}), ...entry }
}

const commonFields = computed(() => (schema.value?.common || []).map(resolveField))
const advancedFields = computed(() => (schema.value?.advanced || []).map(resolveField))
const allFields = computed(() => [...commonFields.value, ...advancedFields.value])
const showLabels = computed(() => !!schema.value && !['qwen3vl', 'qwen3_5'].includes(normalizeType.value))
const readonlyFieldKeys = new Set(['input_size'])
const maxLabelCount = 80

const fieldDefault = (field) => {
  if (field.type === 'number') return field.numProps?.min ?? 0
  if (field.type === 'slider01') return 0
  if (field.type === 'switch') return false
  if (field.type === 'select') return field.options?.[0]?.value ?? ''
  return []
}

const syncArrayText = () => {
  Object.keys(arrayText).forEach((key) => delete arrayText[key])
  allFields.value.forEach((field) => {
    if (field.type === 'array' || field.type === 'textarea') {
      arrayText[field.key] = JSON.stringify(form[field.key] ?? [])
    }
  })
}

const syncForm = () => {
  syncing = true
  const source = JSON.parse(JSON.stringify(props.modelValue || {}))
  allFields.value.forEach((field) => {
    if (source[field.key] === undefined) {
      source[field.key] = fieldDefault(field)
    }
  })
  Object.keys(form).forEach((key) => {
    if (!(key in source)) delete form[key]
  })
  Object.keys(source).forEach((key) => {
    form[key] = source[key]
  })
  syncArrayText()
  nextTick(() => {
    syncing = false
  })
}

const syncLabels = () => {
  syncingLabels = true
  localLabels.value = (Array.isArray(props.labels) ? props.labels : [])
    .slice(0, maxLabelCount)
    .map((item) => ({
      id: item?.id != null ? String(item.id) : '',
      name: item?.name || '',
      threshold: Array.isArray(item?.threshold) ? [...item.threshold] : [0.25, 0.25]
    }))
  Object.keys(labelThresholdText).forEach((key) => delete labelThresholdText[key])
  localLabels.value.forEach((item, index) => {
    labelThresholdText[index] = JSON.stringify(item.threshold)
  })
  nextTick(() => {
    syncingLabels = false
  })
}

const isFieldVisible = (field) => {
  if (!field.showWhen) return true
  return !!form[field.showWhen]
}

const isReadonlyField = (field) => readonlyFieldKeys.has(field.key)

const parseArray = (key) => {
  try {
    form[key] = JSON.parse(arrayText[key] || '[]')
  } catch {
    arrayText[key] = JSON.stringify(form[key] ?? [])
  }
}

const addLabel = () => {
  if (localLabels.value.length >= maxLabelCount) return
  const nextId = localLabels.value.reduce((max, item) => {
    const id = Number(item.id)
    return Number.isNaN(id) ? max : Math.max(max, id)
  }, -1) + 1
  localLabels.value.push({
    id: String(nextId),
    name: '',
    threshold: [0.25, 0.25]
  })
  labelThresholdText[localLabels.value.length - 1] = '[0.25,0.25]'
}

const removeLabel = (index) => {
  localLabels.value.splice(index, 1)
  Object.keys(labelThresholdText).forEach((key) => delete labelThresholdText[key])
  localLabels.value.forEach((item, idx) => {
    labelThresholdText[idx] = JSON.stringify(item.threshold || [])
  })
}

const parseLabelThreshold = (index) => {
  try {
    const parsed = JSON.parse(labelThresholdText[index] || '[]')
    if (Array.isArray(parsed) && localLabels.value[index]) {
      localLabels.value[index].threshold = parsed
    }
  } catch {
    if (localLabels.value[index]) {
      labelThresholdText[index] = JSON.stringify(localLabels.value[index].threshold || [])
    }
  }
}

watch(
  () => [props.modelValue, props.modelType],
  syncForm,
  { deep: true, immediate: true }
)

watch(
  form,
  () => {
    if (syncing) return
    emit('update:modelValue', JSON.parse(JSON.stringify(form)))
  },
  { deep: true }
)

watch(
  () => props.labels,
  syncLabels,
  { deep: true, immediate: true }
)

watch(
  localLabels,
  () => {
    if (syncingLabels) return
    emit('update:labels', JSON.parse(JSON.stringify(localLabels.value.slice(0, maxLabelCount))))
  },
  { deep: true }
)
</script>

<style scoped lang="scss">
.card {
  margin-top: 12px;
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

.card-body {
  padding: 20px;
}

.collapse-icon {
  cursor: pointer;
  transition: transform 0.2s ease;
}

.collapse-icon.rotated {
  transform: rotate(180deg);
}

.title-span-tip {
  margin-left: 8px;
  font-size: 12px;
  color: #909399;
}

.field-control {
  width: 260px;
}

.textarea-control {
  width: 420px;
}

.slider-row {
  display: flex;
  align-items: center;
}

.slider {
  width: 240px;
}

.slider-input {
  margin-left: 8px;
}

.tip {
  display: block;
  color: #909399;
  font-size: 12px;
  margin-top: 4px;
}

.labels-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 20px;
  margin-bottom: 6px;
}

.labels-desc {
  color: #909399;
  font-size: 12px;
  margin-bottom: 8px;
}

.label-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.label-threshold {
  width: 220px;
}

.label-id {
  width: 60px;
}

.label-name {
  width: 160px;
}

.add-label-btn {
  font-size: 14px;
  color: var(--primary-color);
}
</style>
