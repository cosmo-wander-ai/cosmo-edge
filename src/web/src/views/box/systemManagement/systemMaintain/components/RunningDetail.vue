<template>
  <div class="runtime-detail">
    <div class="table-container">
      <div class="table-header">
        <el-button type="primary" size="small" class="refresh-btn ui-secondary-button" @click="refreshData">{{ t('action.refresh') }}</el-button>
      </div>
      <el-table :data="tableData" :header-cell-style="{ background: '#fafafa' }" style="width: 100%" default-expand-all
        :tree-props="{ children: 'actionStatus', hasChildren: 'hasChildren' }" row-key="id">
        <el-table-column prop="channelId" :label="t('systemManage.channelId')" min-width="148" show-overflow-tooltip></el-table-column>
        <el-table-column prop="taskId" :label="t('systemManage.taskId')" min-width="152" show-overflow-tooltip></el-table-column>
        <el-table-column :label="t('systemManage.algorithmService')" min-width="132" show-overflow-tooltip>
          <template #default="scope">
            <span>{{ translateAlgorithmName(scope.row.algorithmName) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="actionId" :label="t('systemManage.processId')" min-width="132" show-overflow-tooltip></el-table-column>
        <el-table-column :label="t('systemManage.processName')" min-width="196" show-overflow-tooltip>
          <template #default="scope">
            <span>{{ translateActionName(scope.row.name) }}</span>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.status')" min-width="176" show-overflow-tooltip>
          <template #default="scope">
            <span
              class="runtime-status"
              :data-status-key="scope.row.statusDescKey || (!scope.row.statusDesc ? 'api.requestFailed' : '')"
              :title="translateApiMessage(scope.row.statusDescKey, scope.row.statusDesc)"
            >{{ translateApiMessage(scope.row.statusDescKey, scope.row.statusDesc) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="fps" :label="t('systemManage.frameRate')" width="76" align="right">
          <template #default="scope">
            <div v-if="!scope.row.channelId">
              {{ scope.row.periodMs ? Number(((scope.row.insertCountPeriod * 1000) / scope.row.periodMs).toFixed(2)) ||
              0 : 0 }}
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="processCountPeriod" :label="t('systemManage.processedPackets')" min-width="92" align="right"></el-table-column>
        <el-table-column prop="discardCountPeriod" :label="t('systemManage.droppedPackets')" min-width="92" align="right"></el-table-column>
        <el-table-column prop="insertCountPeriod" :label="t('systemManage.insertedPackets')" min-width="92" align="right"></el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, getCurrentInstance } from 'vue'
import { v4 } from 'uuid'
import { t, translateApiMessage, translateActionName, translateAlgorithmName } from '@/i18n'

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
.runtime-detail {
  min-width: 0;

  :deep(.el-table .cell) {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  :deep(.el-table__body td.el-table__cell) {
    padding-top: 8px;
    padding-bottom: 8px;
  }
}

.table-container {
  margin-top: 16px;
  min-width: 0;

  .table-header {
    display: flex;
    justify-content: flex-end;
    margin-bottom: 12px;
  }
}

.runtime-status {
  display: inline-block;
  max-width: 100%;
  padding: 2px 7px;
  border-radius: 4px;
  color: var(--secondary-color);
  background: var(--bg-secondary);
  font-size: 12px;
  line-height: 20px;
  vertical-align: middle;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;

  &[data-status-key$='Fail'],
  &[data-status-key$='Failed'],
  &[data-status-key='api.requestFailed'] {
    color: var(--danger-color);
    background: var(--el-color-danger-light-9);
  }

  &[data-status-key='api.error.Success'],
  &[data-status-key='api.error.ActionStart'] {
    color: var(--success-color);
    background: var(--el-color-success-light-9);
  }

  &[data-status-key='api.error.ActionReady'],
  &[data-status-key='api.error.ActionStop'] {
    color: var(--secondary-color);
    background: var(--bg-secondary);
  }

  &[data-status-key='api.error.NotInit'],
  &[data-status-key='api.error.NotCreated'] {
    color: var(--warning-color);
    background: var(--el-color-warning-light-9);
  }
}
</style>
