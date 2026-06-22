<template>
  <div class="container">
    <div class="wrapper-img">
      <el-image style="width: 120px; height: 186px" :src="data.pictureUrl" :preview-src-list="[data.pictureUrl]" :alt="data.pictureUrl">
      </el-image>
      <el-checkbox v-model="checked" class="check"></el-checkbox>
    </div>
    <div class="wrapper-title" :title="data.personLib.name">
      {{ t('basePic.workClothesLibLabel') }}{{data.personLib.name}}
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { t } from '@/i18n'

// Props
const props = defineProps({
  data: {
    type: Object,
    required: true
  }
})

// Emits
const emit = defineEmits(['updateTableData'])

// Reactive data
const checked = ref(false)

// Watchers
watch(() => props.data, (newValue) => {
  checked.value = newValue.checked
}, { deep: true, immediate: true })

watch(checked, (newValue) => {
  props.data.checked = checked.value
  emit('updateTableData')
})

// Methods
const handleChoice = () => {
  checked.value = !checked.value
}
</script>

<style lang="scss" scoped>
.container {
  position: relative;
  width: 120px;
  height: 186px;
  // border: 1px solid #ccc;
  .wrapper-img {
    position: relative;
    height: 158px;
    cursor: pointer;
    img {
      width: 100%;
      height: 100%;
    }
    .check {
      position: absolute;
      left: 0;
      top: -3px;
    }
  }
  .wrapper-title {
    position: absolute;
    bottom: 0;
    height: 42px;
    width: 100%;
    line-height: 42px;
    color: #fff;
    text-align: center;
    font-size: 14px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
}
</style>