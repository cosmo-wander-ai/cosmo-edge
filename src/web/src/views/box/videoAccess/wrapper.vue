<template>
  <div class="mv-wrap ui-admin-page">
    <el-tabs v-model="activeName" type="border-card" class="custom-tabs">
      <el-tab-pane :label="t('boxOther.videoChannel')" name="first">
        <camera-management v-if="activeName === 'first'" />
      </el-tab-pane>
      <el-tab-pane :label="t('boxOther.timeTemplate')" name="second">
        <time-template v-if="activeName === 'second'" />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import CameraManagement from '@/views/gam/taskManager/index.vue'
import TimeTemplate from './timeTemplate/index.vue'
import { t } from '@/i18n'

const activeName = ref('first')
</script>

<style scoped lang="scss">
.custom-tabs {
  background: var(--bg-white, #ffffff);
  box-shadow: var(--shadow-sm, 0 1px 2px rgba(0, 0, 0, 0.05));
  border: 1px solid var(--border-light, var(--bg-secondary));
  overflow: hidden;
  height: 100%;
  display: flex;
  flex-direction: column;

  :deep(.el-tabs__header) {
    background: var(--bg-secondary);
    margin: 0;
  }

  :deep(.el-tabs__nav) {
    border: none;
  }

  :deep(.el-tabs__item) {
    color: var(--text-secondary, var(--text-secondary));
    font-weight: 500;
    font-size: 0.95rem;
    padding: 16px 24px;
    border: none;
    transition: all 0.3s ease;
    position: relative;

    &:hover {
      color: var(--primary-color, var(--primary-color));
      background: var(--theme-accent-soft, rgba(88, 82, 223, 0.05));
    }

    &.is-active {
      color: var(--primary-color, var(--primary-color));
      font-weight: 600;
      background: var(--bg-white, #ffffff);

      &::after {
        content: '';
        position: absolute;
        bottom: 0;
        left: 0;
        right: 0;
        height: 3px;
        background: var(--bg-secondary);
        border-radius: 3px 3px 0 0;
      }
    }
  }

  :deep(.el-tabs__content) {
    background: var(--bg-white, #ffffff);
    flex: 1;
    padding: 0;
    overflow-y: auto;
  }
}

// 响应式
@media (max-width: 768px) {
  .mv-wrap {
    height: 100%;
    padding: 12px;
  }

  .custom-tabs {
    :deep(.el-tabs__header) {
      padding: 0 12px;
    }

    :deep(.el-tabs__item) {
      padding: 12px 16px;
      font-size: 0.875rem;
    }
  }
}
</style>
