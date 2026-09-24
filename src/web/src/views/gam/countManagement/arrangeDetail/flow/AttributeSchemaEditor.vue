<template>
  <div class="attribute-schema-editor">
    <el-alert :title="t('attributeAnalysis.mappingHint')" type="info" :closable="false" />
    <el-form-item :label="t('attributeAnalysis.objectType')">
      <el-input v-model="schema.objectType" maxlength="64" @change="save" />
    </el-form-item>
    <el-card v-for="(attribute, index) in schema.attributes" :key="index" class="attribute-card" shadow="never">
      <div class="attribute-fields">
        <el-form-item :label="t('attributeAnalysis.key')"><el-input v-model="attribute.key" maxlength="64" @change="save" /></el-form-item>
        <el-form-item :label="t('attributeAnalysis.name')"><el-input v-model="attribute.name" maxlength="64" @change="save" /></el-form-item>
        <el-form-item :label="t('attributeAnalysis.source')">
          <el-select v-model="attribute.sourceNode" @change="sourceChanged(attribute)" filterable>
            <el-option v-for="source in sources" :key="source.position" :value="String(source.position)" :label="`${source.atomicName} (${source.position})`" />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('attributeAnalysis.type')">
          <el-select v-model="attribute.type" @change="typeChanged(attribute)"><el-option value="single" :label="t('attributeAnalysis.single')" /><el-option value="multiple" :label="t('attributeAnalysis.multiple')" /><el-option value="binary" :label="t('attributeAnalysis.binary')" /></el-select>
        </el-form-item>
        <el-form-item :label="t('attributeAnalysis.threshold')"><el-input-number v-model="attribute.threshold" :min="0" :max="1" :step="0.05" @change="save" /></el-form-item>
        <el-form-item :label="t('attributeAnalysis.ratio')"><el-input-number v-model="attribute.minRatio" :min="0.51" :max="1" :step="0.05" @change="save" /></el-form-item>
      </div>
      <template v-if="attribute.type === 'binary'">
        <el-alert :title="t('attributeAnalysis.binaryHint')" type="info" :closable="false" />
        <el-form-item :label="t('attributeAnalysis.modelLabel')"><el-input v-model="attribute.options[0].label" @change="save" /></el-form-item>
        <div v-for="(option, optionIndex) in attribute.options" :key="optionIndex" class="attribute-fields">
          <el-form-item :label="optionIndex === 0 ? t('attributeAnalysis.positiveValue') : t('attributeAnalysis.negativeValue')"><el-input v-model="option.value" @change="save" /></el-form-item>
          <el-form-item :label="optionIndex === 0 ? t('attributeAnalysis.positiveName') : t('attributeAnalysis.negativeName')"><el-input v-model="option.name" @change="save" /></el-form-item>
        </div>
      </template>
      <el-table v-else :data="attribute.options" size="small">
        <el-table-column :label="t('attributeAnalysis.modelLabel')"><template #default="{ row }"><el-input v-model="row.label" @change="save" /></template></el-table-column>
        <el-table-column :label="t('attributeAnalysis.value')"><template #default="{ row }"><el-input v-model="row.value" @change="save" /></template></el-table-column>
        <el-table-column :label="t('attributeAnalysis.name')"><template #default="{ row }"><el-input v-model="row.name" @change="save" /></template></el-table-column>
        <el-table-column width="80"><template #default="{ $index }"><el-button link type="danger" @click="attribute.options.splice($index, 1); save()">{{ t('action.delete') }}</el-button></template></el-table-column>
      </el-table>
      <el-button v-if="attribute.type !== 'binary'" @click="attribute.options.push({ label: '', value: '', name: '' }); save()" :disabled="attribute.options.length >= 64">{{ t('attributeAnalysis.addOption') }}</el-button>
      <el-button type="danger" plain @click="schema.attributes.splice(index, 1); save()">{{ t('attributeAnalysis.removeAttribute') }}</el-button>
    </el-card>
    <el-button @click="add" :disabled="schema.attributes.length >= 32">{{ t('attributeAnalysis.addAttribute') }}</el-button>
    <el-alert v-if="!validAttributeSchema(schema)" :title="t('attributeAnalysis.invalidSchema')" type="warning" :closable="false" />
  </div>
</template>
<script setup>
import { computed, ref, watch } from 'vue'
import { t } from '@/i18n'
import { parseAttributeSchema, validAttributeSchema } from '@/utils/attributeAnalysis'
const props = defineProps({ modelValue: String, atomicList: { type: Array, default: () => [] } })
const emit = defineEmits(['update:modelValue'])
const schema = ref(parseAttributeSchema(props.modelValue))
watch(() => props.modelValue, value => { if (value !== JSON.stringify(schema.value)) schema.value = parseAttributeSchema(value) })
const sources = computed(() => props.atomicList.filter(a => a.position != null))
const save = () => emit('update:modelValue', JSON.stringify(schema.value))
const add = () => { schema.value.attributes.push({ key: `attribute_${schema.value.attributes.length + 1}`, name: '', sourceNode: '', modelCode: '', type: 'single', threshold: 0.5, minRatio: 0.6, options: [] }); save() }
const typeChanged = (attribute) => {
  if (attribute.type === 'binary') {
    const positive = attribute.options.find(option => option.label)
    attribute.options = [
      { label: positive?.label || '', value: positive?.value || 'yes', name: positive?.name || t('attributeAnalysis.yes') },
      { label: '', value: positive?.value === 'no' ? 'negative' : 'no', name: t('attributeAnalysis.no') }
    ]
  } else attribute.options = attribute.options.filter(option => option.label)
  save()
}
const sourceChanged = (attribute) => {
  const source = sources.value.find(a => String(a.position) === attribute.sourceNode)
  attribute.modelCode = String(source?.atomicCode || '')
  const options = (source?.labelList || []).map((label, index) => ({ label: String(label.class_name), value: /^[A-Za-z0-9_.-]{1,64}$/.test(label.class_name) ? label.class_name : `value_${index + 1}`, name: label.nameCN || label.class_name }))
  if (attribute.type === 'binary') {
    const label = options.find(option => option.label === attribute.options[0].label) || options[0]
    attribute.options[0].label = label?.label || ''
  } else attribute.options = options
  save()
}
</script>
<style scoped>
.attribute-schema-editor { width: 100%; min-width: 340px; }
.attribute-card { margin: 12px 0; }
.attribute-fields { display: grid; gap: 8px; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); }
.attribute-fields :deep(.el-form-item) { display: block; margin: 0; }
.attribute-fields :deep(.el-select) { width: 100%; }
</style>
