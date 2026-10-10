<template>
  <div class="mv-wrap ui-admin-page video-access-workspace">
    <header class="workspace-heading">
      <h1>{{ t('nav.videoAccess') }}</h1>
      <p>{{ t('boxOther.videoChannel') }} · {{ t('glossary.scenarioTask') }}</p>
    </header>
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
.video-access-workspace {
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-height: 0;
}

.workspace-heading {
  flex: none;

  h1 {
    margin: 0;
    color: var(--text-primary);
    font-size: 24px;
    font-weight: 650;
    line-height: 1.35;
  }

  p {
    margin: 5px 0 0;
    color: var(--text-secondary);
    font-size: 13px;
  }
}

.custom-tabs {
  background: transparent;
  box-shadow: none;
  border: 0;
  min-height: 0;
  flex: 1;
  display: flex;
  flex-direction: column;

  :deep(.el-tabs__header) {
    background: transparent;
    margin: 0;
    border-bottom: 1px solid var(--border-color);
  }

  :deep(.el-tabs__nav) {
    border: none;
  }

  :deep(.el-tabs__item) {
    color: var(--text-secondary, var(--text-secondary));
    font-weight: 500;
    font-size: 14px;
    height: 40px;
    padding: 0 20px;
    border: none;
    position: relative;

    &:hover {
      color: var(--primary-color, var(--primary-color));
      background: var(--bg-hover);
    }

    &.is-active {
      color: var(--primary-color, var(--primary-color));
      font-weight: 600;
      background: var(--bg-white, #ffffff);

      &::after {
        content: '';
        position: absolute;
        bottom: 0;
        left: 16px;
        right: 16px;
        height: 2px;
        background: var(--primary-color);
      }
    }
  }

  :deep(.el-tabs__content) {
    background: transparent;
    flex: 1;
    padding: 12px 0 0;
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
