<template>
  <FlowNodeIcon :kind="iconKind" />
</template>

<script setup>
import { computed } from 'vue'
import FlowNodeIcon from '../arrangeDetail/flow/FlowNodeIcon.vue'

const props = defineProps({ type: { type: String, default: 'other' } })

// Repository metadata keeps choosing the type; common capabilities share the
// same glyph as their orchestration nodes.
const iconKind = computed(() => ({
  detect: 'target',
  classify: 'grid'
})[props.type] || props.type)
</script>
