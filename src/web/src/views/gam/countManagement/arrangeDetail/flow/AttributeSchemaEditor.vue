<template>
  <div class="attribute-schema-editor">
    <el-alert :title="t('attributeAnalysis.mappingHint')" type="info" :closable="false" />
    <el-form-item :label="t('attributeAnalysis.objectType')">
      <el-select v-model="schema.objectType" data-field="object-type" @change="save">
        <el-option v-for="option in objectTypes" :key="option.value" :value="option.value" :label="option.name" />
      </el-select>
    </el-form-item>
    <el-card v-for="(attribute, index) in schema.attributes" :key="index" class="attribute-card" shadow="never">
      <div class="attribute-fields">
        <el-form-item :label="t('attributeAnalysis.name')">
          <el-select v-model="attribute.name" data-field="attribute-name" filterable allow-create default-first-option @change="save">
            <el-option v-for="name in nameChoices(attribute)" :key="name" :value="name" :label="name" />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('attributeAnalysis.source')">
          <el-select v-model="attribute.sourceNode" data-field="source" @change="sourceChanged(attribute)" filterable>
            <el-option v-for="source in sources" :key="source.position" :value="String(source.position)" :label="sourceName(source)" />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('attributeAnalysis.type')">
          <el-select v-model="attribute.type" data-field="type" @change="typeChanged(attribute)"><el-option value="single" :label="t('attributeAnalysis.single')" /><el-option value="multiple" :label="t('attributeAnalysis.multiple')" /><el-option value="binary" :label="t('attributeAnalysis.binary')" /></el-select>
        </el-form-item>
        <el-form-item :label="t('attributeAnalysis.threshold')">
          <el-select v-model="attribute.threshold" data-field="threshold" @change="save"><el-option v-for="option in thresholdChoices(attribute)" :key="option.value" :value="option.value" :label="option.name" /></el-select>
        </el-form-item>
        <el-form-item :label="t('attributeAnalysis.ratio')">
          <el-select v-model="attribute.minRatio" data-field="ratio" @change="save"><el-option v-for="value in unique([attribute.minRatio, 0.6, 0.7, 0.8, 0.9, 1])" :key="value" :value="value" :label="percent(value)" /></el-select>
        </el-form-item>
      </div>
      <el-alert v-if="attribute.type === 'single' && attribute.options.length === 1" :title="t('attributeAnalysis.positiveOnlyHint')" type="warning" :closable="false" />
      <template v-if="attribute.type === 'binary'">
        <el-alert :title="t('attributeAnalysis.binaryHint')" type="info" :closable="false" />
        <el-form-item :label="t('attributeAnalysis.modelLabel')">
          <el-select :model-value="attribute.options[0].label" data-field="binary-label" filterable @change="binaryLabelChanged(attribute, $event)">
            <el-option v-for="label in labelChoices(attribute, attribute.options[0].label)" :key="label.label" :value="label.label" :label="label.name" :disabled="label.unavailable" />
          </el-select>
        </el-form-item>
        <div class="attribute-fields">
          <el-form-item v-for="(option, optionIndex) in attribute.options" :key="optionIndex" :label="optionIndex === 0 ? t('attributeAnalysis.positiveName') : t('attributeAnalysis.negativeName')">
            <el-select v-model="option.name" :data-field="optionIndex === 0 ? 'positive-name' : 'negative-name'" filterable allow-create default-first-option @change="save">
              <el-option v-for="name in outcomeNames(attribute, optionIndex)" :key="name" :value="name" :label="name" />
            </el-select>
          </el-form-item>
        </div>
      </template>
      <el-table v-else :data="attribute.options" size="small">
        <el-table-column :label="t('attributeAnalysis.modelLabel')"><template #default="{ row, $index }">
          <el-select :model-value="row.label" data-field="option-label" filterable @change="optionLabelChanged(attribute, $index, $event)">
            <el-option v-for="label in labelChoices(attribute, row.label)" :key="label.label" :value="label.label" :label="label.name" :disabled="label.unavailable || attribute.options.some((o, i) => i !== $index && o.label === label.label)" />
          </el-select>
        </template></el-table-column>
        <el-table-column :label="t('attributeAnalysis.name')"><template #default="{ row }">
          <el-select v-model="row.name" data-field="option-name" filterable allow-create default-first-option @change="save"><el-option v-for="name in unique([row.name, modelLabels(attribute).find(label => label.label === row.label)?.name])" :key="name" :value="name" :label="name" /></el-select>
        </template></el-table-column>
        <el-table-column width="80"><template #default="{ $index }"><el-button link type="danger" @click="attribute.options.splice($index, 1); save()">{{ t('action.delete') }}</el-button></template></el-table-column>
      </el-table>
      <el-button v-if="attribute.type !== 'binary'" data-field="add-option" @click="addOption(attribute)" :disabled="attribute.options.length >= 64 || !unusedLabels(attribute).length">{{ t('attributeAnalysis.addOption') }}</el-button>
      <el-collapse><el-collapse-item :title="t('attributeAnalysis.customParameters')">
        <div class="attribute-fields">
          <el-form-item :label="t('attributeAnalysis.threshold')"><el-input-number v-model="attribute.threshold" :min="0" :max="1" :step="0.01" @change="save" /></el-form-item>
          <el-form-item :label="t('attributeAnalysis.ratio')"><el-input-number v-model="attribute.minRatio" :min="0.51" :max="1" :step="0.01" @change="save" /></el-form-item>
        </div>
      </el-collapse-item></el-collapse>
      <el-button type="danger" plain @click="schema.attributes.splice(index, 1); save()">{{ t('attributeAnalysis.removeAttribute') }}</el-button>
    </el-card>
    <el-button data-field="add-attribute" @click="add" :disabled="schema.attributes.length >= 32">{{ t('attributeAnalysis.addAttribute') }}</el-button>
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
const unique = values => [...new Set(values.filter(value => value !== '' && value != null))]
const percent = value => `${+(value * 100).toFixed(2)}%`
const negativeName = name => t('attributeAnalysis.notLabel', { name })
const objectTypes = computed(() => {
  const options = [{ value: 'person', name: t('attributeAnalysis.person') }, { value: 'vehicle', name: t('attributeAnalysis.vehicle') }, { value: 'face', name: t('attributeAnalysis.face') }, { value: 'object', name: t('attributeAnalysis.otherObject') }]
  if (schema.value.objectType && !options.some(option => option.value === schema.value.objectType)) options.push({ value: schema.value.objectType, name: schema.value.objectType })
  return options
})
const sourceFor = attribute => sources.value.find(source => String(source.position) === attribute.sourceNode)
const sourceName = source => {
  const sameName = sources.value.filter(item => item.atomicName === source.atomicName)
  return sameName.length > 1 ? `${source.atomicName} · ${sameName.indexOf(source) + 1}` : source.atomicName
}
const modelLabels = attribute => {
  const labels = (sourceFor(attribute)?.labelList || []).filter(label => label.used !== false && label.class_name)
  return [...new Map(labels.map(label => [String(label.class_name), { label: String(label.class_name), name: label.nameCN || label.class_name, thresholds: (Array.isArray(label.threshold) ? label.threshold : [label.threshold]).filter(value => Number.isFinite(value) && value >= 0 && value <= 1) }])).values()]
}
const labelChoices = (attribute, current) => {
  const labels = modelLabels(attribute)
  return current && !labels.some(label => label.label === current) ? [...labels, { label: current, name: t('attributeAnalysis.unavailableLabel', { name: current }), unavailable: true }] : labels
}
const nameChoices = attribute => unique([attribute.name, sourceFor(attribute)?.atomicName, ...modelLabels(attribute).map(label => label.name)])
const outcomeNames = (attribute, index) => {
  const name = modelLabels(attribute).find(label => label.label === attribute.options[0]?.label)?.name || attribute.name
  return unique([attribute.options[index]?.name, ...(index === 0 ? [attribute.name, name, t('attributeAnalysis.yes')] : [negativeName(attribute.name || name), negativeName(name), t('attributeAnalysis.no')])])
}
const thresholdChoices = attribute => {
  const model = attribute.type === 'binary' ? modelLabels(attribute).find(label => label.label === attribute.options[0]?.label) : null
  const choices = (model?.thresholds || []).map((value, index) => ({ value, name: `${index === 0 ? t('attributeAnalysis.strictThreshold') : t('attributeAnalysis.recommendedThreshold')} · ${percent(value)}` }))
  for (const value of unique([attribute.threshold, 0.3, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95])) if (!choices.some(option => option.value === value)) choices.push({ value, name: percent(value) })
  return choices
}
const add = () => {
  let index = 1
  while (schema.value.attributes.some(attribute => attribute.key === `attribute_${index}`)) index++
  schema.value.attributes.push({ key: `attribute_${index}`, name: '', sourceNode: '', modelCode: '', type: 'single', threshold: 0.5, minRatio: 0.6, options: [] })
  save()
}
const valueKey = (label, options) => {
  const base = /^[A-Za-z0-9_.-]{1,64}$/.test(label) ? label : 'value'
  let key = base, index = 2
  while (options.some(option => option.value === key)) key = `${base.slice(0, 56)}_${index++}`
  return key
}
const unusedLabels = attribute => modelLabels(attribute).filter(label => !attribute.options.some(option => option.label === label.label))
const optionLabelChanged = (attribute, index, selected) => {
  const label = modelLabels(attribute).find(label => label.label === selected)
  if (!label || attribute.options.some((option, i) => i !== index && option.label === selected)) return
  if (attribute.options[index]?.label === selected) return
  attribute.options[index] = { label: selected, value: valueKey(selected, attribute.options.filter((_, i) => i !== index)), name: label.name }
  save()
}
const addOption = attribute => {
  const label = unusedLabels(attribute)[0]
  if (!label || attribute.options.length >= 64) return
  attribute.options.push({ label: label.label, value: valueKey(label.label, attribute.options), name: label.name })
  save()
}
const binaryLabelChanged = (attribute, selected) => {
  const label = modelLabels(attribute).find(label => label.label === selected)
  if (!label) return
  const previous = modelLabels(attribute).find(label => label.label === attribute.options[0]?.label)
  const previousName = attribute.name
  if (!attribute.name || attribute.name === previous?.name || attribute.name === sourceFor(attribute)?.atomicName) attribute.name = label.name
  const positive = attribute.options[0], negative = attribute.options[1]
  if (!positive.name || [t('attributeAnalysis.yes'), previous?.name, previousName].includes(positive.name)) positive.name = label.name
  if (!negative.name || [t('attributeAnalysis.no'), negativeName(previous?.name || previousName), negativeName(previousName)].includes(negative.name)) negative.name = negativeName(label.name)
  positive.label = selected
  if (label.thresholds.length) attribute.threshold = label.thresholds[0]
  save()
}
const typeChanged = (attribute) => {
  if (attribute.type === 'binary') {
    const positive = attribute.options.find(option => option.label)
    const label = modelLabels(attribute).find(label => label.label === positive?.label)
    if (label && (!attribute.name || attribute.name === sourceFor(attribute)?.atomicName)) attribute.name = label.name
    const name = attribute.name || label?.name || t('attributeAnalysis.yes')
    attribute.options = [
      { label: positive?.label || '', value: positive?.value || 'yes', name: !positive?.name || positive.name === t('attributeAnalysis.yes') ? name : positive.name },
      { label: '', value: positive?.value === 'no' ? 'negative' : 'no', name: negativeName(name) }
    ]
  } else attribute.options = attribute.options.filter(option => option.label)
  save()
}
const sourceChanged = (attribute) => {
  const source = sourceFor(attribute)
  attribute.modelCode = String(source?.atomicCode || '')
  if (!attribute.name) attribute.name = source?.atomicName || ''
  const labels = modelLabels(attribute)
  if (attribute.type === 'binary') {
    if (!labels.some(label => label.label === attribute.options[0].label)) {
      if (labels.length) binaryLabelChanged(attribute, labels[0].label)
      else attribute.options[0].label = ''
    }
  } else {
    attribute.options = []
    for (const label of labels.slice(0, 64)) attribute.options.push({ label: label.label, value: valueKey(label.label, attribute.options), name: label.name })
  }
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
