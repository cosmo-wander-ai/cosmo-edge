<template>
  <div class="mv-wrap">
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
  border: 1px solid var(--border-light, #f1f5f9);
  overflow: hidden;
  height: 100%;
  display: flex;
  flex-direction: column;

  :deep(.el-tabs__header) {
    background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
    margin: 0;
  }

  :deep(.el-tabs__nav) {
    border: none;
  }

  :deep(.el-tabs__item) {
    color: var(--text-secondary, #64748b);
    font-weight: 500;
    font-size: 0.95rem;
    padding: 16px 24px;
    border: none;
    transition: all 0.3s ease;
    position: relative;

    &:hover {
      color: var(--primary-color, #3182ce);
      background: rgba(49, 130, 206, 0.05);
    }

    &.is-active {
      color: var(--primary-color, #3182ce);
      font-weight: 600;
      background: var(--bg-white, #ffffff);

      &::after {
        content: '';
        position: absolute;
        bottom: 0;
        left: 0;
        right: 0;
        height: 3px;
        background: linear-gradient(
          135deg,
          var(--primary-color, #3182ce) 0%,
          var(--primary-light, #4299e1) 100%
        );
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
