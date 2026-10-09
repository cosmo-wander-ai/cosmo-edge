<template>
  <div class="resource-usage">
    <!-- 评分卡片 -->
    <div class="score-card">
      <div class="score-icon">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
        </svg>
      </div>
      <div class="score-content">
        <div class="score-label">{{ t('resource.systemHealthScore') }}</div>
        <div class="score-value">
          <span class="number">{{ customScore }}</span>
          <span class="unit">{{ t('resource.scoreUnit') }}</span>
        </div>
      </div>
      <div class="score-status" :class="getScoreStatus(customScore)">
        {{ getScoreText(customScore) }}
      </div>
    </div>

    <!-- 资源监控网格 -->
    <div class="charts-container">
      <div
        v-for="(item, index) in resourceList"
        :key="item.key"
        class="chart-item"
        :class="{ 'is-unavailable': !isResourceAvailable(item) }"
        :data-progress-color="getProgressColor(resourcePercent(item))"
      >
        <div class="chart-header">
          <div class="chart-title">
            <div class="title-icon" aria-hidden="true">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path v-if="item.key.includes('cpu') || item.key.includes('npu')" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z" />
                <path v-else-if="item.key.includes('Memory')" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 12h14M5 12a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v4a2 2 0 01-2 2M5 12a2 2 0 00-2 2v4a2 2 0 002 2h14a2 2 0 002-2v-4a2 2 0 00-2-2m-2-4h.01M17 16h.01" />
                <path v-else-if="item.key.includes('MMC')" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4m0 5c0 2.21-3.582 4-8 4s-8-1.79-8-4" />
                <path v-else stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
              </svg>
            </div>
            <span>{{ resolveResourceName(item) }}</span>
          </div>
        </div>

        <div class="chart-percentage">
          <span>{{ isResourceAvailable(item) ? item.usedPercent : t('resource.unavailable') }}</span>
          <span v-if="isResourceAvailable(item)" class="percentage-unit">%</span>
        </div>

        <!-- 细进度条，保留原数值与阈值分类 -->
        <div class="progress-wrapper">
          <el-progress
            :percentage="resourcePercent(item)"
            color="var(--metric-accent)"
            :stroke-width="5"
            :show-text="false"
            :aria-label="resolveResourceName(item)"
          />
        </div>

        <!-- 使用详情 -->
        <div class="usage-stats">
          <div class="stat-row">
            <div class="stat-label">
              <span v-if="item.key === 'packetDiscardUtilization'">{{ t('resource.packetLostCount') }}</span>
              <span v-else>{{ t('resource.usedLabel') }}</span>
            </div>
            <div class="stat-value">
              {{ isResourceAvailable(item) ? item.usedSize : '--' }}
            </div>
          </div>
          <div class="stat-row">
            <div class="stat-label">
              <span v-if="item.key === 'packetDiscardUtilization'">{{ t('resource.totalPacketCount') }}</span>
              <span v-else>{{ t('resource.unusedLabel') }}</span>
            </div>
            <div class="stat-value stat-value-gray">
              {{ isResourceAvailable(item) ? item.unusedSize : '--' }}
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount, getCurrentInstance } from 'vue'
import { t } from '@/i18n'

const { proxy } = getCurrentInstance()

const resourceList = ref([])
const timer = ref(null)
const customScore = ref('0')

// Map backend item.key → i18n key
const RESOURCE_KEY_MAP = {
  cpuUtilization: 'resource.itemCpuUtilization',
  generalMemoryUtilization: 'resource.itemGeneralMemory',
  npuUtilization: 'resource.itemNpuUtilization',
  modelMemoryUtilization: 'resource.itemModelMemory',
  pictureMemoryUtilization: 'resource.itemPictureMemory',
  specialMemoryUtilization: 'resource.itemSpecialMemory',
  TPPMemoryUtilization: 'resource.itemTPPMemory',
  eMMCUtilization: 'resource.itemEmmcUtilization',
  packetDiscardUtilization: 'resource.itemPacketDiscard'
}

const resolveResourceName = (item) => {
  const i18nKey = RESOURCE_KEY_MAP[item.key]
  return i18nKey ? t(i18nKey) : item.name
}

const isResourceAvailable = (item) => item?.available !== 0
const resourcePercent = (item) => isResourceAvailable(item) ? Number(item.usedPercent) || 0 : 0

// 查询硬件资源
const queryHardwareResource = () => {
  proxy.$API.queryHardwareResource().then((res) => {
    const { resData } = res
    // 后端返回的是负载分(0=空闲,100=满载,>100=过载)，取反转为健康分
    const loadScore = resData?.customScore ? Number(resData.customScore) : 0
    const healthScore = Math.max(0, Math.min(100, 100 - loadScore))
    customScore.value = healthScore.toFixed(2)
    resourceList.value = resData?.itemList || []
  })
}

// 启动定时器
const startTimer = () => {
  timer.value = setInterval(() => {
    queryHardwareResource()
  }, 5000)
}

// 清除定时器
const clearTimer = () => {
  if (timer.value) {
    clearInterval(timer.value)
    timer.value = null
  }
}

// 获取进度条颜色
const getProgressColor = (percentage) => {
  if (percentage <= 50) return '#3182ce'
  if (percentage <= 80) return '#f59e0b'
  return '#ef4444'
}

