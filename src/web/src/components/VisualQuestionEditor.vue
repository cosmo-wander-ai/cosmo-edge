<template>
  <div class="visual-questions">
    <el-alert v-if="parseFailed" :title="t('visualQuestions.invalid')" type="error" :closable="false" />
    <template v-else>
      <p class="hint">{{ t('visualQuestions.hint') }}</p>
      <div v-for="(q, index) in catalog.questions" :key="q.id" class="question">
        <div class="question-title">
          <span>{{ t('visualQuestions.question') }} {{ index + 1 }}</span>
          <el-select :model-value="q.type" @update:model-value="changeType(index, $event)" :aria-label="t('visualQuestions.type')">
            <el-option value="noul" :label="t('visualQuestions.boolean')" />
            <el-option value="choice" :label="t('visualQuestions.choice')" />
          </el-select>
          <el-button link type="danger" @click="remove(index)">{{ t('action.delete') }}</el-button>
        </div>
        <el-input :model-value="q.instructions" @update:model-value="edit(index, 'instructions', $event)" type="textarea" :rows="2" :aria-label="t('visualQuestions.prompt')" :placeholder="t('visualQuestions.prompt')" />
        <div v-if="q.type === 'choice'" class="options">
          <div v-for="(option, optionIndex) in options(q)" :key="optionIndex" class="option">
            <el-input :model-value="option.label" @update:model-value="editOption(index, optionIndex, 'label', $event)" :placeholder="t('visualQuestions.option')" />
            <el-input :model-value="option.description" @update:model-value="editOption(index, optionIndex, 'description', $event)" :placeholder="t('visualQuestions.description')" />
            <el-button link @click="removeOption(index, optionIndex)" :disabled="options(q).length <= 2">{{ t('action.delete') }}</el-button>
          </div>
          <el-button link @click="addOption(index)" :disabled="options(q).length >= 16">{{ t('visualQuestions.addOption') }}</el-button>
        </div>
        <el-checkbox :model-value="catalog.default.includes(q.id)" @update:model-value="select(q.id, $event)">{{ t('visualQuestions.default') }}</el-checkbox>
      </div>
      <el-button @click="add" :disabled="catalog.questions.length >= 32">{{ t('visualQuestions.add') }}</el-button>
      <div class="decision-policy">
        <label>{{ t('visualQuestions.decisionMode') }}</label>
        <el-select :model-value="catalog.decision?.mode || 'review'" @update:model-value="setMode" :aria-label="t('visualQuestions.decisionMode')">
          <el-option value="review" :label="t('visualQuestions.reviewOnly')" />
          <el-option value="filter" :label="t('visualQuestions.filterAccepted')" :disabled="profiles.length === 0" />
        </el-select>
        <el-select v-if="catalog.decision?.mode === 'filter'" :model-value="catalog.decision.profile_id" @update:model-value="setProfile" :aria-label="t('visualQuestions.acceptedPolicy')">
          <el-option v-for="profile in profiles" :key="profile.id" :value="profile.id" :label="profile.id" />
        </el-select>
        <p class="hint">{{ t('visualQuestions.policyHint') }}</p>
      </div>
      <p v-if="!validCatalog(modelValue)" class="error">{{ t('visualQuestions.invalid') }}</p>
    </template>
  </div>
</template>
<script setup>
import { computed, getCurrentInstance, onMounted, ref } from 'vue'
import { t } from '@/i18n'
import { emptyCatalog, readCatalog, validCatalog } from '@/utils/visualQuestions'
const props = defineProps({ modelValue: { type: String, default: '' } })
const emit = defineEmits(['update:modelValue'])
const { proxy } = getCurrentInstance()
const profiles = ref([])
onMounted(async () => {
  if (!proxy.$API?.boxQueryLayaReview) return
  try {
    const response = await proxy.$API.boxQueryLayaReview({ format: 'typed-v1', pageNum: 1, pageSize: 1 })
    if (response?.resCode === 1 && Array.isArray(response.resData?.runtime?.accepted_profiles)) profiles.value = response.resData.runtime.accepted_profiles
  } catch { /* Review remains available if no accepted policy can be loaded. */ }
})
const setMode = mode => update(c => { c.decision = { mode, profile_id: c.decision?.profile_id || (mode === 'filter' ? profiles.value[0]?.id || '' : '') } })
const setProfile = id => update(c => { c.decision = { mode: 'filter', profile_id: id } })
const parseFailed = computed(() => { try { readCatalog(props.modelValue); return false } catch { return true } })
const catalog = computed(() => { try { return readCatalog(props.modelValue) } catch { return emptyCatalog() } })
const update = fn => { const next = readCatalog(props.modelValue); fn(next); emit('update:modelValue', JSON.stringify(next)) }
const options = q => Array.isArray(q.criteria) ? q.criteria : Object.entries(q.criteria || {}).map(([label, description]) => ({ label, description }))
const edit = (i, key, value) => update(c => { c.questions[i][key] = value; c.questions[i].version++ })
const changeType = (i, value) => update(c => { const q = c.questions[i]; q.type = value; q.version++; delete q.labels; delete q.criteria; if (value === 'choice') q.criteria = [{ label: '', description: '' }, { label: '', description: '' }] })
const editOption = (i, j, key, value) => update(c => { const q = c.questions[i]; q.criteria = options(q); q.criteria[j][key] = value; q.version++ })
const addOption = i => update(c => { c.questions[i].criteria = [...options(c.questions[i]), { label: '', description: '' }]; c.questions[i].version++ })
const removeOption = (i, j) => update(c => { c.questions[i].criteria = options(c.questions[i]).filter((_, n) => n !== j); c.questions[i].version++ })
const select = (id, enabled) => update(c => { c.default = enabled ? [...c.default, id] : c.default.filter(x => x !== id) })
const add = () => update(c => { let n = 1; while (c.questions.some(q => q.id === `question-${n}`)) n++; const id = `question-${n}`; c.questions.push({ id, version: 1, type: 'noul', instructions: '' }); if (c.default.length < 8) c.default.push(id) })
const remove = i => update(c => { const id = c.questions[i].id; c.questions.splice(i, 1); c.default = c.default.filter(x => x !== id) })
</script>
<style scoped>
.visual-questions { width: 100%; min-width: 300px; max-width: 650px; }
.hint { color: var(--theme-text-secondary, #606266); line-height: 1.5; }
.question { padding: 12px; margin: 10px 0; border: 1px solid var(--theme-border, #dcdfe6); border-radius: 6px; }
.question-title, .option { display: flex; gap: 8px; align-items: center; margin-bottom: 8px; }
.question-title .el-select { width: 150px; }
.options { margin-top: 10px; }
.decision-policy { margin-top: 16px; }
.decision-policy .el-select { margin-top: 8px; }
.error { color: var(--theme-danger, #f56c6c); }
</style>
