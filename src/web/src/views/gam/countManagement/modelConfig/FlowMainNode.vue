<template>
  <div class="main-node" :style="style">
    <Handle id="left-in" type="target" position="left" />
    <Handle id="right-out" type="source" position="right" />
    <Handle id="top-in" type="target" position="top" />
    <Handle id="bottom-out" type="source" position="bottom" />
    <div class="label">{{ label }}</div>
  </div>
</template>

<script setup>
import { Handle } from '@vue-flow/core'

const props = defineProps({
  label: {
    type: String,
    default: ''
  },
  style: {
    type: Object,
    default: () => ({})
  }
})
</script>

<style scoped>
.main-node {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 180px;
  height: 80px;
  border-radius: 18px;
  color: #fff;
  border: none;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.25);
}
.label {
  font-weight: 700;
  text-align: center;
  line-height: 1.4;
  white-space: pre-line;
}
</style>
