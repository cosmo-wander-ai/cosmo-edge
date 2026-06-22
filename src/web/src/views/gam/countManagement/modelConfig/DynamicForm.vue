<template>
  <div class="dynamic-form">
    <el-form :label-width="labelWidth" :size="size" :disabled="disabled">
      <el-form-item v-for="item in formSchema" :key="item.key" :prop="item.key">
        <template #label>
          <span class="label-wrap">
            <span>{{ resolveI18nText(item, 'name') }}</span>
            <el-tooltip v-if="item.description" :content="resolveI18nText(item, 'description')" placement="top" effect="dark">
              <el-icon class="help-icon">
                <QuestionFilled />
              </el-icon>
            </el-tooltip>
          </span>
        </template>

        <el-input v-if="item.type === 'text'" v-model="formData[item.key]" clearable :placeholder="resolveI18nText(item, 'placeholder') || t('validate.pleaseEnter', { name: resolveI18nText(item, 'name') })" />

        <el-select v-else-if="item.type === 'select'" v-model="formData[item.key]" class="full-width" :placeholder="resolveI18nText(item, 'placeholder') || t('validate.pleaseSelect', { name: resolveI18nText(item, 'name') })">
          <el-option v-for="option in item.options" :key="option.value" :label="resolveI18nOptionLabel(option)" :value="option.value" />
        </el-select>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup>
import { computed, reactive, watch } from 'vue'
import { QuestionFilled } from '@element-plus/icons-vue'
import { t } from '@/i18n'
import { resolveI18nText, resolveI18nOptionLabel } from '@/utils/i18nResource'

const props = defineProps({
  modelValue: {
    type: Object,
    default: () => ({})
  },
  schema: {
    type: Array,
    default: () => []
  },
  labelWidth: {
    type: String,
    default: '140px'
  },
  size: {
    type: String,
    default: 'default'
  },
  disabled: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['update:modelValue', 'change'])

const formSchema = computed(() =>
  (Array.isArray(props.schema) ? props.schema : []).filter(
    (item) => item && item.key && ['text', 'select'].includes(item.type)
  )
)

const createFormData = () => {
  const result = {}
  formSchema.value.forEach((item) => {
    if (
      props.modelValue[item.key] !== undefined &&
      props.modelValue[item.key] !== null
    ) {
      result[item.key] = props.modelValue[item.key]
    } else if (item.defaultValue !== undefined && item.defaultValue !== null) {
      result[item.key] = item.defaultValue
    } else {
      result[item.key] = ''
    }
  })
  return result
}

const formData = reactive(createFormData())

const syncLocalFormData = () => {
  const next = createFormData()
  Object.keys(formData).forEach((key) => {
    if (!(key in next)) {
      delete formData[key]
    }
  })
  Object.keys(next).forEach((key) => {
    formData[key] = next[key]
  })
}

watch(
  () => [props.schema, props.modelValue],
  () => {
    syncLocalFormData()
  },
  { deep: true }
)

watch(
  formData,
  (value) => {
    const next = { ...value }
    emit('update:modelValue', next)
    emit('change', next)
  },
  { deep: true }
)
</script>

<style scoped lang="scss">
.dynamic-form {
  width: 100%;
}

.label-wrap {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.help-icon {
  color: #909399;
  font-size: 14px;
}

.full-width {
  width: 100%;
}
</style>
