<template>
  <div class="topBar-wrap">
    <div class="form-search">
      <div class="formDiv">
        <span class="formTitle">{{ t('field.channelName') }}{{ localeColon }}</span>
        <el-input class="el-form" v-model="formData.channelName" :placeholder="t('placeholder.enter', { field: t('field.channelName') })" size="small"></el-input>
      </div>
      <div class="formDiv" v-if="active">
        <span class="formTitle">{{ t('field.channelStatus') }}{{ localeColon }}</span>
        <el-select class="el-form" v-model="formData.channelStatus" :placeholder="t('placeholder.select', { field: t('field.channelStatus') })" size="small">
          <el-option v-for="item in channelStatusList" :key="item.value" :label="item.label" :value="item.value">
          </el-option>
        </el-select>
      </div>
    </div>
    <div class="btnBar">
      <el-button class="mv-el-button" type="primary" size="small" @click="getFormData">{{ t('action.search') }}</el-button>
      <el-button size="small" @click="resetFormData">{{ t('action.reset') }}</el-button>
    </div>
  </div>
</template>

<script setup>
import { reactive, computed, onMounted } from 'vue'
import { t, localeColon } from '@/i18n'

const props = defineProps({
  active: {
    type: Boolean,
    default: false
  }
})
const emit = defineEmits(['init', 'search', 'reset1', 'reset'])
const formData = reactive({
  channelName: '',
  taskStatus: '',
  status: '',
  channelType: '',
  channelStatus: -1,
  algorithmUsage: ''
})

const channelStatusList = computed(() => [
  { label: t('common.all'), value: -1 },
  { label: t('status.offline'), value: 0 },
  { label: t('status.online'), value: 1 }
])

const init = () => {
  emit('init', formData)
}

const getFormData = (id) => {
  window.localStorage.setItem('taskCustId', formData.custId)
  const joinType = window.localStorage.getItem('joinType')
  let regionId = null
  if (id && typeof id === 'number') regionId = id
  emit('search', formData, joinType, regionId)
}

const resetFormData = () => {
  formData.channelName = ''
  window.localStorage.setItem('joinType', '1')
  formData.custId = ''
  formData.algorithmId = ''
  formData.algorithmUsage = ''
  formData.taskStatus = ''
  formData.channelStatus = -1
  emit('reset1')
  emit('reset', formData)
  localStorage.setItem('currentCustId', '')
}

onMounted(() => {
  init()
})
</script>

<style scoped lang="scss">
.topBar-wrap {
  flex-shrink: 0;
  position: relative;
  background: var(--theme-surface, #fff);
  padding: 14px 18px;
  margin: 0;
  border-radius: 6px;
  display: flex;
  align-items: center;
  gap: 12px 24px;
  flex-wrap: wrap;

  .form-search {
    flex: 1;
    display: flex;
    flex-wrap: wrap;
    align-content: flex-start;
    gap: 12px 24px;

    .formDiv {
      display: flex;
      align-items: center;
      margin: 0;

      .formTitle {
        display: inline-block;
        margin-right: 10px;
        font-size: 13px;
        text-align: right;
        color: var(--text-primary);
        white-space: nowrap;
      }

      .el-form {
        width: 180px;
      }

      .el-formDate {
        width: 420px;
      }

      .doublesNum {
        .doublesText-line {
          width: 30px;
          height: 32px;
          line-height: 32px;
          text-align: center;
        }
      }
    }
  }

  .btnBar {
    flex-shrink: 0;
    display: flex;
    align-items: center;
    margin-left: auto;

    .optionBtn {
      margin-left: 10px;
      line-height: 32px;
      color: var(--theme-accent, #1890ff);
      font-size: 14px;
      cursor: pointer;

      >i {
        display: inline-block;
        transition: all 300ms;

        &.retract {
          transform: rotate(180deg);
        }
      }
    }
  }

  :deep(.el-input__inner) {
    height: 32px;
    line-height: 32px;
  }
}
</style>
