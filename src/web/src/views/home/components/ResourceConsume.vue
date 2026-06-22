<template>
  <div class="resource-usage">
    <!-- 背景装饰 -->
    <div class="bg-decoration">
      <div class="grid-lines"></div>
      <div class="glow-orb glow-orb-1"></div>
      <div class="glow-orb glow-orb-2"></div>
    </div>

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
        <div class="score-status" :class="getScoreStatus(customScore)">
          {{ getScoreText(customScore) }}
        </div>
      </div>
      <!-- <div class="score-ring">
        <svg viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="45" class="ring-bg" />
          <circle 
            cx="50" 
            cy="50" 
            r="45" 
            class="ring-progress"
            :style="{ strokeDashoffset: getRingOffset(customScore) }"
          />
        </svg>
      </div> -->
    </div>

    <!-- 资源监控网格 -->
    <div class="charts-container">
      <div 
        v-for="(item, index) in resourceList" 
        :key="item.key" 
        class="chart-item"
        :style="{ animationDelay: `${index * 0.1}s` }"
      >
        <div class="chart-header">
          <div class="chart-title">
            <div class="title-icon" :style="{ background: getGradientColor(item.usedPercent) }">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path v-if="item.key.includes('cpu') || item.key.includes('npu')" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z" />
                <path v-else-if="item.key.includes('Memory')" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 12h14M5 12a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v4a2 2 0 01-2 2M5 12a2 2 0 00-2 2v4a2 2 0 002 2h14a2 2 0 002-2v-4a2 2 0 00-2-2m-2-4h.01M17 16h.01" />
                <path v-else-if="item.key.includes('MMC')" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4m0 5c0 2.21-3.582 4-8 4s-8-1.79-8-4" />
                <path v-else stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
              </svg>
            </div>
            <span>{{ resolveResourceName(item) }}</span>
          </div>
          <div class="chart-percentage" :style="{ color: getProgressColor(item.usedPercent) }">
            {{ item.usedPercent }}%
          </div>
        </div>

        <!-- 圆形进度条 -->
        <div class="progress-wrapper">
          <el-progress 
            type="circle" 
            :percentage="item.usedPercent" 
            :color="getProgressColor(item.usedPercent)" 
            :width="110" 
            :stroke-width="10"
            :show-text="false"
          />
          <div class="progress-center">
            <div class="center-value">{{ item.usedPercent }}</div>
            <div class="center-unit">%</div>
          </div>
        </div>

        <!-- 使用详情 -->
        <div class="usage-stats">
          <div class="stat-row">
            <div class="stat-label">
              <span class="stat-dot" :style="{ background: getProgressColor(item.usedPercent) }"></span>
              <span v-if="item.key === 'packetDiscardUtilization'">{{ t('resource.packetLostCount') }}</span>
              <span v-else>{{ t('resource.usedLabel') }}</span>
            </div>
            <div class="stat-value" :style="{ color: getProgressColor(item.usedPercent) }">
              {{ item.usedSize }}
            </div>
          </div>
          <div class="stat-row">
            <div class="stat-label">
              <span class="stat-dot stat-dot-gray"></span>
              <span v-if="item.key === 'packetDiscardUtilization'">{{ t('resource.totalPacketCount') }}</span>
              <span v-else>{{ t('resource.unusedLabel') }}</span>
            </div>
            <div class="stat-value stat-value-gray">
              {{ item.unusedSize }}
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
  position: relative;
  padding: 20px;
  background: #f5f7fa;
  overflow: hidden;
}

// 背景装饰
.bg-decoration {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  pointer-events: none;
  overflow: hidden;
}

