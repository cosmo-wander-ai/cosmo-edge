<template>
  <svg class="scene-task-icon" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" aria-hidden="true" focusable="false" :data-scene-icon="kind">
    <path v-for="(path, index) in paths" :key="index" :d="path" />
  </svg>
</template>

<script setup>
import { computed } from 'vue'
import { sceneTaskIcon, SCENE_TASK_ICON_PATHS } from '@/utils/sceneTaskIcons'

const props = defineProps({ task: { type: Object, default: () => ({}) } })
const kind = computed(() => sceneTaskIcon(props.task))
const paths = computed(() => SCENE_TASK_ICON_PATHS[kind.value])
</script>

<style scoped>
.scene-task-icon {
  display: block;
  width: 24px;
  height: 24px;
  flex-shrink: 0;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.6;
  stroke-linecap: round;
  stroke-linejoin: round;
}
</style>
