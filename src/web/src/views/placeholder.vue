<script setup>
import { useRoute } from 'vue-router'
import { t } from '@/i18n'

const route = useRoute()
</script>

<template>
  <div class="placeholder-container">
    <div class="placeholder-content">
      <svg class="placeholder-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
      </svg>
      <h2 class="placeholder-title">{{ t('placeholder.pageInDevelopment') }}</h2>
      <p class="placeholder-text">{{ t('placeholder.currentRoute') }}: {{ route.path }}</p>
      <p class="placeholder-subtitle">{{ t('placeholder.featureComingSoon') }}</p>
    </div>
  </div>
</template>

<style lang="scss" scoped>
.placeholder-container {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 500px;
  background: #fff;
  border-radius: 16px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.05);
}

.placeholder-content {
  text-align: center;
  padding: 48px;
}

.placeholder-icon {
  width: 80px;
  height: 80px;
  color: #cbd5e1;
  margin: 0 auto 24px;
}

.placeholder-title {
  font-size: 24px;
  font-weight: 700;
  color: #1a1f3a;
  margin: 0 0 12px;
}

.placeholder-text {
  font-size: 14px;
  color: #3182ce;
  font-family: monospace;
  background: #f1f5f9;
  padding: 8px 16px;
  border-radius: 8px;
  display: inline-block;
  margin: 0 0 12px;
}

.placeholder-subtitle {
  font-size: 16px;
  color: #64748b;
  margin: 0;
}
</style>
