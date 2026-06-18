<template>
  <div class="box-node-container">
    <div class="node-header">
      <div class="node-title">{{ label }}</div>
    </div>

    <div class="node-body nodrag nopan" @click.stop @mousedown.stop>
      <!-- 配置区 -->
      <div class="config-section">
        <div class="config-title">{{ data?.kind === 'input' ? t('glossary.inputConfig') : t('glossary.outputConfig') }}</div>
        <el-table :data="tableData" border size="small" style="width: 100%" class="custom-table">
          <el-table-column :label="t('field.name')" min-width="120">
            <template #default="scope">
              <el-input v-model="scope.row.name" size="small" disabled @change="handleChange(scope.$index, 'name', scope.row.name)" />
            </template>
          </el-table-column>
          <el-table-column :label="t('field.shape')" min-width="150">
            <template #default="scope">
              <el-input v-model="scope.row.shape" size="small" disabled @change="handleChange(scope.$index, 'shape', scope.row.shape)" />
            </template>
          </el-table-column>
          <el-table-column :label="t('field.dataType')" min-width="140">
            <template #default="scope">
              <el-select v-model="scope.row.data_type" size="small" disabled @change="handleChange(scope.$index, 'data_type', scope.row.data_type)">
                <el-option :label="t('glossary.float32')" value="0" />
                <el-option :label="t('glossary.int32')" value="1" />
                <el-option :label="t('glossary.float16Half')" value="2" />
                <el-option :label="t('glossary.float16')" value="3" />
                <el-option :label="t('glossary.uint8')" value="4" />
                <el-option :label="t('glossary.int8')" value="5" />
              </el-select>
            </template>
          </el-table-column>
        </el-table>
      </div>
    </div>

    <Handle type="target" position="left" id="left-in" />
    <Handle type="source" position="right" id="right-out" />
  </div>
</template>

<script setup>
import { ref, watch, computed } from 'vue'
import { Handle } from '@vue-flow/core'
import { t } from '@/i18n'

const props = defineProps({
  label: {
    type: String,
    default: ''
  },
  style: {
    type: Object,
    default: () => ({})
  },
  data: {
    type: Object,
    default: () => ({})
  }
})

const tableData = ref([])

const toText = (value) => {
  if (Array.isArray(value) || (value && typeof value === 'object')) {
    return JSON.stringify(value)
  }
  if (value === undefined || value === null) return ''
  return String(value)
}

watch(
  () => props.data?.list,
  (newList) => {
    if (newList) {
      tableData.value = newList.map(item => ({
        name: toText(item.name),
        shape: toText(item.shape),
        data_type: toText(item.data_type ?? '0')
      }))
    }
  },
  { immediate: true, deep: true }
)

const handleChange = (index, field, value) => {
  if (typeof props.data?.onUpdate === 'function') {
    props.data.onUpdate(index, { [field]: value })
  }
}
</script>

<style scoped lang="scss">
.box-node-container {
  background: #f7f8fa;
  border-radius: 16px;
  width: 560px;
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.08);
  position: relative;
  display: flex;
  flex-direction: column;
   padding: 10px 14px 14px;
  border: 1px solid #f0f2f5;
  cursor: default;
}

.node-header {
  margin-bottom: 24px;
  text-align: center;
  color: #1d2129;
}

.node-title {
  font-size: 16px;
  font-weight: 500;
  color: inherit;
}

.node-body {
  width: 100%;
}

.config-section {
  background: #fff;
  border-radius: 12px;
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

.config-title {
  font-size: 18px;
  font-weight: 600;
  color: #1d2129;
  margin-bottom: 4px;
}

.custom-table {
  border-radius: 8px;
  overflow: hidden;
  
  :deep(.el-table__header-wrapper) th {
    background-color: #f7f8fa !important;
    color: #1d2129;
    font-weight: 600;
  }
}

:deep(.vue-flow__handle) {
  width: 10px;
  height: 10px;
  background: #909399;
  border: 2px solid #fff;
  box-shadow: 0 0 0 2px rgba(144, 147, 153, 0.2);
}
</style>
