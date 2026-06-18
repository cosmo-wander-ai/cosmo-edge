<template>
  <el-input v-model="_value" @blur="handlerBlur" v-bind="$attrs" size="small"></el-input>
</template>

<script setup>
import { computed } from 'vue'

// 定义组件名称
defineOptions({
  name: 'MiniInput',
  inheritAttrs: false
})

// Props
const props = defineProps({
  modelValue: {
    type: String,
    default: ''
  }
})

// Emits
const emit = defineEmits(['update:modelValue'])

// Computed
const _value = computed({
  get() {
    return props.modelValue
  },
  set(val) {
    emit('update:modelValue', val)
  }
})

// Methods
function handlerBlur() {
  emit('update:modelValue', _value.value.trim())
}
</script>

<style scoped>
</style>