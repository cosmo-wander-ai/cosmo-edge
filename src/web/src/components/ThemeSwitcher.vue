<script setup>
import { computed } from 'vue'
import { Sunny, Moon, Monitor, Check } from '@element-plus/icons-vue'
import { t } from '@/i18n'
import { useAppearance } from '@/composables/useAppearance'

const { appearance, setAppearance } = useAppearance()
const options = [
  { value: 'light', label: 'appearance.light', icon: Sunny },
  { value: 'dark', label: 'appearance.dark', icon: Moon },
  { value: 'system', label: 'appearance.system', icon: Monitor }
]
const active = computed(() => options.find((option) => option.value === appearance.value.preference))
</script>

<template>
  <el-dropdown trigger="click" placement="bottom-end" popper-class="appearance-menu" @command="setAppearance">
    <button type="button" class="appearance-trigger" :aria-label="t('appearance.switch')" :title="`${t('appearance.switch')} · ${t(active.label)}`">
      <el-icon><component :is="active.icon" /></el-icon>
      <span class="appearance-label">{{ t(active.label) }}</span>
    </button>
    <template #dropdown>
      <el-dropdown-menu :aria-label="t('appearance.switch')">
        <el-dropdown-item v-for="option in options" :key="option.value" :command="option.value" :class="{ 'is-selected': appearance.preference === option.value }">
          <el-icon><component :is="option.icon" /></el-icon>
          <span>{{ t(option.label) }}</span>
          <el-icon class="appearance-check" :class="{ 'is-visible': appearance.preference === option.value }"><Check /></el-icon>
        </el-dropdown-item>
      </el-dropdown-menu>
    </template>
  </el-dropdown>
</template>

<style lang="scss">
.appearance-trigger {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  min-width: 32px;
  height: 32px;
  padding: 0 8px;
  border: 1px solid transparent;
  border-radius: 6px;
  color: var(--secondary-color);
  background: transparent;
  font: inherit;
  font-size: 13px;
  white-space: nowrap;
  cursor: pointer;
  .el-icon { font-size: 17px; }
  &:hover { background: var(--bg-secondary); color: var(--primary-color); }
  &:focus-visible { outline: 2px solid var(--primary-color); outline-offset: 2px; }
}
.appearance-menu .el-dropdown-menu__item {
  display: flex;
  gap: 8px;
  min-width: 150px;
  .el-icon { margin: 0; }
  &.is-selected { color: var(--primary-color); background: var(--el-color-primary-light-9); }
  .appearance-check { margin-left: auto; visibility: hidden; }
  .appearance-check.is-visible { visibility: visible; }
}
@media (max-width: 1024px) { .appearance-trigger .appearance-label { display: none; } }
</style>
