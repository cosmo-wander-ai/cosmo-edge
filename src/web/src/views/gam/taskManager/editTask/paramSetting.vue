<template>
  <div class="mv-table-card-wrap" style="margin-bottom: 0px;">
    <div class="mvtitle flexjustify">
      <span v-if="showHigher">
        <el-radio v-model="isHigher" :value="false">{{ t('glossary.basicParams') }}</el-radio>
        <el-radio v-model="isHigher" :value="true">{{ t('glossary.advancedParams') }}</el-radio>
      </span>
    </div>
    <dynamicform v-model:modelValue="taskParam" :channelId="config?.channelId" :algorithmCode="algorithmCode" :alarmType="alarmType" :isHigher="isHigher" class="param-body" ref="submitForm"></dynamicform>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onBeforeUnmount, computed } from 'vue'
import { t } from '@/i18n'
import dynamicform from './dynamicForm.vue'

const props = defineProps({
  area: Object,
  config: {
    type: Object,
    default: () => ({
      taskParam: [],
      channelId: ''
    })
  },
  algorithmCode: [String, Number],
  alarmType: String,
  activeName: String
})

const isHigher = ref(false)
const showHigher = ref(false)
const submitForm = ref(null)

const taskParam = computed({
  get() {
    return props.config?.taskParam || []
  },
  set(value) {
    if (props.config) {
      props.config.taskParam = value
    }
  }
})

watch(() => props.algorithmCode, (newVal) => {
  if (newVal) {
    showHigher.value = false
    isHigher.value = false
  }
})

const validateAndCollect = async () => {
  return await submitForm.value?.validateAndCollect() || { valid: false, params: [] }
}

const collect = () => submitForm.value?.collect() || []
defineExpose({ validateAndCollect, collect })

onMounted(() => {
  document.addEventListener('keydown', handleKeyDown)
})

onBeforeUnmount(() => {
  document.removeEventListener('keydown', handleKeyDown)
})

const handleKeyDown = (e) => {
  if (props.activeName !== 'params') return
  if (
    (e.ctrlKey && e.altKey && e.key === 'g') ||
    (e.metaKey && e.ctrlKey && e.key === 'g')
  ) {
    e.preventDefault()
    showHigher.value = true
  }
}
</script>

<style scoped lang="scss">
.mvtitle {
  padding: 0px 0 0px 20px;
}
.param-body {
  min-width: 0;
  width: 100%;
  max-height: calc(100vh - 350px);
  overflow: auto;
  padding: 10px 0;
}
.flexjustify {
  display: flex;
  align-items: center;
}

.mv-table-card-wrap {
  background: #fff;
  height: 100%;
}
.rightlable {
  color: #ccc;
  font-style: normal;
  margin-left: 20px;
}
.form-wrap {
  padding: 0 30px 0 10px;
  > p {
    border-left: 3px solid var(--primary-color);
    margin-left: 15px;
    padding-left: 10px;
    line-height: 14px;
  }
  .mv-el-input {
    width: 300px;
  }
}
.obsBtoon {
  text-align: center;
  margin-top: 30px;
  .el-button + .el-button {
    margin-left: 20px;
  }
}
button {
  padding: 8px 18px;
}
.mv-table-title {
  font-weight: 500;
  margin-bottom: 0px;
  margin-right: 20px;
}
</style>
