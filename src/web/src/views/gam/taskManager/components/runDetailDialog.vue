<template>
  <el-dialog v-model="show" :title="t('glossary.runningDetails')" width="1000px" center @close="emit('update:visible', false)">
    <div class="run-detail">
      <div class="table-header">
        <div class="table-desc">
          <span>{{ t('field.taskId') }}{{ localeColon }}{{ taskObj.taskId }}</span>
          <span>{{ t('glossary.algorithmService') }}{{ localeColon }}{{ taskObj.algorithmName }}</span>
        </div>
        <el-button type="primary" size="small" @click="refresh">{{ t('action.refresh') }}</el-button>
      </div>
      <el-table :data="tableData" style="width: 100%" stripe>
        <el-table-column prop="actionId" :label="t('field.flowId')" show-overflow-tooltip></el-table-column>
        <el-table-column prop="name" :label="t('field.flowName')"></el-table-column>
        <el-table-column :label="t('field.status')">
          <template #default="scope">
            <span>{{ scope.row.statusDesc }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="fps" :label="t('glossary.frameRate')">
          <template #default="scope">
            {{ scope.row.periodMs ? Number(((scope.row.insertCountPeriod * 1000) / scope.row.periodMs).toFixed(2)) || 0 : 0 }} </template>
        </el-table-column>
        <el-table-column prop="processCountPeriod" :label="t('field.processedPackets')"></el-table-column>
        <el-table-column prop="discardCountPeriod" :label="t('field.droppedPackets')"></el-table-column>
        <el-table-column prop="insertCountPeriod" :label="t('field.insertedPackets')"></el-table-column>
      </el-table>
    </div>
  </el-dialog>
</template>

<script setup>
import { ref, watch, getCurrentInstance } from 'vue'
import { t, localeColon } from '@/i18n'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  taskObj: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['update:visible'])
const { proxy } = getCurrentInstance()

const show = ref(false)
const tableData = ref([])
const custId = ref(localStorage.getItem('currentCustId') || '')

const getDetail = () => {
  const params = {
    taskId: props.taskObj.taskId,
    custId: custId.value
  }
  return proxy.$API.taskRunningDetail(params).then((res) => {
    const { resData } = res
    tableData.value = resData?.actionStatus || []
  })
}

const refresh = () => {
  getDetail().then(() => {
    proxy.$message.closeAll()
    proxy.$message.success(t('common.operationSucceeded'))
  })
}

watch(() => props.visible, (val) => {
  if (val) {
    getDetail()
  }
  show.value = val
})
</script>

<style lang="scss" scoped>
.run-detail {
  padding-bottom: 40px;
  .table-header {
    display: flex;
    justify-content: space-between;
    margin-bottom: 20px;
  }

  .table-desc {
    span {
      display: inline-block;
      margin-right: 10px;
    }
  }
}
</style>
