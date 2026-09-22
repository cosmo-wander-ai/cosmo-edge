<template>
  <div class="privacy-settings">
    <el-alert :title="t('alarmPrivacy.scope')" type="info" :closable="false" show-icon />
    <el-form label-position="right" :label-width="currentLocale === 'en-US' ? '180px' : '140px'">
      <el-form-item :label="t('alarmPrivacy.enabled')">
        <el-switch :model-value="modelValue.enabled" active-value="1" inactive-value="0" @update:model-value="update('enabled', $event)" />
      </el-form-item>
      <template v-if="modelValue.enabled === '1'">
        <el-form-item :label="t('alarmPrivacy.targets')">
          <div class="privacy-targets">
            <el-radio-group :model-value="labelMode" @update:model-value="setLabelMode">
              <el-radio value="all">{{ t('alarmPrivacy.allDetectedTargets') }}</el-radio>
              <el-radio v-if="availableLabels.length || labelMode === 'selected'" value="selected" :disabled="!availableLabels.length">{{ t('alarmPrivacy.selectedTargets') }}</el-radio>
            </el-radio-group>
            <el-select v-if="labelMode === 'selected'" :model-value="selectedLabels" multiple :placeholder="t('alarmPrivacy.selectTargets')" @update:model-value="update('labels', $event.join(','))">
              <el-option v-for="label in availableLabels" :key="label" :label="label" :value="label" />
              <el-option v-for="label in missingLabels" :key="label" :label="label" :value="label" disabled />
            </el-select>
            <p class="privacy-tip">{{ t('alarmPrivacy.targetsTip') }}</p>
            <p v-if="missingLabels.length" class="privacy-error">{{ t('alarmPrivacy.unavailableTargets', { labels: missingLabels.join(', ') }) }}</p>
          </div>
        </el-form-item>
        <el-form-item :label="t('alarmPrivacy.strength')">
          <el-radio-group :model-value="modelValue.strength" @update:model-value="update('strength', $event)">
            <el-radio value="1">{{ t('alarmPrivacy.strengthStandard') }}</el-radio>
            <el-radio value="2">{{ t('alarmPrivacy.strengthStrong') }}</el-radio>
            <el-radio value="3">{{ t('alarmPrivacy.strengthMaximum') }}</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item>
          <p class="privacy-tip">{{ t('alarmPrivacy.outputPolicy') }}</p>
        </el-form-item>
      </template>
    </el-form>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { t, currentLocale } from '@/i18n'

const props = defineProps({
  modelValue: { type: Object, required: true },
  availableLabels: { type: Array, default: () => [] }
})
const emit = defineEmits(['update:modelValue'])
const labelMode = computed(() => props.modelValue.labels === '*' ? 'all' : 'selected')
const selectedLabels = computed(() => labelMode.value === 'all'
  ? []
  : props.modelValue.labels.split(',').map((label) => label.trim()).filter(Boolean))
const missingLabels = computed(() => selectedLabels.value.filter((label) =>
  !props.availableLabels.includes(label)
))

const update = (key, value) => emit('update:modelValue', { ...props.modelValue, [key]: value })
const setLabelMode = (mode) => update('labels', mode === 'all' ? '*' : props.availableLabels[0] || '')
</script>

<style scoped lang="scss">
.privacy-settings {
  max-width: 880px;
  padding: 20px 30px;

  .el-form {
    margin-top: 24px;
  }
}
.privacy-targets {
  width: 100%;

  .el-radio-group {
    display: flex;
    flex-wrap: wrap;
  }

  .el-select {
    display: block;
    max-width: 420px;
    margin: 8px 0;
  }
}
.privacy-tip,
.privacy-error {
  margin: 4px 0 0;
  line-height: 1.65;
}
.privacy-tip {
  color: var(--el-text-color-secondary);
}
.privacy-error {
  color: var(--el-color-danger);
}
</style>
