<template>
  <div class="page-container">
    <div class="toolbar">
      <div class="toolbar-left">
        <span class="page-title">{{ t('linkageStrategy.title') }}</span>
      </div>
      <div class="toolbar-right">
        <el-button type="primary" size="small" @click="handleSave">{{ t('action.save') }}</el-button>
      </div>
    </div>

    <div class="editor-container">
      <aside class="strategy-sidebar">
        <div class="sidebar-header">
          <span class="title">{{ t('linkageStrategy.allStrategies') }}</span>
          <el-icon class="add-icon" @click="handleAddStrategy">
            <Plus />
          </el-icon>
        </div>
        <div class="sidebar-list">
          <div v-for="(item, idx) in strategyList" :key="item.id || idx" class="sidebar-item" :class="{ active: activeStrategy?.id === item.id }" @click="handleSelectStrategy(item)">
            <div class="item-left">
              <div class="item-name">
                {{ item.name }}
              </div>
            </div>
            <div class="item-tools" @click.stop>
              <el-switch v-model="item.enabled" size="small" @change="handleSwitchStrategy(item)" />
              <el-icon class="tool-icon" @click="handleEditStrategy(item)">
                <EditPen />
              </el-icon>
              <el-icon class="tool-icon danger" @click="handleDeleteStrategy(item)">
                <Delete />
              </el-icon>
            </div>
          </div>
        </div>
      </aside>

      <div class="flow-container" ref="containerRef">
        <ArrangeFlow ref="flowRef" :width="width" :height="height" :workFlow="currentStrategy.workFlow" :actionList="actionList" :atomicCode="''" @onMetadata="onMetadata" />
      </div>
      <el-dialog :title="strategyMode === 'edit' ? t('linkageStrategy.editStrategy') : t('linkageStrategy.addStrategy')" v-model="strategyDialogVisible" center width="420px" @close="onDialogClose">
        <el-form ref="strategyFormRef" :model="strategyForm" :rules="strategyRules" :label-width="currentLocale === 'en-US' ? '170px' : '120px'" label-position="right">
          <el-form-item :label="t('linkageStrategy.strategyName') + localeColon" prop="name">
            <el-input v-model="strategyForm.name" :placeholder="t('linkageStrategy.strategyNamePlaceholder')" />
          </el-form-item>
        </el-form>
        <template #footer>
          <div class="dialog-footer">
            <el-button type="primary" size="small" @click="submitStrategy">{{ t('action.confirm') }}</el-button>
            <el-button size="small" @click="strategyDialogVisible = false">{{ t('action.cancel') }}</el-button>
          </div>
        </template>
      </el-dialog>
    </div>
  </div>
</template>

<script setup>
import {
  ref,
  reactive,
  onMounted,
  onBeforeUnmount,
  nextTick,
  getCurrentInstance
} from 'vue'
import ArrangeFlow from '../components/ArrangeFlow.vue'
import { Plus, EditPen, Delete } from '@element-plus/icons-vue'
import { t, localeColon, currentLocale } from '@/i18n'

const { proxy } = getCurrentInstance()
const $API = proxy.$API

const flowRef = ref(null)
const containerRef = ref(null)
const width = ref(0)
const height = ref(600)

const actionList = ref([])
const strategyList = ref([])
const activeStrategy = ref(null)
const strategyDialogVisible = ref(false)
const strategyFormRef = ref(null)
const strategyForm = reactive({
  id: '',
  name: '',
  workFlow: '[]'
})
const currentStrategy = ref({
  id: '',
  name: '',
  workFlow: '[]',
  enabled: false
})

const strategyRules = reactive({
  name: [{ required: true, message: t('linkageStrategy.strategyNameRequired'), trigger: 'blur' }]
})
const strategyMode = ref('add')

const updateCanvasSize = () => {
  nextTick(() => {
    const el = containerRef.value
    if (!el) return
    const rect = el.getBoundingClientRect()
    const viewportH = window.innerHeight
    const bottomPadding = 32
    width.value = el.clientWidth
    height.value = Math.max(300, viewportH - rect.top - bottomPadding)
  })
}

const handleResize = () => {
  updateCanvasSize()
}

onMounted(() => {
  updateCanvasSize()
  window.addEventListener('resize', handleResize)
  // 加载可编排动作
  $API.boxActionList({}).then((res) => {
    actionList.value = res?.resData?.strages || []
    queryStrategyList()
  })
})

const queryStrategyList = (opt = {}) => {
  $API.boxQueryStrategyList({}).then((res) => {
    const data = res?.resData || {}
    const list = Array.isArray(data.tasks)
      ? data.tasks
      : Array.isArray(data.list)
      ? data.list
      : []
    // 规范化字段：id、name、enabled、workFlow
    strategyList.value = list.map((item, idx) => ({
      id: item.id || item.strategyId || idx,
      name: item.name || item.strategyName || String(idx + 1),
      enabled: item.status === true || item.enabled === true,
      workFlow: item.workFlow || item.workFLoW || '[]'
    }))
    if (strategyList.value.length > 0) {
      if (opt.selectId != null) {
        const t = strategyList.value.find(
          (s) => String(s.id) === String(opt.selectId)
        )
        if (t) return handleSelectStrategy(t)
      }
      if (opt.selectName) {
        const t = strategyList.value.find((s) => s.name === opt.selectName)
        if (t) return handleSelectStrategy(t)
      }
      handleSelectStrategy(strategyList.value[0])
    } else {
      activeStrategy.value = null
      currentStrategy.value = {
        id: '',
        name: '',
        workFlow: '[]',
        enabled: false
      }
      if (flowRef.value && typeof flowRef.value.clearFlow === 'function') {
        flowRef.value.clearFlow()
      }
    }
  })
}

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
})

