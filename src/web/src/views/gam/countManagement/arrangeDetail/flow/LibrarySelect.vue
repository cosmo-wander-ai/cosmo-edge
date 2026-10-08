<template>
  <el-select
    v-model="selectedIds"
    multiple
    filterable
    clearable
    :loading="loading"
    size="small"
    style="width: 100%"
    @visible-change="visible => visible && loadLibraries()"
  >
    <el-option v-for="library in libraries" :key="library.id" :label="library.name" :value="String(library.id)" />
  </el-select>
</template>

<script setup>
import { computed, getCurrentInstance, onBeforeUnmount, ref, watch } from 'vue'

const props = defineProps({
  modelValue: { type: [String, Number], default: '' },
  type: { type: String, required: true }
})
const emit = defineEmits(['update:modelValue'])
const { proxy } = getCurrentInstance()
const libraries = ref([])
const loading = ref(false)
let requestVersion = 0

// Workflow parameters store library IDs as a comma-separated string.
const selectedIds = computed({
  get: () => String(props.modelValue ?? '').split(',').map(id => id.trim()).filter(Boolean),
  set: ids => emit('update:modelValue', ids.map(String).join(','))
})

const loadLibraries = async () => {
  const version = ++requestVersion
  const isFace = props.type === 'faceSet'
  loading.value = true
  try {
    const query = isFace ? proxy.$API.boxQueryFaceLibInfo : proxy.$API.boxQueryPersonLibInfo
    const { resData } = await query({ pageNum: 1, pageSize: 1000 })
    if (version !== requestVersion) return
    libraries.value = (isFace ? resData?.faceLibList : resData?.personLibList) || []
  } catch {
    // The request layer reports API errors; retain the saved binding on failure.
  } finally {
    if (version === requestVersion) loading.value = false
  }
}

watch(() => props.type, () => {
  libraries.value = []
  loadLibraries()
}, { immediate: true })
onBeforeUnmount(() => { requestVersion++ })
</script>
