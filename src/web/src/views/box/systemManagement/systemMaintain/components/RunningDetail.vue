<template>
  <div>
    <div class="table-container">
      <div class="table-header">
        <el-button type="primary" size="small" class="refresh-btn" @click="refreshData">{{ t('action.refresh') }}</el-button>
      </div>
      <el-table :data="tableData" :header-cell-style="{ background: '#fafafa' }" style="width: 100%" default-expand-all
        :tree-props="{ children: 'actionStatus', hasChildren: 'hasChildren' }" row-key="id">
        <el-table-column prop="channelId" :label="t('systemManage.channelId')"></el-table-column>
        <el-table-column prop="taskId" :label="t('systemManage.taskId')"></el-table-column>
        <el-table-column prop="algorithmName" :label="t('systemManage.algorithmService')"></el-table-column>
        <el-table-column prop="actionId" :label="t('systemManage.processId')" show-overflow-tooltip></el-table-column>
        <el-table-column prop="name" :label="t('systemManage.processName')"></el-table-column>
        <el-table-column :label="t('field.status')">
          <template #default="scope">
            <span>{{ scope.row.statusDesc }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="fps" :label="t('systemManage.frameRate')">
          <template #default="scope">
            <div v-if="!scope.row.channelId">
              {{ scope.row.periodMs ? Number(((scope.row.insertCountPeriod * 1000) / scope.row.periodMs).toFixed(2)) ||
              0 : 0 }}
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="processCountPeriod" :label="t('systemManage.processedPackets')"></el-table-column>
        <el-table-column prop="discardCountPeriod" :label="t('systemManage.droppedPackets')"></el-table-column>
        <el-table-column prop="insertCountPeriod" :label="t('systemManage.insertedPackets')"></el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, getCurrentInstance } from 'vue'
import { v4 } from 'uuid'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const tableData = ref([])

const init = () => {
  $API.queryRunningDetail({}).then((res) => {
    const { resData } = res
    tableData.value = resData.status || []
    tableData.value.forEach((item) => {
      item.id = v4()
      item.actionStatus.forEach((subItem) => {
        subItem.id = v4()
      })
    })
  })
}

const refreshData = () => {
  init()
}

onMounted(() => {
  init()
})
</script>

<style lang="scss" scoped>
.topBar-wrap {
  padding: 0;
  margin: 0;
}

.table-container {
  margin-top: 20px;

  .table-header {
    float: right;
    margin-bottom: 10px;
  }
}
</style>