// 获取渐变色
const getGradientColor = (percentage) => {
  if (percentage <= 50) return 'linear-gradient(135deg, #3182ce 0%, #4299e1 100%)'
  if (percentage <= 80) return 'linear-gradient(135deg, #f59e0b 0%, #f97316 100%)'
  return 'linear-gradient(135deg, #ef4444 0%, #dc2626 100%)'
}

// 获取评分状态
const getScoreStatus = (score) => {
  const numScore = Number(score)
  if (numScore >= 90) return 'excellent'
  if (numScore >= 70) return 'good'
  if (numScore >= 50) return 'warning'
  return 'danger'
}

// 获取评分文本
const getScoreText = (score) => {
  const numScore = Number(score)
  if (numScore >= 90) return t('resource.excellent')
  if (numScore >= 70) return t('resource.good')
  if (numScore >= 50) return t('resource.fair')
  return t('resource.poor')
}

// 获取环形进度偏移
const getRingOffset = (score) => {
  const numScore = Number(score)
  const circumference = 2 * Math.PI * 45
  const offset = circumference - (numScore / 100) * circumference
  return offset
}

// 生命周期
onMounted(() => {
  queryHardwareResource()
  startTimer()
})

onBeforeUnmount(() => {
  clearTimer()
})
</script>

<style lang="scss" scoped>
.resource-usage {
  min-width: 0;
  color: var(--text-primary);
  background: var(--bg-white);
}

.score-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 18px 20px;
  margin-bottom: 16px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 8px;
}

.score-icon {
  display: flex;
  flex: 0 0 36px;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  color: var(--primary-color);
  background: var(--el-color-primary-light-9);
  border-radius: 8px;

  svg {
    width: 20px;
    height: 20px;
  }
}

.score-content {
  display: flex;
  flex: 1;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px 24px;
  min-width: 0;
}

.score-label {
  color: var(--text-secondary);
  font-size: 14px;
  font-weight: 500;
}

.score-value {
  display: flex;
  align-items: baseline;
  gap: 6px;
  font-variant-numeric: tabular-nums;

  .number {
    color: var(--text-primary);
    font-size: 32px;
    line-height: 1.25;
    font-weight: 650;
  }

  .unit {
    color: var(--text-secondary);
    font-size: 13px;
  }
}

.score-status {
  flex-shrink: 0;
  padding: 3px 9px;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 500;

  &.excellent {
    background: var(--el-color-primary-light-9);
    color: var(--primary-color);
  }

  &.good {
    background: var(--theme-success-soft, #edf6f0);
    color: var(--success-color);
  }

  &.warning {
    background: var(--theme-warning-soft, #fbf4e6);
    color: var(--warning-color);
  }

  &.danger {
    background: var(--theme-danger-soft, #fbebee);
    color: var(--danger-color);
  }
}

.charts-container {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(min(100%, max(240px, calc((100% - 48px) / 4))), 1fr));
  gap: 16px;
  align-items: stretch;
}

.chart-item {
  --metric-accent: var(--primary-color);
  min-width: 0;
  padding: 18px 20px;
  background: var(--bg-white);
  border: 1px solid var(--border-color);
  border-radius: 8px;

  // Reuse the existing threshold classifier; only map its colors to UI tokens.
  &[data-progress-color='#f59e0b'] {
    --metric-accent: var(--warning-color);

    .chart-percentage { color: var(--warning-color); }
  }

  &[data-progress-color='#ef4444'] {
    --metric-accent: var(--danger-color);

    .chart-percentage { color: var(--danger-color); }
  }

  &.is-unavailable {
    --metric-accent: var(--text-muted);

    .chart-percentage {
      color: var(--text-secondary);
      font-size: 18px;
    }
  }
}

.chart-header {
  min-height: 22px;
  margin-bottom: 12px;
}

.chart-title {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--text-primary);
  font-size: 13px;
  line-height: 20px;
  font-weight: 600;
  overflow-wrap: anywhere;
}

.title-icon {
  display: flex;
  flex: 0 0 18px;
  align-items: center;
  justify-content: center;
  width: 18px;
  height: 18px;
  color: var(--text-secondary);

  svg {
    width: 18px;
    height: 18px;
  }
}

.chart-percentage {
  display: flex;
  align-items: baseline;
  gap: 4px;
  min-height: 38px;
  color: var(--text-primary);
  font-size: 30px;
  line-height: 38px;
  font-weight: 650;
  font-variant-numeric: tabular-nums;
  overflow-wrap: anywhere;
}

.percentage-unit {
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 500;
}

.progress-wrapper {
  margin: 10px 0 16px;

  :deep(.el-progress-bar__outer) {
    background: var(--border-light);
  }
}

.usage-stats {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
  padding-top: 12px;
  border-top: 1px solid var(--border-light);
}

.stat-row {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 0;
}

.stat-label {
  color: var(--text-secondary);
  font-size: 12px;
  line-height: 18px;
  overflow-wrap: anywhere;
}

.stat-value {
  color: var(--text-primary);
  font-size: 13px;
  line-height: 20px;
  font-weight: 500;
  font-variant-numeric: tabular-nums;
  overflow-wrap: anywhere;
}

@media (max-width: 768px) {
  .score-card {
    flex-wrap: wrap;
    gap: 12px;
    padding: 16px;
  }

  .score-content { gap: 4px 16px; }
  .chart-item { padding: 16px; }
}
</style>
