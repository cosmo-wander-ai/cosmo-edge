<template>
  <div class="roi-questions">
    <el-form-item :label="t('visualQuestions.roiMode')">
      <el-select v-model="draft.mode" :aria-label="t('visualQuestions.roiMode')">
        <el-option value="inherit" :label="t('visualQuestions.inherit')" />
        <el-option value="select" :label="t('visualQuestions.select')" />
        <el-option value="prompt" :label="t('visualQuestions.independent')" />
      </el-select>
    </el-form-item>
    <el-checkbox-group v-if="draft.mode === 'select'" v-model="draft.selected">
      <el-checkbox v-for="q in questions" :key="q.id" :value="q.id">{{ q.instructions || q.id }}</el-checkbox>
    </el-checkbox-group>
    <el-input v-if="draft.mode === 'prompt'" v-model="draft.prompt" type="textarea" :rows="3" :placeholder="t('visualQuestions.prompt')" :aria-label="t('visualQuestions.prompt')" />
    <p v-if="invalid" class="error">{{ t('visualQuestions.roiInvalid') }}</p>
  </div>
</template>
<script setup>
import { computed, ref, watch } from 'vue'
import { t } from '@/i18n'
import { readRoiQuestions, writeRoiQuestions } from '@/utils/visualQuestions'
const props = defineProps({ params: { type: Array, default: () => [] }, questions: { type: Array, default: () => [] } })
const draft = ref(readRoiQuestions(props.params))
watch(() => props.params, value => { draft.value = readRoiQuestions(value) }, { deep: true })
const invalid = computed(() => draft.value.mode === 'select' ? draft.value.selected.length < 1 || draft.value.selected.length > 8 || draft.value.selected.some(id => !props.questions.some(q => q.id === id)) : draft.value.mode === 'prompt' && !draft.value.prompt?.trim())
defineExpose({ collect: () => ({ valid: !invalid.value, params: writeRoiQuestions(props.params, draft.value) }) })
</script>
<style scoped>.roi-questions { margin: 12px 20px; } .error { color: #f56c6c; }</style>
