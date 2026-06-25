<template>
  <div
    class="stage-bracket"
    :style="{
      width: `${data.width}px`,
      height: `${data.height}px`
    }"
  >
    <div class="bracket-label" :style="{ color: data.color }">
      {{ data.titleKey ? t(data.titleKey) : data.title }}
    </div>
    <div class="bracket-line" :style="{ borderColor: data.stroke }"></div>
  </div>
</template>

<script setup>
import { t } from '@/i18n'

defineProps({
  data: {
    type: Object,
    default: () => ({})
  }
})
</script>

<style scoped>
.stage-bracket {
  box-sizing: border-box;
  pointer-events: none;
  position: relative;
}

.bracket-label {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  text-align: center;
  font-size: 13px;
  font-weight: 700;
  line-height: 18px;
  white-space: nowrap;
}

.bracket-line {
  position: absolute;
  top: 24px;
  left: 0;
  right: 0;
  height: 10px;
  border-top: 2px dashed;
  border-left: 2px dashed;
  border-right: 2px dashed;
  border-bottom: none;
  border-radius: 0;
  pointer-events: none;
}
</style>