const handleAddStrategy = () => {
  strategyMode.value = 'add'
  strategyForm.id = ''
  strategyForm.name = ''
  strategyForm.workFlow = '[]'
  strategyDialogVisible.value = true
}

const handleEditStrategy = (item) => {
  strategyMode.value = 'edit'
  strategyForm.id = item.id
  strategyForm.name = item.name || ''
  strategyForm.workFlow = item.workFlow || '[]'
  strategyDialogVisible.value = true
}

const handleDeleteStrategy = (item) => {
  const id = item?.id
  if (id == null || id === '') return
  $API.boxStrategyDelete({ id }).then(() => {
    proxy.$message.success(t('common.operationSucceeded'))
    queryStrategyList()
  })
}

const handleSwitchStrategy = (item) => {
  const params = { id: item.id, status: item.enabled ? 1 : 0 }
  $API.boxStrategySwitch(params).then(() => {
    proxy.$message.success(t('common.operationSucceeded'))
  })
}

const handleSelectStrategy = (item) => {
  activeStrategy.value = item
  currentStrategy.value = {
    id: item.id,
    name: item.name || '',
    enabled: !!item.enabled,
    workFlow: item.workFlow || '[]'
  }
}

const onDialogClose = () => {
  strategyFormRef.value && strategyFormRef.value.resetFields()
}

const submitStrategy = () => {
  strategyFormRef.value?.validate((valid) => {
    if (!valid) return
    const params = {
      id: strategyForm.id,
      name: strategyForm.name,
      workFlow: strategyForm.workFlow
    }
    const req =
      strategyMode.value === 'edit'
        ? $API.boxStrategyUpdate(params)
        : $API.boxStrategyAdd(params)
    req.then((res) => {
      proxy.$message.success(t('common.operationSucceeded'))
      strategyDialogVisible.value = false
      if (strategyMode.value === 'edit') {
        const idx = strategyList.value.findIndex(
          (s) => s.id === strategyForm.id
        )
        if (idx > -1) {
          strategyList.value[idx].name = strategyForm.name
        }
      } else {
        const newId = res?.resData?.id
        queryStrategyList({ selectId: newId, selectName: strategyForm.name })
      }
    })
  })
}

const onMetadata = () => {}

const handleSave = () => {
  if (!flowRef.value || typeof flowRef.value.saveFlowData !== 'function') {
    return proxy.$message.warning(t('linkageStrategy.noFlowContent'))
  }
  // 从流程组件收集数据
  const payload = flowRef.value.saveFlowData()
  const newParams = {
    id: currentStrategy.value.id,
    name: currentStrategy.value.name,
    workFlow: payload.workFlow
  }
  $API.boxStrategyUpdate(newParams).then((res) => {
    proxy.$message.success(t('common.operationSucceeded'))
    queryStrategyList()
  })
}
</script>

<style scoped lang="scss">
.page-container {
  display: flex;
  flex-direction: column;
  min-height: calc(100vh - 84px);
  box-sizing: border-box;
}

.toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 10px;
  padding: 12px 16px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  box-shadow: var(--shadow-sm);

  .page-title {
    font-size: 16px;
    font-weight: 600;
    color: var(--text-primary);
  }
}

.editor-container {
  display: flex;
  gap: 10px;
  height: 100%;
}

.strategy-sidebar {
  width: 260px;
  min-width: 240px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  box-shadow: var(--shadow-sm);
  overflow: hidden;

  .sidebar-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 12px;
    border-bottom: 1px solid var(--border-color);
    background: var(--bg-secondary);
    .title {
      font-size: 14px;
      font-weight: 600;
      color: var(--text-primary);
    }
    .add-icon {
      cursor: pointer;
    }
  }

  .sidebar-list {
    padding: 8px;
    .sidebar-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 10px;
      border-radius: 8px;
      transition: background 0.2s ease;
      cursor: pointer;

      &.active {
        background: #f5f7fa;
      }
      &:hover {
        background: #f8fafc;
      }

      .item-left {
        display: flex;
        align-items: center;
        gap: 8px;
        .item-index {
          width: 18px;
          height: 18px;
          border-radius: 50%;
          background: #eef2ff;
          color: #3182ce;
          font-size: 12px;
          display: flex;
          align-items: center;
          justify-content: center;
        }
        .item-name {
          max-width: 120px;
          overflow: hidden;
          white-space: nowrap;
          text-overflow: ellipsis;
          color: var(--text-secondary);
        }
      }

      .item-tools {
        display: flex;
        align-items: center;
        gap: 6px;
        .tool-icon {
          cursor: pointer;
          &:hover {
            color: var(--el-color-primary);
          }
          &.danger:hover {
            color: var(--el-color-danger);
          }
        }
      }
    }
  }
}

.flow-container {
  flex: 1 1 auto;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-sm);
  padding: 4px;
  box-shadow: var(--shadow-sm);
  overflow: hidden;
}

.dialog-footer {
  display: flex;
  justify-content: center;
}
.strategy-sidebar .item-tools .tool-icon.danger {
  color: var(--el-color-danger);
}
</style>