.grid-lines {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-image: 
    linear-gradient(rgba(49, 130, 206, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(49, 130, 206, 0.03) 1px, transparent 1px);
  background-size: 40px 40px;
}

.glow-orb {
  position: absolute;
  border-radius: 50%;
  filter: blur(60px);
  opacity: 0.15;
  animation: float 8s ease-in-out infinite;
}

.glow-orb-1 {
  width: 300px;
  height: 300px;
  background: radial-gradient(circle, #3182ce 0%, transparent 70%);
  top: -150px;
  left: -150px;
}

.glow-orb-2 {
  width: 250px;
  height: 250px;
  background: radial-gradient(circle, #4299e1 0%, transparent 70%);
  bottom: -125px;
  right: -125px;
  animation-delay: 4s;
}

@keyframes float {
  0%, 100% {
    transform: translate(0, 0);
  }
  50% {
    transform: translate(20px, 20px);
  }
}

// 评分卡片
.score-card {
  position: relative;
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 20px 24px;
  margin-bottom: 24px;
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 16px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.04);
  animation: slideIn 0.6s ease-out;
}

@keyframes slideIn {
  from {
    opacity: 0;
    transform: translateY(-20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.score-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #3182ce 0%, #4299e1 100%);
  border-radius: 12px;
  box-shadow: 0 4px 12px rgba(49, 130, 206, 0.3);

  svg {
    width: 24px;
    height: 24px;
    color: #fff;
  }
}

.score-content {
  flex: 1;
}

.score-label {
  font-size: 13px;
  color: #6b7280;
  margin-bottom: 6px;
  font-weight: 500;
}

.score-value {
  display: flex;
  align-items: baseline;
  gap: 4px;
  margin-bottom: 6px;

  .number {
    font-size: 32px;
    font-weight: 700;
    background: linear-gradient(135deg, #3182ce 0%, #4299e1 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
  }

  .unit {
    font-size: 16px;
    color: #9ca3af;
  }
}

.score-status {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;

  &.excellent {
    background: rgba(49, 130, 206, 0.1);
    color: #3182ce;
  }

  &.good {
    background: rgba(16, 185, 129, 0.1);
    color: #10b981;
  }

  &.warning {
    background: rgba(245, 158, 11, 0.1);
    color: #f59e0b;
  }

  &.danger {
    background: rgba(239, 68, 68, 0.1);
    color: #ef4444;
  }
}

.score-ring {
  width: 80px;
  height: 80px;
  position: relative;

  svg {
    transform: rotate(-90deg);
  }

  .ring-bg {
    fill: none;
    stroke: #e5e7eb;
    stroke-width: 6;
  }

  .ring-progress {
    fill: none;
    stroke: url(#scoreGradient);
    stroke-width: 6;
    stroke-linecap: round;
    stroke-dasharray: 283;
    transition: stroke-dashoffset 1s ease;
  }
}

// 资源监控网格
.charts-container {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 16px;
}

.chart-item {
  position: relative;
  padding: 20px;
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 16px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.04);
  transition: all 0.3s ease;
  animation: fadeInUp 0.6s ease-out backwards;

  &:hover {
    transform: translateY(-4px);
    border-color: #3182ce;
    box-shadow: 0 8px 24px rgba(49, 130, 206, 0.15);
  }
}

@keyframes fadeInUp {
  from {
    opacity: 0;
    transform: translateY(20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.chart-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}

.chart-title {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 14px;
  font-weight: 600;
  color: #1f2937;

  .title-icon {
    width: 32px;
    height: 32px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 8px;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);

    svg {
      width: 16px;
      height: 16px;
      color: #fff;
    }
  }
}

.chart-percentage {
  font-size: 20px;
  font-weight: 700;
}

.progress-wrapper {
  position: relative;
  display: flex;
  justify-content: center;
  margin: 16px 0;

  :deep(.el-progress) {
    .el-progress__circle {
      filter: drop-shadow(0 0 8px currentColor);
    }
  }
}

.progress-center {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  text-align: center;

  .center-value {
    font-size: 28px;
    font-weight: 700;
    color: #1f2937;
    line-height: 1;
  }

  .center-unit {
    font-size: 12px;
    color: #9ca3af;
    margin-top: 2px;
  }
}

.usage-stats {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.stat-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 10px;
  background: #f9fafb;
  border-radius: 6px;
}

.stat-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: #6b7280;
}

.stat-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  box-shadow: 0 0 6px currentColor;

  &.stat-dot-gray {
    background: #d1d5db;
    box-shadow: none;
  }
}

.stat-value {
  font-size: 13px;
  font-weight: 600;

  &.stat-value-gray {
    color: #9ca3af;
  }
}

// 响应式
@media (max-width: 1200px) {
  .charts-container {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .charts-container {
    grid-template-columns: 1fr;
  }

  .score-card {
    flex-direction: column;
    text-align: center;
  }
}
</style>
